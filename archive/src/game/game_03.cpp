// Recovered (static, batch game_03): the Object base class (constructor, destructor, property readers, damage,
// kill/husk, removal, attachment, list membership, matrices, scale, force actions), DefaultGameObject and
// RocketTowerBuilding virtuals, and the ObjectSystem (construction, behaviour-name table, level object spawning and
// teardown). Compile-checked only (the game was not run); the oracle (tools/compare.py) is still to do. Functions the
// recorded runs never execute are marked "not executed in recorded runs". Vtable slots come from a scan of .rdata in
// game.unpacked.exe; class names from the vtable's slot 1 descriptor (+0x14 "CLASS(Name)"). The Object base vtable is
// 0x005b0b20 ("CLASSV(Object)", written by the constructor 0x004511d0); DefaultGameObject is 0x005a2bf0 / 0x005ab650,
// RocketTowerBuilding 0x005a87c0, ObjectSystem 0x005a826c ("CLASSP(ObjectSystem)").
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
float& F(void* p, uint32_t off)
{
    return Field<float>(p, off);
}
uint32_t& D(void* p, uint32_t off)
{
    return Field<uint32_t>(p, off);
}
void* G()
{
    return GameGlobal<void*>(0x005fe490);
}
// Game+0xa84: the ObjectSystem (object lists at +0xe6c.., +0xe70 all objects, force action pool +0xf30).
void* Objs()
{
    return Field<void*>(G(), 0xa84);
}

// Game float constants (read in place, as the original does).
inline const float& kZero = GameGlobal<float>(0x0056c05c);      // 0.0
inline const float& kOne = GameGlobal<float>(0x005bcb2c);       // 1.0
inline const float& kFrameTime = GameGlobal<float>(0x00604a90); // frame time step
inline const float& kGameTime = GameGlobal<float>(0x00604a98);  // game time

const uint32_t kHeap = 0x0063a210;   // the game heap (PMS)
const uint32_t kTagNew = 0x0058b5e8; // allocation tag string

// The reference release inlined everywhere: --obj+0x28; at 0, unless obj vt+0xd8() returns true, 0x00487da0 (eax =
// obj) frees it. Returns the last eax (approximately).
uint32_t ReleaseRef(void* o)
{
    if (--Field<int32_t>(o, 0x28) != 0)
        return 0;
    const uint32_t r = VCall<uint32_t>(o, 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        return r;
    return orig::fn_00487da0(u(o));
}

// End of a list iteration (0x004e7340 / 0x004e73c0): the iterator's node (second dword) loses a reference (+0x18).
void EndIter(const uint32_t* it)
{
    if (it[1])
        --Field<int32_t>(P(it[1]), 0x18);
}

// Calls an original function that takes eax and returns st0 (0x004e7800: vec3 length; symbols/abi.csv says "returns
// none", the callers use st0).
float CallSt0_Eax(uint32_t target, uint32_t eax_)
{
    float r;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov eax, eax_
        call dword ptr [target]
        pop ebp
        pop edi
        pop esi
        pop ebx
        fstp r
    }
    return r;
}
float Vec3_Length(uint32_t v)
{
    return CallSt0_Eax(0x004e7800, v);
}

void Strcpy(uint32_t dst, uint32_t src)
{
    const char* s = reinterpret_cast<const char*>(src);
    char* d = reinterpret_cast<char*>(dst);
    char c;
    do
    {
        c = *s++;
        *d++ = c;
    } while (c);
}
}  // namespace

// Functions recovered in this file, called directly.
uint32_t ParamSet_FindValue(uint32_t set, uint32_t name, uint32_t values, uint32_t a0);
uint32_t ParamSet_GetString(uint32_t set, uint32_t name, uint32_t values, uint32_t a0, uint32_t out);
uint32_t ParamSet_GetValue(uint32_t set, uint32_t name, uint32_t values, uint32_t a0, uint32_t out);
uint32_t ParamSet_GetBool(uint32_t set, uint32_t name, uint32_t values, uint32_t a0, uint32_t out);
uint32_t Object_ReadStringProp(uint32_t def, uint32_t out, uint32_t obj, uint32_t name, uint32_t set, uint32_t values,
                               uint32_t flag);
uint32_t Object_ReadValueProp(uint32_t def, uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                              uint32_t flag);
uint32_t Object_ReadValuePropEbx(uint32_t def, uint32_t name, uint32_t values, uint32_t out, uint32_t obj,
                                 uint32_t set, uint32_t flag);
uint32_t Object_ReadValuePropDef(uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                                 uint32_t def, uint32_t flag);
uint32_t Object_ReadBoolProp(uint32_t def, uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                             uint32_t flag);
uint32_t Object_GetName(void* obj);
uint32_t Object_Remove(void* obj);
uint32_t Object_IsDead(void* obj);
uint32_t Object_TakeDamage(void* obj, uint32_t dmg);
uint32_t Object_Kill(void* obj, uint32_t a0);
uint32_t Object_SpawnHusk(void* obj);
uint32_t fn_00454bc0(void* obj);
uint64_t ObjectList_Link(uint32_t list, uint32_t obj);
uint32_t ObjectList_Unlink(uint32_t list, uint32_t obj);
uint32_t ObjectList_Contains(uint32_t list, uint32_t obj);
uint32_t Object_UnlinkAll(uint32_t obj);
uint64_t Object_UpdateRenderList(void* obj);
uint32_t Object_Detach(void* obj);
uint64_t Object_Attach(uint32_t name, void* parent, void* obj, uint32_t name2, uint32_t flags, uint32_t a2);
uint32_t Object_GetParent(void* obj);
float Object_GetHealth(void* obj);
float Object_GetMaxHealth(void* obj);
uint32_t fn_00450490(void* obj);
uint32_t fn_00450da0(void* obj, uint32_t dmg);
uint32_t fn_00450ee0(void* obj, uint32_t a0);
uint32_t fn_00453640(void* obj, uint32_t mask, uint32_t on);
uint32_t fn_00453740(void* obj);
uint32_t fn_00455a00(uint32_t handle);
uint32_t fn_00455aa0(uint32_t vec, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);
uint32_t fn_004512b0(uint32_t obj);
uint32_t fn_0045d570(void* objs, uint32_t id);
uint32_t fn_0045d750(void* objs, uint32_t id);
uint32_t fn_0045d7a0(uint32_t objs, uint32_t id, uint32_t entry);
uint32_t fn_0045da20(void* objs);
uint32_t ObjectSystem_InitBehaviourTable(void* objs);

// ===============================================================================================================
// Object property readers. A parameter set (an object's placement record) has a name index (0x004f1160, eax = set;
// stack: name -> entry, entry+0x20 = value index) and a value array (pointer per value). The readers take the value
// first from the placement (set, values) and otherwise from the object's class template (obj+0x414 -> +0x10 -> +0x3c:
// {set, ?, values}).

// 0x0045dea0 (eax = set, ecx = name, esi = values; stack: unused (Game+0xa84); ret 4). Returns the value pointer of
// name, or 0. Caller 0x00451360.
uint32_t ParamSet_FindValue(uint32_t set, uint32_t name, uint32_t values, uint32_t /*a0*/)
{
    if (!set || !values)
        return 0;
    const uint32_t e = orig::fn_004f1160(set, name);
    if (!e)
        return 0;
    return D(P(values), D(P(e), 0x20) * 4);
}
static uint32_t ParamSet_FindValue_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return ParamSet_FindValue(eax, ecx, esi, a0);
}
ABI_ENTRY_0045dea0(ParamSet_FindValue_Entry, ParamSet_FindValue_Abi)
HOOK(0x0045dea0, "ParamSet_FindValue", ParamSet_FindValue_Entry, nullptr);

// 0x0045dec0 (eax = set, ecx = name, esi = values; stack: unused, out; ret 8). String value: strncpy(out, value, 0x20)
// (0x005382a0) and al = 1; al = 0 when missing (upper eax bits as the original leaves them).
uint32_t ParamSet_GetString(uint32_t set, uint32_t name, uint32_t values, uint32_t /*a0*/, uint32_t out)
{
    if (!set)
        return 0;
    if (!values)
        return set & 0xffffff00u;
    const uint32_t e = orig::fn_004f1160(set, name);
    if (!e)
        return 0;
    const uint32_t v = D(P(values), D(P(e), 0x20) * 4);
    const uint32_t r = orig::fn_005382a0(out, v, 0x20);
    return (r & 0xffffff00u) | 1u;
}
static uint32_t ParamSet_GetString_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return ParamSet_GetString(eax, ecx, esi, a0, a1);
}
ABI_ENTRY_0045dec0(ParamSet_GetString_Entry, ParamSet_GetString_Abi)
HOOK(0x0045dec0, "ParamSet_GetString", ParamSet_GetString_Entry, nullptr);

// 0x0045df00 (eax = set, ecx = name, esi = values; stack: unused, out; ret 8). Dword value: *out = *value, al = 1.
uint32_t ParamSet_GetValue(uint32_t set, uint32_t name, uint32_t values, uint32_t /*a0*/, uint32_t out)
{
    if (!set)
        return 0;
    if (!values)
        return set & 0xffffff00u;
    const uint32_t e = orig::fn_004f1160(set, name);
    if (!e)
        return 0;
    const uint32_t v = D(P(values), D(P(e), 0x20) * 4);
    D(P(out), 0) = D(P(v), 0);
    return (v & 0xffffff00u) | 1u;
}
static uint32_t ParamSet_GetValue_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return ParamSet_GetValue(eax, ecx, esi, a0, a1);
}
ABI_ENTRY_0045df00(ParamSet_GetValue_Entry, ParamSet_GetValue_Abi)
HOOK(0x0045df00, "ParamSet_GetValue", ParamSet_GetValue_Entry, nullptr);

// 0x0045df30 (eax = set, ecx = name, esi = values; stack: unused, out; ret 8). Bool value: byte *out = (*value != 0).
uint32_t ParamSet_GetBool(uint32_t set, uint32_t name, uint32_t values, uint32_t /*a0*/, uint32_t out)
{
    if (!set)
        return 0;
    if (!values)
        return set & 0xffffff00u;
    const uint32_t e = orig::fn_004f1160(set, name);
    if (!e)
        return 0;
    const uint32_t v = D(P(values), D(P(e), 0x20) * 4);
    Field<uint8_t>(P(out), 0) = D(P(v), 0) != 0 ? 1 : 0;
    return (v & 0xffffff00u) | 1u;
}
static uint32_t ParamSet_GetBool_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return ParamSet_GetBool(eax, ecx, esi, a0, a1);
}
ABI_ENTRY_0045df30(ParamSet_GetBool_Entry, ParamSet_GetBool_Abi)
HOOK(0x0045df30, "ParamSet_GetBool", ParamSet_GetBool_Entry, nullptr);

namespace
{
// The class template's {set, values} (obj+0x414 -> +0x10 -> +0x3c), or 0.
uint32_t TemplateParams(uint32_t obj)
{
    return D(P(D(P(D(P(obj), 0x414)), 0x10)), 0x3c);
}
}  // namespace

// 0x004532e0 (eax = object; stack: name, out; ret 8). String value from the class template (0x0045dec0), al = 0 when
// the template has no parameters.
uint32_t Object_GetTemplateString(uint32_t obj, uint32_t name, uint32_t out)
{
    const uint32_t t = TemplateParams(obj);
    if (!t)
        return 0;
    return ParamSet_GetString(D(P(t), 0), name, D(P(t), 8), D(G(), 0xa84), out);
}
static uint32_t Object_GetTemplateString_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Object_GetTemplateString(eax, a0, a1);
}
ABI_ENTRY_004532e0(Object_GetTemplateString_Entry, Object_GetTemplateString_Abi)
HOOK(0x004532e0, "Object_GetTemplateString", Object_GetTemplateString_Entry, nullptr);

// 0x00453320 (eax = object; stack: name, out; ret 8). Dword value from the class template (0x0045df00).
uint32_t Object_GetTemplateValue(uint32_t obj, uint32_t name, uint32_t out)
{
    const uint32_t t = TemplateParams(obj);
    if (!t)
        return 0;
    return ParamSet_GetValue(D(P(t), 0), name, D(P(t), 8), D(G(), 0xa84), out);
}
static uint32_t Object_GetTemplateValue_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Object_GetTemplateValue(eax, a0, a1);
}
ABI_ENTRY_00453320(Object_GetTemplateValue_Entry, Object_GetTemplateValue_Abi)
HOOK(0x00453320, "Object_GetTemplateValue", Object_GetTemplateValue_Entry, nullptr);

// 0x00453360 (eax = object; stack: name, out; ret 8). Bool value from the class template (0x0045df30).
uint32_t Object_GetTemplateBool(uint32_t obj, uint32_t name, uint32_t out)
{
    const uint32_t t = TemplateParams(obj);
    if (!t)
        return 0;
    return ParamSet_GetBool(D(P(t), 0), name, D(P(t), 8), D(G(), 0xa84), out);
}
static uint32_t Object_GetTemplateBool_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Object_GetTemplateBool(eax, a0, a1);
}
ABI_ENTRY_00453360(Object_GetTemplateBool_Entry, Object_GetTemplateBool_Abi)
HOOK(0x00453360, "Object_GetTemplateBool", Object_GetTemplateBool_Entry, nullptr);

// 0x004533a0 (eax = default string, edi = out; stack: object, name, set, values, template only; ret 0x14). Copies the
// default to out, then unless `template only` reads name from the placement (0x0045dec0: returns 1); otherwise from
// the class template (0x004532e0): returns -1 when found there, 0 when not found anywhere.
uint32_t Object_ReadStringProp(uint32_t def, uint32_t out, uint32_t obj, uint32_t name, uint32_t set, uint32_t values,
                               uint32_t flag)
{
    Strcpy(out, def);
    if (!static_cast<uint8_t>(flag) &&
        static_cast<uint8_t>(ParamSet_GetString(set, name, values, D(G(), 0xa84), out)))
        return 1;
    return static_cast<uint8_t>(Object_GetTemplateString(obj, name, out)) ? 0xffffffffu : 0u;
}
static uint32_t Object_ReadStringProp_Abi(uint32_t eax, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                                          uint32_t a3, uint32_t a4)
{
    return Object_ReadStringProp(eax, edi, a0, a1, a2, a3, a4);
}
ABI_ENTRY_004533a0(Object_ReadStringProp_Entry, Object_ReadStringProp_Abi)
HOOK(0x004533a0, "Object_ReadStringProp", Object_ReadStringProp_Entry, nullptr);

// 0x00453400 (eax = default, esi = values, edi = out; stack: object, name, set, template only; ret 0x10). Dword
// property: *out = default, then the placement (0x0045df00) or the class template (0x00453320), as 0x004533a0.
uint32_t Object_ReadValueProp(uint32_t def, uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                              uint32_t flag)
{
    D(P(out), 0) = def;
    if (!static_cast<uint8_t>(flag) &&
        static_cast<uint8_t>(ParamSet_GetValue(set, name, values, D(G(), 0xa84), out)))
        return 1;
    return static_cast<uint8_t>(Object_GetTemplateValue(obj, name, out)) ? 0xffffffffu : 0u;
}
static uint32_t Object_ReadValueProp_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                         uint32_t a2, uint32_t a3)
{
    return Object_ReadValueProp(eax, esi, edi, a0, a1, a2, a3);
}
ABI_ENTRY_00453400(Object_ReadValueProp_Entry, Object_ReadValueProp_Abi)
HOOK(0x00453400, "Object_ReadValueProp", Object_ReadValueProp_Entry, nullptr);

// 0x00453450 (eax = default, ebx = name, esi = values, edi = out; stack: object, set, template only; ret 0xc). As
// 0x00453400 with the name in ebx.
uint32_t Object_ReadValuePropEbx(uint32_t def, uint32_t name, uint32_t values, uint32_t out, uint32_t obj,
                                 uint32_t set, uint32_t flag)
{
    D(P(out), 0) = def;
    if (!static_cast<uint8_t>(flag) &&
        static_cast<uint8_t>(ParamSet_GetValue(set, name, values, D(G(), 0xa84), out)))
        return 1;
    return static_cast<uint8_t>(Object_GetTemplateValue(obj, name, out)) ? 0xffffffffu : 0u;
}
static uint32_t Object_ReadValuePropEbx_Abi(uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0,
                                            uint32_t a1, uint32_t a2)
{
    return Object_ReadValuePropEbx(eax, ebx, esi, edi, a0, a1, a2);
}
ABI_ENTRY_00453450(Object_ReadValuePropEbx_Entry, Object_ReadValuePropEbx_Abi)
HOOK(0x00453450, "Object_ReadValuePropEbx", Object_ReadValuePropEbx_Entry, nullptr);

// 0x004534a0 (esi = values, edi = out; stack: object, name, set, default, template only; ret 0x14). As 0x00453400
// with the default on the stack (used for floats).
uint32_t Object_ReadValuePropDef(uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                                 uint32_t def, uint32_t flag)
{
    D(P(out), 0) = def;
    if (!static_cast<uint8_t>(flag) &&
        static_cast<uint8_t>(ParamSet_GetValue(set, name, values, D(G(), 0xa84), out)))
        return 1;
    return static_cast<uint8_t>(Object_GetTemplateValue(obj, name, out)) ? 0xffffffffu : 0u;
}
static uint32_t Object_ReadValuePropDef_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2,
                                            uint32_t a3, uint32_t a4)
{
    return Object_ReadValuePropDef(esi, edi, a0, a1, a2, a3, a4);
}
ABI_ENTRY_004534a0(Object_ReadValuePropDef_Entry, Object_ReadValuePropDef_Abi)
HOOK(0x004534a0, "Object_ReadValuePropDef", Object_ReadValuePropDef_Entry, nullptr);

// 0x004534f0 (al = default, esi = values, edi = out byte; stack: object, name, set, template only; ret 0x10). Bool
// property via 0x0045df30 / 0x00453360.
uint32_t Object_ReadBoolProp(uint32_t def, uint32_t values, uint32_t out, uint32_t obj, uint32_t name, uint32_t set,
                             uint32_t flag)
{
    Field<uint8_t>(P(out), 0) = static_cast<uint8_t>(def);
    if (!static_cast<uint8_t>(flag) &&
        static_cast<uint8_t>(ParamSet_GetBool(set, name, values, D(G(), 0xa84), out)))
        return 1;
    return static_cast<uint8_t>(Object_GetTemplateBool(obj, name, out)) ? 0xffffffffu : 0u;
}
static uint32_t Object_ReadBoolProp_Abi(uint32_t eax, uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1,
                                        uint32_t a2, uint32_t a3)
{
    return Object_ReadBoolProp(eax, esi, edi, a0, a1, a2, a3);
}
ABI_ENTRY_004534f0(Object_ReadBoolProp_Entry, Object_ReadBoolProp_Abi)
HOOK(0x004534f0, "Object_ReadBoolProp", Object_ReadBoolProp_Entry, nullptr);

// ===============================================================================================================
// DefaultGameObject (vtables 0x005a2bf0 / 0x005ab650) and RocketTowerBuilding (0x005a87c0, derived).

// 0x0044fd20 (ecx = object; stack: a0, a1, set, values; ret 0x10). DefaultGameObject / RocketTowerBuilding slot 6, the
// load slot. +0x4a8 = a new list (heap 0x18 bytes, 0x004e7010 eax = Game+0xac4, edx = 0, esi = memory; 0 when the
// allocation fails); +0x4d0 (flags) = +0x520 = +0x518 = +0x514 = 0; the base load 0x004517b0 (same arguments). Then the
// properties (strings in .rdata): attach_audio_loop (+0x4f4, flag 0x20000 when set), pinned, gravity (+0x464, 1.0;
// other values add the object to list Game+0xa84 +0xf1c and clear "pinned"), unpin_health_pct (+0x4b0; > 0 re-pins),
// a pinned object gets 0x004577b0 (eax = object; 1); on_hit_damage (0x40: direct_damage +0x46c, direct_damage_force
// +0x468), on_hit_fx "name/count/interval" (name -> +0x470, count -> +0x498 (_atol), interval -> +0x494 (_atof), +0x490
// = game time - interval, flag 0x20; malformed: Debug_Printf "on_hit_fx is invalid"), crush (0x200 and not 0x40,
// crush_speed +0x4ac = 10), receive_trigger_kill +0x4c4, receive_trigger_deactivate +0x4c8, takes_damage (1),
// takes_crushing_damage (0x1000, default on), takes_bullet_damage (2), blocks_projectiles, no_collide (8),
// play_boulder_sound (0x8000), on_death_player_health +0x4b4, on_death_unlock_level +0x4b8, death_timeout +0x4bc (copied
// to +0x4c0), collide_barrier, receive_trigger_start_blocking +0x4cc. A collide barrier (4) sets collision vt+0xf8(0x40
// or 0x4000 when it waits for a trigger, 0) and clears 0x2000; otherwise, unless no_collide: platform -> vt+0xf8(...)
// and list +0xe9c; damage takers / projectile blockers join lists +0xe90, +0xe94, +0xea0 (+0xea4 when taking damage);
// block_path (default on). Blocking (0x2000) objects that are keyframed (0x00457790) or static (body +0xc bit 0) read
// object_block (0x4000 when off), then vt+0xfc(1, 2). on_hit_death (0x10), impact_force +0x4e4 (2010.0), area_damage
// +0x4d4, damage_radius_inner +0x4d8, damage_radius_outer +0x4dc (+0x4e0 = 1 / (outer - inner) when outer > 0.0 at
// 0x005bcbec and outer > inner, else 1.0), send_trigger_on_death +0x4e8, score_value +0x460. The original reuses the
// a0 / a1 argument slots as bool temporaries. Returns the last reader's eax. Not executed in recorded runs.
uint32_t DefaultGameObject_Load(void* obj, uint32_t a0, uint32_t a1, uint32_t set, uint32_t values)
{
    const uint32_t o = u(obj);
    const uint32_t mem = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, kTagNew);
    const uint32_t list = mem ? orig::fn_004e7010(D(G(), 0xac4), 0, mem) : 0;
    D(obj, 0x4a8) = list;
    D(obj, 0x4d0) = 0;
    D(obj, 0x520) = 0;
    D(obj, 0x518) = 0;
    D(obj, 0x514) = 0;
    orig::fn_004517b0(o, a0, a1, set, values);

    uint8_t slotA0 = static_cast<uint8_t>(a0);  // the a0 argument slot, reused as a bool
    uint8_t slotA1 = static_cast<uint8_t>(a1);  // the a1 argument slot, reused as a bool
    uint8_t blocksProjectiles = 0;
    char fx[0x20];

    Object_ReadStringProp(0x0058a9a4, o + 0x4f4, o, 0x005b1068, set, values, 0);  // attach_audio_loop
    if (Field<uint8_t>(obj, 0x4f4))
        D(obj, 0x4d0) |= 0x20000;
    Object_ReadBoolProp(1, values, u(&slotA1), o, 0x005b1060, set, 0);  // pinned
    Object_ReadValuePropDef(values, o + 0x464, o, 0x005b1058, set, 0x3f800000, 0);  // gravity
    if (D(obj, 0x464) != 0x3f800000)
    {
        slotA1 = 0;
        ObjectList_Link(D(Objs(), 0xf1c), o);
    }
    Object_ReadValuePropDef(values, o + 0x4b0, o, 0x005b1044, set, 0, 0);  // unpin_health_pct
    if (F(obj, 0x4b0) > kZero)
        slotA1 = 1;
    if (slotA1)
        orig::fn_004577b0(o, 1);
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b1034, set, 0);  // on_hit_damage
    if (slotA1)
    {
        D(obj, 0x4d0) |= 0x40;
        Object_ReadValuePropDef(values, o + 0x46c, o, 0x005b1024, set, 0, 0);  // direct_damage
        Object_ReadValuePropDef(values, o + 0x468, o, 0x005b1010, set, 0, 0);  // direct_damage_force
    }
    if (Object_ReadStringProp(0x005bb9e0, u(fx), o, 0x005b1004, set, values, 0))  // on_hit_fx
    {
        bool ok = false;
        char* p = reinterpret_cast<char*>(orig::fn_005386b0(u(fx), 0x2f));
        if (p)
        {
            *p = 0;
            Strcpy(o + 0x470, u(fx));
            *p = '/';
            char* q = reinterpret_cast<char*>(orig::fn_005386b0(u(p + 1), 0x2f));
            if (q)
            {
                *q = 0;
                D(obj, 0x498) = static_cast<uint32_t>(GameFunction<int(__cdecl*)(const char*)>(0x005387f6)(p + 1));
                *q = '/';
                const float f = orig::fn_005387fb(u(q + 1));
                F(obj, 0x494) = f;
                F(obj, 0x490) = kGameTime - f;
                D(obj, 0x4d0) |= 0x20;
                ok = true;
            }
        }
        if (!ok)
            orig::fn_00407ff0(D(G(), 0x34), 0x005b0fe8, u(fx), Object_GetName(obj));
    }
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0fe0, set, 0);  // crush
    if (slotA1)
    {
        Object_ReadValuePropDef(values, o + 0x4ac, o, 0x005b0fd4, set, 0x41200000, 0);  // crush_speed
        const uint32_t f = D(obj, 0x4d0) | 0x200;
        D(obj, 0x4d0) = f;
        D(obj, 0x4d0) = f & 0xffffffbfu;
    }
    Object_ReadValuePropEbx(0, 0x005bb870, values, o + 0x4c4, o, set, 0);  // receive_trigger_kill
    Object_ReadValuePropEbx(0, 0x005bb780, values, o + 0x4c8, o, set, 0);  // receive_trigger_deactivate
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005bb920, set, 0);     // takes_damage
    if (slotA1)
        D(obj, 0x4d0) |= 1;
    Object_ReadBoolProp(1, values, u(&slotA1), o, 0x005b0fbc, set, 0);  // takes_crushing_damage
    if (slotA1)
        D(obj, 0x4d0) |= 0x1000;
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005bb8dc, set, 0);  // takes_bullet_damage
    if (slotA1)
        D(obj, 0x4d0) |= 2;
    Object_ReadBoolProp(0, values, u(&blocksProjectiles), o, 0x005b0fa8, set, 0);  // blocks_projectiles
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0f9c, set, 0);             // no_collide
    if (slotA1)
        D(obj, 0x4d0) |= 8;
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0f88, set, 0);  // play_boulder_sound
    if (slotA1)
        D(obj, 0x4d0) |= 0x8000;
    Object_ReadValuePropDef(values, o + 0x4b4, o, 0x005b0f70, set, 0, 0);  // on_death_player_health
    Object_ReadValueProp(0, values, o + 0x4b8, o, 0x005b0f58, set, 0);     // on_death_unlock_level
    Object_ReadValuePropDef(values, o + 0x4bc, o, 0x005b0f48, set, 0, 0);  // death_timeout
    D(obj, 0x4c0) = D(obj, 0x4bc);
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0f38, set, 0);              // collide_barrier
    Object_ReadValuePropEbx(0, 0x005b0f18, values, o + 0x4cc, o, set, 0);          // receive_trigger_start_blocking
    slotA0 = 1;
    if (slotA1)
    {
        D(obj, 0x4d0) |= 4;
        if (D(obj, 0x4cc) == 0)
        {
            VCall<uint32_t>(obj, 0xf8 / 4, 0x40u, 0u);
            D(obj, 0x3ec) &= 0xffffffe7u;
        }
        else
            VCall<uint32_t>(obj, 0xf8 / 4, 0x4000u, 0u);
        D(obj, 0x4d0) &= 0xffffdfffu;
        slotA0 = 0;
    }
    else
    {
        if (!(D(obj, 0x4d0) & 8))
        {
            Object_ReadBoolProp(1, values, u(&slotA1), o, 0x005b0f0c, set, 0);  // platform
            uint32_t c = 0;
            if (slotA1)
                c = (Field<uint8_t>(Field<void*>(obj, 0x1c4), 0xc) & 1) ? 4u : 0x40000u;
            if (Field<uint8_t>(Field<void*>(obj, 0x1c4), 0xc) & 1)
                c |= 1;
            else
                c |= 0x30001;
            VCall<uint32_t>(obj, 0xf8 / 4, c, 0u);
            ObjectList_Link(D(Objs(), 0xe9c), o);
        }
        else
            slotA0 = 0;
        const uint32_t f = D(obj, 0x4d0);
        if (((f & 1) || blocksProjectiles) && !(f & 8))
        {
            ObjectList_Link(D(Objs(), 0xe90), o);
            ObjectList_Link(D(Objs(), 0xe94), o);
            ObjectList_Link(D(Objs(), 0xea0), o);
            if (D(obj, 0x4d0) & 1)
                ObjectList_Link(D(Objs(), 0xea4), o);
        }
        if (slotA0)
            Object_ReadBoolProp(1, values, u(&slotA0), o, 0x005b94ac, set, 0);  // block_path
    }
    if (!(D(obj, 0x424) & 0x20000) && slotA0)
    {
        D(obj, 0x4d0) |= 0x2000;
        if (static_cast<uint8_t>(orig::fn_00457790(o)) || (Field<uint8_t>(Field<void*>(obj, 0x1c4), 0xc) & 1))
        {
            Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0efc, set, 0);  // object_block
            if (!slotA1)
                D(obj, 0x4d0) |= 0x4000;
        }
        VCall<uint32_t>(obj, 0xfc / 4, 1u, 2u);
    }
    Object_ReadBoolProp(0, values, u(&slotA1), o, 0x005b0eec, set, 0);  // on_hit_death
    if (slotA1)
        D(obj, 0x4d0) |= 0x10;
    Object_ReadValuePropDef(values, o + 0x4e4, o, 0x005b93c8, set, 0x44fb4000, 0);  // impact_force
    Object_ReadValuePropDef(values, o + 0x4d4, o, 0x005b93bc, set, 0, 0);           // area_damage
    Object_ReadValuePropDef(values, o + 0x4d8, o, 0x005b93a8, set, 0, 0);           // damage_radius_inner
    Object_ReadValuePropDef(values, o + 0x4dc, o, 0x005b9394, set, 0, 0);           // damage_radius_outer
    const float outer = F(obj, 0x4dc);
    if (outer > GameGlobal<float>(0x005bcbec) && outer > F(obj, 0x4d8))
        F(obj, 0x4e0) = kOne / (F(obj, 0x4dc) - F(obj, 0x4d8));
    else
        D(obj, 0x4e0) = 0x3f800000;
    Object_ReadValuePropEbx(0, 0x005bb888, values, o + 0x4e8, o, set, 0);          // send_trigger_on_death
    return Object_ReadValuePropDef(values, o + 0x460, o, 0x005bb864, set, 0, 0);   // score_value
}
static uint32_t DefaultGameObject_Load_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return DefaultGameObject_Load(P(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_0044fd20(DefaultGameObject_Load_Entry, DefaultGameObject_Load_Abi)
HOOK(0x0044fd20, "DefaultGameObject_Load", DefaultGameObject_Load_Entry, nullptr);

// 0x00450490 (ecx = object). DefaultGameObject slot 0x37 (remove): Object_Remove 0x004547b0; the reference +0x520 is
// released and cleared; when blocking (0x2000) and not "no_collide without barrier" (flags 4 / 8), vt+0xfc(0, 2); the
// list +0x4a8 has every element released, then its vt+0x10(1) (delete) and +0x4a8 = 0; the sounds +0x518 and +0x514
// stop (Game+0x2c vt+0xd8(&handle, 0)). Returns the last call's eax. Caller 0x00451070.
uint32_t fn_00450490(void* obj)
{
    uint32_t it[2] = {0, 0};
    uint32_t node = 0;
    Object_Remove(obj);
    void* t = Field<void*>(obj, 0x520);
    if (t)
    {
        ReleaseRef(t);
        D(obj, 0x520) = 0;
    }
    const uint32_t f = D(obj, 0x4d0);
    if (((f & 4) || !(f & 8)) && (f & 0x2000))
        VCall<uint32_t>(obj, 0xfc / 4, 0u, 2u);
    if (D(obj, 0x4a8))
    {
        for (uint32_t e = orig::fn_004e7340(D(obj, 0x4a8), u(it)); e; e = orig::fn_004e73c0(u(it)))
            ReleaseRef(P(e));
        void* list = Field<void*>(obj, 0x4a8);
        if (list)
            VCall<uint32_t>(list, 0x10 / 4, 1u);
        node = it[1];
    }
    D(obj, 0x4a8) = 0;
    void* snd = Field<void*>(G(), 0x2c);
    VCall<uint32_t>(snd, 0xd8 / 4, u(obj) + 0x518, 0u);
    const uint32_t r = VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd8 / 4, u(obj) + 0x514, 0u);
    if (node)
        --Field<int32_t>(P(node), 0x18);
    return r;
}
static uint32_t fn_00450490_Abi(uint32_t ecx)
{
    return fn_00450490(P(ecx));
}
ABI_ENTRY_00450490(fn_00450490_Entry, fn_00450490_Abi)
HOOK(0x00450490, "fn_00450490", fn_00450490_Entry, nullptr);

// 0x004505a0 (ecx = object). DefaultGameObject / RocketTowerBuilding slot 0x2f (per-frame update). Starts the
// attach_audio_loop sound (Game+0x2c vt+0xc4(name, object, 1, 0, -1, 0) -> +0x514; failure clears 0x20000); a dead
// (0x00454780) +0x520 sets 0x80. 0x10000 -> Object_Kill(0); 0x80 -> vt+0xd4(0). play_boulder_sound (0x8000): when the
// horizontal speed squared (Body_getLinearVelocity 0x00458000) is above [0x005bce84] and 0x00454bc0 is true, the loop
// "imp_boulder_roll_lp" starts (+0x518) and +0x51c = 0.5; otherwise +0x51c counts down and at <= 0 the loop stops.
// Blocking objects that are neither keyframed nor static get vt+0xfc(1, 2). Pending impact effects: 0x40000 ->
// Fx_Create 0x0041cc60 ("fx_boulder_impact", params {8, .., pos +0x49c}); 0x100 -> the on_hit_fx name +0x470. When the
// list +0x4a8 has elements (vt+0x34): a damage record (0x34 bytes: +0 type, +4 position (vt+0x54), +0x10 -1, +0x14
// direct_damage (or the target's health when crushing, 0x4000 type), +0x18 normalised velocity, +0x24
// direct_damage_force, +0x28/+0x2c object, +0x30 0) goes to each element's vt+0xd0, which is then released; the list is
// emptied in place. death_timeout +0x4c0 counts down, at <= 0 vt+0xd4(0). Not executed in recorded runs.
uint32_t fn_004505a0(void* obj)
{
    const uint32_t o = u(obj);
    if ((D(obj, 0x4d0) & 0x20000) && !D(obj, 0x514))
    {
        const uint32_t h = VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xc4 / 4, o + 0x4f4, o, 1u, 0u, 0xffffffffu, 0u);
        D(obj, 0x514) = h;
        if (!h)
            D(obj, 0x4d0) &= 0xfffdffffu;
    }
    void* t = Field<void*>(obj, 0x520);
    if (t && static_cast<uint8_t>(Object_IsDead(t)))
        D(obj, 0x4d0) |= 0x80;
    const uint32_t f = D(obj, 0x4d0);
    if (f & 0x10000)
        return Object_Kill(obj, 0);
    if (f & 0x80)
        return VCall<uint32_t>(obj, 0xd4 / 4, 0u);

    // Original frame (after the pushes): +0 iterator, +8 damage record (0x34 bytes, zeroed), +0x3c velocity, +0x48
    // effect parameters (only +0 and +0xc..+0x14 set by the original).
    uint32_t fr[0xc8 / 4] = {};
    uint32_t* it = &fr[0];
    uint8_t* dmg = reinterpret_cast<uint8_t*>(&fr[2]);
    float* vel = reinterpret_cast<float*>(&fr[0x3c / 4]);
    uint32_t* fxp = &fr[0x48 / 4];
    uint32_t node = 0;
    if (f & 0x8000)
    {
        orig::fn_00458000(o, u(vel));
        if (vel[1] * vel[1] + vel[0] * vel[0] > GameGlobal<float>(0x005bce84) &&
            static_cast<uint8_t>(fn_00454bc0(obj)))
        {
            if (!D(obj, 0x518))
                D(obj, 0x518) =
                    VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xc4 / 4, 0x005b0ed8u, o, 1u, 0u, 0xffffffffu, 0u);
            D(obj, 0x51c) = 0x3f000000;  // 0.5
        }
        else
        {
            const float tt = F(obj, 0x51c) - kFrameTime;
            F(obj, 0x51c) = tt;
            if (tt <= kZero)
                VCall<uint32_t>(Field<void*>(G(), 0x2c), 0xd8 / 4, o + 0x518, 0u);
        }
    }
    if (!static_cast<uint8_t>(orig::fn_00457790(o)) && !(Field<uint8_t>(Field<void*>(obj, 0x1c4), 0xc) & 1) &&
        (D(obj, 0x4d0) & 0x2000))
        VCall<uint32_t>(obj, 0xfc / 4, 1u, 2u);
    if (D(obj, 0x4d0) & 0x40000)
    {
        fxp[3] = D(obj, 0x49c);
        fxp[4] = D(obj, 0x4a0);
        fxp[5] = D(obj, 0x4a4);
        fxp[0] = 8;
        orig::fn_0041cc60(D(obj, 0x4a4) & 0xffffff00u, D(G(), 0xa90), 0x005b0ec4, u(fxp));
        D(obj, 0x4d0) &= 0xfffbffffu;
    }
    if (D(obj, 0x4d0) & 0x100)
    {
        fxp[3] = D(obj, 0x49c);
        fxp[4] = D(obj, 0x4a0);
        fxp[5] = D(obj, 0x4a4);
        fxp[0] = 8;
        orig::fn_0041cc60(D(obj, 0x4a4) & 0xffffff00u, D(G(), 0xa90), o + 0x470, u(fxp));
        D(obj, 0x4d0) &= 0xfffffeffu;
    }
    if (VCall<uint32_t>(Field<void*>(obj, 0x4a8), 0x34 / 4) != 0)
    {
        D(dmg, 0x14) = D(obj, 0x46c);
        D(dmg, 0x24) = D(obj, 0x468);
        D(dmg, 0x10) = 0xffffffffu;
        D(dmg, 0x30) = 0;
        D(dmg, 0) = 0;
        VCall<uint32_t>(obj, 0x54 / 4, u(dmg) + 4);
        D(dmg, 0x28) = o;
        D(dmg, 0x2c) = o;
        float* v = reinterpret_cast<float*>(dmg + 0x18);
        orig::fn_00458000(o, u(v));
        const float len = std::sqrt(v[2] * v[2] + v[1] * v[1] + v[0] * v[0]);
        if (len != kZero)
        {
            const float inv = kOne / len;
            v[0] = v[0] * inv;
            v[1] = v[1] * inv;
            v[2] = v[2] * inv;
        }
        for (uint32_t e = orig::fn_004e7340(D(obj, 0x4a8), u(it)); e; e = orig::fn_004e73c0(u(it)))
        {
            if (D(obj, 0x4d0) & 0x200)
            {
                D(dmg, 0) = 0x4000;
                F(dmg, 0x14) = Object_GetHealth(P(e));
            }
            VCall<uint32_t>(P(e), 0xd0 / 4, u(dmg));
            ReleaseRef(P(e));
        }
        void* l = Field<void*>(obj, 0x4a8);
        const uint32_t c = D(l, 0xc);
        if (c)
        {
            void* p2 = Field<void*>(l, 8);
            D(Field<void*>(l, 0x10), 0) = D(p2, 8);
            D(p2, 8) = c;
        }
        node = it[1];
        D(l, 0xc) = 0;
        D(l, 0x10) = 0;
        D(l, 0x14) = 0;
    }
    if (F(obj, 0x4c0) > kZero)
    {
        const float tt = F(obj, 0x4c0) - kFrameTime;
        F(obj, 0x4c0) = tt;
        if (tt <= kZero)
            VCall<uint32_t>(obj, 0xd4 / 4, 0u);
    }
    if (node)
        --Field<int32_t>(P(node), 0x18);
    return D(obj, 0x4d0);
}
static uint32_t fn_004505a0_Abi(uint32_t ecx)
{
    return fn_004505a0(P(ecx));
}
ABI_ENTRY_004505a0(fn_004505a0_Entry, fn_004505a0_Abi)
HOOK(0x004505a0, "fn_004505a0", fn_004505a0_Entry, nullptr);

// 0x00450980 (eax = target, ecx = object; caller 0x0040ad80). When +0x520 is empty, stores target there and references
// it (+0x28 += 1). Not executed in recorded runs.
void fn_00450980(uint32_t target, void* obj)
{
    if (D(obj, 0x520) == 0)
    {
        D(obj, 0x520) = target;
        ++Field<int32_t>(P(target), 0x28);
    }
}
static void fn_00450980_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00450980(eax, P(ecx));
}
ABI_ENTRY_00450980(fn_00450980_Entry, fn_00450980_Abi)
HOOK(0x00450980, "fn_00450980", fn_00450980_Entry, nullptr);

// 0x00450af0 (ecx = object; stack: other, pos, a2, a3, a4; ret 0x14). DefaultGameObject / RocketTowerBuilding slot 0x2a
// (collision; the Object base slot is 0x004548f0, called at the end with the same arguments). With another object and
// unless this one is dead (0x00454780): on_hit_damage (0x40) queues the other object in the list +0x4a8 (vt+0x1c
// find, vt+0x14 add, reference +0x28); crush (0x200) queues it when the relative speed reaches crush_speed +0x4ac and
// either the other's speed (0x004e7800) is below it or the normalised relative velocity dotted with this object's
// normalised velocity is below [0x005bce80] (both checks guarded by this object's own speed reaching +0x4ac). Without
// another object: on_hit_fx (0x20) fires (0x100, position = pos) when its interval +0x494 has passed since +0x490,
// counting +0x498 down (0x20 cleared at 0); play_boulder_sound (0x8000) marks an impact (0x40000, position = pos) when
// this object's vertical velocity is below [0x005bce7c]. on_hit_death (0x10) sets 0x80 and makes the result 0. Returns
// al = base result && not on_hit_death. The original keeps the distance in the `other` argument slot. Not executed in
// recorded runs.
uint32_t DefaultGameObject_Collide(void* obj, uint32_t other, uint32_t pos, uint32_t a2, uint32_t a3, uint32_t a4)
{
    const uint32_t o = u(obj);
    float fr[9] = {};  // original frame: +0 delta, +0xc own velocity, +0x18 other velocity
    float* dl = &fr[0];
    float* v1 = &fr[3];
    float* v2 = &fr[6];
    uint8_t ok = 1;
    if (other)
    {
        if (!static_cast<uint8_t>(Object_IsDead(obj)))
        {
            bool add = false;
            const uint32_t f = D(obj, 0x4d0);
            if (f & 0x40)
                add = true;
            else if (f & 0x200)
            {
                orig::fn_00458000(o, u(v1));
                orig::fn_00458000(other, u(v2));
                dl[0] = v1[0] - v2[0];
                dl[1] = v1[1] - v2[1];
                dl[2] = v1[2] - v2[2];
                const float dist = std::sqrt(dl[2] * dl[2] + dl[1] * dl[1] + dl[0] * dl[0]);
                if (dist >= F(obj, 0x4ac))
                {
                    const float l1 = Vec3_Length(u(v1));
                    if (l1 >= F(obj, 0x4ac))
                    {
                        const float i1 = kOne / l1;
                        v1[0] = v1[0] * i1;
                        v1[1] = v1[1] * i1;
                        v1[2] = v1[2] * i1;
                        const float i2 = kOne / dist;
                        dl[0] = dl[0] * i2;
                        dl[1] = dl[1] * i2;
                        dl[2] = dl[2] * i2;
                        const float l2 = Vec3_Length(u(v2));
                        if (l2 < F(obj, 0x4ac))
                            add = true;
                        else if (dl[2] * v1[2] + dl[1] * v1[1] + dl[0] * v1[0] < GameGlobal<float>(0x005bce80))
                            add = true;
                    }
                }
            }
            if (add && !VCall<uint32_t>(Field<void*>(obj, 0x4a8), 0x1c / 4, other))
            {
                VCall<uint32_t>(Field<void*>(obj, 0x4a8), 0x14 / 4, other);
                ++Field<int32_t>(P(other), 0x28);
            }
        }
    }
    else
    {
        if (D(obj, 0x4d0) & 0x20)
        {
            const float gt = kGameTime;
            if (gt - F(obj, 0x490) > F(obj, 0x494))
            {
                F(obj, 0x490) = gt;
                D(obj, 0x49c) = D(P(pos), 0);
                D(obj, 0x4a0) = D(P(pos), 4);
                D(obj, 0x4a4) = D(P(pos), 8);
                D(obj, 0x4d0) |= 0x100;
                const int32_t n = static_cast<int32_t>(D(obj, 0x498)) - 1;
                D(obj, 0x498) = static_cast<uint32_t>(n);
                if (n <= 0)
                    D(obj, 0x4d0) &= 0xffffffdfu;
            }
        }
        if (D(obj, 0x4d0) & 0x8000)
        {
            orig::fn_00458000(o, u(v2));
            if (v2[2] < GameGlobal<float>(0x005bce7c))
            {
                D(obj, 0x49c) = D(P(pos), 0);
                D(obj, 0x4a0) = D(P(pos), 4);
                D(obj, 0x4a4) = D(P(pos), 8);
                D(obj, 0x4d0) |= 0x40000;
            }
        }
    }
    if (D(obj, 0x4d0) & 0x10)
    {
        D(obj, 0x4d0) |= 0x80;
        ok = 0;
    }
    const uint32_t r = orig::fn_004548f0(o, other, pos, a2, a3, a4);
    return (r & 0xffffff00u) | ((static_cast<uint8_t>(r) && ok) ? 1u : 0u);
}
static uint32_t DefaultGameObject_Collide_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                              uint32_t a4)
{
    return DefaultGameObject_Collide(P(ecx), a0, a1, a2, a3, a4);
}
ABI_ENTRY_00450af0(DefaultGameObject_Collide_Entry, DefaultGameObject_Collide_Abi)
HOOK(0x00450af0, "DefaultGameObject_Collide", DefaultGameObject_Collide_Entry, nullptr);

// 0x00450da0 (ecx = object; stack: damage record; ret 4). DefaultGameObject / RocketTowerBuilding slot 0x34 (damage;
// the Object base slot is Object_TakeDamage 0x004555e0). Ignores damage from itself (record +0x2c = source), when dead
// or killed (0x10080), bullet damage (record bit 0x400) unless takes_bullet_damage (2), crushing damage (0x4000) unless
// takes_crushing_damage (0x1000). takes_damage (1) applies it (0x004555e0). When dead afterwards and the source is the
// player (Game+0x748): on_death_player_health +0x4b4 (when non-zero) goes to 0x00475a30 (ecx = player), and
// on_death_unlock_level +0x4b8 unlocks level 2 unless already unlocked (0x0044a660 (eax = Game+0xa64, cl = 0) ->
// 0x004a0720 (ecx = 2, ebx = game) / 0x004a07a0 (ecx = game, edi = 2; 1)), then Game byte +0x2720 = 1. Alive: when
// unpin_health_pct * max health exceeds the health, collision vt+0xf8(0x70001, 0) and 0x004577b0 (eax = object; 0).
uint32_t DefaultGameObject_OnDamage(void* obj, uint32_t dmg)
{
    if (dmg && D(P(dmg), 0x2c) == u(obj))
        return 0;
    const uint32_t f = D(obj, 0x4d0);
    if (f & 0x10080)
        return f;
    if (!(f & 2) && (D(P(dmg), 0) & 0x400))
        return f;
    if (!(f & 0x1000) && (D(P(dmg), 0) & 0x4000))
        return f;
    if (f & 1)
        Object_TakeDamage(obj, dmg);
    const uint32_t dead = Object_IsDead(obj);
    if (static_cast<uint8_t>(dead))
    {
        if (!dmg)
            return dead;
        void* g = G();
        if (D(P(dmg), 0x2c) == D(g, 0x748) && !(F(obj, 0x4b4) == kZero))
            orig::fn_00475a30(D(g, 0x748), D(obj, 0x4b4));
        if (D(P(dmg), 0x2c) != D(g, 0x748) || !D(obj, 0x4b8))
            return 0;
        const uint32_t q = orig::fn_0044a660(D(g, 0xa64), D(P(dmg), 0x2c) & 0xffffff00u);
        const uint32_t r = orig::fn_004a0720(q, 2, u(g));
        if (static_cast<uint8_t>(r))
            return r;
        void* g2 = G();
        const uint32_t q2 = orig::fn_0044a660(D(g2, 0xa64), 0);
        const uint32_t r2 = orig::fn_004a07a0(q2, u(g2), 2, 1);
        Field<uint8_t>(G(), 0x2720) = 1;
        return r2;
    }
    if (F(obj, 0x4b0) * F(obj, 0x458) > F(obj, 0x454))
    {
        VCall<uint32_t>(obj, 0xf8 / 4, 0x70001u, 0u);
        return static_cast<uint32_t>(orig::fn_004577b0(u(obj), 0));
    }
    return 0;
}
static uint32_t DefaultGameObject_OnDamage_Abi(uint32_t ecx, uint32_t a0)
{
    return DefaultGameObject_OnDamage(P(ecx), a0);
}
ABI_ENTRY_00450da0(DefaultGameObject_OnDamage_Entry, DefaultGameObject_OnDamage_Abi)
HOOK(0x00450da0, "DefaultGameObject_OnDamage", DefaultGameObject_OnDamage_Entry, nullptr);

// 0x00450ee0 (ecx = object; stack: a0 unused; ret 4). DefaultGameObject / RocketTowerBuilding slot 0x35 (death; the
// Object base slot is Object_Kill 0x00455640). Once (flag 0x10000): score_value +0x460 is added to Game+0x2724 (>= 0)
// or Game+0x2728 (negative); send_trigger_on_death +0x4e8 fires (Object_SendTrigger 0x00461870, eax = Game+0xa84, ebx =
// trigger; object); a non-zero area_damage +0x4d4 runs 0x0047f020 (ebx = &area_damage; object, &position (vt+0x54),
// object, impact_force +0x4e4, object, 0, 0).
uint32_t DefaultGameObject_OnDeath(void* obj, uint32_t /*a0*/)
{
    if (D(obj, 0x4d0) & 0x10000)
        return D(obj, 0x4d0);
    const float s = F(obj, 0x460);
    void* g = G();
    if (s >= kZero)
        F(g, 0x2724) = s + F(g, 0x2724);
    else
        F(g, 0x2728) = s + F(g, 0x2728);
    if (D(obj, 0x4e8))
        orig::fn_00461870(D(g, 0xa84), D(obj, 0x4e8), u(obj));
    if (!(F(obj, 0x4d4) == kZero))
    {
        float pos[3];
        VCall<uint32_t>(obj, 0x54 / 4, u(pos));
        orig::fn_0047f020(u(obj) + 0x4d4, u(obj), u(pos), u(obj), D(obj, 0x4e4), u(obj), 0, 0);
    }
    D(obj, 0x4d0) |= 0x10000;
    return D(obj, 0x4d0);
}
static uint32_t DefaultGameObject_OnDeath_Abi(uint32_t ecx, uint32_t a0)
{
    return DefaultGameObject_OnDeath(P(ecx), a0);
}
ABI_ENTRY_00450ee0(DefaultGameObject_OnDeath_Entry, DefaultGameObject_OnDeath_Abi)
HOOK(0x00450ee0, "DefaultGameObject_OnDeath", DefaultGameObject_OnDeath_Entry, nullptr);

// 0x00450fa0 (ecx = object; stack: out; ret 4). DefaultGameObject / RocketTowerBuilding slot 0x23 (the Object base
// slot 0x004585f0 writes 0): *out = gravity +0x464. Not executed in recorded runs.
uint32_t DefaultGameObject_GetGravity(void* obj, uint32_t out)
{
    const uint32_t v = D(obj, 0x464);
    D(P(out), 0) = v;
    return v;
}
static uint32_t DefaultGameObject_GetGravity_Abi(uint32_t ecx, uint32_t a0)
{
    return DefaultGameObject_GetGravity(P(ecx), a0);
}
ABI_ENTRY_00450fa0(DefaultGameObject_GetGravity_Entry, DefaultGameObject_GetGravity_Abi)
HOOK(0x00450fa0, "DefaultGameObject_GetGravity", DefaultGameObject_GetGravity_Entry, nullptr);

// 0x00450fb0 (ecx = object; stack: a0, trigger; ret 8). DefaultGameObject / RocketTowerBuilding slot 0xe (trigger
// received): receive_trigger_kill +0x4c4 -> vt+0xd4(0); receive_trigger_deactivate +0x4c8 -> vt+0xdc();
// receive_trigger_start_blocking +0x4cc -> collision vt+0xf8(0x40, 0) and list Game+0xa84 +0xe90 (0x004546d0); the
// matched trigger field is cleared. Always returns al = 0. Not executed in recorded runs.
uint32_t DefaultGameObject_OnTrigger(void* obj, uint32_t /*a0*/, uint32_t trigger)
{
    if (trigger == D(obj, 0x4c4))
    {
        const uint32_t r = VCall<uint32_t>(obj, 0xd4 / 4, 0u);
        D(obj, 0x4c4) = 0;
        return r & 0xffffff00u;
    }
    if (trigger == D(obj, 0x4c8))
    {
        const uint32_t r = VCall<uint32_t>(obj, 0xdc / 4);
        D(obj, 0x4c8) = 0;
        return r & 0xffffff00u;
    }
    if (trigger == D(obj, 0x4cc))
    {
        VCall<uint32_t>(obj, 0xf8 / 4, 0x40u, 0u);
        const uint32_t r = static_cast<uint32_t>(ObjectList_Link(D(Objs(), 0xe90), u(obj)));
        D(obj, 0x4cc) = 0;
        return r & 0xffffff00u;
    }
    return trigger & 0xffffff00u;
}
static uint32_t DefaultGameObject_OnTrigger_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return DefaultGameObject_OnTrigger(P(ecx), a0, a1);
}
ABI_ENTRY_00450fb0(DefaultGameObject_OnTrigger_Entry, DefaultGameObject_OnTrigger_Abi)
HOOK(0x00450fb0, "DefaultGameObject_OnTrigger", DefaultGameObject_OnTrigger_Entry, nullptr);

// 0x00451040 (ecx = object; stack: damage record; ret 4). DefaultGameObject (vtable 0x005ab650) slot 0x34: the damage
// slot 0x00450da0, then when dead (0x00454780) Game byte +0xdd1 = 1. Not executed in recorded runs.
uint32_t fn_00451040(void* obj, uint32_t dmg)
{
    DefaultGameObject_OnDamage(obj, dmg);
    const uint32_t r = Object_IsDead(obj);
    if (static_cast<uint8_t>(r))
        Field<uint8_t>(G(), 0xdd1) = 1;
    return r;
}
static uint32_t fn_00451040_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00451040(P(ecx), a0);
}
ABI_ENTRY_00451040(fn_00451040_Entry, fn_00451040_Abi)
HOOK(0x00451040, "fn_00451040", fn_00451040_Entry, nullptr);

// 0x00451070 (ecx = object). RocketTowerBuilding slot 0x37 (remove): 0x00450490, then the turret reference +0x524 is
// released and cleared. Not executed in recorded runs.
uint32_t RocketTowerBuilding_Remove(void* obj)
{
    fn_00450490(obj);
    void* t = Field<void*>(obj, 0x524);
    uint32_t r = 0;
    if (t)
    {
        r = ReleaseRef(t);
        D(obj, 0x524) = 0;
    }
    return r;
}
static uint32_t RocketTowerBuilding_Remove_Abi(uint32_t ecx)
{
    return RocketTowerBuilding_Remove(P(ecx));
}
ABI_ENTRY_00451070(RocketTowerBuilding_Remove_Entry, RocketTowerBuilding_Remove_Abi)
HOOK(0x00451070, "RocketTowerBuilding_Remove", RocketTowerBuilding_Remove_Entry, nullptr);

// 0x004510b0 (ecx = object). RocketTowerBuilding slot 8: finds the "ref_turret" matrix (0x00453c50, stack: object,
// name, &matrix, 1, 0, 0, 1, 0, 0) and creates "v_rockettower_turret" there (ObjectSystem_CreateObject 0x0045f020, eax
// = 0, ecx = name; Game+0xa84, &matrix, 0x3ff, 0, 0, 0, 0, 0xaa, 0) -> +0x524; on success Game+0x2710 += 1, the turret
// is referenced, 0x0049c520 (eax = object, ecx = turret), 0x0049c530 (eax = turret, ecx = +0x4e8, edx = tag +0x390)
// and +0x390 = 0. Not executed in recorded runs.
uint32_t RocketTowerBuilding_SpawnTurret(void* obj)
{
    uint32_t m[0x44 / 4];
    const uint32_t r = orig::fn_00453c50(u(obj), 0x005b5fb4, u(m), 1, 0, 0, 1, 0, 0);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t t = orig::fn_0045f020(0, 0x005b0eac, D(G(), 0xa84), u(m), 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    D(obj, 0x524) = t;
    if (!t)
        return 0;
    ++Field<int32_t>(G(), 0x2710);
    ++Field<int32_t>(P(D(obj, 0x524)), 0x28);
    orig::fn_0049c520(u(obj), D(obj, 0x524));
    const uint32_t r2 = orig::fn_0049c530(D(obj, 0x524), D(obj, 0x4e8), D(obj, 0x390));
    D(obj, 0x390) = 0;
    return r2;
}
static uint32_t RocketTowerBuilding_SpawnTurret_Abi(uint32_t ecx)
{
    return RocketTowerBuilding_SpawnTurret(P(ecx));
}
ABI_ENTRY_004510b0(RocketTowerBuilding_SpawnTurret_Entry, RocketTowerBuilding_SpawnTurret_Abi)
HOOK(0x004510b0, "RocketTowerBuilding_SpawnTurret", RocketTowerBuilding_SpawnTurret_Entry, nullptr);

// 0x00451160 (ecx = object; stack: a0; ret 4). RocketTowerBuilding slot 0x35 (death): 0x00450ee0, then unless vt+0xd8()
// is true the turret +0x524 is detached (+0x524 = 0), killed when its vt+0xd8() is true (vt+0xd4(0)) and released. Not
// executed in recorded runs.
uint32_t RocketTowerBuilding_OnDeath(void* obj, uint32_t a0)
{
    DefaultGameObject_OnDeath(obj, a0);
    const uint32_t r = VCall<uint32_t>(obj, 0xd8 / 4);
    if (static_cast<uint8_t>(r))
        return r;
    void* t = Field<void*>(obj, 0x524);
    if (!t)
        return 0;
    D(obj, 0x524) = 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(t, 0xd8 / 4)))
        VCall<uint32_t>(t, 0xd4 / 4, 0u);
    return ReleaseRef(t);
}
static uint32_t RocketTowerBuilding_OnDeath_Abi(uint32_t ecx, uint32_t a0)
{
    return RocketTowerBuilding_OnDeath(P(ecx), a0);
}
ABI_ENTRY_00451160(RocketTowerBuilding_OnDeath_Entry, RocketTowerBuilding_OnDeath_Abi)
HOOK(0x00451160, "RocketTowerBuilding_OnDeath", RocketTowerBuilding_OnDeath_Entry, nullptr);

// ===============================================================================================================
// Object base class (vtable 0x005b0b20).

// 0x004511d0 (edx = object; callers 0x00437290, 0x0045f020, ...). The Object constructor: vtable 0x005b0b20, clears
// the members, +0x370 = -1.0, +0x39c = +0x398 = +0x388 = -1 (force action handles, scale index), +0x3e4 = 7 (shadow
// exponent default). Returns the object.
uint32_t Object_Construct(void* o)
{
    D(o, 0) = 0x005b0b20;
    D(o, 0x1c0) = 0;
    D(o, 0x1cc) = 0;
    D(o, 0x1c4) = 0;
    D(o, 0x1bc) = 0;
    D(o, 0x1c8) = 0;
    Field<uint8_t>(o, 0x421) = 0;
    D(o, 0x2e4) = 0;
    D(o, 0x2e0) = 0;
    D(o, 0x2dc) = 0;
    D(o, 0x424) = 0;
    D(o, 0x28) = 0;
    D(o, 0x390) = 0;
    D(o, 0x2c) = 0;
    D(o, 0x30) = 0;
    D(o, 0x410) = 0;
    D(o, 0x1d0) = 0;
    D(o, 0x374) = 0;
    D(o, 0x414) = 0;
    D(o, 0x370) = 0xbf800000u;
    D(o, 0x39c) = 0xffffffffu;
    D(o, 0x398) = 0xffffffffu;
    for (uint32_t i = 0; i < 0xb; ++i)
        D(o, 0x428 + 4 * i) = 0;
    D(o, 0x400) = 0;
    D(o, 0x404) = 0;
    D(o, 0x408) = 0;
    D(o, 0x40c) = 0;
    D(o, 0x3ec) = 0;
    D(o, 0x3e0) = 0;
    D(o, 0x3e8) = 0;
    D(o, 0x388) = 0xffffffffu;
    D(o, 0x3e4) = 7;
    return u(o);
}
static uint32_t Object_Construct_Abi(uint32_t edx)
{
    return Object_Construct(P(edx));
}
ABI_ENTRY_004511d0(Object_Construct_Entry, Object_Construct_Abi)
HOOK(0x004511d0, "Object_Construct", Object_Construct_Entry, nullptr);

// 0x004512b0 (stack: object; ret 4; caller 0x0045da20). The Object destructor: vtable 0x005b0b20, unlinks from every
// list (0x004546f0), 0x00487df0 (esi), 0x00470c00 (eax = object; Game+0xaa4); the sound +0x410 stops (0x004ed1e0, esi)
// when the template's +8 is not negative; +0x410 = 0; the membership list +0x2d8 is deleted (vt+0x10(1)); the bodies
// +0x1bc (0x00456fa0, edi) and +0x1c4 (0x00457000, esi) are released to Game+0xa8c; +0x1bc = 0, byte +0x421 = 0.
uint32_t Object_Destruct(uint32_t obj)
{
    void* o = P(obj);
    D(o, 0) = 0x005b0b20;
    Object_UnlinkAll(obj);
    orig::fn_00487df0(obj);
    orig::fn_00470c00(obj, D(G(), 0xaa4));
    const uint32_t s = D(o, 0x410);
    if (s && static_cast<int32_t>(D(P(D(o, 0x414)), 8)) >= 0)
        orig::fn_004ed1e0(s);
    D(o, 0x410) = 0;
    void* m = Field<void*>(o, 0x2d8);
    if (m)
        VCall<uint32_t>(m, 0x10 / 4, 1u);
    const uint32_t b1 = D(o, 0x1bc);
    D(o, 0x2d8) = 0;
    orig::fn_00456fa0(b1, D(G(), 0xa8c));
    const uint32_t b2 = D(o, 0x1c4);
    D(o, 0x1bc) = 0;
    Field<uint8_t>(o, 0x421) = 0;
    return orig::fn_00457000(b2, D(G(), 0xa8c));
}
static uint32_t Object_Destruct_Abi(uint32_t a0)
{
    return Object_Destruct(a0);
}
ABI_ENTRY_004512b0(Object_Destruct_Entry, Object_Destruct_Abi)
HOOK(0x004512b0, "Object_Destruct", Object_Destruct_Entry, nullptr);

// 0x00451360 (eax = values; stack: object, set; ret 8; caller 0x004517b0). Object_LoadBaseProps: tag +0x390,
// max_health +0x458 (default the current one, 0x004555c0; health +0x454 = it), fx_death +0x3a0 and husk +0x3c0 (default
// "null"); when a husk is given, husk1..husk9 are read too and one of the n names is picked at random ((_rand * n) >>
// 15). scale_index +0x388, ref_anim_name +0x4 (placement, then template, else sprintf "%s_ref" of the class name),
// movable (0x00457810), inert (+0x424 |= 0x20000), speed_trap (0x100000), visible (default on; off -> vt+0xf4(0)),
// shadow_radius_max +0x3fc, shadow_radius_min +0x3f8, shadow_exponent +0x3e4 (7), shadow_blur +0x3e8, shadow_enable
// (+0x3ec |= 3), shadow_drawlocked (4), shadow_castontoobjects (0x20), shadow_receive (0x10, on), decal_receive (8, on);
// +0x3f4 = +0x3f0 = 1.0. object_lock names an object to create (0x0045f020) and attach to this one (Object_Attach
// 0x004539f0, names from the template's object_lock_parent / object_lock_child, flags 1). Returns the last eax.
uint32_t Object_LoadBaseProps(uint32_t values, void* obj, uint32_t set)
{
    const uint32_t o = u(obj);
    // Original frame (0x184 bytes): +3 bool, +4 "huskN", +0x24 object_lock name, +0x44 husk names (10 x 0x20).
    uint8_t fr[0x184];
    uint8_t& b = fr[3];
    char* nm = reinterpret_cast<char*>(&fr[4]);
    char* lockName = reinterpret_cast<char*>(&fr[0x24]);
    char* husks = reinterpret_cast<char*>(&fr[0x44]);
    Object_ReadValuePropEbx(0, 0x005b0ea8, values, o + 0x390, o, set, 0);  // tag
    const float mh = Object_GetMaxHealth(obj);
    Object_ReadValuePropDef(values, o + 0x458, o, 0x005b0e9c, set, f2u(mh), 0);  // max_health
    D(obj, 0x454) = D(obj, 0x458);
    Object_ReadStringProp(0x005bba94, o + 0x3a0, o, 0x005b0e90, set, values, 0);  // fx_death
    if (Object_ReadStringProp(0x005bba94, o + 0x3c0, o, 0x005b0e88, set, values, 0))  // husk
    {
        Strcpy(u(husks), o + 0x3c0);
        std::memcpy(nm, reinterpret_cast<const void*>(0x005b0e88), 4);
        nm[4] = GameGlobal<char>(0x005b0e8c);
        nm[5] = 0;
        int32_t n = 1;
        do
        {
            nm[4] = static_cast<char>(n + 0x30);
            if (!Object_ReadStringProp(0x0058a9a4, u(husks + n * 0x20), o, u(nm), set, values, 0))
                break;
            ++n;
        } while (n <= 9);
        const int32_t idx = (static_cast<int32_t>(static_cast<uint32_t>(orig::fn_005391cd())) * n) >> 15;
        Strcpy(o + 0x3c0, u(husks + idx * 0x20));
    }
    Object_ReadValuePropEbx(D(obj, 0x388), 0x005b0e7c, values, o + 0x388, o, set, 0);  // scale_index
    if (!static_cast<uint8_t>(ParamSet_GetString(set, 0x005b0e6c, values, D(G(), 0xa84), o + 4)) &&
        !static_cast<uint8_t>(Object_GetTemplateString(o, 0x005b0e6c, o + 4)))  // ref_anim_name
        orig::fn_0053814f(o + 4, 0x005b0e64, D(P(D(obj, 0x414)), 0x10) + 4);
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0e5c, set, 0);  // movable
    if (b)
        orig::fn_00457810(o);
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0e54, set, 0);  // inert
    if (b)
        D(obj, 0x424) |= 0x20000;
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0e48, set, 0);  // speed_trap
    if (b)
        D(obj, 0x424) |= 0x100000;
    Object_ReadBoolProp(1, values, u(&b), o, 0x005bba64, set, 0);  // visible
    if (!b)
        VCall<uint32_t>(obj, 0xf4 / 4, 0u);
    Object_ReadValuePropDef(values, o + 0x3fc, o, 0x005b0e34, set, 0, 0);  // shadow_radius_max
    Object_ReadValuePropDef(values, o + 0x3f8, o, 0x005b0e20, set, 0, 0);  // shadow_radius_min
    Object_ReadValuePropEbx(7, 0x005b0e10, values, o + 0x3e4, o, set, 0);  // shadow_exponent
    Object_ReadValuePropEbx(0, 0x005b0e04, values, o + 0x3e8, o, set, 0);  // shadow_blur
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0df4, set, 0);          // shadow_enable
    if (b)
        D(obj, 0x3ec) |= 3;
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0de0, set, 0);  // shadow_drawlocked
    if (b)
        D(obj, 0x3ec) |= 4;
    Object_ReadBoolProp(0, values, u(&b), o, 0x005b0dc8, set, 0);  // shadow_castontoobjects
    if (b)
        D(obj, 0x3ec) |= 0x20;
    Object_ReadBoolProp(1, values, u(&b), o, 0x005b0db8, set, 0);  // shadow_receive
    if (b)
        D(obj, 0x3ec) |= 0x10;
    Object_ReadBoolProp(1, values, u(&b), o, 0x005b0da8, set, 0);  // decal_receive
    if (b)
        D(obj, 0x3ec) |= 8;
    D(obj, 0x3f4) = 0x3f800000;
    D(obj, 0x3f0) = 0x3f800000;
    uint32_t r = Object_ReadStringProp(0x005bba94, u(lockName), o, 0x005b0d9c, set, values, 0);  // object_lock
    if (r)
    {
        uint32_t t = TemplateParams(o);
        const uint32_t parentName = ParamSet_FindValue(D(P(t), 0), 0x005b0d88, D(P(t), 8), D(G(), 0xa84));
        t = TemplateParams(o);
        const uint32_t childName = ParamSet_FindValue(D(P(t), 0), 0x005b0d74, D(P(t), 8), D(G(), 0xa84));
        const uint32_t made = orig::fn_0045f020(0, u(lockName), D(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
        r = static_cast<uint32_t>(Object_Attach(parentName, obj, P(made), childName, 1, 0));
    }
    return r;
}
static uint32_t Object_LoadBaseProps_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return Object_LoadBaseProps(eax, P(a0), a1);
}
ABI_ENTRY_00451360(Object_LoadBaseProps_Entry, Object_LoadBaseProps_Abi)
HOOK(0x00451360, "Object_LoadBaseProps", Object_LoadBaseProps_Entry, nullptr);

// 0x00451780 (ecx = object). Object slot 0x10: when the class template (+0x414 -> +0x10) has more than one entry
// (+0x24), looks the object's name +4 up in the template's index (0x004f1160, eax = +0); returns the entry's first
// dword when found and not negative, else 1; 0 for single-entry templates. Not executed in recorded runs.
uint32_t fn_00451780(void* obj)
{
    void* t = Field<void*>(Field<void*>(obj, 0x414), 0x10);
    if (D(t, 0x24) <= 1)
        return 0;
    const uint32_t e = orig::fn_004f1160(D(t, 0), u(obj) + 4);
    if (!e)
        return 1;
    const uint32_t v = D(P(e), 0);
    if (static_cast<int32_t>(v) < 0)
        return 1;
    return v;
}
static uint32_t fn_00451780_Abi(uint32_t ecx)
{
    return fn_00451780(P(ecx));
}
ABI_ENTRY_00451780(fn_00451780_Entry, fn_00451780_Abi)
HOOK(0x00451780, "fn_00451780", fn_00451780_Entry, nullptr);

// 0x00452790 (ecx = object; stack: float* out; ret 4). Object slot 0x12: a speed trap (+0x424 bit 0x100000) whose
// template byte +0x4c bit 0 is clear writes 1.0 and returns al = 1; otherwise writes 0 and returns 0 (out may be null).
// Not executed in recorded runs.
uint32_t fn_00452790(void* obj, uint32_t out)
{
    if ((D(obj, 0x424) & 0x100000) && !(Field<uint8_t>(Field<void*>(obj, 0x414), 0x4c) & 1))
    {
        if (out)
            D(P(out), 0) = 0x3f800000;
        return (out & 0xffffff00u) | 1u;
    }
    if (out)
        D(P(out), 0) = 0;
    return out & 0xffffff00u;
}
static uint32_t fn_00452790_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00452790(P(ecx), a0);
}
ABI_ENTRY_00452790(fn_00452790_Entry, fn_00452790_Abi)
HOOK(0x00452790, "fn_00452790", fn_00452790_Entry, nullptr);

// 0x004531e0 (ecx = object). True when +0x424 bit 0x40 is set, else vt+0x80(&c, &a, &b) and a > 0. Callers include
// 0x00418190, 0x00418610 (shadows).
uint32_t fn_004531e0(void* obj)
{
    if (Field<uint8_t>(obj, 0x424) & 0x40)
        return 1;
    float l[3];  // original frame: +0 a, +4 b, +8 c
    VCall<uint32_t>(obj, 0x80 / 4, u(&l[2]), u(&l[0]), u(&l[1]));
    return l[0] > kZero ? 1u : 0u;
}
static uint32_t fn_004531e0_Abi(uint32_t ecx)
{
    return fn_004531e0(P(ecx));
}
ABI_ENTRY_004531e0(fn_004531e0_Entry, fn_004531e0_Abi)
HOOK(0x004531e0, "fn_004531e0", fn_004531e0_Entry, nullptr);

// 0x00453230 (returns st0). Object slot 0x21: returns 0.0 ([0x0056c05c]). Not executed in recorded runs.
float fn_00453230()
{
    return kZero;
}
static float fn_00453230_Abi()
{
    return fn_00453230();
}
ABI_ENTRY_00453230(fn_00453230_Entry, fn_00453230_Abi)
HOOK(0x00453230, "fn_00453230", fn_00453230_Entry, nullptr);

// 0x00453240 (stack: a, b, c; ret 0xc). Object slot 0x20: *a = 0, *b = 0, *c = 1.0. Returns a. Not executed in
// recorded runs.
uint32_t fn_00453240(uint32_t a, uint32_t b, uint32_t c)
{
    D(P(a), 0) = 0;
    D(P(b), 0) = 0;
    D(P(c), 0) = 0x3f800000;
    return a;
}
static uint32_t fn_00453240_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00453240(a0, a1, a2);
}
ABI_ENTRY_00453240(fn_00453240_Entry, fn_00453240_Abi)
HOOK(0x00453240, "fn_00453240", fn_00453240_Entry, nullptr);

// 0x00453270 (eax = object). Object_GetName: the class template's name (+0x414 -> +0x10, +4), or "" (0x0058a9a4).
uint32_t Object_GetName(void* obj)
{
    const uint32_t t = D(obj, 0x414);
    if (t)
    {
        const uint32_t c = D(P(t), 0x10);
        if (c)
            return c + 4;
    }
    return 0x0058a9a4;
}
static uint32_t Object_GetName_Abi(uint32_t eax)
{
    return Object_GetName(P(eax));
}
ABI_ENTRY_00453270(Object_GetName_Entry, Object_GetName_Abi)
HOOK(0x00453270, "Object_GetName", Object_GetName_Entry, nullptr);

// 0x00453290 (eax = object, ecx = out). When the tag +0x390 is set, *out = tag and al = 1; else al = 0.
uint32_t fn_00453290(void* obj, uint32_t out)
{
    const uint32_t t = D(obj, 0x390);
    if (!t)
        return 0;
    D(P(out), 0) = t;
    return (t & 0xffffff00u) | 1u;
}
static uint32_t fn_00453290_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00453290(P(eax), ecx);
}
ABI_ENTRY_00453290(fn_00453290_Entry, fn_00453290_Abi)
HOOK(0x00453290, "fn_00453290", fn_00453290_Entry, nullptr);

// 0x004532b0 (ecx = object; stack: a0, velocity out, a2; ret 0xc). Object slot 0x14: outside cut scenes (Game+0x2684
// == 0) Body_getLinearVelocity 0x00458000 (eax = object, ecx = a1) then vt+0x58(a0). Not executed in recorded runs.
uint32_t fn_004532b0(void* obj, uint32_t a0, uint32_t a1, uint32_t /*a2*/)
{
    if (D(G(), 0x2684))
        return u(G());
    orig::fn_00458000(u(obj), a1);
    return VCall<uint32_t>(obj, 0x58 / 4, a0);
}
static uint32_t fn_004532b0_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004532b0(P(ecx), a0, a1, a2);
}
ABI_ENTRY_004532b0(fn_004532b0_Entry, fn_004532b0_Abi)
HOOK(0x004532b0, "fn_004532b0", fn_004532b0_Entry, nullptr);

// 0x00453540 (eax = object, cl = on). Sets or clears +0x424 bit 0x400.
void fn_00453540(void* obj, uint32_t on)
{
    if (static_cast<uint8_t>(on))
        D(obj, 0x424) |= 0x400;
    else
        D(obj, 0x424) &= 0xfffffbffu;
}
static void fn_00453540_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00453540(P(eax), ecx);
}
ABI_ENTRY_00453540(fn_00453540_Entry, fn_00453540_Abi)
HOOK(0x00453540, "fn_00453540", fn_00453540_Entry, nullptr);

// 0x00453570 (eax = object). al = +0x424 bit 0x400 (upper eax bits: +0x424 >> 10, as the original).
uint32_t fn_00453570(void* obj)
{
    const uint32_t v = D(obj, 0x424) >> 10;
    return (v & 0xffffff00u) | (v & 1u);
}
static uint32_t fn_00453570_Abi(uint32_t eax)
{
    return fn_00453570(P(eax));
}
ABI_ENTRY_00453570(fn_00453570_Entry, fn_00453570_Abi)
HOOK(0x00453570, "fn_00453570", fn_00453570_Entry, nullptr);

// 0x00453580 (ecx = object; stack: v; ret 4; caller 0x0042eb60). Object slot 0x40: +0x378 = v and +0x424 bit 0x400
// set (0x00453540). Returns the object (eax).
uint32_t fn_00453580(void* obj, uint32_t v)
{
    D(obj, 0x378) = v;
    fn_00453540(obj, 1);
    return u(obj);
}
static uint32_t fn_00453580_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00453580(P(ecx), a0);
}
ABI_ENTRY_00453580(fn_00453580_Entry, fn_00453580_Abi)
HOOK(0x00453580, "fn_00453580", fn_00453580_Entry, nullptr);

// 0x004535a0 (eax = object). al = +0x424 bit 0x10 (scaled); the upper eax bits stay the object's.
uint32_t fn_004535a0(void* obj)
{
    return (u(obj) & 0xffffff00u) | ((Field<uint8_t>(obj, 0x424) >> 4) & 1u);
}
static uint32_t fn_004535a0_Abi(uint32_t eax)
{
    return fn_004535a0(P(eax));
}
ABI_ENTRY_004535a0(fn_004535a0_Entry, fn_004535a0_Abi)
HOOK(0x004535a0, "fn_004535a0", fn_004535a0_Entry, nullptr);

// 0x004535d0 (eax = vec3 scale, ecx = object). Object_SetScale: +0x424 bit 0x10 set unless the scale is exactly (1, 1,
// 1) (bit compare), +0x37c = scale (read back by 0x004535b0 for Matrix_Scale), +0x370 = -1.0 (matrix cache time).
// Returns the scale's z bits (eax).
uint32_t Object_SetScale(uint32_t vec, void* obj)
{
    const uint32_t* s = static_cast<const uint32_t*>(P(vec));
    if (s[0] == 0x3f800000 && s[1] == 0x3f800000 && s[2] == 0x3f800000)
        D(obj, 0x424) &= 0xffffffefu;
    else
        D(obj, 0x424) |= 0x10;
    D(obj, 0x37c) = s[0];
    D(obj, 0x380) = s[1];
    D(obj, 0x384) = s[2];
    D(obj, 0x370) = 0xbf800000u;
    return s[2];
}
static uint32_t Object_SetScale_Abi(uint32_t eax, uint32_t ecx)
{
    return Object_SetScale(eax, P(ecx));
}
ABI_ENTRY_004535d0(Object_SetScale_Entry, Object_SetScale_Abi)
HOOK(0x004535d0, "Object_SetScale", Object_SetScale_Entry, nullptr);

// 0x00453630 (eax = object; returns st0). Returns +0x378 (set by 0x00453580).
float fn_00453630(void* obj)
{
    return F(obj, 0x378);
}
static float fn_00453630_Abi(uint32_t eax)
{
    return fn_00453630(P(eax));
}
ABI_ENTRY_00453630(fn_00453630_Entry, fn_00453630_Abi)
HOOK(0x00453630, "fn_00453630", fn_00453630_Entry, nullptr);

// 0x00453670 (eax = object). Unless the object is already in the list Game+0xaa0 +0x3c (ObjectList_Contains
// 0x00454690), links it there (0x004546d0). Returns eax (and edx for 0x004539f0) of the last call.
uint64_t fn_00453670(void* obj)
{
    const uint32_t r = ObjectList_Contains(D(P(D(G(), 0xaa0)), 0x3c), u(obj));
    if (static_cast<uint8_t>(r))
        return r;
    return ObjectList_Link(D(P(D(G(), 0xaa0)), 0x3c), u(obj));
}
static uint32_t fn_00453670_Abi(uint32_t eax)
{
    return static_cast<uint32_t>(fn_00453670(P(eax)));
}
ABI_ENTRY_00453670(fn_00453670_Entry, fn_00453670_Abi)
HOOK(0x00453670, "fn_00453670", fn_00453670_Entry, nullptr);

// 0x00453640 (eax = object, ecx = mask, dl = on; caller 0x004536b0). +0x400 |= mask (on) or &= ~mask, then a tail
// jump to 0x00453670.
uint32_t fn_00453640(void* obj, uint32_t mask, uint32_t on)
{
    if (static_cast<uint8_t>(on))
        D(obj, 0x400) |= mask;
    else
        D(obj, 0x400) &= ~mask;
    return static_cast<uint32_t>(fn_00453670(obj));
}
static uint32_t fn_00453640_Abi(uint32_t eax, uint32_t ecx, uint32_t edx)
{
    return fn_00453640(P(eax), ecx, edx);
}
ABI_ENTRY_00453640(fn_00453640_Entry, fn_00453640_Abi)
HOOK(0x00453640, "fn_00453640", fn_00453640_Entry, nullptr);

// 0x004536b0 (ecx = object; stack: show; ret 4; caller 0x0043eac0). Object slot 0x3d: when show differs from
// 0x00453740 (not hidden), clears (show) or sets (hide) +0x424 bit 4, links to (0x004546d0) or unlinks from
// (0x00454750) the list Game+0xa84 +0xe78 (the shadow list, see shadows.cpp), then 0x00453640(mask 1, show).
uint32_t fn_004536b0(void* obj, uint32_t show)
{
    const uint32_t cur = fn_00453740(obj);
    const uint8_t s = static_cast<uint8_t>(show);
    if (static_cast<uint8_t>(cur) == s)
        return cur;
    if (s)
    {
        D(obj, 0x424) &= 0xfffffffbu;
        ObjectList_Link(D(Objs(), 0xe78), u(obj));
        return fn_00453640(obj, 1, 1);
    }
    D(obj, 0x424) |= 4;
    ObjectList_Unlink(D(Objs(), 0xe78), u(obj));
    return fn_00453640(obj, 1, 0);
}
static uint32_t fn_004536b0_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_004536b0(P(ecx), a0);
}
ABI_ENTRY_004536b0(fn_004536b0_Entry, fn_004536b0_Abi)
HOOK(0x004536b0, "fn_004536b0", fn_004536b0_Entry, nullptr);

// 0x00453740 (eax = object). al = !(+0x424 bit 4) (upper eax bits: +0x424 >> 2, as the original).
uint32_t fn_00453740(void* obj)
{
    const uint32_t v = D(obj, 0x424) >> 2;
    return (v & 0xffffff00u) | (~v & 1u);
}
static uint32_t fn_00453740_Abi(uint32_t eax)
{
    return fn_00453740(P(eax));
}
ABI_ENTRY_00453740(fn_00453740_Entry, fn_00453740_Abi)
HOOK(0x00453740, "fn_00453740", fn_00453740_Entry, nullptr);

// 0x004539d0 (eax = object). Object_GetParent: +0x34 when attached (+0x424 bits 1|2), else 0.
uint32_t Object_GetParent(void* obj)
{
    if (Field<uint8_t>(obj, 0x424) & 3)
        return D(obj, 0x34);
    return 0;
}
static uint32_t Object_GetParent_Abi(uint32_t eax)
{
    return Object_GetParent(P(eax));
}
ABI_ENTRY_004539d0(Object_GetParent_Entry, Object_GetParent_Abi)
HOOK(0x004539d0, "Object_GetParent", Object_GetParent_Entry, nullptr);

// 0x004539e0 (eax = object). al = (+0x34 != 0); the upper eax bits stay the object's.
uint32_t fn_004539e0(void* obj)
{
    return (u(obj) & 0xffffff00u) | (D(obj, 0x34) != 0 ? 1u : 0u);
}
static uint32_t fn_004539e0_Abi(uint32_t eax)
{
    return fn_004539e0(P(eax));
}
ABI_ENTRY_004539e0(fn_004539e0_Entry, fn_004539e0_Abi)
HOOK(0x004539e0, "fn_004539e0", fn_004539e0_Entry, nullptr);

namespace
{
void Identity(uint32_t m)
{
    for (uint32_t i = 0; i < 0x11; ++i)
        D(P(m), 4 * i) = 0;
    D(P(m), 0) = 0x3f800000;
    D(P(m), 0x14) = 0x3f800000;
    D(P(m), 0x28) = 0x3f800000;
    D(P(m), 0x3c) = 0x3f800000;
}
}  // namespace

// 0x004539f0 (eax = parent bone name, ecx = parent, edx = object; stack: child bone name, flags, a2; ret 0xc).
// Object_Attach: detaches (0x00453b20), references the parent (+0x28) into +0x34; +0xec..+0xf8 = 0 and +0x80 = identity
// (offset matrix); the parent bone name is copied to +0xcc (+0xc4 = it or 0), +0xc8 = &+0xec; +0x1ac..+0x1b8 = 0, +0x140
// = identity, the child bone name to +0x18c (+0x184 = it or 0); +0x38 = a2, +0x188 = &+0x1ac; +0x424 |= flags | 1;
// 0x004577b0 (eax = object; 1) and 0x00453670. Returns eax edx of 0x00453670.
uint64_t Object_Attach(uint32_t name, void* parent, void* obj, uint32_t name2, uint32_t flags, uint32_t a2)
{
    const uint32_t o = u(obj);
    Object_Detach(obj);
    ++Field<int32_t>(parent, 0x28);
    D(obj, 0x34) = u(parent);
    D(obj, 0xec) = 0;
    D(obj, 0xf0) = 0;
    D(obj, 0xf4) = 0;
    D(obj, 0xf8) = 0;
    Identity(o + 0x80);
    if (name)
    {
        Strcpy(o + 0xcc, name);
        D(obj, 0xc4) = o + 0xcc;
    }
    else
        D(obj, 0xc4) = 0;
    D(obj, 0xc8) = o + 0xec;
    D(obj, 0x1ac) = 0;
    D(obj, 0x1b0) = 0;
    D(obj, 0x1b4) = 0;
    D(obj, 0x1b8) = 0;
    Identity(o + 0x140);
    if (name2)
    {
        Strcpy(o + 0x18c, name2);
        D(obj, 0x184) = o + 0x18c;
    }
    else
        D(obj, 0x184) = 0;
    D(obj, 0x38) = a2;
    D(obj, 0x188) = o + 0x1ac;
    D(obj, 0x424) |= flags | 1;
    orig::fn_004577b0(o, 1);
    return fn_00453670(obj);
}
static uint64_t Object_Attach_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Object_Attach(eax, P(ecx), P(edx), a0, a1, a2);
}
ABI_ENTRY_004539f0(Object_Attach_Entry, Object_Attach_Abi)
HOOK(0x004539f0, "Object_Attach", Object_Attach_Entry, nullptr);

// 0x00453b20 (esi = object). Object_Detach: when attached (+0x424 bit 1) clears it, releases the parent +0x34 (= 0),
// 0x004577b0 (eax = object; 0) when +0x38 bit 0x10, then 0x00453670; always clears +0x424 bit 2.
uint32_t Object_Detach(void* obj)
{
    uint32_t r = 0;
    if (Field<uint8_t>(obj, 0x424) & 1)
    {
        D(obj, 0x424) &= 0xfffffffeu;
        ReleaseRef(Field<void*>(obj, 0x34));
        D(obj, 0x34) = 0;
        if (Field<uint8_t>(obj, 0x38) & 0x10)
            orig::fn_004577b0(u(obj), 0);
        r = static_cast<uint32_t>(fn_00453670(obj));
    }
    D(obj, 0x424) &= 0xfffffffdu;
    return r;
}
static uint32_t Object_Detach_Abi(uint32_t esi)
{
    return Object_Detach(P(esi));
}
ABI_ENTRY_00453b20(Object_Detach_Entry, Object_Detach_Abi)
HOOK(0x00453b20, "Object_Detach", Object_Detach_Entry, nullptr);

// 0x004540a0 (eax = object; callers 0x00420770, 0x004517b0). +0x2d4 (last update time) = -1.0.
void fn_004540a0(void* obj)
{
    D(obj, 0x2d4) = 0xbf800000u;
}
static void fn_004540a0_Abi(uint32_t eax)
{
    fn_004540a0(P(eax));
}
ABI_ENTRY_004540a0(fn_004540a0_Entry, fn_004540a0_Abi)
HOOK(0x004540a0, "fn_004540a0", fn_004540a0_Entry, nullptr);

// 0x004540b0 (ecx = object). Object slot 0xf (update), once per game time (+0x2d4): references itself (+0x28); with an
// animated template (+0x414 -> +0x10 -> +0x28) vt+0x9c(), clears +0x424 bit 0x80 and advances the animation
// (0x00409ec0, eax = object; frame time, 0); 0x00453750(object); when vt+0xd8() and +0x424 bit 0x100: animation events
// (0x0040a200) and, still alive, vt+0xbc() and 0x0040a3f0; then the self reference is released (tail jump to 0x00487da0
// at zero). Not executed in recorded runs.
uint32_t Object_Update(void* obj)
{
    const float gt = kGameTime;
    if (gt == F(obj, 0x2d4))
        return 0;
    F(obj, 0x2d4) = gt;
    ++Field<int32_t>(obj, 0x28);
    if (D(obj, 0x414) && D(P(D(P(D(obj, 0x414)), 0x10)), 0x28))
    {
        VCall<uint32_t>(obj, 0x9c / 4);
        const uint32_t dt = GameGlobal<uint32_t>(0x00604a90);
        D(obj, 0x424) &= 0xffffff7fu;
        orig::fn_00409ec0(u(obj), dt, 0);
    }
    orig::fn_00453750(u(obj));
    if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0xd8 / 4)) && (D(obj, 0x424) & 0x100))
    {
        bool go = true;
        if (D(obj, 0x414))
        {
            orig::fn_0040a200(u(obj));
            go = static_cast<uint8_t>(VCall<uint32_t>(obj, 0xd8 / 4)) != 0;
        }
        if (go)
        {
            VCall<uint32_t>(obj, 0xbc / 4);
            if (D(obj, 0x414))
                orig::fn_0040a3f0(u(obj), u(obj));
        }
    }
    return ReleaseRef(obj);
}
static uint32_t Object_Update_Abi(uint32_t ecx)
{
    return Object_Update(P(ecx));
}
ABI_ENTRY_004540b0(Object_Update_Entry, Object_Update_Abi)
HOOK(0x004540b0, "Object_Update", Object_Update_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Object list membership. A list is an object with vt+0x18 add, +0x1c find, +0x28 remove, +0x34 count; every object
// keeps the lists it is in at +0x2d8 (same interface).

// 0x00454690 (esi = list, edi = object). ObjectList_Contains: searches the smaller side (list vt+0x34 count against
// the object's +0x2d8 count): list vt+0x1c(object) or +0x2d8 vt+0x1c(list); returns 1 when found.
uint32_t ObjectList_Contains(uint32_t list, uint32_t obj)
{
    const uint32_t n1 = VCall<uint32_t>(Field<void*>(P(obj), 0x2d8), 0x34 / 4);
    const uint32_t n2 = VCall<uint32_t>(P(list), 0x34 / 4);
    if (n2 < n1)
        return VCall<uint32_t>(P(list), 0x1c / 4, obj) != 0 ? 1u : 0u;
    return VCall<uint32_t>(Field<void*>(P(obj), 0x2d8), 0x1c / 4, list) != 0 ? 1u : 0u;
}
static uint32_t ObjectList_Contains_Abi(uint32_t esi, uint32_t edi)
{
    return ObjectList_Contains(esi, edi);
}
ABI_ENTRY_00454690(ObjectList_Contains_Entry, ObjectList_Contains_Abi)
HOOK(0x00454690, "ObjectList_Contains", ObjectList_Contains_Entry, nullptr);

// 0x004546d0 (esi = list, edi = object). ObjectList_Link: list vt+0x18(object), object +0x2d8 vt+0x18(list). Returns
// eax edx of the second call.
uint64_t ObjectList_Link(uint32_t list, uint32_t obj)
{
    VCall<uint32_t>(P(list), 0x18 / 4, obj);
    return VCall<uint64_t>(Field<void*>(P(obj), 0x2d8), 0x18 / 4, list);
}
static uint64_t ObjectList_Link_Abi(uint32_t esi, uint32_t edi)
{
    return ObjectList_Link(esi, edi);
}
ABI_ENTRY_004546d0(ObjectList_Link_Entry, ObjectList_Link_Abi)
HOOK(0x004546d0, "ObjectList_Link", ObjectList_Link_Entry, nullptr);

// 0x004546f0 (ebx = object; callers 0x004512b0, 0x004547b0, 0x0048d250). Object_UnlinkAll: for each list in +0x2d8
// (iterator 0x004e7340 / 0x004e73c0) ObjectList_Unlink 0x00454750.
uint32_t Object_UnlinkAll(uint32_t obj)
{
    uint32_t it[2] = {0, 0};
    const uint32_t m = D(P(obj), 0x2d8);
    if (!m)
        return 0;
    for (uint32_t e = orig::fn_004e7340(m, u(it)); e; e = orig::fn_004e73c0(u(it)))
        ObjectList_Unlink(e, obj);
    EndIter(it);
    return it[1];
}
static uint32_t Object_UnlinkAll_Abi(uint32_t ebx)
{
    return Object_UnlinkAll(ebx);
}
ABI_ENTRY_004546f0(Object_UnlinkAll_Entry, Object_UnlinkAll_Abi)
HOOK(0x004546f0, "Object_UnlinkAll", Object_UnlinkAll_Entry, nullptr);

// 0x00454750 (esi = list, edi = object). ObjectList_Unlink: list vt+0x28(object), object +0x2d8 vt+0x28(list).
uint32_t ObjectList_Unlink(uint32_t list, uint32_t obj)
{
    VCall<uint32_t>(P(list), 0x28 / 4, obj);
    return VCall<uint32_t>(Field<void*>(P(obj), 0x2d8), 0x28 / 4, list);
}
static uint32_t ObjectList_Unlink_Abi(uint32_t esi, uint32_t edi)
{
    return ObjectList_Unlink(esi, edi);
}
ABI_ENTRY_00454750(ObjectList_Unlink_Entry, ObjectList_Unlink_Abi)
HOOK(0x00454750, "ObjectList_Unlink", ObjectList_Unlink_Entry, nullptr);

// 0x00454770 (ecx = object). Object slot 0x36: al = !(+0x424 bit 8) (bit 8 = removed, set by Object_Remove). Not
// executed in recorded runs.
uint32_t fn_00454770(void* obj)
{
    return (~(Field<uint8_t>(obj, 0x424) >> 3)) & 1u;
}
static uint32_t fn_00454770_Abi(uint32_t ecx)
{
    return fn_00454770(P(ecx));
}
ABI_ENTRY_00454770(fn_00454770_Entry, fn_00454770_Abi)
HOOK(0x00454770, "fn_00454770", fn_00454770_Entry, nullptr);

// 0x00454780 (esi = object). al = 1 when vt+0xd8() is false or the health +0x454 is <= 0 (guess: "is dead").
uint32_t Object_IsDead(void* obj)
{
    const uint32_t r = VCall<uint32_t>(obj, 0xd8 / 4);
    if (!static_cast<uint8_t>(r))
        return (r & 0xffffff00u) | 1u;
    return F(obj, 0x454) <= kZero ? 1u : 0u;
}
static uint32_t Object_IsDead_Abi(uint32_t esi)
{
    return Object_IsDead(P(esi));
}
ABI_ENTRY_00454780(Object_IsDead_Entry, Object_IsDead_Abi)
HOOK(0x00454780, "Object_IsDead", Object_IsDead_Entry, nullptr);

// 0x004547b0 (ecx = object). Object_Remove (Object slot 0x37), once (+0x424 bit 8; returns 8 when already removed):
// decal notification 0x00416620 (eax = Game+0xa7c, edi = object) when +0x3ec has 0x60000000; the force actions +0x39c
// and +0x398 are released (0x00455a00); the shadow +0x3e0 loses its owner (0x00417b90, ecx = shadow, edx = 0) and is
// cleared; Object_Detach; 0x0048f040 (eax = object, ebx = Game+0xaa0) when set; Object_UnlinkAll; 0x00459630 (esi =
// object; Game+0xa84 + 0xf2c). Unreferenced objects go to 0x00487da0, others to the list Game+0xa84 +0xf28 (vt+0x18);
// then 0x00458dd0(object, 0, 0, 0) and a tail jump to 0x00458db0 (eax = object).
uint32_t Object_Remove(void* obj)
{
    if (Field<uint8_t>(obj, 0x424) & 8)
        return 8;
    D(obj, 0x424) |= 8;
    if ((D(obj, 0x3ec) & 0x60000000) && D(G(), 0xa7c))
        orig::fn_00416620(D(G(), 0xa7c), u(obj));
    D(obj, 0x39c) = fn_00455a00(D(obj, 0x39c));
    D(obj, 0x398) = fn_00455a00(D(obj, 0x398));
    const uint32_t sh = D(obj, 0x3e0);
    if (sh)
    {
        orig::fn_00417b90(sh, 0);
        D(obj, 0x3e0) = 0;
    }
    Object_Detach(obj);
    const uint32_t aa0 = D(G(), 0xaa0);
    if (aa0)
        orig::fn_0048f040(u(obj), aa0);
    Object_UnlinkAll(u(obj));
    orig::fn_00459630(u(obj), D(G(), 0xa84) + 0xf2c);
    if (D(obj, 0x28) == 0)
        orig::fn_00487da0(u(obj));
    else
        VCall<uint32_t>(Field<void*>(Objs(), 0xf28), 0x18 / 4, u(obj));
    orig::fn_00458dd0(u(obj), 0, 0, 0);
    return orig::fn_00458db0(u(obj));
}
static uint32_t Object_Remove_Abi(uint32_t ecx)
{
    return Object_Remove(P(ecx));
}
ABI_ENTRY_004547b0(Object_Remove_Entry, Object_Remove_Abi)
HOOK(0x004547b0, "Object_Remove", Object_Remove_Entry, nullptr);

// 0x004548c0 (stack: 2 unused; ret 8). An empty method (Object slot 0x2d and many other vtables). Not executed in
// recorded runs.
void fn_004548c0(uint32_t /*a0*/, uint32_t /*a1*/)
{
}
static void fn_004548c0_Abi(uint32_t a0, uint32_t a1)
{
    fn_004548c0(a0, a1);
}
ABI_ENTRY_004548c0(fn_004548c0_Entry, fn_004548c0_Abi)
HOOK(0x004548c0, "fn_004548c0", fn_004548c0_Entry, nullptr);

// 0x004548d0 (eax = record; stack: a0, threshold; ret 8; caller 0x00454940). al = record +8 (float) > threshold.
uint32_t fn_004548d0(uint32_t rec, uint32_t /*a0*/, uint32_t threshold)
{
    return F(P(rec), 8) > u2f(threshold) ? 1u : 0u;
}
static uint32_t fn_004548d0_Abi(uint32_t eax, uint32_t a0, uint32_t a1)
{
    return fn_004548d0(eax, a0, a1);
}
ABI_ENTRY_004548d0(fn_004548d0_Entry, fn_004548d0_Abi)
HOOK(0x004548d0, "fn_004548d0", fn_004548d0_Entry, nullptr);

// 0x00454930 (stack: 4 unused; ret 0x10). An empty method (Object slots 0xd and 0x2c, many vtables). Not executed in
// recorded runs.
void fn_00454930(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/, uint32_t /*a3*/)
{
}
static void fn_00454930_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    fn_00454930(a0, a1, a2, a3);
}
ABI_ENTRY_00454930(fn_00454930_Entry, fn_00454930_Abi)
HOOK(0x00454930, "fn_00454930", fn_00454930_Entry, nullptr);

// 0x00454bc0 (ecx = object). Returns vt+0xb8(&b, &c, &a, 0) (original frame: a +0, b +4, c +8; 0x14 bytes).
uint32_t fn_00454bc0(void* obj)
{
    uint32_t l[5] = {};
    return VCall<uint32_t>(obj, 0xb8 / 4, u(&l[1]), u(&l[2]), u(&l[0]), 0u);
}
static uint32_t fn_00454bc0_Abi(uint32_t ecx)
{
    return fn_00454bc0(P(ecx));
}
ABI_ENTRY_00454bc0(fn_00454bc0_Entry, fn_00454bc0_Abi)
HOOK(0x00454bc0, "fn_00454bc0", fn_00454bc0_Entry, nullptr);

// 0x00454be0 (ecx = object). vt+0xb8(&a, &c, &b, 0); al = (result al && a == 0). Not executed in recorded runs.
uint32_t fn_00454be0(void* obj)
{
    uint32_t l[5] = {};
    const uint32_t r = VCall<uint32_t>(obj, 0xb8 / 4, u(&l[0]), u(&l[2]), u(&l[1]), 0u);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return (l[0] & 0xffffff00u) | (l[0] == 0 ? 1u : 0u);
}
static uint32_t fn_00454be0_Abi(uint32_t ecx)
{
    return fn_00454be0(P(ecx));
}
ABI_ENTRY_00454be0(fn_00454be0_Entry, fn_00454be0_Abi)
HOOK(0x00454be0, "fn_00454be0", fn_00454be0_Entry, nullptr);

// 0x00454c20 (ecx = object). As 0x00454be0 with a != 0. Not executed in recorded runs.
uint32_t fn_00454c20(void* obj)
{
    uint32_t l[5] = {};
    const uint32_t r = VCall<uint32_t>(obj, 0xb8 / 4, u(&l[0]), u(&l[2]), u(&l[1]), 0u);
    if (!static_cast<uint8_t>(r))
        return r & 0xffffff00u;
    return (l[0] & 0xffffff00u) | (l[0] != 0 ? 1u : 0u);
}
static uint32_t fn_00454c20_Abi(uint32_t ecx)
{
    return fn_00454c20(P(ecx));
}
ABI_ENTRY_00454c20(fn_00454c20_Entry, fn_00454c20_Abi)
HOOK(0x00454c20, "fn_00454c20", fn_00454c20_Entry, nullptr);

// 0x00454c60 (ecx = object). vt+0xb8(&a, &c, &b, 0); returns a when the result al is set, else 0.
uint32_t fn_00454c60(void* obj)
{
    uint32_t l[5] = {};
    const uint32_t r = VCall<uint32_t>(obj, 0xb8 / 4, u(&l[0]), u(&l[2]), u(&l[1]), 0u);
    return static_cast<uint8_t>(r) ? l[0] : 0u;
}
static uint32_t fn_00454c60_Abi(uint32_t ecx)
{
    return fn_00454c60(P(ecx));
}
ABI_ENTRY_00454c60(fn_00454c60_Entry, fn_00454c60_Abi)
HOOK(0x00454c60, "fn_00454c60", fn_00454c60_Entry, nullptr);

// 0x00454c90 (eax = object, cl = on; stack: unused; ret 4). Sets or clears +0x424 bit 0x20.
void fn_00454c90(void* obj, uint32_t on, uint32_t /*a0*/)
{
    if (static_cast<uint8_t>(on))
        D(obj, 0x424) |= 0x20;
    else
        D(obj, 0x424) &= 0xffffffdfu;
}
static void fn_00454c90_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    fn_00454c90(P(eax), ecx, a0);
}
ABI_ENTRY_00454c90(fn_00454c90_Entry, fn_00454c90_Abi)
HOOK(0x00454c90, "fn_00454c90", fn_00454c90_Entry, nullptr);

// 0x00454fe0 (cdecl: a, b). Compares 8 bytes (repe cmpsb): 0 when equal, -1 / 1 by the first differing byte
// (unsigned). A sort / search callback (no direct callers).
uint32_t fn_00454fe0(uint32_t a, uint32_t b)
{
    const uint8_t* p = static_cast<const uint8_t*>(P(a));
    const uint8_t* q = static_cast<const uint8_t*>(P(b));
    for (int i = 0; i < 8; ++i)
    {
        if (p[i] != q[i])
            return p[i] < q[i] ? 0xffffffffu : 1u;
    }
    return 0;
}
static uint32_t fn_00454fe0_Abi(uint32_t a0, uint32_t a1)
{
    return fn_00454fe0(a0, a1);
}
ABI_ENTRY_00454fe0(fn_00454fe0_Entry, fn_00454fe0_Abi)
HOOK(0x00454fe0, "fn_00454fe0", fn_00454fe0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Health, damage and death.

// 0x004555b0 (eax = object; returns st0). Object_GetHealth: +0x454.
float Object_GetHealth(void* obj)
{
    return F(obj, 0x454);
}
static float Object_GetHealth_Abi(uint32_t eax)
{
    return Object_GetHealth(P(eax));
}
ABI_ENTRY_004555b0(Object_GetHealth_Entry, Object_GetHealth_Abi)
HOOK(0x004555b0, "Object_GetHealth", Object_GetHealth_Entry, nullptr);

// 0x004555c0 (eax = object; returns st0). Object_GetMaxHealth: +0x458 (max_health, see 0x00451360).
float Object_GetMaxHealth(void* obj)
{
    return F(obj, 0x458);
}
static float Object_GetMaxHealth_Abi(uint32_t eax)
{
    return Object_GetMaxHealth(P(eax));
}
ABI_ENTRY_004555c0(Object_GetMaxHealth_Entry, Object_GetMaxHealth_Abi)
HOOK(0x004555c0, "Object_GetMaxHealth", Object_GetMaxHealth_Entry, nullptr);

// 0x004555d0 (eax = object). Returns the class template +0x414.
uint32_t fn_004555d0(void* obj)
{
    return D(obj, 0x414);
}
static uint32_t fn_004555d0_Abi(uint32_t eax)
{
    return fn_004555d0(P(eax));
}
ABI_ENTRY_004555d0(fn_004555d0_Entry, fn_004555d0_Abi)
HOOK(0x004555d0, "fn_004555d0", fn_004555d0_Entry, nullptr);

// 0x004555e0 (ecx = object; stack: damage record; ret 4). Object_TakeDamage (Object slot 0x34): a living object (health
// +0x454 > 0) loses record +0x14; at <= 0 a tail jump to vt+0xd4(record) (death), otherwise the health is capped at the
// max health +0x458.
uint32_t Object_TakeDamage(void* obj, uint32_t dmg)
{
    if (F(obj, 0x454) <= kZero)
        return 0;
    const float h = F(obj, 0x454) - F(P(dmg), 0x14);
    F(obj, 0x454) = h;
    if (h <= kZero)
        return VCall<uint32_t>(obj, 0xd4 / 4, dmg);
    if (h > F(obj, 0x458))
        D(obj, 0x454) = D(obj, 0x458);
    return 0;
}
static uint32_t Object_TakeDamage_Abi(uint32_t ecx, uint32_t a0)
{
    return Object_TakeDamage(P(ecx), a0);
}
ABI_ENTRY_004555e0(Object_TakeDamage_Entry, Object_TakeDamage_Abi)
HOOK(0x004555e0, "Object_TakeDamage", Object_TakeDamage_Entry, nullptr);

// 0x00455640 (ecx = object; stack: unused; ret 4). Object_Kill (Object slot 0x35): vt+0xdc() then Object_SpawnHusk.
uint32_t Object_Kill(void* obj, uint32_t /*a0*/)
{
    VCall<uint32_t>(obj, 0xdc / 4);
    return Object_SpawnHusk(obj);
}
static uint32_t Object_Kill_Abi(uint32_t ecx, uint32_t a0)
{
    return Object_Kill(P(ecx), a0);
}
ABI_ENTRY_00455640(Object_Kill_Entry, Object_Kill_Abi)
HOOK(0x00455640, "Object_Kill", Object_Kill_Entry, nullptr);

// 0x00455660 (esi = object; ecx is not read). Object_SpawnHusk: unless fx_death +0x3a0 is "null" (_stricmp), Fx_Create
// 0x0041cc60 (ecx = Game+0xa90; name, params {2, .., matrix vt+0x64 at +0x30, 0 at +0x74}); unless husk +0x3c0 is
// "null", ObjectSystem_CreateObject 0x0045f020 (eax = 0, ecx = husk; Game+0xa84, &matrix (vt+0x5c), word +0x394, 0, 0,
// 0, 0, 0xaa, 0). Returns the created object or 0.
uint32_t Object_SpawnHusk(void* obj)
{
    const uint32_t o = u(obj);
    uint32_t fr[0x80 / 4] = {};
    if (orig::fn_005448ee(0, o + 0x3a0, 0x005bba94))
    {
        const uint32_t m = VCall<uint32_t>(obj, 0x64 / 4, u(&fr[0x30 / 4]));
        fr[0x74 / 4] = 0;
        fr[0] = 2;
        orig::fn_0041cc60(m & 0xffffff00u, D(G(), 0xa90), o + 0x3a0, u(fr));
    }
    if (!orig::fn_005448ee(0, o + 0x3c0, 0x005bba94))
        return 0;
    VCall<uint32_t>(obj, 0x5c / 4, u(fr));
    return orig::fn_0045f020(0, o + 0x3c0, D(G(), 0xa84), u(fr), Field<uint16_t>(obj, 0x394), 0, 0, 0, 0, 0xaa, 0);
}
static uint32_t Object_SpawnHusk_Abi(uint32_t /*ecx*/, uint32_t esi)
{
    return Object_SpawnHusk(P(esi));
}
ABI_ENTRY_00455660(Object_SpawnHusk_Entry, Object_SpawnHusk_Abi)
HOOK(0x00455660, "Object_SpawnHusk", Object_SpawnHusk_Entry, nullptr);

// 0x004559c0 (eax = object, cl = on). Sets or clears +0x424 bit 0x200.
void fn_004559c0(void* obj, uint32_t on)
{
    if (static_cast<uint8_t>(on))
        D(obj, 0x424) |= 0x200;
    else
        D(obj, 0x424) &= 0xfffffdffu;
}
static void fn_004559c0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_004559c0(P(eax), ecx);
}
ABI_ENTRY_004559c0(fn_004559c0_Entry, fn_004559c0_Abi)
HOOK(0x004559c0, "fn_004559c0", fn_004559c0_Entry, nullptr);

// 0x004559f0 (eax = object). al = +0x424 bit 0x200 (upper eax bits: +0x424 >> 9).
uint32_t fn_004559f0(void* obj)
{
    const uint32_t v = D(obj, 0x424) >> 9;
    return (v & 0xffffff00u) | (v & 1u);
}
static uint32_t fn_004559f0_Abi(uint32_t eax)
{
    return fn_004559f0(P(eax));
}
ABI_ENTRY_004559f0(fn_004559f0_Entry, fn_004559f0_Abi)
HOOK(0x004559f0, "fn_004559f0", fn_004559f0_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Force actions (the pool Game+0xa84 +0xf30; ForceActionPool_Add 0x00403fb0 eax, ecx = pool, esi = &vec4 (16-byte
// aligned, w = 0); 5 stack arguments).

// 0x00455a00 (edx = handle). Releases a force action: ForceActionPool_Release 0x00404060 (eax = game, edi = pool;
// handle, 1), or -1 without a pool.
uint32_t fn_00455a00(uint32_t handle)
{
    const uint32_t pool = D(Objs(), 0xf30);
    if (!pool)
        return 0xffffffffu;
    return orig::fn_00404060(u(G()), pool, handle, 1);
}
static uint32_t fn_00455a00_Abi(uint32_t edx)
{
    return fn_00455a00(edx);
}
ABI_ENTRY_00455a00(fn_00455a00_Entry, fn_00455a00_Abi)
HOOK(0x00455a00, "fn_00455a00", fn_00455a00_Entry, nullptr);

// 0x00455a30 (eax = &vec3, edx = flags; stack: a0..a4; ret 0x14). ForceActionPool_Add (eax = a1; a0, a3, a2, a4,
// flags | 2); 0 without a pool.
uint32_t fn_00455a30(uint32_t vec, uint32_t flags, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    alignas(16) uint32_t v[4];
    v[1] = D(P(vec), 4);
    v[0] = D(P(vec), 0);
    v[2] = D(P(vec), 8);
    const uint32_t pool = D(Objs(), 0xf30);
    v[3] = 0;
    if (!pool)
        return 0;
    return orig::fn_00403fb0(a1, pool, u(v), a0, a3, a2, a4, flags | 2);
}
static uint32_t fn_00455a30_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_00455a30(eax, edx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00455a30(fn_00455a30_Entry, fn_00455a30_Abi)
HOOK(0x00455a30, "fn_00455a30", fn_00455a30_Entry, nullptr);

// 0x00455aa0 (eax = &vec3; stack: a0..a4; ret 0x14). ForceActionPool_Add (eax = -1; a0, a2, a1, a3, a4); without a
// pool returns Game+0xa84 (eax as the original leaves it).
uint32_t fn_00455aa0(uint32_t vec, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    alignas(16) uint32_t v[4];
    v[1] = D(P(vec), 4);
    v[0] = D(P(vec), 0);
    v[2] = D(P(vec), 8);
    const uint32_t objs = D(G(), 0xa84);
    const uint32_t pool = D(P(objs), 0xf30);
    v[3] = 0;
    if (!pool)
        return objs;
    return orig::fn_00403fb0(0xffffffffu, pool, u(v), a0, a2, a1, a3, a4);
}
static uint32_t fn_00455aa0_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return fn_00455aa0(eax, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00455aa0(fn_00455aa0_Entry, fn_00455aa0_Abi)
HOOK(0x00455aa0, "fn_00455aa0", fn_00455aa0_Entry, nullptr);

// 0x00455b10 (eax = &vec3, edx = flags; stack: a0..a4; ret 0x14; callers 0x00454cc0, 0x00454e60). As 0x00455a30 with
// flags | 0x22.
uint32_t fn_00455b10(uint32_t vec, uint32_t flags, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    alignas(16) uint32_t v[4];
    v[1] = D(P(vec), 4);
    v[0] = D(P(vec), 0);
    v[2] = D(P(vec), 8);
    const uint32_t pool = D(Objs(), 0xf30);
    v[3] = 0;
    if (!pool)
        return 0;
    return orig::fn_00403fb0(a1, pool, u(v), a0, a3, a2, a4, flags | 0x22);
}
static uint32_t fn_00455b10_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4)
{
    return fn_00455b10(eax, edx, a0, a1, a2, a3, a4);
}
ABI_ENTRY_00455b10(fn_00455b10_Entry, fn_00455b10_Abi)
HOOK(0x00455b10, "fn_00455b10", fn_00455b10_Entry, nullptr);

// 0x00455b80 (eax = &vec3, edx = flags; stack: a0..a3; ret 0x10; caller 0x0041f8f0). ForceActionPool_Add (eax = -1;
// a0, a2, a1, a3, flags | 0x20); without a pool returns the game pointer (eax as the original leaves it).
uint32_t fn_00455b80(uint32_t vec, uint32_t flags, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    alignas(16) uint32_t v[4];
    v[1] = D(P(vec), 4);
    v[0] = D(P(vec), 0);
    v[2] = D(P(vec), 8);
    const uint32_t g = u(G());
    const uint32_t pool = D(P(D(P(g), 0xa84)), 0xf30);
    v[3] = 0;
    if (!pool)
        return g;
    return orig::fn_00403fb0(0xffffffffu, pool, u(v), a0, a2, a1, a3, flags | 0x20);
}
static uint32_t fn_00455b80_Abi(uint32_t eax, uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_00455b80(eax, edx, a0, a1, a2, a3);
}
ABI_ENTRY_00455b80(fn_00455b80_Entry, fn_00455b80_Abi)
HOOK(0x00455b80, "fn_00455b80", fn_00455b80_Entry, nullptr);

// 0x00456050 (ebx, esi = &direction, edi; stack: strength (float); ret 4; callers 0x0043ee90, 0x004788a0). s = strength
// * [0x005bcb4c]; 0x004574b0 (eax = edi, ecx = ebx, edx = &body slot; the original writes into the argument slot);
// with a body, s is capped at [0x005bcdac] / (1 / body +8 -> +0x2c, 0 for a zero mass) when s divided by that
// inverse exceeds [0x005bcdac]; then 0x00455aa0(&(s * direction); ebx, (float)sqrt([0x005bcdb0] double), edi, 0, 0).
uint32_t fn_00456050(uint32_t ebx_, uint32_t esi_, uint32_t edi_, uint32_t a0)
{
    uint32_t fr[4];  // original frame: +0 sqrt constant, +4 force
    uint32_t slot = a0;
    float s = u2f(a0) * GameGlobal<float>(0x005bcb4c);
    fr[0] = f2u(static_cast<float>(std::sqrt(GameGlobal<double>(0x005bcdb0))));
    orig::fn_004574b0(edi_, ebx_, u(&slot));
    if (slot)
    {
        const uint32_t c = D(P(slot), 8);
        const float m = F(P(c), 0x2c);
        const float inv = (m == kZero) ? kZero : kOne / m;
        if (s / inv > GameGlobal<float>(0x005bcdac))
        {
            const float m2 = F(P(c), 0x2c);
            const float inv2 = (m2 == kZero) ? kZero : kOne / m2;
            s = inv2 * GameGlobal<float>(0x005bcdac);
        }
    }
    float* f = reinterpret_cast<float*>(&fr[1]);
    f[0] = s * F(P(esi_), 0);
    f[1] = s * F(P(esi_), 4);
    f[2] = s * F(P(esi_), 8);
    return fn_00455aa0(u(f), ebx_, fr[0], edi_, 0, 0);
}
static uint32_t fn_00456050_Abi(uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t a0)
{
    return fn_00456050(ebx, esi, edi, a0);
}
ABI_ENTRY_00456050(fn_00456050_Entry, fn_00456050_Abi)
HOOK(0x00456050, "fn_00456050", fn_00456050_Entry, nullptr);

// 0x00456180 (ecx = reference name, esi = object; stack: name, a1..a6; ret 0x1c; callers 0x0040ad80, 0x0041c180,
// 0x00496e90). Finds the reference matrix (0x00453c50: object, ref, &matrix, 1, 0, 0, 1, 0, 0) and creates `name`
// (0x0045f020, eax = 0; Game+0xa84, 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0); the new object gets 0x004324d0 (eax = 0, ebx = new;
// object, 1), 0x00457990 (al = 0; new, &matrix), 0x0042d000 (eax = object, ecx = new; a2, a4, a1, a3, 0x10),
// 0x0042cc60 (eax = new, ecx = a5, edx = a6), 0x004577b0 (eax = new; 1), and Game+0x2710 += 1.
uint32_t fn_00456180(uint32_t ref, uint32_t obj, uint32_t name, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                     uint32_t a5, uint32_t a6)
{
    uint32_t m[0x44 / 4];
    const uint32_t r = orig::fn_00453c50(obj, ref, u(m), 1, 0, 0, 1, 0, 0);
    if (!static_cast<uint8_t>(r))
        return r;
    const uint32_t made = orig::fn_0045f020(0, name, D(G(), 0xa84), 0, 0x3ff, 0, 0, 0, 0, 0xaa, 0);
    if (!made)
        return 0;
    const uint32_t q = orig::fn_004324d0(0, made, obj, 1);
    orig::fn_00457990(q & 0xffffff00u, made, u(m));
    orig::fn_0042d000(obj, made, a2, a4, a1, a3, 0x10);
    orig::fn_0042cc60(made, a5, a6);
    orig::fn_004577b0(made, 1);
    ++Field<int32_t>(G(), 0x2710);
    return u(G());
}
static uint32_t fn_00456180_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                uint32_t a4, uint32_t a5, uint32_t a6)
{
    return fn_00456180(ecx, esi, a0, a1, a2, a3, a4, a5, a6);
}
ABI_ENTRY_00456180(fn_00456180_Entry, fn_00456180_Abi)
HOOK(0x00456180, "fn_00456180", fn_00456180_Entry, nullptr);

// 0x00456240. Object slot 1: the class descriptor 0x0057f108 (+0x14 "CLASSV(Object)").
uint32_t Object_GetClassDesc()
{
    return 0x0057f108;
}
static uint32_t Object_GetClassDesc_Abi()
{
    return Object_GetClassDesc();
}
ABI_ENTRY_00456240(Object_GetClassDesc_Entry, Object_GetClassDesc_Abi)
HOOK(0x00456240, "Object_GetClassDesc", Object_GetClassDesc_Entry, nullptr);

// ---------------------------------------------------------------------------------------------------------------
// Matrices. +0x2dc position offset, +0x2e8 world matrix and +0x32c scaled world matrix (both rebuilt by 0x00457c20, ebx
// = object, from the body +0x1c4 when the game time differs from +0x370), +0x37c scale.

// 0x00457820 (eax = &vec3, ecx = object; caller 0x00477570). +0x2dc = vec, +0x370 = -1.0 (rebuild). Returns z bits.
uint32_t fn_00457820(uint32_t vec, void* obj)
{
    D(obj, 0x2dc) = D(P(vec), 0);
    D(obj, 0x2e0) = D(P(vec), 4);
    const uint32_t z = D(P(vec), 8);
    D(obj, 0x2e4) = z;
    D(obj, 0x370) = 0xbf800000u;
    return z;
}
static uint32_t fn_00457820_Abi(uint32_t eax, uint32_t ecx)
{
    return fn_00457820(eax, P(ecx));
}
ABI_ENTRY_00457820(fn_00457820_Entry, fn_00457820_Abi)
HOOK(0x00457820, "fn_00457820", fn_00457820_Entry, nullptr);

// 0x00457850 (eax = out vec3, ecx = object). out = +0x2dc.
void fn_00457850(uint32_t out, void* obj)
{
    D(P(out), 0) = D(obj, 0x2dc);
    D(P(out), 4) = D(obj, 0x2e0);
    D(P(out), 8) = D(obj, 0x2e4);
}
static void fn_00457850_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00457850(eax, P(ecx));
}
ABI_ENTRY_00457850(fn_00457850_Entry, fn_00457850_Abi)
HOOK(0x00457850, "fn_00457850", fn_00457850_Entry, nullptr);

// 0x00457aa0 (eax = object; caller 0x0049b030). Returns +0x410.
uint32_t fn_00457aa0(void* obj)
{
    return D(obj, 0x410);
}
static uint32_t fn_00457aa0_Abi(uint32_t eax)
{
    return fn_00457aa0(P(eax));
}
ABI_ENTRY_00457aa0(fn_00457aa0_Entry, fn_00457aa0_Abi)
HOOK(0x00457aa0, "fn_00457aa0", fn_00457aa0_Entry, nullptr);

// 0x00457ba0 (eax = out game matrix, ecx = in 4x4; callers 0x00457c20, 0x00459000). Transposes the rotation (in row i,
// column j -> out column i, row j), translation in +0x30..+0x38 -> out[3], out[7], out[11]; out[12..14] = 0, out[15] =
// 1.0, flags out[16] = 0x11.
void fn_00457ba0(uint32_t out, uint32_t in)
{
    uint32_t* o = static_cast<uint32_t*>(P(out));
    const uint32_t* i = static_cast<const uint32_t*>(P(in));
    o[0] = i[0];
    o[1] = i[4];
    o[2] = i[8];
    o[4] = i[1];
    o[5] = i[5];
    o[6] = i[9];
    o[8] = i[2];
    o[9] = i[6];
    o[10] = i[10];
    const float tx = u2f(i[12]);
    const float ty = u2f(i[13]);
    const uint32_t tz = i[14];
    reinterpret_cast<float*>(o)[3] = tx;
    reinterpret_cast<float*>(o)[7] = ty;
    o[11] = tz;
    o[14] = 0;
    o[13] = 0;
    o[12] = 0;
    o[15] = 0x3f800000;
    o[16] = 0x11;
}
static void fn_00457ba0_Abi(uint32_t eax, uint32_t ecx)
{
    fn_00457ba0(eax, ecx);
}
ABI_ENTRY_00457ba0(fn_00457ba0_Entry, fn_00457ba0_Abi)
HOOK(0x00457ba0, "fn_00457ba0", fn_00457ba0_Entry, nullptr);

// 0x00457d60 (ecx = object; stack: out; ret 4). Object slot 0x19: 0x00457c20 (rebuild), out = the scaled world matrix
// +0x32c (0x44 bytes).
uint32_t fn_00457d60(void* obj, uint32_t out)
{
    const uint32_t r = orig::fn_00457c20(u(obj));
    std::memcpy(P(out), reinterpret_cast<uint8_t*>(obj) + 0x32c, 0x44);
    return r;
}
static uint32_t fn_00457d60_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00457d60(P(ecx), a0);
}
ABI_ENTRY_00457d60(fn_00457d60_Entry, fn_00457d60_Abi)
HOOK(0x00457d60, "fn_00457d60", fn_00457d60_Entry, nullptr);

// 0x00457d90 (ecx = object; stack: out; ret 4). Object slot 0x17: 0x00457c20, out = the world matrix +0x2e8.
uint32_t fn_00457d90(void* obj, uint32_t out)
{
    const uint32_t r = orig::fn_00457c20(u(obj));
    std::memcpy(P(out), reinterpret_cast<uint8_t*>(obj) + 0x2e8, 0x44);
    return r;
}
static uint32_t fn_00457d90_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00457d90(P(ecx), a0);
}
ABI_ENTRY_00457d90(fn_00457d90_Entry, fn_00457d90_Abi)
HOOK(0x00457d90, "fn_00457d90", fn_00457d90_Entry, nullptr);

// 0x00457ea0 (ecx = object). Object slot 0x1a: 0x00457c20, returns &+0x32c.
uint32_t fn_00457ea0(void* obj)
{
    orig::fn_00457c20(u(obj));
    return u(obj) + 0x32c;
}
static uint32_t fn_00457ea0_Abi(uint32_t ecx)
{
    return fn_00457ea0(P(ecx));
}
ABI_ENTRY_00457ea0(fn_00457ea0_Entry, fn_00457ea0_Abi)
HOOK(0x00457ea0, "fn_00457ea0", fn_00457ea0_Entry, nullptr);

// 0x00457eb0 (ecx = object). Object slot 0x18: 0x00457c20, returns &+0x2e8.
uint32_t fn_00457eb0(void* obj)
{
    orig::fn_00457c20(u(obj));
    return u(obj) + 0x2e8;
}
static uint32_t fn_00457eb0_Abi(uint32_t ecx)
{
    return fn_00457eb0(P(ecx));
}
ABI_ENTRY_00457eb0(fn_00457eb0_Entry, fn_00457eb0_Abi)
HOOK(0x00457eb0, "fn_00457eb0", fn_00457eb0_Entry, nullptr);

// 0x00457ec0 (ecx = object). Object slot 0x1f: without a body (byte +0x421 set) +0x32c becomes the scale matrix of
// +0x37c (0x004535b0, Matrix_Scale 0x004e2d90 edx = matrix, esi = scale; it reads no stack arguments) and &+0x32c is
// returned; otherwise vt+0x68().
uint32_t fn_00457ec0(void* obj)
{
    if (Field<uint8_t>(obj, 0x421))
    {
        uint32_t s[3];
        orig::fn_004535b0(u(s), u(obj));
        orig::fn_004e2d90(u(obj) + 0x32c, u(s), 0, 0, 0);
        return u(obj) + 0x32c;
    }
    return VCall<uint32_t>(obj, 0x68 / 4);
}
static uint32_t fn_00457ec0_Abi(uint32_t ecx)
{
    return fn_00457ec0(P(ecx));
}
ABI_ENTRY_00457ec0(fn_00457ec0_Entry, fn_00457ec0_Abi)
HOOK(0x00457ec0, "fn_00457ec0", fn_00457ec0_Entry, nullptr);

// 0x00457f00 (ecx = object). Object slot 0x1d: without a body +0x2e8 = identity and &+0x2e8 is returned; otherwise a
// tail jump to vt+0x60().
uint32_t fn_00457f00(void* obj)
{
    if (Field<uint8_t>(obj, 0x421))
    {
        Identity(u(obj) + 0x2e8);
        return u(obj) + 0x2e8;
    }
    return VCall<uint32_t>(obj, 0x60 / 4);
}
static uint32_t fn_00457f00_Abi(uint32_t ecx)
{
    return fn_00457f00(P(ecx));
}
ABI_ENTRY_00457f00(fn_00457f00_Entry, fn_00457f00_Abi)
HOOK(0x00457f00, "fn_00457f00", fn_00457f00_Entry, nullptr);

// 0x00457f40 (ecx = object; stack: out; ret 4). Object slot 0x1e: without a body out = the scale matrix of +0x37c
// (Matrix_Scale's eax returned); otherwise vt+0x64(out).
uint32_t fn_00457f40(void* obj, uint32_t out)
{
    if (Field<uint8_t>(obj, 0x421))
    {
        uint32_t s[3];
        orig::fn_004535b0(u(s), u(obj));
        return orig::fn_004e2d90(out, u(s), 0, 0, 0);
    }
    return VCall<uint32_t>(obj, 0x64 / 4, out);
}
static uint32_t fn_00457f40_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00457f40(P(ecx), a0);
}
ABI_ENTRY_00457f40(fn_00457f40_Entry, fn_00457f40_Abi)
HOOK(0x00457f40, "fn_00457f40", fn_00457f40_Entry, nullptr);

// 0x00457f80 (ecx = object; stack: out; ret 4). Object slot 0x1c: without a body out = identity (returns 1.0's bits);
// otherwise a tail jump to vt+0x5c(out).
uint32_t fn_00457f80(void* obj, uint32_t out)
{
    if (Field<uint8_t>(obj, 0x421))
    {
        Identity(out);
        return 0x3f800000;
    }
    return VCall<uint32_t>(obj, 0x5c / 4, out);
}
static uint32_t fn_00457f80_Abi(uint32_t ecx, uint32_t a0)
{
    return fn_00457f80(P(ecx), a0);
}
ABI_ENTRY_00457f80(fn_00457f80_Entry, fn_00457f80_Abi)
HOOK(0x00457f80, "fn_00457f80", fn_00457f80_Entry, nullptr);

// 0x00458270 (edx; stack: a0, a1 unused, z; ret 0xc). Builds the matrix diag(k, k, z or k when z == 0, 1) with k =
// [0x00588dc4] (flags 0) and passes it to 0x004582d0 (esi = &matrix; edx).
uint32_t fn_00458270(uint32_t edx_, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t a2)
{
    float z = u2f(a2);
    if (z == kZero)
        z = GameGlobal<float>(0x00588dc4);
    uint32_t m[17] = {};
    reinterpret_cast<float*>(m)[10] = z;
    const uint32_t k = GameGlobal<uint32_t>(0x00588dc4);
    m[15] = 0x3f800000;
    m[0] = k;
    m[5] = k;
    return orig::fn_004582d0(u(m), edx_);
}
static uint32_t fn_00458270_Abi(uint32_t edx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_00458270(edx, a0, a1, a2);
}
ABI_ENTRY_00458270(fn_00458270_Entry, fn_00458270_Abi)
HOOK(0x00458270, "fn_00458270", fn_00458270_Entry, nullptr);

// 0x004585f0 (stack: out; ret 4). Object slot 0x23 (gravity): *out = 0; returns out.
uint32_t fn_004585f0(uint32_t out)
{
    D(P(out), 0) = 0;
    return out;
}
static uint32_t fn_004585f0_Abi(uint32_t a0)
{
    return fn_004585f0(a0);
}
ABI_ENTRY_004585f0(fn_004585f0_Entry, fn_004585f0_Abi)
HOOK(0x004585f0, "fn_004585f0", fn_004585f0_Entry, nullptr);

// 0x004592c0 (eax = body or 0; stack: object; ret 4; callers 0x00459000, 0x00487a90). Returns (body ? body : object
// +0x1c4) +0x20.
uint32_t fn_004592c0(uint32_t body, uint32_t obj)
{
    if (!body)
        body = D(P(obj), 0x1c4);
    return D(P(body), 0x20);
}
static uint32_t fn_004592c0_Abi(uint32_t eax, uint32_t a0)
{
    return fn_004592c0(eax, a0);
}
ABI_ENTRY_004592c0(fn_004592c0_Entry, fn_004592c0_Abi)
HOOK(0x004592c0, "fn_004592c0", fn_004592c0_Entry, nullptr);

// 0x0045a450 (cdecl: object). Releases a reference (ReleaseRef, tail jump to 0x00487da0). A callback (no direct
// callers).
uint32_t fn_0045a450(uint32_t obj)
{
    return ReleaseRef(P(obj));
}
static uint32_t fn_0045a450_Abi(uint32_t a0)
{
    return fn_0045a450(a0);
}
ABI_ENTRY_0045a450(fn_0045a450_Entry, fn_0045a450_Abi)
HOOK(0x0045a450, "fn_0045a450", fn_0045a450_Entry, nullptr);

// 0x0045a480 (cdecl: object; callers 0x0041ff60, 0x00420080). al = 1 when the object or one of its ancestors
// (Object_GetParent 0x004539d0) is the player (Game+0x748) or one of the player's ancestors.
uint32_t fn_0045a480(uint32_t obj)
{
    uint32_t c = obj;
    if (!c)
        return 0;
    const uint32_t pl = D(G(), 0x748);
    for (;;)
    {
        for (uint32_t e = pl; e; e = Object_GetParent(P(e)))
        {
            if (c == e)
                return 1;
        }
        c = Object_GetParent(P(c));
        if (!c)
            return 0;
    }
}
static uint32_t fn_0045a480_Abi(uint32_t a0)
{
    return fn_0045a480(a0);
}
ABI_ENTRY_0045a480(fn_0045a480_Entry, fn_0045a480_Abi)
HOOK(0x0045a480, "fn_0045a480", fn_0045a480_Entry, nullptr);

// ===============================================================================================================
// ObjectSystem (Game+0xa84, vtable 0x005a826c "CLASSP(ObjectSystem)").

namespace
{
// The PMS_List constructor inlined in 0x0045a4e0: heap 0x18 bytes, vtable 0x0058b098; with the shared node pool
// Game+0xac4 (+4 = 0, +8 = pool), else its own (0x004e06f0, edx = 0; heap, "PMS_List::m_listData", 0x1c, 2, 5; +4 =
// 1); +0xc..+0x14 = 0. 0 when the allocation fails.
uint32_t NewPmsList()
{
    const uint32_t m = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, kTagNew);
    if (!m)
        return 0;
    const uint32_t pool = D(G(), 0xac4);
    D(P(m), 0) = 0x0058b098;
    if (pool)
    {
        Field<uint8_t>(P(m), 4) = 0;
        D(P(m), 8) = pool;
    }
    else
    {
        const uint32_t d = orig::fn_004e06f0(0, kHeap, 0x0058b0e0, 0x1c, 2, 5);
        Field<uint8_t>(P(m), 4) = 1;
        D(P(m), 8) = d;
    }
    D(P(m), 0xc) = 0;
    D(P(m), 0x10) = 0;
    D(P(m), 0x14) = 0;
    return m;
}
}  // namespace

// 0x0045a4e0 (edi = system; ecx is not read; caller Game_Init 0x004011e0). The ObjectSystem constructor: vtable
// 0x005a826c, clears its counters, 0x2f object lists at +0xe6c.. and one at +0xf28 (NewPmsList), the force action pool
// +0xf30 (heap 0x18, ForceActionPool_Init 0x00403ef0 eax = 0x200, esi = memory), +0xe5c = 0, byte +0xf34 = 1, then the
// behaviour name table (0x0045df60). Returns the system.
uint32_t ObjectSystem_Construct(uint32_t /*ecx*/, void* s)
{
    D(s, 0) = 0x005a826c;
    D(s, 4) = 0;
    D(s, 8) = 0;
    static const uint16_t kZeroed[] = {0xe0c, 0xe18, 0xe1c, 0xe10, 0xe14, 0xe20, 0xe2c, 0xe30, 0xe24, 0xe28, 0xe34,
                                       0xe38, 0xe3c, 0xe40, 0xe44, 0xe48, 0xe54, 0xe4c, 0xe58, 0xe60, 0xe64, 0xe68};
    for (const uint16_t off : kZeroed)
        D(s, off) = 0;
    for (uint32_t i = 0; i < 0x2f; ++i)
        D(s, 0xe6c + 4 * i) = NewPmsList();
    D(s, 0xf28) = NewPmsList();
    const uint32_t mem = orig::fn_004e07f0(kHeap, 0x10, 0x18, 2, kTagNew);
    D(s, 0xf30) = mem ? orig::fn_00403ef0(0x200, mem) : 0;
    D(s, 0xe5c) = 0;
    Field<uint8_t>(s, 0xf34) = 1;
    ObjectSystem_InitBehaviourTable(s);
    return u(s);
}
static uint32_t ObjectSystem_Construct_Abi(uint32_t ecx, uint32_t edi)
{
    return ObjectSystem_Construct(ecx, P(edi));
}
ABI_ENTRY_0045a4e0(ObjectSystem_Construct_Entry, ObjectSystem_Construct_Abi)
HOOK(0x0045a4e0, "ObjectSystem_Construct", ObjectSystem_Construct_Entry, nullptr);

// 0x0045d570 (eax = system, ebx = id; caller 0x0045d930). Finds the object (list +0xe70) whose word +0x396 equals id;
// on a hit the iterator's list vt+0xc(node) and the node loses its reference (+0x18). Returns the object or 0.
uint32_t fn_0045d570(void* objs, uint32_t id)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(objs, 0xe70), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        if (Field<uint16_t>(P(e), 0x396) == id)
        {
            VCall<uint32_t>(P(it[0]), 0xc / 4, it[1]);
            --Field<int32_t>(P(it[1]), 0x18);
            return e;
        }
    }
    EndIter(it);
    return 0;
}
static uint32_t fn_0045d570_Abi(uint32_t eax, uint32_t ebx)
{
    return fn_0045d570(P(eax), ebx);
}
ABI_ENTRY_0045d570(fn_0045d570_Entry, fn_0045d570_Abi)
HOOK(0x0045d570, "fn_0045d570", fn_0045d570_Entry, nullptr);

// 0x0045d5f0 (eax = system, ebx = name; callers 0x00475650, 0x004836b0). ObjectSystem_FindByName: the object (list
// +0xe70) whose Object_GetName matches (_stricmp); hit handling as 0x0045d570. Returns the object or 0.
uint32_t ObjectSystem_FindByName(void* objs, uint32_t name)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(objs, 0xe70), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        if (orig::fn_005448ee(0, Object_GetName(P(e)), name) == 0)
        {
            VCall<uint32_t>(P(it[0]), 0xc / 4, it[1]);
            --Field<int32_t>(P(it[1]), 0x18);
            return e;
        }
    }
    EndIter(it);
    return 0;
}
static uint32_t ObjectSystem_FindByName_Abi(uint32_t eax, uint32_t ebx)
{
    return ObjectSystem_FindByName(P(eax), ebx);
}
ABI_ENTRY_0045d5f0(ObjectSystem_FindByName_Entry, ObjectSystem_FindByName_Abi)
HOOK(0x0045d5f0, "ObjectSystem_FindByName", ObjectSystem_FindByName_Entry, nullptr);

// 0x0045d670 (stack: system, bit; ret 8; callers 0x0044a4b0, 0x0045d4a0). Kills (vt+0xdc) every object (list +0xe70)
// unless bit != -1 and the object's word +0x394 has 0x400 or (1 << bit) without 0x800; then Camera_Reset 0x0040e050
// (Game+0xa68), 0x0046acb0 (edx = Game+0xaac), FxSystem_KillAll 0x0041cab0 (Game+0xa90), ForceActionPool_ReleaseAll
// 0x00404110 (eax = +0xf30), each when present, and 0x0045da20 (ebx = system).
uint32_t fn_0045d670(uint32_t objs, uint32_t bit)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(P(objs), 0xe70), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        bool kill = true;
        if (bit != 0xffffffffu)
        {
            const uint16_t w = Field<uint16_t>(P(e), 0x394);
            if (!(w & 0x800))
            {
                if (w & 0x400)
                    kill = false;
                else if (w & (1u << (bit & 31)))
                    kill = false;
            }
        }
        if (kill)
            VCall<uint32_t>(P(e), 0xdc / 4);
    }
    EndIter(it);
    void* g = G();
    if (D(g, 0xa68))
    {
        orig::fn_0040e050(D(g, 0xa68));
        g = G();
    }
    if (D(g, 0xaac))
        orig::fn_0046acb0(D(g, 0xaac));
    if (D(g, 0xa90))
        orig::fn_0041cab0(D(g, 0xa90));
    if (D(P(objs), 0xf30))
        orig::fn_00404110(D(P(objs), 0xf30));
    return fn_0045da20(P(objs));
}
static uint32_t fn_0045d670_Abi(uint32_t a0, uint32_t a1)
{
    return fn_0045d670(a0, a1);
}
ABI_ENTRY_0045d670(fn_0045d670_Entry, fn_0045d670_Abi)
HOOK(0x0045d670, "fn_0045d670", fn_0045d670_Entry, nullptr);

// 0x0045d750 (eax = system; stack: id; ret 4; caller 0x0045d7a0). The level's object table +0xe5c: dword group count,
// then per group a dword entry count followed by entries that start with their byte size. Returns the entry number id
// (1-based over all groups), or 0.
uint32_t fn_0045d750(void* objs, uint32_t id)
{
    if (!id)
        return 0;
    uint32_t p = D(objs, 0xe5c);
    const uint32_t n = D(P(p), 0);
    p += 4;
    uint32_t k = 1;
    for (uint32_t i = 0; i < n; ++i)
    {
        const uint32_t cnt = D(P(p), 0);
        p += 4;
        for (uint32_t j = 0; j < cnt; ++j)
        {
            if (k == id)
                return p;
            p += D(P(p), 0);
            ++k;
        }
    }
    return 0;
}
static uint32_t fn_0045d750_Abi(uint32_t eax, uint32_t a0)
{
    return fn_0045d750(P(eax), a0);
}
ABI_ENTRY_0045d750(fn_0045d750_Entry, fn_0045d750_Abi)
HOOK(0x0045d750, "fn_0045d750", fn_0045d750_Entry, nullptr);

// 0x0045d7a0 (ecx is not read; stack: system, id, entry or 0; ret 0xc; callers 0x0045d930, 0x0048cb30). Spawns a level
// table entry (looked up by id when 0): the entry (size +0) is copied to the stack (frame 0x1074 bytes, 8-aligned: +0
// flags, +4 data base, +0xc matrix, +0x54 name, +0x74 entry copy); name = entry +4 (0x20 bytes), flags = entry +0x24,
// matrix = identity with rows from entry +0x28 (12 dwords) and flags |= 0x13; a parameter set (heap 0x20: +0x10 =
// 0x00495ee0) is filled from entry +0x5c (0x004f1220, eax = data, ecx = set); the value table at entry +0x5c + [entry
// +0x58] (count, ?, offsets) has its offsets turned into pointers in place; ObjectSystem_CreateObject 0x0045f020 (eax
// = 0, ecx = name; system, &matrix, flags, 0, set, values, id, 0xaa, 1). The set is then freed (0x004f11c0 and its +0x1c
// unless byte +0xc). Returns the created object.
uint32_t fn_0045d7a0(uint32_t objs, uint32_t id, uint32_t entry)
{
    if (!entry)
    {
        entry = fn_0045d750(P(objs), id);
        if (!entry)
            return 0;
    }
    alignas(8) uint8_t fr[0x1074];
    uint8_t* b = fr + 0x74;
    std::memcpy(b, P(entry), D(P(entry), 0));
    const uint32_t flags = D(b, 0x24);
    std::memcpy(fr + 0x54, b + 4, 0x20);
    D(fr, 0) = flags;
    uint32_t* m = reinterpret_cast<uint32_t*>(fr + 0xc);
    std::memset(m, 0, 0x44);
    m[15] = 0x3f800000;
    std::memcpy(m, b + 0x28, 0x30);
    const uint32_t off = D(b, 0x58);
    m[16] |= 0x13;
    uint32_t tbl = u(b) + 0x5c + off;
    const uint32_t p = orig::fn_004e07f0(kHeap, 0x10, 0x20, 2, kTagNew);
    uint32_t set = 0;
    if (p)
    {
        D(P(p), 0) = 0;
        D(P(p), 4) = 0;
        D(P(p), 8) = 0;
        D(P(p), 0x10) = 0x00495ee0;
        D(P(p), 0x14) = 0;
        D(P(p), 0x1c) = 0;
        set = p;
    }
    orig::fn_004f1220(u(b) + 0x5c, set);
    const uint32_t cnt = D(P(tbl), 0);
    tbl += 8;
    const uint32_t base = tbl + cnt * 4;
    D(fr, 4) = base;
    D(fr, 8) = tbl;
    uint32_t* arr = static_cast<uint32_t*>(P(tbl));
    for (uint32_t i = 0; i < cnt; ++i)
        arr[i] = arr[i] + base;
    const uint32_t made = orig::fn_0045f020(0, u(fr + 0x54), objs, u(m), D(fr, 0), 0, set, tbl, id, 0xaa, 1);
    if (set)
    {
        if (Field<uint8_t>(P(set), 0xc) == 0)
        {
            orig::fn_004f11c0(set);
            orig::fn_004e08d0(kHeap, D(P(set), 0x1c));
        }
        orig::fn_004e08d0(kHeap, set);
    }
    return made;
}
static uint32_t fn_0045d7a0_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_0045d7a0(a0, a1, a2);
}
ABI_ENTRY_0045d7a0(fn_0045d7a0_Entry, fn_0045d7a0_Abi)
HOOK(0x0045d7a0, "fn_0045d7a0", fn_0045d7a0_Entry, nullptr);

// 0x0045d930 (ecx is not read; stack: system, bit; ret 8; caller 0x0044a4b0). Spawns every level table entry (see
// 0x0045d750) whose flags (+0x24) have (1 << bit) (all when bit == -1) and whose id is not already present (0x0045d570):
// 0x0045d7a0(system, id, entry).
uint32_t fn_0045d930(uint32_t objs, uint32_t bit)
{
    uint32_t p = D(P(objs), 0xe5c);
    uint32_t n = D(P(p), 0);
    p += 4;
    uint32_t k = 1;
    if (!n)
        return 0;
    do
    {
        uint32_t cnt = D(P(p), 0);
        p += 4;
        if (cnt)
        {
            do
            {
                const uint32_t size = D(P(p), 0);
                if (bit == 0xffffffffu || ((1u << (bit & 31)) & D(P(p), 0x24)))
                {
                    if (!fn_0045d570(P(objs), k))
                        fn_0045d7a0(objs, k, p);
                }
                p += size;
                ++k;
            } while (--cnt);
        }
    } while (--n);
    return 0;
}
static uint32_t fn_0045d930_Abi(uint32_t /*ecx*/, uint32_t a0, uint32_t a1)
{
    return fn_0045d930(a0, a1);
}
ABI_ENTRY_0045d930(fn_0045d930_Entry, fn_0045d930_Abi)
HOOK(0x0045d930, "fn_0045d930", fn_0045d930_Entry, nullptr);

// 0x0045d9b0 (eax = system; caller 0x0044a4b0). Every object (list +0xe70) without +0x424 bit 0x100 gets vt+0x20()
// (slot 8, e.g. RocketTowerBuilding_SpawnTurret) and the bit.
uint32_t fn_0045d9b0(void* objs)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(objs, 0xe70), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        if (!(D(P(e), 0x424) & 0x100))
        {
            VCall<uint32_t>(P(e), 0x20 / 4);
            D(P(e), 0x424) |= 0x100;
        }
    }
    EndIter(it);
    return it[1];
}
static uint32_t fn_0045d9b0_Abi(uint32_t eax)
{
    return fn_0045d9b0(P(eax));
}
ABI_ENTRY_0045d9b0(fn_0045d9b0_Entry, fn_0045d9b0_Abi)
HOOK(0x0045d9b0, "fn_0045d9b0", fn_0045d9b0_Entry, nullptr);

// 0x0045da20 (ebx = system; callers 0x0045d670, 0x0045da90). Deletes every object of the list +0xe6c: unlinks it
// (0x00454750), destroys it (Object_Destruct 0x004512b0, called directly) and frees it (0x004e08d0).
uint32_t fn_0045da20(void* objs)
{
    uint32_t it[2] = {0, 0};
    for (uint32_t e = orig::fn_004e7340(D(objs, 0xe6c), u(it)); e; e = orig::fn_004e73c0(u(it)))
    {
        ObjectList_Unlink(D(objs, 0xe6c), e);
        if (e)
        {
            Object_Destruct(e);
            orig::fn_004e08d0(kHeap, e);
        }
    }
    EndIter(it);
    return it[1];
}
static uint32_t fn_0045da20_Abi(uint32_t ebx)
{
    return fn_0045da20(P(ebx));
}
ABI_ENTRY_0045da20(fn_0045da20_Entry, fn_0045da20_Abi)
HOOK(0x0045da20, "fn_0045da20", fn_0045da20_Entry, nullptr);

namespace
{
// The (name, behaviour id) pairs registered by 0x0045df60, in its order (strings in .rdata: "fx" 0x38, "proj_bullet" 1,
// ...).
const uint32_t kBehaviours[][2] = {
    {0x005bbb04, 0x38}, {0x005bc040, 0x39}, {0x005b0848, 0x1}, {0x005b0834, 0x3},
    {0x005b0820, 0x2}, {0x005b0808, 0x4}, {0x005b07f0, 0x5}, {0x005b07dc, 0x6},
    {0x005b07c8, 0x7}, {0x005b07ac, 0x8}, {0x005b0794, 0x9}, {0x005b0788, 0xa},
    {0x005b0774, 0xc}, {0x005b075c, 0xd}, {0x005b0744, 0xe}, {0x005b0730, 0x9},
    {0x005b071c, 0xb}, {0x005b0704, 0xc}, {0x005b06f4, 0xf}, {0x005b06e4, 0x10},
    {0x005b06d0, 0x11}, {0x005b06bc, 0x12}, {0x005b06ac, 0x13}, {0x005b069c, 0x14},
    {0x005b068c, 0x15}, {0x005b0678, 0x16}, {0x005b065c, 0x17}, {0x005b0648, 0x18},
    {0x005bc7fc, 0x19}, {0x005bc6ec, 0x1a}, {0x005bc764, 0x1b}, {0x005bc650, 0x1c},
    {0x005bc554, 0x1e}, {0x005bc5c8, 0x1d}, {0x005bc4d8, 0x1f}, {0x005bc444, 0x20},
    {0x005b0634, 0x21}, {0x005bc33c, 0x22}, {0x005bc2b4, 0x23}, {0x005b061c, 0x24},
    {0x005b060c, 0x25}, {0x005b2c24, 0x26}, {0x005b05f4, 0x27}, {0x005b05e0, 0x28},
    {0x005b05cc, 0x29}, {0x005b05bc, 0x2a}, {0x005b05a8, 0x25}, {0x005bc86c, 0x2e},
    {0x005bc740, 0x2d}, {0x005bc7d8, 0x2c}, {0x005bc5a4, 0x30}, {0x005bc628, 0x2f},
    {0x005bc6c8, 0x31}, {0x005bc49c, 0x2b}, {0x005bc40c, 0x2b}, {0x005b0590, 0x2b},
    {0x005b0580, 0x2b}, {0x005b0574, 0x32}, {0x005b15ac, 0x34}, {0x005b0560, 0x33},
    {0x005b0558, 0x35}, {0x005b0548, 0x36}, {0x005b0544, 0x37}, {0x005b27b0, 0x3a},
    {0x005bbfe8, 0x3b}, {0x005bc01c, 0x3c}, {0x005bbff8, 0x3d}, {0x005b0534, 0x3f},
    {0x005b0524, 0x3e}, {0x005b2ce8, 0x40}, {0x005b2cd0, 0x41}, {0x005b0518, 0x42},
    {0x005bb1bc, 0x43}, {0x005b050c, 0x44}, {0x005b0500, 0x45}, {0x005b68d0, 0x46},
    {0x005b04f4, 0x47}, {0x005b640c, 0x48}, {0x005b2e9c, 0x49}, {0x005b04e8, 0x4a},
    {0x005b04e0, 0x4b}, {0x005bbfe0, 0x4c}, {0x005b04d4, 0x4d}, {0x005b04c0, 0x4e},
    {0x005b04b0, 0x4f}, {0x005b04a0, 0x50}, {0x005b048c, 0x51}, {0x005b047c, 0x52},
    {0x005b0468, 0x53}, {0x005b0460, 0x54}, {0x005b0454, 0x55}, {0x005b044c, 0x56},
    {0x005b0444, 0x57}, {0x005b043c, 0x58}, {0x005b0434, 0x59}, {0x005b0424, 0x5a},
    {0x005b0414, 0x5b}, {0x005b0404, 0x5c}, {0x005b03fc, 0x5d}, {0x005b03f4, 0x5e},
    {0x005b03e8, 0x5f}, {0x005b03e4, 0x60}, {0x005b03d8, 0x61}, {0x005b03cc, 0x62},
    {0x005b03c0, 0xa8}, {0x005b03b4, 0x63}, {0x005b03a8, 0x64}, {0x005b0398, 0xa2},
    {0x005b0388, 0xa3}, {0x005b0378, 0xa7}, {0x005b036c, 0xa4}, {0x005b035c, 0xa5},
    {0x005b034c, 0xa6}, {0x005b033c, 0x65}, {0x005b0334, 0x66}, {0x005b0324, 0x67},
    {0x005b0310, 0x68}, {0x005b02fc, 0x69}, {0x005b02ec, 0x6a}, {0x005b02dc, 0x6b},
    {0x005b02c8, 0x6c}, {0x005b02bc, 0x6d}, {0x005b02a8, 0x6e}, {0x005b1640, 0x6f},
    {0x005b0294, 0x6f}, {0x005b0288, 0x6f}, {0x005b026c, 0x70}, {0x005b0254, 0x70},
    {0x005b0240, 0x70}, {0x005b022c, 0x70}, {0x005b021c, 0x71}, {0x005b0208, 0x72},
    {0x005b01f8, 0x73}, {0x005b01e4, 0x74}, {0x005b01d8, 0x75}, {0x005b01c0, 0x76},
    {0x005bc274, 0x77}, {0x005b162c, 0x78}, {0x005b15cc, 0x79}, {0x005b01ac, 0x7a},
    {0x005b019c, 0x7a}, {0x005b0188, 0x7a}, {0x005b017c, 0x7a}, {0x005b0170, 0x7b},
    {0x005b0160, 0x7b}, {0x005b0154, 0x7b}, {0x005b0144, 0x7b}, {0x005b0130, 0x7d},
    {0x005b0120, 0x7c}, {0x005bc6dc, 0x7e}, {0x005bc750, 0x7e}, {0x005bc7ec, 0x7e},
    {0x005bbbd8, 0x7e}, {0x005bbbc4, 0x7e}, {0x005bbbb4, 0x7e}, {0x005bc53c, 0x7e},
    {0x005bc5b4, 0x7e}, {0x005bc638, 0x7e}, {0x005bbb9c, 0x7e}, {0x005bbbf0, 0x7e},
    {0x005b0114, 0x7f}, {0x005b010c, 0x80}, {0x005b0100, 0x81}, {0x005b00f0, 0x82},
    {0x005b00d4, 0x83}, {0x005b00cc, 0x84}, {0x005b00c4, 0x85}, {0x005b00b8, 0x86},
    {0x005bad88, 0x87}, {0x005b085c, 0x88}, {0x005bab84, 0x89}, {0x005b00ac, 0x8a},
    {0x005bb2ac, 0x8b}, {0x005b009c, 0x8c}, {0x005b0090, 0x8d}, {0x005b0084, 0x8e},
    {0x005b007c, 0x8f}, {0x005b0070, 0x90}, {0x005b005c, 0x91}, {0x005b0048, 0x92},
    {0x005bc024, 0x93}, {0x005b003c, 0x94}, {0x005b0034, 0x95}, {0x005b002c, 0x96},
    {0x005b001c, 0x9d}, {0x005b0008, 0xa9}, {0x005afff4, 0x9e}, {0x005affe8, 0x9f},
    {0x005affe0, 0xa0}, {0x005affd4, 0x97}, {0x005affc8, 0x98}, {0x005affbc, 0x9c},
    {0x005affb4, 0x99}, {0x005affac, 0x9a}, {0x005affa4, 0x9b}, {0x005aff9c, 0xa1},
    {0x005affbc, 0x9c},
};
}  // namespace

// 0x0045df60 (eax = system; caller 0x0045a4e0). Classifier name ObjectSystem_GetBehaviourId; it builds the behaviour
// name table instead: +4 = +8 = 0, a free list of 0xe0 16-byte nodes at +0xc (zeroed, each linked to the next, the
// last to the old +4 = 0; +4 = first node), then for 197 class names (strings in .rdata: "fx", "actor", "ambient",
// ...) the name hash (0x0044ad70, eax = name) is inserted with its behaviour id into the bit trie at +4 (0x00461ff0,
// edx = hash, ebx = id, edi = system + 4; it logs on a full pool or a hash collision with another id). Returns the last
// insert's eax.
uint32_t ObjectSystem_InitBehaviourTable(void* objs)
{
    const uint32_t root = u(objs) + 4;
    const uint32_t nodes = u(objs) + 0xc;
    D(objs, 8) = 0;
    D(objs, 4) = 0;
    std::memset(P(nodes), 0, 0x380 * 4);
    for (uint32_t i = 0; i < 0xdf; ++i)
        D(P(nodes), i * 0x10) = nodes + (i + 1) * 0x10;
    D(P(nodes), 0xdf0) = D(objs, 4);
    D(objs, 4) = nodes;
    uint32_t r = 0;
    for (const auto& b : kBehaviours)
        r = orig::fn_00461ff0(orig::fn_0044ad70(b[0]), b[1], root);
    return r;
}
static uint32_t ObjectSystem_InitBehaviourTable_Abi(uint32_t eax)
{
    return ObjectSystem_InitBehaviourTable(P(eax));
}
ABI_ENTRY_0045df60(ObjectSystem_InitBehaviourTable_Entry, ObjectSystem_InitBehaviourTable_Abi)
HOOK(0x0045df60, "ObjectSystem_InitBehaviourTable", ObjectSystem_InitBehaviourTable_Entry, nullptr);
