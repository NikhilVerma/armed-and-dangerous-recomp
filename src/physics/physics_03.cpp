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

// 0x004a4600 (eax = dest, ecx = source; plain ret; eax preserved; caller 0x004deb20): copies 3 rows of 4 dwords (a
// 3x4 matrix). Not executed in recorded runs.
void Matrix3_Copy(uint32_t* dest, const uint32_t* source)
{
    for (int row = 0; row < 3; ++row)
    {
        dest[row * 4 + 0] = source[row * 4 + 0];
        dest[row * 4 + 1] = source[row * 4 + 1];
        dest[row * 4 + 2] = source[row * 4 + 2];
        dest[row * 4 + 3] = source[row * 4 + 3];
    }
}

static uint32_t Matrix3_Copy_Abi(uint32_t eax, uint32_t ecx)
{
    Matrix3_Copy(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<const uint32_t*>(ecx));
    return eax;
}
ABI_ENTRY_004a4600(Matrix3_Copy_Entry, Matrix3_Copy_Abi)
HOOK(0x004a4600, "Matrix3_Copy", Matrix3_Copy_Entry, nullptr);

// 0x004a4640 (eax = a, ecx = out, edx = b, esi = c; plain ret; eax preserved; caller 0x004a47a0): out = (b - a) x
// (c - a), out[3] = 0 (the triangle normal, unnormalised). Not executed in recorded runs.
void Triangle_Normal(const float* a, float* out, const float* b, const float* c)
{
    const float d0 = b[0] - a[0];
    const float d1 = b[1] - a[1];
    const float d2 = b[2] - a[2];
    const float e0 = c[0] - a[0];
    const float e1 = c[1] - a[1];
    const float e2 = c[2] - a[2];
    Field<uint32_t>(out, 0xc) = 0;
    out[0] = e2 * d1 - e1 * d2;
    out[1] = d2 * e0 - e2 * d0;
    out[2] = e1 * d0 - e0 * d1;
}

static uint32_t Triangle_Normal_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    Triangle_Normal(reinterpret_cast<const float*>(eax), reinterpret_cast<float*>(ecx),
                    reinterpret_cast<const float*>(edx), reinterpret_cast<const float*>(esi));
    return eax;
}
ABI_ENTRY_004a4640(Triangle_Normal_Entry, Triangle_Normal_Abi)
HOOK(0x004a4640, "Triangle_Normal", Triangle_Normal_Entry, nullptr);

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

// 0x004a46c0 (ecx = out; stack: radius, mass; caller pops; callers 0x00411960, 0x004527d0, 0x004a53a0, 0x004a62f0):
// sphere mass properties. Returns 1 (failure) when mass <= 0 or radius <= 0 (NaN passes); otherwise out +0x20..+0x4c
// = the 3x4 identity of 0x005cad50 with the diagonal set to radius * radius * mass * 0.4, centre of mass +0x10..+0x1c
// = 0, +4 = mass, +0 = pi * radius^3 * 4/3 (volume); returns 0. Same semantics as Havok's
// computeSphereVolumeMassProperties (guess: possibly library code that sits in the havok-ext range).
uint32_t SphereMassProperties(uint8_t* out, float radius, float mass)
{
    if (mass <= K(0x0056c05c))
        return 1;
    if (radius <= K(0x0056c05c))
        return 1;
    float inertia = radius * radius;
    for (int i = 0; i < 12; ++i)
        Field<uint32_t>(out, 0x20 + 4 * i) = GameGlobal<uint32_t>(0x005cad50 + 4 * i);
    inertia = inertia * mass;
    inertia = inertia * K(0x005bcd94);
    Field<float>(out, 0x20) = inertia;
    Field<float>(out, 0x34) = inertia;
    Field<float>(out, 0x48) = inertia;
    Field<uint32_t>(out, 0x1c) = 0;
    Field<uint32_t>(out, 0x18) = 0;
    Field<uint32_t>(out, 0x14) = 0;
    Field<uint32_t>(out, 0x10) = 0;
    Field<float>(out, 4) = mass;
    Field<float>(out, 0) = K(0x0057e864) * radius * radius * radius * K(0x005bcd90);
    return 0;
}

static uint32_t SphereMassProperties_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return SphereMassProperties(B(ecx), bf(a0), bf(a1));
}
ABI_ENTRY_004a46c0(SphereMassProperties_Entry, SphereMassProperties_Abi)
HOOK(0x004a46c0, "SphereMassProperties", SphereMassProperties_Entry, nullptr);

void Inertia_ShiftFromCentre(float* m, const float* c, float mass);

// 0x004a47a0 (eax = P; stack: R, Q, mass, thickness, out; caller pops; caller 0x004a62f0): mass properties of a
// triangle (P, Q, R) with a thickness. Returns 1 when mass <= 0 or thickness < 0 (NaN passes). n = Triangle_Normal
// 0x004a4640 (eax = P, edx = Q, esi = R), |n| kept for the volume. Three cases:
//  - thickness < 1e-5: centre c = ((R + P) + Q) / 3, inertia of three point masses plus the centre weighted by 9,
//    scaled by mass / 12, then shifted to the centre (0x004a6830, eax = inertia, ecx = c; mass);
//  - |n| < 1e-5: c as above, inertia of a point mass at c;
//  - otherwise: a prism of 6 vertices (P, Q, R offset by +/- thickness / 2 along the unit normal, in the order R + h n,
//    R - h n, P + h n, P - h n, Q + h n, Q - h n) and 8 triangles, built as a geometry (0x004b85c0 vertices,
//    0x004b8740 triangles), mass properties by 0x004a7a60 (ebx = geometry, esi = result; mass), geometry destroyed
//    (0x004b84d0 esi), centre and inertia taken from the result; the local arrays are freed only when owned.
// out +4 = mass, +0x10 centre, +0x20..+0x4c inertia (3 rows of 4; the w column is uninitialised stack in the first
// two cases: zero here), +0 = |n| * thickness * 0.5; returns 0. Same shape as Havok's
// computeTriangleSurfaceMassProperties (guess: library code in the havok-ext range). Not executed in recorded runs.
uint32_t TriangleMassProperties(const float* p, const float* r, const float* q, float mass, float thickness,
                                uint8_t* out)
{
    if (mass <= K(0x0056c05c))
        return 1;
    if (thickness < K(0x0056c05c))
        return 1;
    Frame<0x1b0> f;
    Triangle_Normal(p, &f.F(0x10), q, r);
    const float len2 = (f.F(0x10) * f.F(0x10) + f.F(0x18) * f.F(0x18)) + f.F(0x14) * f.F(0x14);
    f.F(0x7c) = std::sqrt(len2);
    if (thickness < K(0x005bcc64))
    {
        for (int i = 0; i < 4; ++i)
            f.F(0x10 + 4 * i) = ((r[i] + p[i]) + q[i]) * K(0x005bcc60);
        const float k9 = K(0x005bcd88);
        const float k12 = K(0x005bccac);
        const float x = (((f.F(0x10) * f.F(0x10) * k9 + q[0] * q[0]) + p[0] * p[0]) + r[0] * r[0]) * mass * k12;
        const float y = (((q[1] * q[1] + p[1] * p[1]) + f.F(0x14) * f.F(0x14) * k9) + r[1] * r[1]) * mass * k12;
        const float z = (((q[2] * q[2] + p[2] * p[2]) + f.F(0x18) * f.F(0x18) * k9) + r[2] * r[2]) * mass * k12;
        f.F(0x40) = z + y;
        f.F(0x54) = z + x;
        f.F(0x68) = y + x;
        float t = -((((q[0] * q[1] + r[0] * r[1]) + f.F(0x14) * f.F(0x10) * k9) + p[0] * p[1]) * mass * k12);
        f.F(0x44) = t;
        f.F(0x50) = t;
        t = -((((r[0] * r[2] + q[0] * q[2]) + f.F(0x18) * f.F(0x10) * k9) + p[0] * p[2]) * mass * k12);
        f.F(0x48) = t;
        f.F(0x60) = t;
        t = -((((p[2] * p[1] + f.F(0x18) * f.F(0x14) * k9) + r[1] * r[2]) + q[2] * q[1]) * mass * k12);
        f.F(0x64) = t;
        f.F(0x58) = t;
        Inertia_ShiftFromCentre(&f.F(0x40), &f.F(0x10), mass);
    }
    else if (f.F(0x7c) < K(0x005bcc64))
    {
        for (int i = 0; i < 4; ++i)
            f.F(0x10 + 4 * i) = ((r[i] + p[i]) + q[i]) * K(0x005bcc60);
        const float c0 = f.F(0x10);
        const float c1 = f.F(0x14);
        const float c2 = f.F(0x18);
        const float s2 = c2 * c2;
        const float s1 = c1 * c1;
        f.F(0x40) = (s2 + s1) * mass;
        const float s0 = c0 * c0;
        f.F(0x54) = (s2 + s0) * mass;
        f.F(0x68) = (s1 + s0) * mass;
        float t = -(c1 * c0 * mass);
        f.F(0x50) = t;
        f.F(0x44) = t;
        t = -(c2 * c0 * mass);
        f.F(0x60) = t;
        f.F(0x48) = t;
        t = -(c2 * c1 * mass);
        f.F(0x58) = t;
        f.F(0x64) = t;
    }
    else
    {
        const float inv = (len2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(len2);
        f.L(0x80) = f.A(0x90);
        f.F(0x10) = f.F(0x10) * inv;
        f.F(0x14) = f.F(0x14) * inv;
        f.F(0x18) = f.F(0x18) * inv;
        const float n3 = inv * f.F(0x1c);
        const float h = thickness * K(0x005bcb4c);
        const float h2 = thickness * K(0x005bcbc8);
        f.L(0x88) = 0x80000006u;
        f.L(0x84) = 6;
        // Vertices: R + h n, R + h2 n, P + h n, P + h2 n, Q + h n, Q + h2 n (4 floats each, w uses n3).
        const float* src[3] = {r, p, q};
        for (int v = 0; v < 3; ++v)
        {
            const uint32_t plus = 0x90 + v * 0x20;
            const uint32_t minus = plus + 0x10;
            for (int i = 0; i < 3; ++i)
                f.F(plus + 4 * i) = f.F(0x10 + 4 * i) * h + src[v][i];
            f.F(plus + 0xc) = n3 * h + src[v][3];
            for (int i = 0; i < 3; ++i)
                f.F(minus + 4 * i) = f.F(0x10 + 4 * i) * h2 + src[v][i];
            f.F(minus + 0xc) = n3 * h2 + src[v][3];
        }
        orig::fn_004143f0(f.A(0x160));
        f.L(0x18) = 0;
        f.L(0x1c) = 0;
        f.L(0x10) = 0;
        f.L(0x14) = 0;
        f.L(0x20) = 0;
        f.L(0x24) = 0;
        f.C(0x28) = 1;
        f.C(0x29) = 1;
        orig::fn_004b85c0(f.A(0x10), f.L(0x80), 6);
        static const uint32_t triangles[24] = {0, 2, 4, 1, 5, 3, 0, 3, 2, 0, 1, 3, 1, 0, 4, 1, 4, 5, 2, 5, 4, 2, 3, 5};
        for (int i = 0; i < 24; ++i)
            f.L(0xfc + 4 * i) = triangles[i];
        f.L(0xf0) = f.A(0xfc);
        f.L(0xf8) = 0x80000008u;
        f.L(0xf4) = 8;
        orig::fn_004b8740(f.A(0x10), f.A(0xfc), 8);
        orig::fn_004a7a60(f.A(0x10), f.A(0x160), fb(mass));
        if (static_cast<int32_t>(f.L(0xf8)) >= 0)
            HkFree(f.L(0xf0), ((f.L(0xf8) & 0x7fffffffu) * 3) << 2, 0x10);
        orig::fn_004b84d0(f.A(0x10));
        for (int i = 0; i < 4; ++i)
            f.L(0x10 + 4 * i) = f.L(0x170 + 4 * i);
        for (int i = 0; i < 12; ++i)
            f.L(0x40 + 4 * i) = f.L(0x180 + 4 * i);
        if (static_cast<int32_t>(f.L(0x88)) >= 0)
            HkFree(f.L(0x80), (f.L(0x88) & 0x7fffffffu) << 4, 0x10);
    }
    const float volume = f.F(0x7c) * thickness * K(0x005bcb4c);
    Field<float>(out, 4) = mass;
    for (int i = 0; i < 12; ++i)
        Field<uint32_t>(out, 0x20 + 4 * i) = f.L(0x40 + 4 * i);
    for (int i = 0; i < 4; ++i)
        Field<uint32_t>(out, 0x10 + 4 * i) = f.L(0x10 + 4 * i);
    Field<float>(out, 0) = volume;
    return 0;
}

static uint32_t TriangleMassProperties_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                           uint32_t a4)
{
    return TriangleMassProperties(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(a0),
                                  reinterpret_cast<const float*>(a1), bf(a2), bf(a3), B(a4));
}
ABI_ENTRY_004a47a0(TriangleMassProperties_Entry, TriangleMassProperties_Abi)
HOOK(0x004a47a0, "TriangleMassProperties", TriangleMassProperties_Entry, nullptr);

// 0x004a5160 (ecx = out, edx = half extents; stack: mass; caller pops; callers 0x00412450, 0x004a62f0): box mass
// properties. Returns 1 when mass <= 0 (NaN passes). Otherwise out +0x20..+0x4c = the 3x4 identity of 0x005cad50 with
// the diagonal (e1^2 + e2^2) k, (e0^2 + e2^2) k, (e0^2 + e1^2) k for k = mass / 3; centre +0x10..+0x1c = 0, +4 = mass,
// +0 = e2 e1 e0 * [0x005bcb48] (volume); returns 0. Executed in recorded runs.
uint32_t BoxMassProperties(uint8_t* out, const float* e, float mass)
{
    if (mass <= K(0x0056c05c))
        return 1;
    const float e0 = e[0];
    const float e1 = e[1];
    const float e2 = e[2];
    const float k = mass * K(0x005bcc60);
    for (int i = 0; i < 12; ++i)
        Field<uint32_t>(out, 0x20 + 4 * i) = GameGlobal<uint32_t>(0x005cad50 + 4 * i);
    const float s2 = e2 * e2;
    const float s1 = e1 * e1;
    Field<float>(out, 0x20) = (s1 + s2) * k;
    const float s0 = e0 * e0;
    Field<float>(out, 0x34) = (s0 + s2) * k;
    Field<float>(out, 0x48) = (s0 + s1) * k;
    Field<uint32_t>(out, 0x1c) = 0;
    Field<uint32_t>(out, 0x18) = 0;
    Field<uint32_t>(out, 0x14) = 0;
    Field<uint32_t>(out, 0x10) = 0;
    Field<float>(out, 4) = mass;
    Field<float>(out, 0) = e2 * e1 * e0 * K(0x005bcb48);
    return 0;
}

static uint32_t BoxMassProperties_Abi(uint32_t ecx, uint32_t edx, uint32_t a0)
{
    return BoxMassProperties(B(ecx), reinterpret_cast<const float*>(edx), bf(a0));
}
ABI_ENTRY_004a5160(BoxMassProperties_Entry, BoxMassProperties_Abi)
HOOK(0x004a5160, "BoxMassProperties", BoxMassProperties_Entry, nullptr);

// 0x004a5250 (stack: points, stride, count, mass, out; caller pops; caller 0x004a62f0): mass properties of the convex
// hull of a point cloud. Returns 1 when mass <= 0 (NaN passes). Copies count points (3 floats each, source advanced by
// stride bytes) into a local hkArray of 16-byte vectors {ptr, size 0 (never updated), capacity 0x80000000} grown with
// hkArray_reserve 0x004aa830 (&array, count, 0x10) when count > 0; builds the hull geometry with 0x004ba7f0 (eax =
// count, edx = vectors; 0, 0x00800000); on failure frees the array (when owned) and returns 1. Otherwise 0x004a7a60
// (ebx = geometry, esi = out; mass), out +4 = mass, frees the geometry's vertex (+0x0c, when byte +0x18) and triangle
// (+0x14, when byte +0x19) buffers through hkMemory virtual +4 and the geometry itself (0x1c bytes, class 6), frees the
// array when owned, returns 0. Executed in recorded runs.
uint32_t ConvexHullMassProperties(const uint8_t* points, int32_t stride, int32_t count, float mass, uint8_t* out)
{
    if (mass <= K(0x0056c05c))
        return 1;
    uint32_t array[3] = {0, 0, 0x80000000u};
    if (count > 0)
    {
        orig::fn_004aa830(u(array), static_cast<uint32_t>(count), 0x10);
        for (int32_t k = 0; k < count; ++k, points += stride)
        {
            uint32_t* d = reinterpret_cast<uint32_t*>(array[0]) + 4 * k;
            const uint32_t* s = reinterpret_cast<const uint32_t*>(points);
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
    const uint32_t ptr = array[0];
    const uint32_t cap = array[2];
    uint8_t* geometry = B(orig::fn_004ba7f0(static_cast<uint32_t>(count), ptr, 0, 0x00800000u));
    if (!geometry)
    {
        if (static_cast<int32_t>(cap) >= 0)
            HkFree(ptr, (cap & 0x7fffffffu) << 4, 0x10);
        return 1;
    }
    orig::fn_004a7a60(u(geometry), u(out), fb(mass));
    Field<float>(out, 4) = mass;
    if (geometry[0x18])
        VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 4 / 4, Field<uint32_t>(geometry, 0xc));
    if (geometry[0x19])
        VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 4 / 4, Field<uint32_t>(geometry, 0x14));
    HkFree(u(geometry), 0x1c, 6);
    if (static_cast<int32_t>(cap) >= 0)
        HkFree(ptr, (cap & 0x7fffffffu) << 4, 0x10);
    return 0;
}

static uint32_t ConvexHullMassProperties_Abi(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return ConvexHullMassProperties(B(a0), static_cast<int32_t>(a1), static_cast<int32_t>(a2), bf(a3), B(a4));
}
ABI_ENTRY_004a5250(ConvexHullMassProperties_Entry, ConvexHullMassProperties_Abi)
HOOK(0x004a5250, "ConvexHullMassProperties", ConvexHullMassProperties_Entry, nullptr);

uint32_t MassEntries_Combine(uint8_t* out, uint32_t* entries);

namespace
{
// x87 helpers for 0x004a53a0: the C runtime's acos intrinsic 0x00566ad0 (argument and result in st0), fsin, fcos.
inline float X87Acos(float x)
{
    static const uint32_t target = 0x00566ad0;
    float r;
    __asm {
        fld x
        call dword ptr [target]
        fstp r
    }
    return r;
}
inline float X87Sin(float angle)
{
    float r;
    __asm {
        fld angle
        fsin
        fstp r
    }
    return r;
}
inline float X87Cos(float angle)
{
    float r;
    __asm {
        fld angle
        fcos
        fstp r
    }
    return r;
}
// Appends the 0x90-byte mass entry {mass properties (0x50) at entry, transform (0x40) at transform} to the hkArray
// {ptr, size, capacity} the way 0x004a53a0 does after its first entry: grows (size ? size * 2 : 1) through
// hkArray_reserve 0x004aa830 when size == capacity & 0x7fffffff, then MassProperties_Copy 0x00413770 (eax = slot,
// ecx = entry) and Matrix_Copy 0x00414240 (ecx = slot + 0x50; transform).
inline void AppendMassEntry(uint32_t* array, uint32_t entry, uint32_t transform)
{
    if (array[1] == (array[2] & 0x7fffffffu))
        orig::fn_004aa830(u(array), array[1] ? array[1] * 2 : 1, 0x90);
    const uint32_t slot = array[0] + array[1] * 0x90;
    ++array[1];
    orig::fn_00413770(slot, entry);
    orig::fn_00414240(slot + 0x50, transform);
}
}  // namespace

// 0x004a53a0 (eax = A, ecx = B; stack: radius, mass, out; caller pops; caller 0x004a62f0): mass properties of a
// capsule from A to B. Returns 1 when mass <= 0 or radius <= 0 (NaN passes). Frame offsets are the original's esp
// offsets after its register pushes. Steps:
//  - axis d = A - B, len = |d|. When len > 0 and the unit axis u has |u . (0, 0, 1)| (computed as u2 + u1 * 0 + u0 * 0)
//    < 0.99999, the rotation taking z to u: axis ((u2 * 0 - u1), (u0 - u2 * 0), (u1 * 0 - u0 * 0)) normalised,
//    angle acos(clamped dot) (C runtime 0x00566ad0), quaternion (axis sin(angle / 2), cos(angle / 2)) turned into the
//    frame rotation +0x130 by 0x004de840; otherwise the identity of 0x005cad50. Translation +0x160 = (A + B) / 2.
//  - volumes: sphere Vs = pi r^3 * 4/3, cylinder Vc = pi len r^2; masses ms = Vs / (Vc + Vs) * mass, mc likewise.
//  - entry 1, cylinder: inertia diag ((len^2 / 4) / 3 + r^2 / 4, same, r^2 / 2) scaled by mc (0x004deda0), volume Vc,
//    mass mc, centre 0; array reserved for 1 (hkArray_reserve 0x004aa830) and appended.
//  - entries 2 and 3, hemispheres at +/- len / 2 along the axis (0x004de780 rotates (0, 0, +/-len/2) into the
//    transform's translation): sphere mass properties 0x004a46c0 (radius, ms), inertia halved (0x004deda0, 0.5),
//    centre (0, 0, +/-0.375 r), shifted by 0x004a6830 (ms / 2), volume Vs / 2, mass ms / 2; appended.
//  - 0x004a5ed0 (esi = out; &array) combines them; the array is freed when owned. Returns 0.
// Not executed in recorded runs. Same shape as Havok's computeCapsuleVolumeMassProperties (guess: library code).
uint32_t CapsuleMassProperties(const float* a, const float* b, float radius, float mass, uint8_t* out)
{
    if (mass <= K(0x0056c05c))
        return 1;
    if (radius <= K(0x0056c05c))
        return 1;
    Frame<0x170> f;
    const float d0 = a[0] - b[0];
    f.F(0x14) = a[1] - b[1];
    const float d2 = a[2] - b[2];
    const float len2 = (d0 * d0 + d2 * d2) + f.F(0x14) * f.F(0x14);
    f.F(0x24) = std::sqrt(len2);
    bool rotated = false;
    if (f.F(0x24) > K(0x0056c05c))
    {
        const float inv = (len2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(len2);
        f.F(0x10) = inv * d0;
        f.F(0x14) = f.F(0x14) * inv;
        f.F(0x18) = d2 * inv;
        f.F(0x2c) = f.F(0x10) * K(0x0056c05c);
        f.F(0x48) = f.F(0x14) * K(0x0056c05c);
        const float t = (f.F(0x18) + f.F(0x48)) + f.F(0x2c);
        if (std::fabs(t) < K(0x005bcdcc))
        {
            const float z0 = f.F(0x18) * K(0x0056c05c);
            f.F(0x30) = z0 - f.F(0x14);
            const float c1 = f.F(0x10) - z0;
            const float c2 = f.F(0x48) - f.F(0x2c);
            const float n2 = (c2 * c2 + c1 * c1) + f.F(0x30) * f.F(0x30);
            const float inv2 = (n2 == K(0x0056c05c)) ? K(0x0056c05c) : K(0x005bcb2c) / std::sqrt(n2);
            f.F(0x30) = f.F(0x30) * inv2;
            f.F(0x34) = c1 * inv2;
            f.F(0x38) = c2 * inv2;
            f.F(0x28) = t;
            if (std::fabs(t) > K(0x005bcb2c))
            {
                f.L(0x28) = 0x3f800000u;
                if (!(t > K(0x0056c05c)))
                    f.L(0x28) = 0xbf800000u;
            }
            const float half = X87Acos(f.F(0x28)) * K(0x005bcb4c);
            const float s = X87Sin(half);
            f.F(0x10) = f.F(0x30) * s;
            f.F(0x14) = f.F(0x34) * s;
            f.F(0x18) = f.F(0x38) * s;
            f.F(0x1c) = X87Cos(half);
            orig::fn_004de840(f.A(0x10), f.A(0x130));
            rotated = true;
        }
    }
    if (!rotated)
    {
        for (int i = 0; i < 12; ++i)
            f.L(0x130 + 4 * i) = GameGlobal<uint32_t>(0x005cad50 + 4 * i);
    }
    f.L(0x38) = 0x80000000u;
    const float s0 = b[0] + a[0];
    f.F(0x14) = a[1] + b[1];
    f.F(0x18) = a[2] + b[2];
    f.F(0x1c) = b[3] + a[3];
    const float m0 = s0 * K(0x005bcb4c);
    const float m1 = f.F(0x14) * K(0x005bcb4c);
    f.F(0x18) = f.F(0x18) * K(0x005bcb4c);
    f.L(0x168) = f.L(0x18);
    f.F(0x1c) = f.F(0x1c) * K(0x005bcb4c);
    f.L(0x30) = 0;
    f.L(0x34) = 0;
    f.F(0x160) = m0;
    f.L(0x16c) = f.L(0x1c);
    f.F(0x164) = m1;
    // Volumes and masses.
    f.F(0x2c) = K(0x0057e864) * radius * radius * radius * K(0x005bcd90);
    f.F(0x28) = K(0x0057e864) * f.F(0x24) * radius * radius;
    const float q = K(0x005bcb2c) / (f.F(0x28) + f.F(0x2c));
    f.F(0x48) = f.F(0x2c) * q * mass;
    f.F(0x4c) = q * f.F(0x28) * mass;
    // Entry 1: the cylinder.
    orig::fn_004142b0(f.A(0xa0));
    orig::fn_00414240(f.A(0xf0), f.A(0x130));
    const float r2 = radius * radius;
    f.L(0xc4) = GameGlobal<uint32_t>(0x005cad54);
    f.L(0xc8) = GameGlobal<uint32_t>(0x005cad58);
    f.L(0xcc) = GameGlobal<uint32_t>(0x005cad5c);
    f.L(0xd0) = GameGlobal<uint32_t>(0x005cad60);
    f.L(0xd8) = GameGlobal<uint32_t>(0x005cad68);
    f.L(0xdc) = GameGlobal<uint32_t>(0x005cad6c);
    f.L(0xb0) = 0;
    f.L(0xb4) = 0;
    f.L(0xb8) = 0;
    f.L(0xbc) = 0;
    f.L(0xe0) = GameGlobal<uint32_t>(0x005cad70);
    f.L(0xe4) = GameGlobal<uint32_t>(0x005cad74);
    f.L(0xec) = GameGlobal<uint32_t>(0x005cad7c);
    const float ixx = f.F(0x24) * f.F(0x24) * K(0x005bcbe4) * K(0x005bcc60) + r2 * K(0x005bcbe4);
    f.F(0xc0) = ixx;
    f.F(0xd4) = ixx;
    f.F(0xe8) = r2 * K(0x005bcb4c);
    orig::fn_004deda0(f.A(0xc0), f.L(0x4c));
    f.L(0xbc) = 0;
    f.L(0xb8) = 0;
    f.L(0xb4) = 0;
    f.L(0xb0) = 0;
    f.L(0xa0) = f.L(0x28);
    f.L(0xa4) = f.L(0x4c);
    orig::fn_004aa830(f.A(0x30), 1, 0x90);
    {
        const uint32_t slot = f.L(0x30) + f.L(0x34) * 0x90;
        ++f.L(0x34);
        orig::fn_00413770(slot, f.A(0xa0));
        orig::fn_00414240(slot + 0x50, f.A(0xf0));
    }
    // Entries 2 and 3: the hemispheres.
    for (int side = 0; side < 2; ++side)
    {
        orig::fn_004142b0(f.A(0xa0));
        orig::fn_00414240(f.A(0xf0), f.A(0x130));
        if (side == 0)
        {
            const float hl = f.F(0x24) * K(0x005bcb4c);
            f.F(0x4c) = hl;
            f.F(0x18) = hl;
        }
        else
        {
            f.F(0x18) = -f.F(0x4c);
        }
        f.L(0x10) = 0;
        f.L(0x14) = 0;
        f.L(0x1c) = 0;
        orig::fn_004de780(f.A(0x10), f.A(0x130), f.A(0x10));
        const float t0 = f.F(0x10) + f.F(0x120);
        const float t1 = f.F(0x14) + f.F(0x124);
        f.F(0x18) = f.F(0x18) + f.F(0x128);
        f.L(0x128) = f.L(0x18);
        f.F(0x1c) = f.F(0x1c) + f.F(0x12c);
        f.L(0x12c) = f.L(0x1c);
        f.F(0x120) = t0;
        f.F(0x124) = t1;
        f.L(0xb0) = 0;
        f.L(0xb4) = 0;
        f.L(0xbc) = 0;
        f.F(0xb8) = radius * K(side == 0 ? 0x005bce18u : 0x005bce14u);
        static const uint32_t zeroes[] = {0x50, 0x54, 0x6c, 0x68, 0x64, 0x60, 0x7c, 0x78, 0x74,
                                          0x70, 0x8c, 0x88, 0x84, 0x80, 0x9c, 0x98, 0x94, 0x90};
        for (uint32_t o : zeroes)
            f.L(o) = 0;
        SphereMassProperties(&f.C(0x50), radius, f.F(0x48));
        for (int i = 0; i < 12; ++i)
            f.L(0xc0 + 4 * i) = f.L(0x70 + 4 * i);
        orig::fn_004deda0(f.A(0xc0), 0x3f000000u);
        if (side == 0)
            f.F(0x24) = f.F(0x48) * K(0x005bcb4c);
        Inertia_ShiftFromCentre(&f.F(0xc0), &f.F(0xb0), f.F(0x24));
        if (side == 0)
            f.F(0x2c) = f.F(0x2c) * K(0x005bcb4c);
        f.L(0xa0) = f.L(0x2c);
        f.L(0xa4) = f.L(0x24);
        AppendMassEntry(&f.L(0x30), f.A(0xa0), f.A(0xf0));
    }
    MassEntries_Combine(out, &f.L(0x30));
    const uint32_t cap = f.L(0x38);
    if (static_cast<int32_t>(cap) >= 0)
        HkFree(f.L(0x30), ((cap & 0x7fffffffu) * 9) << 4, 0x10);
    return 0;
}

static uint32_t CapsuleMassProperties_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return CapsuleMassProperties(reinterpret_cast<const float*>(eax), reinterpret_cast<const float*>(ecx), bf(a0),
                                 bf(a1), B(a2));
}
ABI_ENTRY_004a53a0(CapsuleMassProperties_Entry, CapsuleMassProperties_Abi)
HOOK(0x004a53a0, "CapsuleMassProperties", CapsuleMassProperties_Entry, nullptr);

void Inertia_ShiftToPoint(float* m, const float* c, float mass);

// 0x004a5ed0 (esi = out; stack: &array; caller pops; callers 0x00413450, 0x004527d0, 0x00456c40, 0x004a53a0,
// 0x004a62f0): combines an hkArray {ptr, size, capacity} of 0x90-byte mass entries {mass properties: +0 volume, +4
// mass, +0x10 centre, +0x20 inertia (3 rows of 4); +0x50 transform (3x4 rotation, translation +0x80)} into out.
// Returns 1 (out untouched) when the array is empty or the total mass <= 0 (NaN passes). Pass 1: total mass, total
// volume and the mass-weighted sum of the transformed centres (((cz T[+0x70] + cy T[+0x60]) + cx T[+0x50]) + T[+0x80]
// per row; w accumulates mass * 0). out +0x10 = sum / total mass, +0 = volume, +4 = mass, +0x20..+0x4c = 0. Pass 2,
// per entry: inertia rotated into the body frame (0x004deab0 (eax = inertia, ecx = transposed rotation, edx = tmp),
// then (eax = rotation, ecx = tmp, edx = inertia)), shifted by the entry's mass to the offset of its transformed centre
// from the combined centre (0x004a68c0), added to out +0x20..+0x4c. Returns 0. Executed in recorded runs.
uint32_t MassEntries_Combine(uint8_t* out, uint32_t* entries)
{
    Frame<0xe0> f;
    f.L(0x1c) = 0;
    f.L(0x18) = 0;
    f.L(0x14) = 0;
    f.L(0x10) = 0;
    int32_t count = static_cast<int32_t>(entries[1]);
    if (count <= 0)
        return 1;
    float totalMass = K(0x0056c05c);
    float totalVolume = K(0x0056c05c);
    float sx = f.F(0x10);
    for (uint8_t* e = B(entries[0]); count != 0; --count, e += 0x90)
    {
        const float cx = Field<float>(e, 0x10);
        const float cy = Field<float>(e, 0x14);
        const float cz = Field<float>(e, 0x18);
        f.F(0x30) = ((cz * Field<float>(e, 0x70) + cy * Field<float>(e, 0x60)) + cx * Field<float>(e, 0x50)) +
                    Field<float>(e, 0x80);
        f.F(0x34) = ((cz * Field<float>(e, 0x74) + cy * Field<float>(e, 0x64)) + cx * Field<float>(e, 0x54)) +
                    Field<float>(e, 0x84);
        const float wz = ((cz * Field<float>(e, 0x78) + cy * Field<float>(e, 0x68)) + cx * Field<float>(e, 0x58)) +
                         Field<float>(e, 0x88);
        const float m = Field<float>(e, 4);
        sx = sx + f.F(0x30) * m;
        f.F(0x14) = f.F(0x34) * m + f.F(0x14);
        f.F(0x18) = wz * m + f.F(0x18);
        f.F(0x1c) = m * K(0x0056c05c) + f.F(0x1c);
        totalMass = totalMass + Field<float>(e, 4);
        totalVolume = totalVolume + Field<float>(e, 0);
    }
    f.F(0x10) = sx;
    f.F(0x2c) = totalVolume;
    if (totalMass <= K(0x0056c05c))
        return 1;
    const float inv = K(0x005bcb2c) / totalMass;
    f.L(0x4c) = 0;
    f.F(0x10) = f.F(0x10) * inv;
    f.F(0x14) = f.F(0x14) * inv;
    Field<uint32_t>(out, 0x14) = f.L(0x14);
    f.F(0x18) = f.F(0x18) * inv;
    Field<uint32_t>(out, 0x18) = f.L(0x18);
    f.F(0x1c) = f.F(0x1c) * inv;
    Field<uint32_t>(out, 0x1c) = f.L(0x1c);
    Field<float>(out, 0x10) = f.F(0x10);
    Field<uint32_t>(out, 0) = f.L(0x2c);
    Field<float>(out, 4) = totalMass;
    for (uint32_t o = 0x20; o < 0x50; o += 4)
        Field<uint32_t>(out, o) = 0;
    if (static_cast<int32_t>(entries[1]) <= 0)
        return 0;
    f.F(0x3c) = -f.F(0x1c);
    f.L(0x2c) = 0;
    do
    {
        uint8_t* e = B(entries[0]) + f.L(0x2c);
        for (int i = 0; i < 12; ++i)
            f.L(0x50 + 4 * i) = Field<uint32_t>(e, 0x20 + 4 * i);
        uint8_t* rotation = e + 0x50;
        for (int i = 0; i < 12; ++i)
            f.L(0x80 + 4 * i) = Field<uint32_t>(rotation, 4 * i);
        // Transpose the 3x3 part in place (rows 0x10 apart).
        const uint32_t t84 = f.L(0x84);
        const uint32_t t88 = f.L(0x88);
        const uint32_t t98 = f.L(0x98);
        const uint32_t ta4 = f.L(0xa4);
        f.L(0x84) = f.L(0x90);
        f.L(0x88) = f.L(0xa0);
        f.L(0x90) = t84;
        f.L(0xa0) = t88;
        f.L(0xa4) = t98;
        f.L(0x98) = ta4;
        orig::fn_004deab0(f.A(0x50), f.A(0x80), f.A(0xb0));
        orig::fn_004deab0(u(rotation), f.A(0xb0), f.A(0x50));
        e = B(entries[0]) + f.L(0x2c);
        const float cx = Field<float>(e, 0x10);
        const float cy = Field<float>(e, 0x14);
        const float cz = Field<float>(e, 0x18);
        const uint32_t m = Field<uint32_t>(e, 4);
        f.F(0x30) = ((cz * Field<float>(e, 0x70) + cy * Field<float>(e, 0x60)) + cx * Field<float>(e, 0x50)) +
                    Field<float>(e, 0x80);
        f.F(0x34) = ((cz * Field<float>(e, 0x74) + cy * Field<float>(e, 0x64)) + cx * Field<float>(e, 0x54)) +
                    Field<float>(e, 0x84);
        const float wz = ((cz * Field<float>(e, 0x78) + cy * Field<float>(e, 0x68)) + cx * Field<float>(e, 0x58)) +
                         Field<float>(e, 0x88);
        f.F(0x30) = f.F(0x30) - f.F(0x10);
        f.F(0x34) = f.F(0x34) - f.F(0x14);
        f.F(0x38) = wz - f.F(0x18);
        Inertia_ShiftToPoint(&f.F(0x50), &f.F(0x30), bf(m));
        for (uint32_t o = 0; o < 0x30; o += 4)
            Field<float>(out, 0x20 + o) = f.F(0x50 + o) + Field<float>(out, 0x20 + o);
        ++f.L(0x4c);
        f.L(0x2c) += 0x90;
    } while (static_cast<int32_t>(f.L(0x4c)) < static_cast<int32_t>(entries[1]));
    return 0;
}

static uint32_t MassEntries_Combine_Abi(uint32_t esi, uint32_t a0)
{
    return MassEntries_Combine(B(esi), reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004a5ed0(MassEntries_Combine_Entry, MassEntries_Combine_Abi)
HOOK(0x004a5ed0, "MassEntries_Combine", MassEntries_Combine_Entry, nullptr);

void MassProperties_ScaleByDensity(uint8_t* props, float density);

// 0x004a62f0 (stack: shape, transform, out; caller pops; callers 0x00412450 and itself): accumulates the mass
// properties of a shape (density 1) placed at transform into out (Havok mass-properties layout: +0 volume, +4 mass,
// +0x10 centre, +0x20 inertia). Dispatch on the shape's virtual +4 (type):
//   2, 0xc, 0x10, 0x11: collection (type 2 uses its +0x0c): for each child i < virtual +0x20(): child = virtual
//     +0x28(virtual +0x24(&key, i, buffer)); recursion with the same transform when not null;
//   3: recursion on +0x10; 0x12, 0x13 (wrapper types with a child at +0x0c, 0x13 = Type13Shape): recursion on +0x0c;
//   0x1a: transform composed with the shape's +0x10 by 0x004de510 (ecx = local; transform, +0x10), recursion on +0x0c;
//   5: SphereMassProperties (radius +0x0c, mass 1); 8: BoxMassProperties (half extents +0x10); 7: TriangleMassProperties
//     (P = +0x20, R = +0x10, Q = +0x30, thickness +0x0c); 0xa: CapsuleMassProperties (A = +0x20, B = +0x10, radius
//     +0x0c); 0xb: vertices fetched by virtual +0x20(&array), copied into a second array, ConvexHullMassProperties
//     (stride 0x10), both arrays freed when owned;
//   other types: nothing.
// For the leaf types, unless the volume is exactly 0, the local properties are scaled by their volume (0x004a6950),
// and out is replaced by MassEntries_Combine of {out (identity transform), local (transform)}. The original returns
// whatever eax holds; the only outside caller (0x00412450) ignores it: 0 here. Executed in recorded runs.
uint32_t Shape_AccumulateMassProperties(uint8_t* shape, uint32_t transform, uint8_t* out)
{
    Frame<0x1b0> f;
    static const uint32_t zeroes[] = {0x50, 0x54, 0x6c, 0x68, 0x64, 0x60, 0x7c, 0x78, 0x74,
                                      0x70, 0x8c, 0x88, 0x84, 0x80, 0x9c, 0x98, 0x94, 0x90};
    for (uint32_t o : zeroes)
        f.L(o) = 0;
    const uint32_t type = VCall<uint32_t>(shape, 4 / 4);
    switch (type)
    {
    case 5:
        SphereMassProperties(&f.C(0x50), Field<float>(shape, 0xc), K(0x005bcb2c));
        break;
    case 8:
        f.L(0x20) = Field<uint32_t>(shape, 0x10);
        f.L(0x28) = Field<uint32_t>(shape, 0x18);
        f.L(0x2c) = Field<uint32_t>(shape, 0x1c);
        f.L(0x24) = Field<uint32_t>(shape, 0x14);
        BoxMassProperties(&f.C(0x50), &f.F(0x20), K(0x005bcb2c));
        break;
    case 0xb:
    {
        f.L(0x30) = 0;
        f.L(0x34) = 0;
        f.L(0x38) = 0x80000000u;
        VCall<uint32_t>(shape, 0x20 / 4, f.A(0x30));
        const int32_t count = static_cast<int32_t>(f.L(0x34));
        f.L(0x20) = 0;
        f.L(0x24) = 0;
        f.L(0x28) = 0x80000000u;
        f.L(0x4c) = static_cast<uint32_t>(count);
        if (count > 0)
            orig::fn_004aa830(f.A(0x20), static_cast<uint32_t>(count), 0x10);
        const uint32_t cap = f.L(0x28);
        const uint32_t points = f.L(0x20);
        for (int32_t k = 0; k < count; ++k)
        {
            const uint32_t* s = reinterpret_cast<const uint32_t*>(f.L(0x30)) + 4 * k;
            uint32_t* d = reinterpret_cast<uint32_t*>(points) + 4 * k;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
        ConvexHullMassProperties(B(points), 0x10, count, K(0x005bcb2c), &f.C(0x50));
        if (static_cast<int32_t>(cap) >= 0)
            HkFree(points, (cap & 0x7fffffffu) << 4, 0x10);
        if (static_cast<int32_t>(f.L(0x38)) >= 0)
            HkFree(f.L(0x30), (f.L(0x38) & 0x7fffffffu) << 4, 0x10);
        break;
    }
    case 7:
        for (int i = 0; i < 4; ++i)
        {
            f.L(0x30 + 4 * i) = Field<uint32_t>(shape, 0x10 + 4 * i);
            f.L(0xa0 + 4 * i) = Field<uint32_t>(shape, 0x20 + 4 * i);
            f.L(0x20 + 4 * i) = Field<uint32_t>(shape, 0x30 + 4 * i);
        }
        TriangleMassProperties(&f.F(0xa0), &f.F(0x30), &f.F(0x20), K(0x005bcb2c), Field<float>(shape, 0xc),
                               &f.C(0x50));
        break;
    case 0xa:
        CapsuleMassProperties(reinterpret_cast<const float*>(shape + 0x20), reinterpret_cast<const float*>(shape + 0x10),
                              Field<float>(shape, 0xc), K(0x005bcb2c), &f.C(0x50));
        break;
    case 3:
        return Shape_AccumulateMassProperties(Field<uint8_t*>(shape, 0x10), transform, out);
    case 0x12:
    case 0x13:
        return Shape_AccumulateMassProperties(Field<uint8_t*>(shape, 0xc), transform, out);
    case 0x1a:
        orig::fn_004de510(f.A(0x50), transform, u(shape + 0x10));
        return Shape_AccumulateMassProperties(Field<uint8_t*>(shape, 0xc), f.A(0x50), out);
    case 2:
    case 0xc:
    case 0x10:
    case 0x11:
    {
        if (VCall<uint32_t>(shape, 4 / 4) == 2)
            shape = Field<uint8_t*>(shape, 0xc);
        for (int32_t i = 0; i < VCall<int32_t>(shape, 0x20 / 4); ++i)
        {
            const uint32_t key = VCall<uint32_t>(shape, 0x24 / 4, f.A(0x14), static_cast<uint32_t>(i), f.A(0xb0));
            uint8_t* child = VCall<uint8_t*>(shape, 0x28 / 4, key);
            if (child)
                Shape_AccumulateMassProperties(child, transform, out);
        }
        return 0;
    }
    default:
        return 0;
    }
    if (f.F(0x50) == K(0x0056c05c))
        return 0;
    MassProperties_ScaleByDensity(&f.C(0x50), f.F(0x50));
    f.L(0x14) = 0;
    f.L(0x18) = 0;
    f.L(0x1c) = 0x80000000u;
    orig::fn_004142b0(f.A(0xb0));
    orig::fn_00413770(f.A(0xb0), u(out));
    orig::fn_004aa830(f.A(0x14), 1, 0x90);
    {
        const uint32_t slot = f.L(0x14) + f.L(0x18) * 0x90;
        ++f.L(0x18);
        orig::fn_00413770(slot, f.A(0xb0));
        orig::fn_00414240(slot + 0x50, f.A(0x100));
    }
    orig::fn_004142b0(f.A(0xb0));
    orig::fn_00413770(f.A(0xb0), f.A(0x50));
    orig::fn_00414240(f.A(0x100), transform);
    AppendMassEntry(&f.L(0x14), f.A(0xb0), f.A(0x100));
    const uint32_t cap = f.L(0x1c);
    MassEntries_Combine(out, &f.L(0x14));
    if (static_cast<int32_t>(cap) >= 0)
        return HkFree(f.L(0x14), ((cap & 0x7fffffffu) * 9) << 4, 0x10);
    return 0;
}

static uint32_t Shape_AccumulateMassProperties_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Shape_AccumulateMassProperties(B(a0), a1, B(a2));
}
ABI_ENTRY_004a62f0(Shape_AccumulateMassProperties_Entry, Shape_AccumulateMassProperties_Abi)
HOOK(0x004a62f0, "Shape_AccumulateMassProperties", Shape_AccumulateMassProperties_Entry, nullptr);

// 0x004a6830 (eax = inertia (3 rows of 4), ecx = offset c; stack: mass; caller pops; eax preserved; callers 0x004a47a0,
// 0x004a53a0): parallel-axis shift towards the centre: diagonal m[0] -= (c1^2 + c2^2) mass, m[5] -= (c0^2 + c2^2)
// mass, m[10] -= (c0^2 + c1^2) mass; off-diagonal m[1] = m[4] = mass c0 c1 + m[1], m[6] = m[9] = mass c2 c1 + m[6],
// m[8] = m[2] = mass c0 c2 + m[8]. Not executed in recorded runs.
void Inertia_ShiftFromCentre(float* m, const float* c, float mass)
{
    m[0] = m[0] - (c[1] * c[1] + c[2] * c[2]) * mass;
    m[5] = m[5] - (c[0] * c[0] + c[2] * c[2]) * mass;
    m[10] = m[10] - (c[0] * c[0] + c[1] * c[1]) * mass;
    m[1] = mass * c[0] * c[1] + m[1];
    m[4] = m[1];
    m[6] = mass * c[2] * c[1] + m[6];
    m[9] = m[6];
    m[8] = mass * c[0] * c[2] + m[8];
    m[2] = m[8];
}

static uint32_t Inertia_ShiftFromCentre_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    Inertia_ShiftFromCentre(reinterpret_cast<float*>(eax), reinterpret_cast<const float*>(ecx), bf(a0));
    return eax;
}
ABI_ENTRY_004a6830(Inertia_ShiftFromCentre_Entry, Inertia_ShiftFromCentre_Abi)
HOOK(0x004a6830, "Inertia_ShiftFromCentre", Inertia_ShiftFromCentre_Entry, nullptr);

// 0x004a68c0 (eax = inertia, ecx = offset c; stack: mass; caller pops; eax preserved; caller 0x004a5ed0): the inverse
// shift of 0x004a6830 (away from the centre): diagonal m[0] = (c1^2 + c2^2) mass + m[0] (likewise m[5], m[10]);
// off-diagonal m[1] = m[4] = m[1] - mass c0 c1, m[6] = m[9] = m[6] - mass c2 c1, m[8] = m[2] = m[8] - mass c0 c2.
// Executed in recorded runs.
void Inertia_ShiftToPoint(float* m, const float* c, float mass)
{
    m[0] = (c[1] * c[1] + c[2] * c[2]) * mass + m[0];
    m[5] = (c[0] * c[0] + c[2] * c[2]) * mass + m[5];
    m[10] = (c[0] * c[0] + c[1] * c[1]) * mass + m[10];
    m[1] = m[1] - mass * c[0] * c[1];
    m[4] = m[1];
    m[6] = m[6] - mass * c[2] * c[1];
    m[9] = m[6];
    m[8] = m[8] - mass * c[0] * c[2];
    m[2] = m[8];
}

static uint32_t Inertia_ShiftToPoint_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    Inertia_ShiftToPoint(reinterpret_cast<float*>(eax), reinterpret_cast<const float*>(ecx), bf(a0));
    return eax;
}
ABI_ENTRY_004a68c0(Inertia_ShiftToPoint_Entry, Inertia_ShiftToPoint_Abi)
HOOK(0x004a68c0, "Inertia_ShiftToPoint", Inertia_ShiftToPoint_Entry, nullptr);

// 0x004a6950 (eax = mass properties; stack: density; caller pops; eax preserved; callers 0x00413450, 0x004527d0,
// 0x004a62f0): mass (+4) and the nine inertia entries (+0x20, +0x30, +0x40, +0x24, +0x34, +0x44, +0x28, +0x38,
// +0x48, in that order) = density * value. Executed in recorded runs.
void MassProperties_ScaleByDensity(uint8_t* props, float density)
{
    static const uint32_t offsets[] = {4, 0x20, 0x30, 0x40, 0x24, 0x34, 0x44, 0x28, 0x38, 0x48};
    for (uint32_t o : offsets)
        Field<float>(props, o) = density * Field<float>(props, o);
}

static uint32_t MassProperties_ScaleByDensity_Abi(uint32_t eax, uint32_t a0)
{
    MassProperties_ScaleByDensity(B(eax), bf(a0));
    return eax;
}
ABI_ENTRY_004a6950(MassProperties_ScaleByDensity_Entry, MassProperties_ScaleByDensity_Abi)
HOOK(0x004a6950, "MassProperties_ScaleByDensity", MassProperties_ScaleByDensity_Entry, nullptr);

namespace
{
// One edge (a0, b0) -> (a1, b1) of 0x004a69c0, in the original's operation order. `first` stores the projection
// integrals into the globals, otherwise adds them (new + old); the return value is the edge's Pabb-like term.
inline float ProjectionEdge(float a0, float b0, float a1, float b1, bool first)
{
    const float k3 = K(0x005bcb7c);
    const float k4 = K(0x005bcbe0);
    const float da = a1 - a0;
    const float db = b1 - b0;
    const float a0s = a0 * a0;
    const float a0c = a0s * a0;
    const float b0s = b0 * b0;
    const float b0c = b0s * b0;
    const float a1s = a1 * a1;
    const float b1s = b1 * b1;
    const float b1c = b1s * b1;
    const float c1 = a1 + a0;
    const float ca2 = a1 * c1 + a0s;
    const float ca3 = ca2 * a1 + a0c;
    const float cb2 = (b1 + b0) * b1 + b0s;
    const float cb3 = cb2 * b1 + b0c;
    float t = a1 * a0;
    t = t + t;
    const float cab = (a1s * k3 + t) + a0s;
    const float kab = (a0s * k3 + t) + a1s;
    struct Term
    {
        uint32_t address;
        float value;
    };
    const Term terms[] = {
        {0x005fed00, c1 * db},
        {0x005fecec, ca2 * db},
        {0x005fed44, ca3 * db},
        {0x005fed58, (a0c * a0 + ca3 * a1) * db},
        {0x005fed1c, cb2 * da},
        {0x005fecf4, cb3 * da},
        {0x005fed54, (b0c * b0 + cb3 * b1) * da},
        {0x005fed30, (cab * b1 + kab * b0) * db},
        {0x005fed24, ((a1s * a1 * k4 + cab * a0) * b1 + (kab * a1 + a0c * k4) * b0) * db},
    };
    for (const Term& term : terms)
    {
        if (first)
            GameGlobal<float>(term.address) = term.value;
        else
            GameGlobal<float>(term.address) = term.value + GameGlobal<float>(term.address);
    }
    const float b1sb0 = b1s * b0;
    const float b0sb1 = b0s * b1;
    float twice = b1sb0;
    twice = twice + twice;
    const float kbb = ((k3 * b0sb1 + twice) + b0c * k4) + b1c;
    float twice2 = b0sb1;
    twice2 = twice2 + twice2;
    const float cbb = ((twice2 + b1sb0 * k3) + b1c * k4) + b0c;
    return (a0 * kbb + cbb * a1) * da;
}
}  // namespace

// 0x004a69c0 (eax = triangle: three 16-byte vertices; plain ret; eax preserved; caller 0x004a7080): projection
// integrals of a polyhedron face (the Mirtich volume-integration scheme, guess from the formulas) for the axes alpha =
// [0x005fed34] and beta = [0x005fed50] (vertex components eax + axis * 4 + 0x10 k), summed over the edges 0->1, 1->2,
// 2->0 into the globals 0x005fed00, 0x005fecec, 0x005fed44, 0x005fed58, 0x005fed1c, 0x005fecf4, 0x005fed54,
// 0x005fed30, 0x005fed24 (first edge stores, the others add), each then scaled by its constant (0x005bcb4c, 0x005bccb0,
// 0x005bccac, 0x005bcc34, 0x005bcca8, 0x005bcca4, 0x005bcc2c, 0x005bcca0, 0x005bcc98); the summed last term times
// [0x005bcc9c] goes to 0x005fed70. Executed in recorded runs.
void Face_ProjectionIntegrals(const uint8_t* triangle)
{
    const uint32_t alpha = GameGlobal<uint32_t>(0x005fed34) * 4;
    const uint32_t beta = GameGlobal<uint32_t>(0x005fed50) * 4;
    const float a0 = Field<float>(triangle, alpha);
    const float b0 = Field<float>(triangle, beta);
    const float a1 = Field<float>(triangle, alpha + 0x10);
    const float b1 = Field<float>(triangle, beta + 0x10);
    const float a2 = Field<float>(triangle, alpha + 0x20);
    const float b2 = Field<float>(triangle, beta + 0x20);
    float r = ProjectionEdge(a0, b0, a1, b1, true);
    r = r + ProjectionEdge(a1, b1, a2, b2, false);
    r = r + ProjectionEdge(a2, b2, a0, b0, false);
    static const uint32_t scale[][2] = {{0x005fed00, 0x005bcb4c}, {0x005fecec, 0x005bccb0}, {0x005fed44, 0x005bccac},
                                        {0x005fed58, 0x005bcc34}, {0x005fed1c, 0x005bcca8}, {0x005fecf4, 0x005bcca4},
                                        {0x005fed54, 0x005bcc2c}, {0x005fed30, 0x005bcca0}, {0x005fed24, 0x005bcc98}};
    for (const auto& s : scale)
        GameGlobal<float>(s[0]) = GameGlobal<float>(s[0]) * K(s[1]);
    GameGlobal<float>(0x005fed70) = r * K(0x005bcc9c);
}

static uint32_t Face_ProjectionIntegrals_Abi(uint32_t eax)
{
    Face_ProjectionIntegrals(B(eax));
    return eax;
}
ABI_ENTRY_004a69c0(Face_ProjectionIntegrals_Entry, Face_ProjectionIntegrals_Abi)
HOOK(0x004a69c0, "Face_ProjectionIntegrals", Face_ProjectionIntegrals_Entry, nullptr);

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
