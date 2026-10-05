// Recovered: game-side physics glue and the game-written Havok extensions (batch physics_03: 0x00459bb0-0x0048e700
// physics glue, 0x004a3c40-0x004a69c0 of the havok-ext block). Static recovery 2026-10-05, compile-checked only (the
// game was not run); the oracle (tools/compare.py) is still to do.
//
// Havok itself is not decompiled: Havok functions are called through orig::fn_XXXXXXXX exactly as the original does,
// and Havok objects are accessed with Field<T> at the offsets documented in docs/subsystems/physics.md (entity +0x08
// motion, +0x10 simulation island, +0x44 world; island +0x24 index, +0x29 active; hkReferencedObject u16 +4 memory
// size, u16 +6 reference count; hkMemory singleton [0x005fed84] virtual +0x10 alloc(size, class), +0x14 free(p,
// size, class)).
//
// havok-ext classes identified here from the .rdata vtable scan (orig/pc/GameData/game.unpacked.exe):
//   0x0058e760 OffsetSphereShape (guess: a sphere-like convex shape with a centre offset): slot 0 0x004a3c80, slot 1
//              0x004a3c40 (type 5), slot 3 0x004a3cb0 (getAabb); ctor 0x004a3c50; +0x10 centre (4 floats), +0x20
//              radius.
//   0x0058e704 Type13Shape (getType 0x004a4520 returns 0x13): ctor 0x004a4530; +0x0c child shape (referenced),
//              +0x10 a value copied into the child's collidable, +0x14 a referenced object whose virtual +4(A, B)
//              decides whether the pair collides (guess: a trigger / filter callback).
//   0x0058e74c Type13 vs shape agent (slots 0x004a3fd0, 0x004a4110, 0x004a4070, 0x004a3f70), 0x0058e738 shape vs
//              Type13 (0x004a41c0, 0x004a4260, 0x004a4240), 0x0058e724 Type13 vs Type13 (0x004a4330, 0x004a4470,
//              0x004a43d0; ctor 0x004a42c0). All share the deleting destructor 0x004a3fa0 (hkMemory class 0x15) and
//              keep the child agent (created by the dispatcher 0x00513420) at +8. Registered by 0x004a3d80 / 0x004a4280.
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Large functions keep the
// original stack frame as one byte array (Frame), addressed with the original's esp offsets after the register pushes.
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
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb5c = -1.0, 0x005bcd94 = 0.4,
// 0x005bcd90 = 4/3, 0x0057e864 = pi).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
// The original stack frame of a large function, addressed by the original's esp offsets.
template <uint32_t N>
struct Frame
{
    alignas(16) uint8_t b[N];
    Frame()
    {
        std::memset(b, 0, N);
    }
    uint32_t& L(uint32_t o)
    {
        return *reinterpret_cast<uint32_t*>(b + o);
    }
    float& F(uint32_t o)
    {
        return *reinterpret_cast<float*>(b + o);
    }
    uint8_t& C(uint32_t o)
    {
        return b[o];
    }
    uint32_t A(uint32_t o)
    {
        return reinterpret_cast<uint32_t>(b + o);
    }
};
// 0x004e07f0 (ecx = heap 0x0063a210; stack: tag, size, align, name; callee pops): the game's tagged allocator.
inline uint32_t Alloc(uint32_t tag, uint32_t size, uint32_t align, uint32_t name)
{
    return orig::fn_004e07f0(0x0063a210u, tag, size, align, name);
}
// 0x004e08d0 (ecx = heap 0x0063a210; stack: block): the game's free.
inline uint32_t Free(uint32_t block)
{
    return orig::fn_004e08d0(0x0063a210u, block);
}
// hkMemory singleton [0x005fed84]: virtual +0x10 alloc(size, class), +0x14 free(p, size, class).
inline uint8_t* HkAlloc(uint32_t size, uint32_t cls)
{
    return VCall<uint8_t*>(GameGlobal<void*>(0x005fed84), 0x10 / 4, size, cls);
}
inline uint32_t HkFree(uint32_t p, uint32_t size, uint32_t cls)
{
    return VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, p, size, cls);
}
// hkReferencedObject::removeReference inlined: u16 +6 reference count; at zero, virtual 0 (deleting destructor)(1).
inline uint32_t HkRelease(uint8_t* object, uint32_t eax)
{
    --Field<uint16_t>(object, 6);
    if (Field<uint16_t>(object, 6) == 0)
        return VCall<uint32_t>(object, 0, 1u);
    return eax;
}
// The inlined Havok wake idiom: when the entity has a simulation island (+0x10) that is not active (island +0x29 ==
// 0), the entity is in a world (+0x44) and the island has a valid index (+0x24 != -1), call hkWorld_activateIsland
// 0x004ad130 (esi = world, edi = island).
inline void WakeEntity(uint8_t* entity)
{
    uint8_t* island = Field<uint8_t*>(entity, 0x10);
    if (island && !island[0x29])
    {
        uint8_t* world = Field<uint8_t*>(entity, 0x44);
        if (world && Field<int32_t>(island, 0x24) != -1)
            orig::fn_004ad130(u(world), u(island));
    }
}
inline uint8_t* Motion(uint8_t* entity)
{
    return Field<uint8_t*>(entity, 8);
}
}  // namespace

// ================================================================================================================
// havok-ext: OffsetSphereShape (vtable 0x0058e760)
// ================================================================================================================

// 0x004a3c40 (no inputs; plain ret): returns 5. Slot 1 (getType, guess) of the shape vtable 0x0058e760, and through
// identical-code folding also of the Havok vtables 0x0056b28c / 0x0058e054 / 0x0058e304 / 0x005ac75c (same body).
// Not executed in recorded runs.
uint32_t OffsetSphereShape_GetType()
{
    return 5;
}

static uint32_t OffsetSphereShape_GetType_Abi()
{
    return OffsetSphereShape_GetType();
}
ABI_ENTRY_004a3c40(OffsetSphereShape_GetType_Entry, OffsetSphereShape_GetType_Abi)
HOOK(0x004a3c40, "OffsetSphereShape_GetType", OffsetSphereShape_GetType_Entry, nullptr);

// 0x004a3c50 (esi = shape; stack: radius; ret 4; callers 0x004527d0, 0x00475220): the Havok convex-shape base
// constructor 0x0050f190 (ecx = shape; radius), then vtable 0x0058e760, +0x20 = radius, centre +0x10..+0x1c = 0.
// Returns the shape.
uint32_t OffsetSphereShape_Construct(uint8_t* shape, uint32_t radius)
{
    orig::fn_0050f190(u(shape), radius);
    Field<uint32_t>(shape, 0) = 0x0058e760;
    Field<uint32_t>(shape, 0x20) = radius;
    Field<uint32_t>(shape, 0x1c) = 0;
    Field<uint32_t>(shape, 0x18) = 0;
    Field<uint32_t>(shape, 0x14) = 0;
    Field<uint32_t>(shape, 0x10) = 0;
    return u(shape);
}

static uint32_t OffsetSphereShape_Construct_Abi(uint32_t esi, uint32_t a0)
{
    return OffsetSphereShape_Construct(B(esi), a0);
}
ABI_ENTRY_004a3c50(OffsetSphereShape_Construct_Entry, OffsetSphereShape_Construct_Abi)
HOOK(0x004a3c50, "OffsetSphereShape_Construct", OffsetSphereShape_Construct_Entry, nullptr);

// 0x004a3c80 (ecx = shape; stack: flags; ret 4): vtable 0x0058e760 slot 0, deleting destructor: base vtable
// 0x0058e3c4 (hkReferencedObject, guess), and when flags bit 0 is set hkMemory free(shape, u16 +4, class 0x1d).
// Returns the shape. Not executed in recorded runs.
uint32_t OffsetSphereShape_DeletingDestructor(uint8_t* shape, uint8_t flags)
{
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(shape), Field<uint16_t>(shape, 4), 0x1d);
    return u(shape);
}

static uint32_t OffsetSphereShape_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return OffsetSphereShape_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004a3c80(OffsetSphereShape_DeletingDestructor_Entry, OffsetSphereShape_DeletingDestructor_Abi)
HOOK(0x004a3c80, "OffsetSphereShape_DeletingDestructor", OffsetSphereShape_DeletingDestructor_Entry, nullptr);

// 0x004a3cb0 (ecx = shape; stack: transform, tolerance, out; ret 0xc): vtable 0x0058e760 slot 3 (getAabb, guess):
// r = tolerance + radius (+0x20); out min = transform translation (+0x30, 4 floats) - r, out max = r + translation;
// then the centre (+0x10, 4 floats) is added to both, unrotated. Executed in recorded runs (no direct callers).
uint32_t OffsetSphereShape_GetAabb(uint8_t* shape, const float* transform, float tolerance, float* out)
{
    const float r = tolerance + Field<float>(shape, 0x20);
    out[0] = transform[12] - r;
    out[1] = transform[13] - r;
    out[2] = transform[14] - r;
    out[3] = transform[15] - r;
    out[4] = r + transform[12];
    out[5] = r + transform[13];
    out[6] = r + transform[14];
    out[7] = r + transform[15];
    for (int i = 0; i < 4; ++i)
        out[i] = Field<float>(shape, 0x10 + 4 * i) + out[i];
    for (int i = 0; i < 4; ++i)
        out[4 + i] = Field<float>(shape, 0x10 + 4 * i) + out[4 + i];
    return u(out);
}

static uint32_t OffsetSphereShape_GetAabb_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return OffsetSphereShape_GetAabb(B(ecx), reinterpret_cast<const float*>(a0), bf(a1), reinterpret_cast<float*>(a2));
}
ABI_ENTRY_004a3cb0(OffsetSphereShape_GetAabb_Entry, OffsetSphereShape_GetAabb_Abi)
HOOK(0x004a3cb0, "OffsetSphereShape_GetAabb", OffsetSphereShape_GetAabb_Entry, nullptr);

// 0x004a3d50 (eax = centre (4 dwords), ecx = shape; plain ret; caller 0x004517b0): copies the centre to +0x10..+0x1c.
// Returns the fourth dword.
uint32_t OffsetSphereShape_SetCentre(const uint32_t* centre, uint8_t* shape)
{
    Field<uint32_t>(shape, 0x10) = centre[0];
    Field<uint32_t>(shape, 0x14) = centre[1];
    Field<uint32_t>(shape, 0x18) = centre[2];
    const uint32_t w = centre[3];
    Field<uint32_t>(shape, 0x1c) = w;
    return w;
}

static uint32_t OffsetSphereShape_SetCentre_Abi(uint32_t eax, uint32_t ecx)
{
    return OffsetSphereShape_SetCentre(reinterpret_cast<const uint32_t*>(eax), B(ecx));
}
ABI_ENTRY_004a3d50(OffsetSphereShape_SetCentre_Entry, OffsetSphereShape_SetCentre_Abi)
HOOK(0x004a3d50, "OffsetSphereShape_SetCentre", OffsetSphereShape_SetCentre_Entry, nullptr);

// 0x004a3d70 (ecx = shape; stack: radius; ret 4; callers 0x004517b0, 0x004527d0, 0x00475220): +0x20 = radius.
uint32_t OffsetSphereShape_SetRadius(uint8_t* shape, uint32_t radius)
{
    Field<uint32_t>(shape, 0x20) = radius;
    return radius;
}

static uint32_t OffsetSphereShape_SetRadius_Abi(uint32_t ecx, uint32_t a0)
{
    return OffsetSphereShape_SetRadius(B(ecx), a0);
}
ABI_ENTRY_004a3d70(OffsetSphereShape_SetRadius_Entry, OffsetSphereShape_SetRadius_Abi)
HOOK(0x004a3d70, "OffsetSphereShape_SetRadius", OffsetSphereShape_SetRadius_Entry, nullptr);

// ================================================================================================================
// havok-ext: Type13 collision agents
// ================================================================================================================

// 0x004a3d80 (esi = collision dispatcher; plain ret; caller 0x00411960): hkCollisionDispatcher_registerAgent 0x00513270
// (ecx = dispatcher; createFn, typeA, typeB) for 0x004a3f10 (0x13, t) and 0x004a3f40 (t, 0x13), for t = 1, 5..0xe and
// 0x12, in that order. Returns the last registration's eax.
uint32_t CollisionAgents_RegisterType13(uint32_t dispatcher)
{
    static const uint32_t types[] = {1, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0x12};
    uint32_t r = 0;
    for (uint32_t t : types)
    {
        orig::fn_00513270(dispatcher, 0x004a3f10u, 0x13, t);
        r = orig::fn_00513270(dispatcher, 0x004a3f40u, t, 0x13);
    }
    return r;
}

static uint32_t CollisionAgents_RegisterType13_Abi(uint32_t esi)
{
    return CollisionAgents_RegisterType13(esi);
}
ABI_ENTRY_004a3d80(CollisionAgents_RegisterType13_Entry, CollisionAgents_RegisterType13_Abi)
HOOK(0x004a3d80, "CollisionAgents_RegisterType13", CollisionAgents_RegisterType13_Entry, nullptr);

// 0x004a3f70 (ecx = agent; stack: a0; ret 4): vtable 0x0058e74c slot 4 (cleanup, guess): when a child agent is held
// (+8), its virtual +0x10(a0); then the agent's virtual 0 (deleting destructor)(1). Executed in recorded runs.
uint32_t Type13Agent_Cleanup(uint8_t* agent, uint32_t a0)
{
    uint8_t* child = Field<uint8_t*>(agent, 8);
    if (child)
        VCall<uint32_t>(child, 0x10 / 4, a0);
    return VCall<uint32_t>(agent, 0, 1u);
}

static uint32_t Type13Agent_Cleanup_Abi(uint32_t ecx, uint32_t a0)
{
    return Type13Agent_Cleanup(B(ecx), a0);
}
ABI_ENTRY_004a3f70(Type13Agent_Cleanup_Entry, Type13Agent_Cleanup_Abi)
HOOK(0x004a3f70, "Type13Agent_Cleanup", Type13Agent_Cleanup_Entry, nullptr);

// 0x004a3f90 (eax = agent; stack: 3 unused arguments; ret 0xc; eax preserved): vtable 0x0058e74c, child agent +8 = 0
// (the Type13-vs-shape agent constructor body, guess). Executed in recorded runs (no direct callers found).
void Type13Agent_Init(uint8_t* agent)
{
    Field<uint32_t>(agent, 0) = 0x0058e74c;
    Field<uint32_t>(agent, 8) = 0;
}

static uint32_t Type13Agent_Init_Abi(uint32_t eax, uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    Type13Agent_Init(B(eax));
    return eax;
}
ABI_ENTRY_004a3f90(Type13Agent_Init_Entry, Type13Agent_Init_Abi)
HOOK(0x004a3f90, "Type13Agent_Init", Type13Agent_Init_Entry, nullptr);

// 0x004a3fa0 (ecx = agent; stack: flags; ret 4): deleting destructor shared by the agent vtables 0x0058e674 (speed
// trap), 0x0058e688, 0x0058e724, 0x0058e738 and 0x0058e74c (slot 0): base vtable 0x0058e3c4, and when flags bit 0 is
// set hkMemory free(agent, u16 +4, class 0x15). Returns the agent. Not executed in recorded runs.
uint32_t HkAgent_DeletingDestructor(uint8_t* agent, uint8_t flags)
{
    Field<uint32_t>(agent, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(agent), Field<uint16_t>(agent, 4), 0x15);
    return u(agent);
}

static uint32_t HkAgent_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return HkAgent_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004a3fa0(HkAgent_DeletingDestructor_Entry, HkAgent_DeletingDestructor_Abi)
HOOK(0x004a3fa0, "HkAgent_DeletingDestructor", HkAgent_DeletingDestructor_Entry, nullptr);

namespace
{
// The collidable copy the Type13 agents pass on: 8 dwords of the original collidable, then +0x10 = the Type13 shape's
// child (+0x0c) and +0x0c = the shape's +0x10 (shape = collidable +0x10).
struct Collidable8
{
    uint32_t d[8];
};
}  // namespace

// Shared body of 0x004a3fd0 / 0x004a4070 (ecx = agent; stack: A, B, input, collector; ret 0x10): copies A (8 dwords)
// with its Type13 shape (A +0x10) replaced by the child shape (+0x0c, at copy +0x10) and copy +0x0c = shape +0x10;
// asks the shape's +0x14 object virtual +4(&copy, B). When it agrees: creates the child agent on first use through
// 0x00513420 (ecx = [input]; &copy, B, input) into agent +8 and returns child virtual `slot`(&copy, B, input,
// collector). Otherwise releases the child agent (virtual +0x10(input)), clears +8; eax as left by the last call.
static uint32_t Type13Agent_Forward4(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t* input, uint32_t collector,
                                     int slot)
{
    uint8_t* shape = B(a[4]);
    Collidable8 copy;
    std::memcpy(copy.d, a, sizeof(copy.d));
    copy.d[4] = Field<uint32_t>(shape, 0xc);
    copy.d[3] = Field<uint32_t>(shape, 0x10);
    uint8_t* test = Field<uint8_t*>(shape, 0x14);
    const uint32_t ok = VCall<uint32_t>(test, 4 / 4, u(&copy), b);
    if (static_cast<uint8_t>(ok))
    {
        if (!Field<uint32_t>(agent, 8))
            Field<uint32_t>(agent, 8) = orig::fn_00513420(input[0], u(&copy), b, u(input));
        return VCall<uint32_t>(Field<uint8_t*>(agent, 8), slot, u(&copy), b, u(input), collector);
    }
    uint8_t* child = Field<uint8_t*>(agent, 8);
    if (child)
    {
        const uint32_t r = VCall<uint32_t>(child, 0x10 / 4, u(input));
        Field<uint32_t>(agent, 8) = 0;
        return r;
    }
    return ok;
}

// 0x004a3fd0 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e74c slot 1: Type13Agent_Forward4
// with child slot 1 (+4). Executed in recorded runs (also called by 0x004a41c0).
uint32_t Type13Agent_Slot1(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t* input, uint32_t collector)
{
    return Type13Agent_Forward4(agent, a, b, input, collector, 4 / 4);
}

static uint32_t Type13Agent_Slot1_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13Agent_Slot1(B(ecx), reinterpret_cast<const uint32_t*>(a0), a1, reinterpret_cast<uint32_t*>(a2), a3);
}
ABI_ENTRY_004a3fd0(Type13Agent_Slot1_Entry, Type13Agent_Slot1_Abi)
HOOK(0x004a3fd0, "Type13Agent_Slot1", Type13Agent_Slot1_Entry, nullptr);

// 0x004a4070 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e74c slot 3: Type13Agent_Forward4
// with child slot 3 (+0xc). Executed in recorded runs (also called by 0x004a4240).
uint32_t Type13Agent_Slot3(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t* input, uint32_t collector)
{
    return Type13Agent_Forward4(agent, a, b, input, collector, 0xc / 4);
}

static uint32_t Type13Agent_Slot3_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13Agent_Slot3(B(ecx), reinterpret_cast<const uint32_t*>(a0), a1, reinterpret_cast<uint32_t*>(a2), a3);
}
ABI_ENTRY_004a4070(Type13Agent_Slot3_Entry, Type13Agent_Slot3_Abi)
HOOK(0x004a4070, "Type13Agent_Slot3", Type13Agent_Slot3_Entry, nullptr);

// 0x004a4110 (ecx = agent; stack: A, B, input; ret 0xc): vtable 0x0058e74c slot 2: as Type13Agent_Forward4 with three
// arguments and child slot 2 (+8). When the shape's test refuses, returns al = 0 (upper eax bytes from the last
// call). Not executed in recorded runs.
uint32_t Type13Agent_Slot2(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t* input)
{
    uint8_t* shape = B(a[4]);
    Collidable8 copy;
    std::memcpy(copy.d, a, sizeof(copy.d));
    copy.d[4] = Field<uint32_t>(shape, 0xc);
    copy.d[3] = Field<uint32_t>(shape, 0x10);
    uint8_t* test = Field<uint8_t*>(shape, 0x14);
    uint32_t r = VCall<uint32_t>(test, 4 / 4, u(&copy), b);
    if (static_cast<uint8_t>(r))
    {
        if (!Field<uint32_t>(agent, 8))
            Field<uint32_t>(agent, 8) = orig::fn_00513420(input[0], u(&copy), b, u(input));
        return VCall<uint32_t>(Field<uint8_t*>(agent, 8), 8 / 4, u(&copy), b, u(input));
    }
    uint8_t* child = Field<uint8_t*>(agent, 8);
    if (child)
    {
        r = VCall<uint32_t>(child, 0x10 / 4, u(input));
        Field<uint32_t>(agent, 8) = 0;
    }
    return r & 0xffffff00u;
}

static uint32_t Type13Agent_Slot2_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type13Agent_Slot2(B(ecx), reinterpret_cast<const uint32_t*>(a0), a1, reinterpret_cast<uint32_t*>(a2));
}
ABI_ENTRY_004a4110(Type13Agent_Slot2_Entry, Type13Agent_Slot2_Abi)
HOOK(0x004a4110, "Type13Agent_Slot2", Type13Agent_Slot2_Entry, nullptr);

// 0x004a41c0 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e738 slot 1 (shape vs Type13):
// remembers the collector's point count (collector +4 -> +4), calls Type13Agent_Slot1 with A and B swapped, then
// multiplies the normal (+0x10, 4 floats) of every point added (0x30 bytes each, array collector +4 -> +0) by -1.
// Returns collector +4 (eax as left by the loop test). Not executed in recorded runs.
uint32_t Type13SwappedAgent_Slot1(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input, uint8_t* collector)
{
    int32_t i = Field<int32_t>(Field<uint8_t*>(collector, 4), 4);
    Type13Agent_Slot1(agent, reinterpret_cast<const uint32_t*>(b), a, reinterpret_cast<uint32_t*>(input),
                      u(collector));
    uint8_t* points = Field<uint8_t*>(collector, 4);
    for (; i < Field<int32_t>(points, 4); ++i)
    {
        float* n = reinterpret_cast<float*>(Field<uint8_t*>(points, 0) + i * 0x30 + 0x10);
        n[0] = n[0] * K(0x005bcb5c);
        n[1] = n[1] * K(0x005bcb5c);
        n[2] = n[2] * K(0x005bcb5c);
        n[3] = n[3] * K(0x005bcb5c);
        points = Field<uint8_t*>(collector, 4);
    }
    return u(points);
}

static uint32_t Type13SwappedAgent_Slot1_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13SwappedAgent_Slot1(B(ecx), a0, a1, a2, B(a3));
}
ABI_ENTRY_004a41c0(Type13SwappedAgent_Slot1_Entry, Type13SwappedAgent_Slot1_Abi)
HOOK(0x004a41c0, "Type13SwappedAgent_Slot1", Type13SwappedAgent_Slot1_Entry, nullptr);

// 0x004a4240 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e738 slot 3: Type13Agent_Slot3 with
// A and B swapped. Not executed in recorded runs.
uint32_t Type13SwappedAgent_Slot3(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input, uint32_t collector)
{
    return Type13Agent_Slot3(agent, reinterpret_cast<const uint32_t*>(b), a, reinterpret_cast<uint32_t*>(input),
                             collector);
}

static uint32_t Type13SwappedAgent_Slot3_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13SwappedAgent_Slot3(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004a4240(Type13SwappedAgent_Slot3_Entry, Type13SwappedAgent_Slot3_Abi)
HOOK(0x004a4240, "Type13SwappedAgent_Slot3", Type13SwappedAgent_Slot3_Entry, nullptr);

// 0x004a4260 (ecx = agent; stack: A, B, input; ret 0xc): vtable 0x0058e738 slot 2: Type13Agent_Slot2 with A and B
// swapped. Not executed in recorded runs.
uint32_t Type13SwappedAgent_Slot2(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input)
{
    return Type13Agent_Slot2(agent, reinterpret_cast<const uint32_t*>(b), a, reinterpret_cast<uint32_t*>(input));
}

static uint32_t Type13SwappedAgent_Slot2_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type13SwappedAgent_Slot2(B(ecx), a0, a1, a2);
}
ABI_ENTRY_004a4260(Type13SwappedAgent_Slot2_Entry, Type13SwappedAgent_Slot2_Abi)
HOOK(0x004a4260, "Type13SwappedAgent_Slot2", Type13SwappedAgent_Slot2_Entry, nullptr);

// 0x004a4280 (ecx = collision dispatcher; plain ret; caller 0x00411960): registerAgent 0x00513270 (ecx; 0x004a4290,
// 0x13, 0x13).
uint32_t CollisionAgents_RegisterType13Pair(uint32_t dispatcher)
{
    return orig::fn_00513270(dispatcher, 0x004a4290u, 0x13, 0x13);
}

static uint32_t CollisionAgents_RegisterType13Pair_Abi(uint32_t ecx)
{
    return CollisionAgents_RegisterType13Pair(ecx);
}
ABI_ENTRY_004a4280(CollisionAgents_RegisterType13Pair_Entry, CollisionAgents_RegisterType13Pair_Abi)
HOOK(0x004a4280, "CollisionAgents_RegisterType13Pair", CollisionAgents_RegisterType13Pair_Entry, nullptr);

namespace
{
// Both collidables of a Type13 pair, copied as 0x004a42c0 / 0x004a4330 / 0x004a43d0 / 0x004a4470 do: copy = 8 dwords
// of the collidable, +0x14 rewritten (same value), +0x10 = shape (+0x10) child (+0x0c), +0x0c = shape +0x10.
inline void CopyType13(Collidable8& copy, const uint32_t* c)
{
    std::memcpy(copy.d, c, sizeof(copy.d));
    copy.d[5] = c[5];
    copy.d[4] = Field<uint32_t>(B(c[4]), 0xc);
    copy.d[3] = Field<uint32_t>(B(c[4]), 0x10);
}
}  // namespace

// 0x004a42c0 (eax = A, edx = B, ebx = agent; stack: input; ret 4): Type13-pair agent constructor body: vtable
// 0x0058e724, copies of A and B with their child shapes (+0x10 = shape child; A's copy +0x0c is not rewritten here,
// unlike the slots), agent +8 = 0x00513420 (ecx = [input]; &copyB, &copyA, input). Returns the agent. Not executed in
// recorded runs (reached from the create function 0x004a4290).
uint32_t Type13PairAgent_Construct(const uint32_t* a, const uint32_t* b, uint8_t* agent, uint32_t* input)
{
    Field<uint32_t>(agent, 0) = 0x0058e724;
    Collidable8 copyB;
    std::memcpy(copyB.d, b, sizeof(copyB.d));
    copyB.d[5] = b[5];
    copyB.d[4] = Field<uint32_t>(B(b[4]), 0xc);
    Collidable8 copyA;
    std::memcpy(copyA.d, a, sizeof(copyA.d));
    copyA.d[5] = a[5];
    copyA.d[4] = Field<uint32_t>(B(a[4]), 0xc);
    Field<uint32_t>(agent, 8) = orig::fn_00513420(input[0], u(&copyB), u(&copyA), u(input));
    return u(agent);
}

static uint32_t Type13PairAgent_Construct_Abi(uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t a0)
{
    return Type13PairAgent_Construct(reinterpret_cast<const uint32_t*>(eax), reinterpret_cast<const uint32_t*>(edx),
                                     B(ebx), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004a42c0(Type13PairAgent_Construct_Entry, Type13PairAgent_Construct_Abi)
HOOK(0x004a42c0, "Type13PairAgent_Construct", Type13PairAgent_Construct_Entry, nullptr);

// Shared body of 0x004a4330 / 0x004a43d0 (ecx = agent; stack: A, B, input, collector; ret 0x10): copies of A and B
// with their child shapes (CopyType13), asks A's shape +0x14 object virtual +4(&copyA, &copyB); when it agrees,
// returns child agent (+8) virtual `slot`(&copyA, &copyB, input, collector), else the test's eax.
static uint32_t Type13PairAgent_Forward4(uint8_t* agent, const uint32_t* a, const uint32_t* b, uint32_t input,
                                         uint32_t collector, int slot)
{
    Collidable8 copyA;
    Collidable8 copyB;
    CopyType13(copyA, a);
    CopyType13(copyB, b);
    const uint32_t ok = VCall<uint32_t>(Field<uint8_t*>(B(a[4]), 0x14), 4 / 4, u(&copyA), u(&copyB));
    if (!static_cast<uint8_t>(ok))
        return ok;
    return VCall<uint32_t>(Field<uint8_t*>(agent, 8), slot, u(&copyA), u(&copyB), input, collector);
}

// 0x004a4330: vtable 0x0058e724 slot 1, Type13PairAgent_Forward4 with child slot 1 (+4). Not executed in recorded
// runs.
uint32_t Type13PairAgent_Slot1(uint8_t* agent, const uint32_t* a, const uint32_t* b, uint32_t input, uint32_t collector)
{
    return Type13PairAgent_Forward4(agent, a, b, input, collector, 4 / 4);
}

static uint32_t Type13PairAgent_Slot1_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13PairAgent_Slot1(B(ecx), reinterpret_cast<const uint32_t*>(a0), reinterpret_cast<const uint32_t*>(a1),
                                 a2, a3);
}
ABI_ENTRY_004a4330(Type13PairAgent_Slot1_Entry, Type13PairAgent_Slot1_Abi)
HOOK(0x004a4330, "Type13PairAgent_Slot1", Type13PairAgent_Slot1_Entry, nullptr);

// 0x004a43d0: vtable 0x0058e724 slot 3, Type13PairAgent_Forward4 with child slot 3 (+0xc). Not executed in recorded
// runs.
uint32_t Type13PairAgent_Slot3(uint8_t* agent, const uint32_t* a, const uint32_t* b, uint32_t input, uint32_t collector)
{
    return Type13PairAgent_Forward4(agent, a, b, input, collector, 0xc / 4);
}

static uint32_t Type13PairAgent_Slot3_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type13PairAgent_Slot3(B(ecx), reinterpret_cast<const uint32_t*>(a0), reinterpret_cast<const uint32_t*>(a1),
                                 a2, a3);
}
ABI_ENTRY_004a43d0(Type13PairAgent_Slot3_Entry, Type13PairAgent_Slot3_Abi)
HOOK(0x004a43d0, "Type13PairAgent_Slot3", Type13PairAgent_Slot3_Entry, nullptr);

// 0x004a4470 (ecx = agent; stack: A, B, input; ret 0xc): vtable 0x0058e724 slot 2: as Type13PairAgent_Forward4 with
// three arguments and child slot 2 (+8); al = 0 when the test refuses. Not executed in recorded runs.
uint32_t Type13PairAgent_Slot2(uint8_t* agent, const uint32_t* a, const uint32_t* b, uint32_t input)
{
    Collidable8 copyA;
    Collidable8 copyB;
    CopyType13(copyA, a);
    CopyType13(copyB, b);
    const uint32_t ok = VCall<uint32_t>(Field<uint8_t*>(B(a[4]), 0x14), 4 / 4, u(&copyA), u(&copyB));
    if (!static_cast<uint8_t>(ok))
        return ok & 0xffffff00u;
    return VCall<uint32_t>(Field<uint8_t*>(agent, 8), 8 / 4, u(&copyA), u(&copyB), input);
}

static uint32_t Type13PairAgent_Slot2_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type13PairAgent_Slot2(B(ecx), reinterpret_cast<const uint32_t*>(a0), reinterpret_cast<const uint32_t*>(a1),
                                 a2);
}
ABI_ENTRY_004a4470(Type13PairAgent_Slot2_Entry, Type13PairAgent_Slot2_Abi)
HOOK(0x004a4470, "Type13PairAgent_Slot2", Type13PairAgent_Slot2_Entry, nullptr);

// ================================================================================================================
// havok-ext: Type13Shape (vtable 0x0058e704)
// ================================================================================================================

// 0x004a4520 (no inputs; plain ret): returns 0x13. Slots 1 and 2 of 0x0058e704 (getType, guess). Not executed in
// recorded runs.
uint32_t Type13Shape_GetType()
{
    return 0x13;
}

static uint32_t Type13Shape_GetType_Abi()
{
    return Type13Shape_GetType();
}
ABI_ENTRY_004a4520(Type13Shape_GetType_Entry, Type13Shape_GetType_Abi)
HOOK(0x004a4520, "Type13Shape_GetType", Type13Shape_GetType_Entry, nullptr);

// 0x004a4530 (eax = shape, ecx = child shape, edx = value; stack: test object; ret 4; eax preserved; callers
// 0x004517b0, 0x004527d0, 0x004574e0): +0x10 = value, +0x0c = child, +0x14 = test object, u16 +6 = 1 (references),
// +8 = 0, vtable 0x0058e704; adds a reference (u16 +6) to the child and to the test object.
void Type13Shape_Construct(uint8_t* shape, uint8_t* child, uint32_t value, uint32_t test)
{
    Field<uint32_t>(shape, 0x10) = value;
    Field<uint8_t*>(shape, 0xc) = child;
    Field<uint32_t>(shape, 0x14) = test;
    Field<uint16_t>(shape, 6) = 1;
    Field<uint32_t>(shape, 8) = 0;
    Field<uint32_t>(shape, 0) = 0x0058e704;
    ++Field<uint16_t>(child, 6);
    ++Field<uint16_t>(Field<uint8_t*>(shape, 0x14), 6);
}

static uint32_t Type13Shape_Construct_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t a0)
{
    Type13Shape_Construct(B(eax), B(ecx), edx, a0);
    return eax;
}
ABI_ENTRY_004a4530(Type13Shape_Construct_Entry, Type13Shape_Construct_Abi)
HOOK(0x004a4530, "Type13Shape_Construct", Type13Shape_Construct_Entry, nullptr);

uint32_t Type13Shape_Destruct(uint8_t* shape);

// 0x004a4560 (ecx = shape; stack: flags; ret 4): vtable 0x0058e704 slot 0, deleting destructor: Type13Shape_Destruct
// 0x004a4590, then when flags bit 0 is set hkMemory free(shape, 0x18, class 0x1d). Returns the shape. Executed in
// recorded runs.
uint32_t Type13Shape_DeletingDestructor(uint8_t* shape, uint8_t flags)
{
    Type13Shape_Destruct(shape);
    if (flags & 1)
        HkFree(u(shape), 0x18, 0x1d);
    return u(shape);
}

static uint32_t Type13Shape_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return Type13Shape_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004a4560(Type13Shape_DeletingDestructor_Entry, Type13Shape_DeletingDestructor_Abi)
HOOK(0x004a4560, "Type13Shape_DeletingDestructor", Type13Shape_DeletingDestructor_Entry, nullptr);

// 0x004a4590 (esi = shape; plain ret): vtable 0x0058e704, releases the child shape (+0x0c) and the test object (+0x14)
// (u16 +6 reference count, virtual 0 (1) at zero), then the base vtable 0x0058e3c4. eax as left: the second release's
// destructor result, else not reproduced (0; the only caller 0x004a4560 ignores it).
uint32_t Type13Shape_Destruct(uint8_t* shape)
{
    Field<uint32_t>(shape, 0) = 0x0058e704;
    HkRelease(Field<uint8_t*>(shape, 0xc), 0);
    const uint32_t r = HkRelease(Field<uint8_t*>(shape, 0x14), 0);
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    return r;
}

static uint32_t Type13Shape_Destruct_Abi(uint32_t esi)
{
    return Type13Shape_Destruct(B(esi));
}
ABI_ENTRY_004a4590(Type13Shape_Destruct_Entry, Type13Shape_Destruct_Abi)
HOOK(0x004a4590, "Type13Shape_Destruct", Type13Shape_Destruct_Entry, nullptr);

// Entry stubs for the two tail-jump slots below (symbols/abi.csv: no ret found). Same shape as src/gen/entry_stubs.h:
// save registers, push ecx and the stack arguments, call the cdecl impl, then pop the arguments the jump target pops.
#define P3_ENTRY_ECX_3(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 36] __asm push dword ptr [esp + 36] __asm push dword ptr [esp + 36] __asm push ecx __asm call impl __asm add esp, 16 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 12 }
#define P3_ENTRY_ECX_5(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push ecx __asm call impl __asm add esp, 24 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 20 }

// 0x004a45d0 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058e704 slot 3 (getAabb): a tail jump
// to the child shape's (+0x0c) virtual +0xc with ecx = child. getAabb takes 3 stack arguments (ret 0xc in 0x004a3cb0
// and the other slot-3 bodies), so it is reproduced as a 3-argument call. Not executed in recorded runs.
uint32_t Type13Shape_GetAabb(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0xc), 0xc / 4, a0, a1, a2);
}

static uint32_t Type13Shape_GetAabb_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type13Shape_GetAabb(B(ecx), a0, a1, a2);
}
P3_ENTRY_ECX_3(Type13Shape_GetAabb_Entry, Type13Shape_GetAabb_Abi)
HOOK(0x004a45d0, "Type13Shape_GetAabb", Type13Shape_GetAabb_Entry, nullptr);

// 0x004a45e0 (no register inputs; stack: 3; ret 0xc): returns 0. vtable 0x0058e704 slot 6 (also folded into the
// vtables 0x0056b36c, 0x0056b44c, 0x005b0b0c, 0x005baa34). Not executed in recorded runs.
uint32_t Type13Shape_Slot6(uint32_t /*a0*/, uint32_t /*a1*/, uint32_t /*a2*/)
{
    return 0;
}

static uint32_t Type13Shape_Slot6_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type13Shape_Slot6(a0, a1, a2);
}
ABI_ENTRY_004a45e0(Type13Shape_Slot6_Entry, Type13Shape_Slot6_Abi)
HOOK(0x004a45e0, "Type13Shape_Slot6", Type13Shape_Slot6_Entry, nullptr);

// 0x004a45f0 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058e704 slot 7: a tail jump to the
// child shape's (+0x0c) virtual +0x1c with ecx = child. Slot 7 takes 5 stack arguments (0x005115f0, slot 7 of the
// sibling vtable 0x0058e760, ends in ret 0x14), so it is reproduced as a 5-argument call. Not executed in recorded
// runs.
uint32_t Type13Shape_Slot7(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0xc), 0x1c / 4, a0, a1, a2, a3, a4);
}

static uint32_t Type13Shape_Slot7_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Type13Shape_Slot7(B(ecx), a0, a1, a2, a3, a4);
}
P3_ENTRY_ECX_5(Type13Shape_Slot7_Entry, Type13Shape_Slot7_Abi)
HOOK(0x004a45f0, "Type13Shape_Slot7", Type13Shape_Slot7_Entry, nullptr);

// ================================================================================================================
// havok-ext: geometry helpers
// ================================================================================================================

// 0x004a46b0 (ecx = object; plain ret): returns +0x0c. Folded into the vtables at 0x005898ec, 0x00589b68,
// 0x00589b7c, 0x0058b09c; called by Havok 0x0051cd20. Not executed in recorded runs.
uint32_t fn_004a46b0(uint8_t* object)
{
    return Field<uint32_t>(object, 0xc);
}

static uint32_t fn_004a46b0_Abi(uint32_t ecx)
{
    return fn_004a46b0(B(ecx));
}
ABI_ENTRY_004a46b0(fn_004a46b0_Entry, fn_004a46b0_Abi)
HOOK(0x004a46b0, "fn_004a46b0", fn_004a46b0_Entry, nullptr);

// ================================================================================================================
// Physics glue (game code outside the Havok ranges)
// ================================================================================================================

// 0x0045a1b0 (eax = constraint description, guess; plain ret; eax preserved; caller 0x00456570
// "RigidBodiesAddConstraints"): +0x84 = pi ([0x00588dc8]), +0x80 = -pi, +0x88 = 0, +0 = 0, +4 = 0 (guess: a free
// angular limit). Not executed in recorded runs.
void fn_0045a1b0(uint8_t* desc)
{
    const uint32_t pi = GameGlobal<uint32_t>(0x00588dc8);
    Field<uint32_t>(desc, 0x84) = pi;
    Field<float>(desc, 0x80) = -K(0x00588dc8);
    Field<uint32_t>(desc, 0x88) = 0;
    Field<uint32_t>(desc, 0) = 0;
    Field<uint32_t>(desc, 4) = 0;
}

static uint32_t fn_0045a1b0_Abi(uint32_t eax)
{
    fn_0045a1b0(B(eax));
    return eax;
}
ABI_ENTRY_0045a1b0(fn_0045a1b0_Entry, fn_0045a1b0_Abi)
HOOK(0x0045a1b0, "fn_0045a1b0", fn_0045a1b0_Entry, nullptr);

// 0x0045a3d0 (eax = constraint description, guess; plain ret; eax preserved; caller 0x00456570): +0x84 = +0 = +4 = 0,
// +0x70 = pi / 6, +0x74 = pi * -0.45, +0x78 = pi * 0.45, +0x7c = pi * -1/6, +0x80 = pi / 6 (guess: ragdoll cone /
// twist limits; pi = [0x00588dc8]). Executed in recorded runs.
void fn_0045a3d0(uint8_t* desc)
{
    const float sixth = K(0x00588dc8) * K(0x005bccb0);
    Field<uint32_t>(desc, 0x84) = 0;
    Field<uint32_t>(desc, 0) = 0;
    Field<uint32_t>(desc, 4) = 0;
    Field<float>(desc, 0x70) = sixth;
    Field<float>(desc, 0x74) = K(0x00588dc8) * K(0x005bcccc);
    Field<float>(desc, 0x78) = K(0x00588dc8) * K(0x005bccc8);
    Field<float>(desc, 0x7c) = K(0x00588dc8) * K(0x005bcca8);
    Field<float>(desc, 0x80) = sixth;
}

static uint32_t fn_0045a3d0_Abi(uint32_t eax)
{
    fn_0045a3d0(B(eax));
    return eax;
}
ABI_ENTRY_0045a3d0(fn_0045a3d0_Entry, fn_0045a3d0_Abi)
HOOK(0x0045a3d0, "fn_0045a3d0", fn_0045a3d0_Entry, nullptr);

// 0x00461e10 (no inputs; plain ret): returns 0x00587148, the class descriptor whose +0x14 is "CLASS(ClothObject)"
// (0x005924bc). Slot 1 of the ClothObject vtable 0x005aca40 (stored by 0x004610a2). Not executed in recorded runs.
uint32_t ClothObject_GetClass()
{
    return 0x00587148;
}

static uint32_t ClothObject_GetClass_Abi()
{
    return ClothObject_GetClass();
}
ABI_ENTRY_00461e10(ClothObject_GetClass_Entry, ClothObject_GetClass_Abi)
HOOK(0x00461e10, "ClothObject_GetClass", ClothObject_GetClass_Entry, nullptr);

uint32_t Entity_SetRotation(uint8_t* entity, uint32_t rotation);

// 0x0048cec0 (eax = only tested for null, ecx = entity, esi = saved body state; plain ret; caller 0x0048cfc0): restores
// a body from a state record (guess from the fields): nothing when eax or the entity is null. Record: +0 flags (bit 0
// keep velocities, bit 1 keyframed, bit 2 in world), +4 restitution, +8 friction, +0x0c collision filter info, +0x10
// motion virtual +0x3c argument, +0x20 position, +0x30 rotation, +0x70 / +0x80 linear / angular velocity. Removes the
// entity from its world (hkWorld_removeEntity 0x004ac0e0 (world, entity)) when it has one, unless flag 4 is set and
// entity +0x0c bit 0 is set; 0x004aff70 (edi = entity; (flags >> 1) & 0xffffff01); entity +0x3c / +0x38 / +0x20 from
// +4 / +8 / +0x0c; motion virtual +0x3c(record + 0x10); Entity_SetPosition 0x00406580 (entity, +0x20);
// Entity_SetRotation 0x0048e700 (entity, +0x30); without flag 1 the velocities (0x00456250, 0x00459680); with flag 4
// and no world, hkWorld_addEntity 0x004aba90 ((Game+0xa8c)+8, entity). Returns eax as left by the last step.
uint32_t Body_RestoreState(uint32_t eax, uint8_t* entity, const uint8_t* record)
{
    if (!eax || !entity)
        return eax;
    uint32_t r = eax;
    if (!(record[0] & 4) || (entity[0xc] & 1))
    {
        const uint32_t world = Field<uint32_t>(entity, 0x44);
        r = world;
        if (world)
            r = orig::fn_004ac0e0(world, u(entity));
    }
    orig::fn_004aff70(u(entity), (Field<uint32_t>(record, 0) >> 1) & 0xffffff01u);
    Field<float>(entity, 0x3c) = Field<float>(record, 4);
    Field<float>(entity, 0x38) = Field<float>(record, 8);
    Field<uint32_t>(entity, 0x20) = Field<uint32_t>(record, 0xc);
    VCall<uint32_t>(Motion(entity), 0x3c / 4, u(record + 0x10));
    orig::fn_00406580(u(entity), u(record + 0x20));
    r = Entity_SetRotation(entity, u(record + 0x30));
    if (!(record[0] & 1))
    {
        orig::fn_00456250(u(entity), u(record + 0x70));
        r = orig::fn_00459680(u(entity), u(record + 0x80));
    }
    if (record[0] & 4)
    {
        r = Field<uint32_t>(entity, 0x44);
        if (!r)
            r = orig::fn_004aba90(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), u(entity));
    }
    return r;
}

static uint32_t Body_RestoreState_Abi(uint32_t eax, uint32_t ecx, uint32_t esi)
{
    return Body_RestoreState(eax, B(ecx), B(esi));
}
ABI_ENTRY_0048cec0(Body_RestoreState_Entry, Body_RestoreState_Abi)
HOOK(0x0048cec0, "Body_RestoreState", Body_RestoreState_Entry, nullptr);

// 0x0048e700 (stack: entity, rotation; ret 8; caller 0x0048cec0): wakes the entity (inlined island activation, as
// 0x00458510) and, unless entity +0x0c bit 0 is set while it is in a world, calls motion virtual +0x44 (setRotation)
// (rotation). Returns that call's eax, else the world pointer.
uint32_t Entity_SetRotation(uint8_t* entity, uint32_t rotation)
{
    WakeEntity(entity);
    if ((entity[0xc] & 1) && Field<uint32_t>(entity, 0x44))
        return Field<uint32_t>(entity, 0x44);
    return VCall<uint32_t>(Motion(entity), 0x44 / 4, rotation);
}

static uint32_t Entity_SetRotation_Abi(uint32_t a0, uint32_t a1)
{
    return Entity_SetRotation(B(a0), a1);
}
ABI_ENTRY_0048e700(Entity_SetRotation_Entry, Entity_SetRotation_Abi)
HOOK(0x0048e700, "Entity_SetRotation", Entity_SetRotation_Entry, nullptr);

namespace
{
// The inlined 4-vector normalisation of 0x0045a1e0: inv = 1 / sqrt(len2) (0 when len2 == 0), v = inv * v (4 floats).
// `firstOrder` selects the sum order (v2^2 + v0^2) + v1^2 used after the first call, otherwise (v0^2 + v1^2) + v2^2.
inline void NormalizeInPlace(float* v, bool firstOrder)
{
    const float len2 = firstOrder ? (v[2] * v[2] + v[0] * v[0]) + v[1] * v[1]
                                  : (v[0] * v[0] + v[1] * v[1]) + v[2] * v[2];
    const float inv = (len2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(len2);
    v[0] = inv * v[0];
    v[1] = inv * v[1];
    v[2] = inv * v[2];
    v[3] = inv * v[3];
}
}  // namespace

// 0x0045a1e0 (esi = constraint data, edi = body A; stack: body B, pivot, axis, axis2; ret 0x10; caller 0x00456570
// "RigidBodiesAddConstraints"): +0 = A, +4 = B; +0x10 / +0x20 = the world pivot in A's / B's frame (0x004de720 (ecx =
// out; motion +0x80 transform, pivot)); +0x50 / +0x60 = axis, and +0x30 / +0x40 = axis2, rotated into A's / B's frame
// (0x004de7e0) and normalised (guess: a hinge-style constraint set in world space). The original pushes the next
// call's vector argument before normalising (0x004de7e0 preserves edx). Returns eax as left: B's transform pointer in
// the upper half, the FPU status word of the last zero test in ax: only the pointer is reproduced (the caller
// 0x00456570 ignores it). Executed in recorded runs.
uint32_t fn_0045a1e0(uint8_t* data, uint8_t* bodyA, uint8_t* bodyB, uint32_t pivot, uint32_t axis, uint32_t axis2)
{
    Field<uint8_t*>(data, 0) = bodyA;
    Field<uint8_t*>(data, 4) = bodyB;
    const uint32_t ta = u(Motion(bodyA)) + 0x80;
    orig::fn_004de720(u(data + 0x10), ta, pivot);
    orig::fn_004de720(u(data + 0x20), u(Motion(bodyB)) + 0x80, pivot);
    orig::fn_004de7e0(u(data + 0x50), u(Motion(bodyA)) + 0x80, axis);
    NormalizeInPlace(reinterpret_cast<float*>(data + 0x50), true);
    orig::fn_004de7e0(u(data + 0x60), u(Motion(bodyB)) + 0x80, axis);
    NormalizeInPlace(reinterpret_cast<float*>(data + 0x60), false);
    orig::fn_004de7e0(u(data + 0x30), u(Motion(bodyA)) + 0x80, axis2);
    NormalizeInPlace(reinterpret_cast<float*>(data + 0x30), false);
    const uint32_t tb = u(Motion(bodyB)) + 0x80;
    orig::fn_004de7e0(u(data + 0x40), tb, axis2);
    NormalizeInPlace(reinterpret_cast<float*>(data + 0x40), false);
    return tb;
}

static uint32_t fn_0045a1e0_Abi(uint32_t esi, uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return fn_0045a1e0(B(esi), B(edi), B(a0), a1, a2, a3);
}
ABI_ENTRY_0045a1e0(fn_0045a1e0_Entry, fn_0045a1e0_Abi)
HOOK(0x0045a1e0, "fn_0045a1e0", fn_0045a1e0_Entry, nullptr);

// 0x00487a90 (ecx = object; plain ret). Virtual slot 0x2f (+0xbc) of the ClothObject vtable 0x005aca40 (slot 1
// 0x00461e10 -> "CLASS(ClothObject)"); no direct callers. With a driver object at +0x594 (guess: the animated actor):
// when its virtual +0xd8 returns false, the object's virtual +0xdc and return. Otherwise driver virtual +0x3c; then
//  - not ragdolled (+0x421 == 0): for each of the +0x590 records (0x4c bytes at +0x460: +0 track key, +4 flags,
//    +8 matrix) whose bone exists (0x004586a0 edx = key, edi = object) and flag 2 is set, the record matrix becomes the
//    inverse (0x004e2b40 eax = pose, ecx = record +8) of the track pose (0x00453c50 (object, key, &pose, 0, 0, 0, 1, 0,
//    0)); then the object transform = record 0 matrix (+0x468) * the driver's pose of key +0x460 (0x004e2910),
//    Object_SetTransform 0x00457990 (al = 0), Object_SetRagdoll 0x00459000 (al = 1, esi = object; 1, 1, 0),
//    Body_addConstraints 0x00456570, Object_SetRagdoll (al = 1; 1, 0, 0), animation slots 0..3 (0x00408800) +0x1c |=
//    0x10, +0x424 |= 0x8000;
//  - ragdolled: Object_SetRagdoll (al = 1; 0, 0, 0).
// Then, when ragdolled, for each record with a bone: flag 1 -> 0x004aff70 (edi = bone; 1) (keyframed), cleared; flags 4
// / 8 -> virtual +0xf8(0x80 / 0x100, bone) unless 0x004592c0 (eax = bone; object) already returns that filter value,
// cleared; flag 2 -> pose = record +8 * driver pose of the key (0x00453c50, 0x004e2910), position / rotation
// (Matrix_ToPosQuat 0x00457ab0), time step min(1 / (Game+0x4c34 (int) * Game+0x4c30), +0x598), keyframed move of the
// bone (0x004df0d0 ecx = position; &rotation, step, bone). The al passed in eax keeps the upper bytes of the previous
// call's eax, as the original. Returns the last call's eax. Executed in recorded runs.
uint32_t fn_00487a90(uint8_t* obj)
{
    Frame<0xd0> f;
    uint8_t* driver = Field<uint8_t*>(obj, 0x594);
    if (!driver)
        return 0;
    if (!static_cast<uint8_t>(VCall<uint32_t>(driver, 0xd8 / 4)))
        return VCall<uint32_t>(obj, 0xdc / 4);
    uint32_t r = VCall<uint32_t>(Field<uint8_t*>(obj, 0x594), 0x3c / 4);
    if (!obj[0x421])
    {
        f.L(0x10) = 0;
        uint8_t* rec = obj + 0x460;
        for (uint32_t i = 0; i < Field<uint32_t>(obj, 0x590); ++i, rec += 0x4c)
        {
            if (!orig::fn_004586a0(Field<uint32_t>(rec, 0), u(obj)))
                continue;
            if (!(rec[4] & 2))
                continue;
            orig::fn_00453c50(u(obj), Field<uint32_t>(rec, 0), f.A(0x40), 0, 0, 0, 1, 0, 0);
            orig::fn_004e2b40(f.A(0x40), u(rec + 8));
        }
        orig::fn_00453c50(Field<uint32_t>(obj, 0x594), Field<uint32_t>(obj, 0x460), f.A(0x40), 0, 0, 0, 1, 0, 0);
        orig::fn_004e2910(u(obj + 0x468), f.A(0x88), f.A(0x40));
        r = orig::fn_00457990(f.A(0x88) & 0xffffff00u, u(obj), f.A(0x88));
        orig::fn_00459000((r & 0xffffff00u) | 1, u(obj), 1, 1, 0);
        r = orig::fn_00456570(u(obj));
        orig::fn_00459000((r & 0xffffff00u) | 1, u(obj), 1, 0, 0);
        for (uint32_t slot = 0; slot < 4; ++slot)
        {
            const uint32_t entry = orig::fn_00408800(slot, u(obj));
            Field<uint32_t>(B(entry), 0x1c) |= 0x10;
        }
        Field<uint32_t>(obj, 0x424) |= 0x8000;
    }
    else
    {
        r = orig::fn_00459000((r & 0xffffff00u) | 1, u(obj), 0, 0, 0);
    }
    if (!obj[0x421])
        return r;
    f.L(0x14) = 0;
    if (!Field<uint32_t>(obj, 0x590))
        return r;
    uint8_t* flags = obj + 0x464;
    f.L(0x18) = u(flags);
    do
    {
        const uint32_t bone = orig::fn_004586a0(Field<uint32_t>(flags - 4, 0), u(obj));
        f.L(0x1c) = bone;
        if (bone)
        {
            if (flags[0] & 1)
            {
                orig::fn_004aff70(bone, 1);
                Field<uint32_t>(flags, 0) &= 0xfffffffeu;
            }
            if (flags[0] & 4)
            {
                if (orig::fn_004592c0(bone, u(obj)) != 0x80)
                    VCall<uint32_t>(obj, 0xf8 / 4, 0x80u, bone);
                Field<uint32_t>(flags, 0) &= 0xfffffffbu;
            }
            if (flags[0] & 8)
            {
                if (orig::fn_004592c0(bone, u(obj)) != 0x100)
                    VCall<uint32_t>(obj, 0xf8 / 4, 0x100u, bone);
                Field<uint32_t>(flags, 0) &= 0xfffffff7u;
            }
            if (flags[0] & 2)
            {
                orig::fn_00453c50(Field<uint32_t>(obj, 0x594), Field<uint32_t>(flags - 4, 0), f.A(0x88), 0, 0, 0, 1, 0,
                                  0);
                orig::fn_004e2910(u(flags + 4), f.A(0x40), f.A(0x88));
                orig::fn_00457ab0(f.A(0x40), f.A(0x30), f.A(0x20));
                const float t = K(0x005bcb2c) / (static_cast<float>(Field<int32_t>(g_game, 0x4c34)) *
                                                 Field<float>(g_game, 0x4c30));
                f.F(0x10) = t;
                if (t > Field<float>(obj, 0x598))
                    f.L(0x10) = Field<uint32_t>(obj, 0x598);
                r = orig::fn_004df0d0(f.A(0x20), f.A(0x30), f.L(0x10), f.L(0x1c));
                flags = B(f.L(0x18));
            }
        }
        ++f.L(0x14);
        flags += 0x4c;
        f.L(0x18) = u(flags);
    } while (f.L(0x14) < Field<uint32_t>(obj, 0x590));
    return r;
}

static uint32_t fn_00487a90_Abi(uint32_t ecx)
{
    return fn_00487a90(B(ecx));
}
ABI_ENTRY_00487a90(fn_00487a90_Entry, fn_00487a90_Abi)
HOOK(0x00487a90, "fn_00487a90", fn_00487a90_Entry, nullptr);

namespace
{
// The inlined reference drop of 0x0045da90 on the object held at obj +0x46c (reference count +0x28): at zero, unless
// its virtual +0xd8 returns true, 0x00487da0 (eax = it); the slot is cleared.
inline void DropHeldReference(uint8_t* obj)
{
    uint8_t* held = Field<uint8_t*>(obj, 0x46c);
    if (held)
    {
        if (--Field<int32_t>(held, 0x28) == 0)
        {
            if (!static_cast<uint8_t>(VCall<uint32_t>(held, 0xd8 / 4)))
                orig::fn_00487da0(u(held));
        }
        Field<uint32_t>(obj, 0x46c) = 0;
    }
}
}  // namespace

// 0x0045da90 (stack: state; ret 4; caller 0x00402e80): per-frame object physics bookkeeping. Camera_Update 0x0040e350
// (Game+0xa68, dt [0x00604a90]) unless Game+0x2684 is set and Game+0xdcb clear; the object list is state +0xe74 when
// Game+0x2684 is set, else state +0xe70. Then (list iterators 0x004e7340 eax = list, esi = iterator / 0x004e73c0 edi =
// iterator):
//  1. list (Game+0xa84)+0xe80: for objects whose body (+0x1c4) has an active island (island +0x29), the contact walk
//     0x00454b20 (ecx = object; &other, &point, &value, 0): on a hit the held object +0x46c is replaced by the contact
//     object (reference +0x28), +0x470..+0x478 = point, +0x468 = value, +0x464 = 0.5; otherwise +0x464 -= dt and at
//     <= 0 the held object is dropped and +0x468 = 0;
//  2. the object list: virtual +0x3c; for live (0x00454780) objects with a body and without +0x424 flag 0x20000 whose
//     position (virtual +0x54) is outside x, y in [-4900, 4900] or z in [-1000, 1900], and whose body (0x004574b0 eax
//     = -1) is in a world: virtual +0xd4(message {0x8000, 12 zero dwords}); when virtual +0xd8 then returns true,
//     physics is enabled (0x004577b0 eax = object; 1) and the linear velocity zeroed (0x00458510);
//  3. list (Game+0xa84)+0xf20: each object's body is added to the world (0x004aba90) unless +0x424 has 0x420000, and
//     the object is unlinked from that list (0x00454750);
//  4. the object list: objects with +0x424 flag 0x20 that pass 0x00454bc0 (ecx = object) or 0x0044e600 go through
//     0x004594b0 (eax = (Game+0xa84)+0xf2c, edi = object), the others through 0x00459630 (esi = object; same list).
// Finally 0x0045da20 (ebx = state); the iterator's node reference (+0x18) is dropped. Returns that node (or 0).
// Executed in recorded runs.
uint32_t fn_0045da90(uint8_t* state)
{
    Frame<0x70> f;
    f.L(0x1c) = 0;
    f.L(0x20) = 0;
    const uint32_t iterator = f.A(0x1c);
    if (Field<uint32_t>(g_game, 0x2684))
    {
        if (Field<uint8_t>(g_game, 0xdcb))
            orig::fn_0040e350(Field<uint32_t>(g_game, 0xa68), GameGlobal<uint32_t>(0x00604a90));
        f.L(0x10) = Field<uint32_t>(state, 0xe74);
    }
    else
    {
        orig::fn_0040e350(Field<uint32_t>(g_game, 0xa68), GameGlobal<uint32_t>(0x00604a90));
        f.L(0x10) = Field<uint32_t>(state, 0xe70);
    }
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe80), iterator)); obj;
         obj = B(orig::fn_004e73c0(iterator)))
    {
        uint8_t* entity = Field<uint8_t*>(obj, 0x1c4);
        if (!entity)
            continue;
        uint8_t* island = Field<uint8_t*>(entity, 0x10);
        if (!island || !island[0x29])
            continue;
        if (static_cast<uint8_t>(orig::fn_00454b20(u(obj), f.A(0x14), f.A(0x24), f.A(0x18), 0)))
        {
            DropHeldReference(obj);
            uint8_t* other = B(f.L(0x14));
            if (other)
            {
                Field<uint8_t*>(obj, 0x46c) = other;
                ++Field<int32_t>(other, 0x28);
            }
            Field<uint32_t>(obj, 0x470) = f.L(0x24);
            Field<uint32_t>(obj, 0x474) = f.L(0x28);
            Field<uint32_t>(obj, 0x478) = f.L(0x2c);
            Field<uint32_t>(obj, 0x468) = f.L(0x18);
            Field<uint32_t>(obj, 0x464) = 0x3f000000u;
        }
        else
        {
            const float left = Field<float>(obj, 0x464) - GameGlobal<float>(0x00604a90);
            Field<float>(obj, 0x464) = left;
            if (left <= K(0x0056c05c))
            {
                DropHeldReference(obj);
                Field<uint32_t>(obj, 0x468) = 0;
            }
        }
    }
    for (uint8_t* obj = B(orig::fn_004e7340(f.L(0x10), iterator)); obj; obj = B(orig::fn_004e73c0(iterator)))
    {
        VCall<uint32_t>(obj, 0x3c / 4);
        if (!Field<uint32_t>(obj, 0x1c4))
            continue;
        if (static_cast<uint8_t>(orig::fn_00454780(u(obj))))
            continue;
        if (Field<uint32_t>(obj, 0x424) & 0x20000)
            continue;
        VCall<uint32_t>(obj, 0x54 / 4, f.A(0x24));
        const float x = f.F(0x24);
        const float y = f.F(0x28);
        const float z = f.F(0x2c);
        const bool outside = x < K(0x005bceec) || x > K(0x005bcee8) || y < K(0x005bceec) || y > K(0x005bcee8) ||
                             z < K(0x005bce70) || z > K(0x005bcee4);
        if (!outside)
            continue;
        orig::fn_004574b0(0xffffffffu, u(obj), f.A(0x18));
        uint8_t* body = B(f.L(0x18));
        if (!body || !Field<uint32_t>(body, 0x44))
            continue;
        for (uint32_t o = 0x3c; o < 0x70; o += 4)
            f.L(o) = 0;
        f.L(0x3c) = 0x8000;
        VCall<uint32_t>(obj, 0xd4 / 4, f.A(0x3c));
        if (!static_cast<uint8_t>(VCall<uint32_t>(obj, 0xd8 / 4)))
            continue;
        orig::fn_004577b0(u(obj), 1);
        f.L(0x30) = 0;
        f.L(0x34) = 0;
        f.L(0x38) = 0;
        orig::fn_00458510(f.A(0x30), u(obj));
    }
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xf20), iterator)); obj;
         obj = B(orig::fn_004e73c0(iterator)))
    {
        if (!(Field<uint32_t>(obj, 0x424) & 0x420000))
            orig::fn_004aba90(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa8c), 8), Field<uint32_t>(obj, 0x1c4));
        orig::fn_00454750(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xf20), u(obj));
    }
    for (uint8_t* obj = B(orig::fn_004e7340(f.L(0x10), iterator)); obj; obj = B(orig::fn_004e73c0(iterator)))
    {
        const uint32_t list = u(Field<uint8_t*>(g_game, 0xa84)) + 0xf2c;
        if ((obj[0x424] & 0x20) &&
            (static_cast<uint8_t>(orig::fn_00454bc0(u(obj))) || static_cast<uint8_t>(orig::fn_0044e600())))
            orig::fn_004594b0(list, u(obj));
        else
            orig::fn_00459630(u(obj), list);
    }
    orig::fn_0045da20(u(state));
    const uint32_t node = f.L(0x20);
    if (node)
        --Field<int32_t>(B(node), 0x18);
    return node;
}

static uint32_t fn_0045da90_Abi(uint32_t a0)
{
    return fn_0045da90(B(a0));
}
ABI_ENTRY_0045da90(fn_0045da90_Entry, fn_0045da90_Abi)
HOOK(0x0045da90, "fn_0045da90", fn_0045da90_Entry, nullptr);

namespace
{
// The inlined quaternion rotation of 0x00459bb0 (q = x, y, z, w at motion +0x30): s = 2 w^2 - 1, d = (v2 qz + v1 qy)
// + v0 qx, c = (v2 qy - v1 qz, v0 qz - v2 qx, v1 qx - v0 qy); out_i = c_i k + (2d q_i + s v_i) for i < 3 and out_3 =
// k * 0 + (2d w + s v3), with k = w * -2 (rotation by the conjugate, into the body frame) or k = 2w (into the world).
inline void RotateByQuaternion(const float* q, const float* v, bool conjugate, float* out)
{
    float s = q[3] * q[3];
    s = s + s;
    s = s - K(0x005bcb2c);
    float d = (v[2] * q[2] + v[1] * q[1]) + v[0] * q[0];
    d = d + d;
    float k;
    if (conjugate)
        k = q[3] * K(0x005bcc40);
    else
        k = q[3] + q[3];
    const float c0 = v[2] * q[1] - v[1] * q[2];
    const float c1 = v[0] * q[2] - v[2] * q[0];
    const float c2 = v[1] * q[0] - v[0] * q[1];
    const float r0 = d * q[0] + s * v[0];
    const float r1 = d * q[1] + s * v[1];
    const float r2 = d * q[2] + s * v[2];
    const float r3 = d * q[3] + s * v[3];
    out[0] = c0 * k + r0;
    out[1] = c1 * k + r1;
    out[2] = c2 * k + r2;
    out[3] = k * K(0x0056c05c) + r3;
}
}  // namespace

// 0x00459bb0 (eax = axis (4 floats), ecx = body A, edx = body B, esi = constraint data; stack: pivot; ret 4; caller
// 0x00456570 "RigidBodiesAddConstraints"): +0 = A, +4 = B; the axis normalised (1/sqrt, 0 for a zero axis) and rotated
// into A's frame (+0x30) and B's frame (+0x40) by the conjugates of the motion rotations (motion +0x30); the pivot into
// A's / B's frame (0x004de720 into +0x10 / +0x20; the original pushes the pivot for the first call early); a
// perpendicular +0x50 from A's axis (the smallest |component| index gets 0, the other two (i < j) get axis[j] and
// -axis[i]), normalised (its +0x5c is scaled, never set here); +0x60 = (+0x34 +0x58 - +0x54 +0x38, +0x50 +0x38 -
// +0x30 +0x58, +0x30 +0x54 - +0x34 +0x50, 0); +0x70 = +0x60 rotated into the world by A and back into B's frame
// (guess: a limited-hinge constraint set in world space). Returns B's rotation pointer (motion +0x30). Not executed in
// recorded runs.
uint32_t fn_00459bb0(const float* axis, uint8_t* bodyA, uint8_t* bodyB, uint8_t* data, uint32_t pivot)
{
    Field<uint8_t*>(data, 0) = bodyA;
    Field<uint8_t*>(data, 4) = bodyB;
    float n[4] = {axis[0], axis[1], axis[2], axis[3]};
    const float len2 = (n[1] * n[1] + n[2] * n[2]) + n[0] * n[0];
    const float inv = (len2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(len2);
    n[0] = n[0] * inv;
    n[1] = n[1] * inv;
    n[2] = n[2] * inv;
    n[3] = n[3] * inv;
    float* out = reinterpret_cast<float*>(data);
    RotateByQuaternion(reinterpret_cast<const float*>(Motion(bodyA) + 0x30), n, true, out + 0xc);
    RotateByQuaternion(reinterpret_cast<const float*>(Motion(bodyB) + 0x30), n, true, out + 0x10);
    orig::fn_004de720(u(data + 0x10), u(Motion(bodyA)) + 0x80, pivot);
    orig::fn_004de720(u(data + 0x20), u(Motion(bodyB)) + 0x80, pivot);
    // Perpendicular to A's axis.
    const float a0 = std::fabs(out[0xc]);
    const float a1 = std::fabs(out[0xd]);
    const float a2 = std::fabs(out[0xe]);
    uint32_t minIndex = 0;
    uint32_t i = 1;
    uint32_t j = 2;
    float smallest = a0;
    if (a1 < a0)
    {
        i = 0;
        smallest = a1;
        minIndex = 1;
    }
    if (a2 < smallest)
    {
        j = minIndex;
        minIndex = 2;
    }
    Field<uint32_t>(data, 0x50 + minIndex * 4) = 0;
    Field<uint32_t>(data, 0x50 + i * 4) = Field<uint32_t>(data, 0x30 + j * 4);
    Field<float>(data, 0x50 + j * 4) = -Field<float>(data, 0x30 + i * 4);
    {
        float* p = out + 0x14;
        const float l2 = (p[0] * p[0] + p[1] * p[1]) + p[2] * p[2];
        const float pinv = (l2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(l2);
        p[0] = pinv * p[0];
        p[1] = pinv * p[1];
        p[2] = pinv * p[2];
        p[3] = pinv * p[3];
    }
    const float x = out[0x14] * out[0xe] - out[0xc] * out[0x16];
    const float y = out[0xc] * out[0x15] - out[0xd] * out[0x14];
    const float z = out[0xd] * out[0x16] - out[0x15] * out[0xe];
    Field<uint32_t>(data, 0x6c) = 0;
    out[0x18] = z;
    out[0x19] = x;
    out[0x1a] = y;
    float world[4];
    RotateByQuaternion(reinterpret_cast<const float*>(Motion(bodyA) + 0x30), out + 0x18, false, world);
    const uint32_t qb = u(Motion(bodyB)) + 0x30;
    RotateByQuaternion(reinterpret_cast<const float*>(qb), world, true, out + 0x1c);
    return qb;
}

static uint32_t fn_00459bb0_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0)
{
    return fn_00459bb0(reinterpret_cast<const float*>(eax), B(ecx), B(edx), B(esi), a0);
}
ABI_ENTRY_00459bb0(fn_00459bb0_Entry, fn_00459bb0_Abi)
HOOK(0x00459bb0, "fn_00459bb0", fn_00459bb0_Entry, nullptr);

namespace
{
// One foot sphere of 0x00475220: an OffsetSphereShape (hkMemory 0x30 bytes, class 0x1d, u16 +4 = 0x30) of radius
// +0x45c, radius then set to +0x45c * cinfo +8, +8 = -1. When the object's virtual +0x48(0) agrees, it is wrapped by
// 0x004563d0 (ecx = sphere, esi = new 0x28-byte block) into the object slot, the sphere's reference dropped, and the
// wrapper gets +8 = -1, byte +0x20 = 1, +0x14 = +0x45c + 0.3, +0x10 = 1e-4, +0x24 = 1.0. The result (wrapper or
// sphere) is wrapped in a transform shape (0x00512ab0, 0x50 bytes; 0 when the allocation fails), whose +8 = -1 (also
// written when the allocation failed, as the original) after the child's reference is dropped.
inline uint8_t* PlayerSphere(uint8_t* obj, uint8_t* cinfo, uint32_t slot)
{
    uint8_t* mem = HkAlloc(0x30, 0x1d);
    Field<uint16_t>(mem, 4) = 0x30;
    uint8_t* shape = B(OffsetSphereShape_Construct(mem, Field<uint32_t>(obj, 0x45c)));
    OffsetSphereShape_SetRadius(shape, fb(Field<float>(obj, 0x45c) * Field<float>(cinfo, 8)));
    Field<int32_t>(shape, 8) = -1;
    if (slot == 0x460)
        Field<uint32_t>(obj, 0x460) = 0;
    if (static_cast<uint8_t>(VCall<uint32_t>(obj, 0x48 / 4, 0u)))
    {
        uint8_t* block = HkAlloc(0x28, 0x1d);
        Field<uint16_t>(block, 4) = 0x28;
        Field<uint32_t>(obj, slot) = orig::fn_004563d0(u(shape), u(block));
        HkRelease(shape, 0);
        uint8_t* wrapper = Field<uint8_t*>(obj, slot);
        Field<int32_t>(wrapper, 8) = -1;
        wrapper[0x20] = 1;
        Field<float>(wrapper, 0x14) = Field<float>(obj, 0x45c) + K(0x005bccd4);
        Field<uint32_t>(wrapper, 0x10) = 0x38d1b717u;
        Field<uint32_t>(wrapper, 0x24) = 0x3f800000u;
        shape = wrapper;
    }
    uint8_t* transformShape = HkAlloc(0x50, 0x1d);
    if (transformShape)
        transformShape = B(orig::fn_00512ab0(u(transformShape), u(shape)));
    HkRelease(shape, 0);
    Field<int32_t>(transformShape, 8) = -1;
    return transformShape;
}
}  // namespace

// 0x00475220 (ecx = object; stack: body cinfo; ret 4). PlayerObject (vtable 0x005aa888, slot 1 0x00461d20 ->
// "CLASS(PlayerObject)") slot 7 (+0x1c); no direct callers. Builds the player's collision: 0x00457810 (eax = object),
// cinfo +8 = 4.0, Object_BuildRadiusCollision 0x004527d0 (ecx = object; cinfo), cinfo +0x7b = 1, +0x460 = +0x464 = 0,
// (+0x1cc)+0x10 = 1e-4, virtual +0x90(&point, &value, 0) (guess: the collision extent); two spheres (PlayerSphere,
// slots +0x460 and +0x464) placed by an identity rotation (0x004de840 from quaternion (0, 0, 0, 1)) at (point x,
// point y, (point z + radius + value) * 0.5) and (point x, point y, point z + value) (Matrix_Copy into the transform
// shape +0x10); a list shape (0x00511350, 0x28 bytes; {cinfo +0x74, sphere 1, sphere 2}, 3) replaces cinfo +0x74 after
// the three references are dropped; cinfo +0 = 0x004f0001; with +0x1bc set, 0x00458600 (esi = object). Returns that
// call's eax, else 0. Executed in recorded runs.
uint32_t PlayerObject_BuildCollision(uint8_t* obj, uint8_t* cinfo)
{
    Frame<0x90> f;
    orig::fn_00457810(u(obj));
    Field<uint32_t>(cinfo, 8) = 0x40800000u;
    orig::fn_004527d0(u(obj), u(cinfo));
    cinfo[0x7b] = 1;
    Field<uint32_t>(obj, 0x464) = 0;
    Field<uint32_t>(obj, 0x460) = 0;
    Field<uint32_t>(Field<uint8_t*>(obj, 0x1cc), 0x10) = 0x38d1b717u;
    f.L(0x40) = 0;
    f.L(0x44) = 0;
    f.L(0x48) = 0;
    f.L(0x4c) = 0x3f800000u;
    VCall<uint32_t>(obj, 0x90 / 4, f.A(0x18), f.A(0x14), 0u);
    for (int k = 0; k < 2; ++k)
    {
        uint8_t* transformShape = PlayerSphere(obj, cinfo, k == 0 ? 0x460u : 0x464u);
        if (k == 0)
        {
            f.L(0x10) = u(transformShape);
            f.F(0x38) = ((f.F(0x20) + Field<float>(obj, 0x45c)) + f.F(0x14)) * K(0x005bcb4c);
        }
        else
        {
            f.F(0x38) = f.F(0x20) + f.F(0x14);
        }
        f.L(0x30) = f.L(0x18);
        f.L(0x34) = f.L(0x1c);
        orig::fn_004de840(f.A(0x40), f.A(0x50));
        f.L(0x80) = f.L(0x30);
        f.L(0x88) = f.L(0x38);
        f.L(0x84) = f.L(0x34);
        f.L(0x8c) = 0;
        orig::fn_00414240(u(transformShape) + 0x10, f.A(0x50));
        if (k == 1)
            f.L(0x2c) = u(transformShape);
    }
    f.L(0x24) = Field<uint32_t>(cinfo, 0x74);
    f.L(0x28) = f.L(0x10);
    uint8_t* list = HkAlloc(0x28, 0x1d);
    Field<uint16_t>(list, 4) = 0x28;
    list = B(orig::fn_00511350(u(list), f.A(0x24), 3));
    Field<int32_t>(list, 8) = -1;
    for (uint32_t i = 0; i < 3; ++i)
        HkRelease(B(f.L(0x24 + 4 * i)), 0);
    Field<uint8_t*>(cinfo, 0x74) = list;
    Field<uint32_t>(cinfo, 0) = 0x004f0001u;
    if (!Field<uint32_t>(obj, 0x1bc))
        return 0;
    return orig::fn_00458600(u(obj));
}

static uint32_t PlayerObject_BuildCollision_Abi(uint32_t ecx, uint32_t a0)
{
    return PlayerObject_BuildCollision(B(ecx), B(a0));
}
ABI_ENTRY_00475220(PlayerObject_BuildCollision_Entry, PlayerObject_BuildCollision_Abi)
HOOK(0x00475220, "PlayerObject_BuildCollision", PlayerObject_BuildCollision_Entry, nullptr);

namespace
{
// The C runtime pow intrinsic 0x00566790 (st1 = base, st0 = exponent; result in st0).
inline float X87Pow(float base, float exponent)
{
    static const uint32_t target = 0x00566790;
    float r;
    __asm {
        fld base
        fld exponent
        call dword ptr [target]
        fstp r
    }
    return r;
}
}  // namespace

// 0x00478200 (eax = player; stack: dt; ret 4; caller 0x004788a0): the player's physics bookkeeping per step (guess
// from the fields). When virtual +0x48(0) agrees, the body (+0x1cc) and the foot shapes (+0x460 / +0x464) get +0x18 =
// Game+0x4c30. History: +0x524 = +0x530, +0x530 = +0x4c0 (3 dwords each); +0x53c = position (virtual +0x54) - +0x500,
// then +0x500 = position; +0x4c0 = linear velocity (0x00458000), +0x4e8 = angular velocity (0x00457fc0); +0x4b8 /
// +0x4bc = |v| / |v.xy|, +0x4e4 = |w|. Friction blend: f = 1, and while +0x570 > 0, f = min(+0x4bc / +0x468, 1)
// (unless +0x468 == 0), v = pow(1 - f, +0x5ac) (C runtime 0x00566790), else v = 0; +0x5a0 = (1 - v) +0x5a8 + v +0x5a4.
// +0x584 = 0, [0x0063a178] = player, +0x570 -= dt (clamped at 0). Contacts: +0x574 = (0, 0, -1), and when the body is
// in a world the contact walk 0x004ae6c0 (eax = body; callback 0x004780f0) refills +0x574 / +0x584; +0xa34 &= ~0x1000.
// With contacts (+0x584 > 0) and +0x57c > -0.1: grounded (+0x570 = 0.5, +0xa34 |= 0x1000) when +0x57c > 0.7, or when
// ((+0x4c8 - +0x538) / dt)^2 < 200 and +0x4c8 > -5; without contacts the previous normal is restored. Not grounded:
// +0x56c = min(+0x56c, +0x4c8). Grounded with flag 0x200, or flag 0x80 without 0x40, and without 0x20: unless flag
// 0x100, landing ("Fx_fs_roman_land" 0x00478800, cue 0x00493b70 (ebx = 2, edi = 0x005fb9cc; 0x0064a72c, 0, player,
// 1, 0, 0), fall damage 0x00478100 (bl = 0; +0x56c), +0x550 = +0x4bc, +0xa34 |= 0x100, +0x640 = +0x644), then +0x56c =
// 0. Wing sounds off (0x0047bb30 (eax = +0xabc, edi = +0xab8; 0), +0xa34 &= ~0xc) with contacts, grounded, flag 4 and
// no flag 0x20. +0x50c = +0x500 with contacts; +0x518 = +0x500 while +0x570 > 0, or with flag 4 when +0x520 > +0x508.
// When +0x4c8 <= 0: flag 0x20 cleared; unless flag 4, when the fall speed threshold (+0xa58 with flag 2 or +0xabc,
// else +0xa5c with flag 0x80, else +0xa50) > +0x4c8: flags |= 0x600 (unless 0x200), |= 0x10 (with +0xabc and no
// contacts), +0xa60 += dt [0x00604a90]. Otherwise +0xa60 = 0 while +0x570 > 0 and no flag 0x100. Damage 0x00478100 (bl =
// 1; 0) when +0x508 < -1000, when +0x508 - +0x520 < -(+0xbfc) (with +0xabc; else -(+0xbf4)), when +0xa60 > +0xbf8 (with
// +0xabc; else +0xbf0), and when +0xa34 is negative. The upper bytes of ebx hold the old +0x574, as the original.
// Returns eax as left: the last damage call's, else +0xa34. Executed in recorded runs.
uint32_t fn_00478200(uint8_t* p, float dt)
{
    if (static_cast<uint8_t>(VCall<uint32_t>(p, 0x48 / 4, 0u)))
    {
        const uint32_t step = Field<uint32_t>(g_game, 0x4c30);
        Field<uint32_t>(Field<uint8_t*>(p, 0x1cc), 0x18) = step;
        if (Field<uint8_t*>(p, 0x460))
            Field<uint32_t>(Field<uint8_t*>(p, 0x460), 0x18) = Field<uint32_t>(g_game, 0x4c30);
        if (Field<uint8_t*>(p, 0x464))
            Field<uint32_t>(Field<uint8_t*>(p, 0x464), 0x18) = Field<uint32_t>(g_game, 0x4c30);
    }
    for (uint32_t i = 0; i < 0xc; i += 4)
        Field<uint32_t>(p, 0x524 + i) = Field<uint32_t>(p, 0x530 + i);
    for (uint32_t i = 0; i < 0xc; i += 4)
        Field<uint32_t>(p, 0x530 + i) = Field<uint32_t>(p, 0x4c0 + i);
    VCall<uint32_t>(p, 0x54 / 4, u(p + 0x53c));
    Field<float>(p, 0x53c) = Field<float>(p, 0x53c) - Field<float>(p, 0x500);
    Field<float>(p, 0x540) = Field<float>(p, 0x540) - Field<float>(p, 0x504);
    Field<float>(p, 0x544) = Field<float>(p, 0x544) - Field<float>(p, 0x508);
    VCall<uint32_t>(p, 0x54 / 4, u(p + 0x500));
    orig::fn_00458000(u(p), u(p + 0x4c0));
    orig::fn_00457fc0(u(p), u(p + 0x4e8));
    const float* v = reinterpret_cast<const float*>(p + 0x4c0);
    Field<float>(p, 0x4b8) = std::sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]);
    Field<float>(p, 0x4bc) = std::sqrt(v[0] * v[0] + v[1] * v[1]);
    const float* w = reinterpret_cast<const float*>(p + 0x4e8);
    Field<float>(p, 0x4e4) = std::sqrt((w[0] * w[0] + w[1] * w[1]) + w[2] * w[2]);
    float f = K(0x005bcb2c);
    float blend;
    if (Field<float>(p, 0x570) > K(0x0056c05c))
    {
        if (!(Field<float>(p, 0x468) == K(0x0056c05c)))
        {
            f = Field<float>(p, 0x4bc) / Field<float>(p, 0x468);
            if (f > K(0x005bcb2c))
                f = K(0x005bcb2c);
        }
        blend = X87Pow(K(0x005bcb2c) - f, Field<float>(p, 0x5ac));
    }
    else
    {
        blend = K(0x0056c05c);
    }
    Field<uint32_t>(p, 0x584) = 0;
    GameGlobal<uint32_t>(0x0063a178) = u(p);
    Field<float>(p, 0x5a0) = (K(0x005bcb2c) - blend) * Field<float>(p, 0x5a8) + blend * Field<float>(p, 0x5a4);
    const float left = Field<float>(p, 0x570) - dt;
    Field<float>(p, 0x570) = left;
    if (left < K(0x0056c05c))
        Field<uint32_t>(p, 0x570) = 0;
    uint32_t ebx = Field<uint32_t>(p, 0x574);
    const uint32_t old578 = Field<uint32_t>(p, 0x578);
    const uint32_t old57c = Field<uint32_t>(p, 0x57c);
    Field<uint32_t>(p, 0x578) = 0;
    Field<uint32_t>(p, 0x574) = 0;
    Field<uint32_t>(p, 0x57c) = 0xbf800000u;
    uint8_t* body = Field<uint8_t*>(p, 0x1c4);
    if (Field<uint32_t>(body, 0x44))
        orig::fn_004ae6c0(u(body), 0x004780f0u);
    Field<uint32_t>(p, 0xa34) &= 0xffffefffu;
    if (static_cast<float>(Field<int32_t>(p, 0x584)) > K(0x0056c05c) && Field<float>(p, 0x57c) > K(0x005bcd14))
    {
        bool grounded = Field<float>(p, 0x57c) > K(0x005bcd48);
        if (!grounded)
        {
            const float rate = (Field<float>(p, 0x4c8) - Field<float>(p, 0x538)) / dt;
            grounded = rate * rate < K(0x005bcda8) && Field<float>(p, 0x4c8) > K(0x005bce74);
        }
        if (grounded)
        {
            Field<uint32_t>(p, 0x570) = 0x3f000000u;
            Field<uint32_t>(p, 0xa34) |= 0x1000;
        }
    }
    else
    {
        Field<uint32_t>(p, 0x574) = ebx;
        Field<uint32_t>(p, 0x578) = old578;
        Field<uint32_t>(p, 0x57c) = old57c;
    }
    const uint32_t flags = Field<uint32_t>(p, 0xa34);
    if (!(flags & 0x1000))
    {
        if (Field<float>(p, 0x56c) > Field<float>(p, 0x4c8))
            Field<uint32_t>(p, 0x56c) = Field<uint32_t>(p, 0x4c8);
    }
    else
    {
        const uint8_t dl = static_cast<uint8_t>(flags);
        const uint8_t dh = static_cast<uint8_t>(flags >> 8);
        const bool landing = ((dh & 2) || ((dl & 0x80) && !(dl & 0x40))) && !(dl & 0x20);
        if (landing)
        {
            if (!(dh & 1))
            {
                orig::fn_00478800(u(p), 0x0059ee3cu);
                orig::fn_00493b70(0, 2, 0x005fb9ccu, 0x0064a72cu, 0, u(p), 1, 0, 0);
                ebx = 0;
                orig::fn_00478100(ebx, u(p), Field<uint32_t>(p, 0x56c));
                Field<uint32_t>(p, 0x550) = Field<uint32_t>(p, 0x4bc);
                Field<uint32_t>(p, 0xa34) |= 0x100;
                Field<uint32_t>(p, 0x640) = Field<uint32_t>(p, 0x644);
            }
            Field<uint32_t>(p, 0x56c) = 0;
        }
    }
    if (Field<int32_t>(p, 0x584) > 0)
    {
        const uint32_t a = Field<uint32_t>(p, 0xa34);
        if ((a & 0x1000) && (a & 4) && !(a & 0x20))
        {
            orig::fn_0047bb30(Field<uint32_t>(p, 0xabc), u(p), u(p + 0xab8), 0);
            Field<uint32_t>(p, 0xa34) &= 0xfffffff3u;
        }
    }
    if (static_cast<float>(Field<int32_t>(p, 0x584)) > K(0x0056c05c))
    {
        for (uint32_t i = 0; i < 0xc; i += 4)
            Field<uint32_t>(p, 0x50c + i) = Field<uint32_t>(p, 0x500 + i);
    }
    if (Field<float>(p, 0x570) > K(0x0056c05c))
    {
        for (uint32_t i = 0; i < 0xc; i += 4)
            Field<uint32_t>(p, 0x518 + i) = Field<uint32_t>(p, 0x500 + i);
    }
    uint32_t ecx = Field<uint32_t>(p, 0xa34);
    if ((ecx & 4) && Field<float>(p, 0x520) > Field<float>(p, 0x508))
    {
        for (uint32_t i = 0; i < 0xc; i += 4)
            Field<uint32_t>(p, 0x518 + i) = Field<uint32_t>(p, 0x500 + i);
    }
    bool falling = false;
    if (Field<float>(p, 0x4c8) <= K(0x0056c05c))
    {
        ecx &= 0xffffffdfu;
        Field<uint32_t>(p, 0xa34) = ecx;
        float threshold;
        if (ecx & 4)
            threshold = Field<float>(p, 0x4c8);
        else if (!(ecx & 2) && !Field<uint32_t>(p, 0xabc))
            threshold = (ecx & 0x80) ? Field<float>(p, 0xa5c) : Field<float>(p, 0xa50);
        else
            threshold = Field<float>(p, 0xa58);
        if (threshold > Field<float>(p, 0x4c8))
        {
            falling = true;
            if (!(ecx & 0x200))
                Field<uint32_t>(p, 0xa34) |= 0x600;
            if (Field<uint32_t>(p, 0xabc) && !Field<uint32_t>(p, 0x584))
                Field<uint32_t>(p, 0xa34) |= 0x10;
            Field<float>(p, 0xa60) = GameGlobal<float>(0x00604a90) + Field<float>(p, 0xa60);
        }
    }
    if (!falling && Field<float>(p, 0x570) > K(0x0056c05c) && !(ecx & 0x100))
        Field<uint32_t>(p, 0xa60) = 0;
    uint32_t r = 0;
    bool called = false;
    auto damage = [&]() {
        ebx = (ebx & 0xffffff00u) | 1;
        r = orig::fn_00478100(ebx, u(p), 0);
        called = true;
    };
    if (Field<float>(p, 0x508) < K(0x005bce70))
        damage();
    const float drop = Field<float>(p, 0x508) - Field<float>(p, 0x520);
    if (Field<uint32_t>(p, 0xabc))
    {
        if (-Field<float>(p, 0xbfc) > drop)
            damage();
        if (Field<float>(p, 0xa60) > Field<float>(p, 0xbf8))
            damage();
    }
    else
    {
        if (-Field<float>(p, 0xbf4) > drop)
            damage();
        if (Field<float>(p, 0xa60) > Field<float>(p, 0xbf0))
            damage();
    }
    const uint32_t last = Field<uint32_t>(p, 0xa34);
    if (static_cast<int32_t>(last) < 0)
    {
        damage();
        return r;
    }
    return last;
}

static uint32_t fn_00478200_Abi(uint32_t eax, uint32_t a0)
{
    return fn_00478200(B(eax), bf(a0));
}
ABI_ENTRY_00478200(fn_00478200_Entry, fn_00478200_Abi)
HOOK(0x00478200, "fn_00478200", fn_00478200_Entry, nullptr);
