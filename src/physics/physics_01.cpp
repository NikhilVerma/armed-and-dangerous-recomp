// Recovered: game-side physics glue around the statically linked Havok 2.x library (batch physics_01,
// 0x00403c70-0x00447660). Static recovery 2026-10-05, compile-checked only (the game was not run); the oracle
// (tools/compare.py) is still to do.
//
// Havok itself is not decompiled: Havok functions are called through orig::fn_XXXXXXXX exactly as the original does,
// and Havok objects are accessed with Field<T> at the offsets documented in docs/subsystems/physics.md (entity +0x08
// motion, +0x10 simulation island, +0x44 world, +0x78/+0x7c pair array; island +0x24 index, +0x29 active; motion
// vtable slots 0x18 getType, 0x5c/0x64/0x6c/0x70 impulse, angular impulse, force, torque).
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour.
#include <cmath>
#include <cstring>

#include "game.h"
#include "game/game_object.h"
#include "gen/entry_stubs.h"
#include "gen/orig_calls.h"
#include "hooks.h"
#include "physics_hooks.h"

void (*g_physicsAfterInit)(uint8_t* manager);
void (*g_physicsBeforeShutdown)(uint8_t* manager);
bool (*g_physicsBeforeSubstep)(uint8_t* manager, float dt);
void (*g_physicsAfterSubstep)(uint8_t* manager, float dt);

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
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x00589178 = 0.2,
// 0x00589174 = 1.1920929e-7 = FLT_EPSILON).
inline float K(uint32_t address)
{
    return GameGlobal<float>(address);
}
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
// The inlined Havok wake idiom the game repeats before touching a body: when the entity has a simulation island
// (+0x10) that is not active (island +0x29 == 0), the entity is in a world (+0x44) and the island has a valid index
// (+0x24 != -1), call hkWorld_activateIsland 0x004ad130 (esi = world, edi = island). The original tests +0x29 twice.
inline void WakeEntity(uint8_t* entity)
{
    uint8_t* island = Field<uint8_t*>(entity, 0x10);
    if (island && !island[0x29] && island)
    {
        uint8_t* world = Field<uint8_t*>(entity, 0x44);
        if (world && !island[0x29] && Field<int32_t>(island, 0x24) != -1)
            orig::fn_004ad130(u(world), u(island));
    }
}
inline uint8_t* Motion(uint8_t* entity)
{
    return Field<uint8_t*>(entity, 8);
}
}  // namespace

// Prototypes (functions of this file call each other directly).
uint32_t ForceActionPool_Release(uint32_t* pool, int32_t index, uint8_t force);
uint32_t ForceActionPool_ReleaseAll(uint32_t* pool);
uint32_t Entity_ApplyLinearImpulse(uint8_t* entity, const float* impulse);
uint32_t Entity_ApplyAngularImpulse(uint8_t* entity, const float* impulse);
uint32_t Entity_ApplyTorque(uint8_t* entity, const float* torque);

// ================================================================================================================
// Pre-step forces (run by the substep loop 0x00411d40 before each hkWorld step)
// ================================================================================================================

// 0x00403c70 (no inputs; caller 0x00411d40, once per substep): applies per-object gravity scaling. For each object of
// the list Game+0xa84 +0xf1c (iterator 0x004e7340 eax = list, esi = iterator / 0x004e73c0 edi = iterator), virtual
// +0x8c(&scale) gives a float; for each of its bodies (one body from 0x004574b0(eax = -1, ecx = object, edx = &out)
// when byte +0x421 is clear, else the 0x4c-byte records of the array at +0x1bc until a null entity) whose motion
// type (motion virtual +0x18) is not 4 (keyframed), force = world gravity (hkWorld +0xf0, 4 floats) *
// ((scale - 1) * mass) with mass = 1 / invMass (motion +0x2c; 0 when invMass is 0), body woken and motion virtual
// +0x6c (applyForce) called. Returns the iterator's node after dropping its reference (+0x18), or 0.
uint32_t Physics_ApplyGravityScale()
{
    uint8_t* manager = Field<uint8_t*>(g_game, 0xa84);
    if (!manager)
        return 0;
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(manager, 0xf1c), u(iterator))); obj;
         obj = B(orig::fn_004e73c0(u(iterator))))
    {
        float scale;
        VCall<void>(obj, 0x8c / 4, &scale);
        if (obj[0x421])
        {
            uint8_t* record = Field<uint8_t*>(obj, 0x1bc);
            for (; Field<uint8_t*>(record, 0); record += 0x4c)
            {
                if (VCall<int32_t>(Motion(Field<uint8_t*>(record, 0)), 0x18 / 4) == 4)
                    continue;
                const float* gravity =
                    reinterpret_cast<const float*>(Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8) + 0xf0);
                alignas(16) float force[4] = {gravity[0], gravity[1], gravity[2], gravity[3]};
                uint8_t* entity = Field<uint8_t*>(record, 0);
                const float invMass = Field<float>(Motion(entity), 0x2c);
                float mass = K(0x0056c05c);
                if (invMass != K(0x0056c05c))
                    mass = K(0x005bcb2c) / invMass;
                const float k = (scale - K(0x005bcb2c)) * mass;
                force[0] = force[0] * k;
                force[1] = force[1] * k;
                force[2] = force[2] * k;
                force[3] = force[3] * k;
                WakeEntity(entity);
                VCall<void>(Motion(entity), 0x6c / 4, force);
            }
        }
        else
        {
            uint32_t entityBits = 0;
            orig::fn_004574b0(0xffffffffu, u(obj), u(&entityBits));
            uint8_t* entity = B(entityBits);
            if (VCall<int32_t>(Motion(entity), 0x18 / 4) == 4)
                continue;
            const float* gravity =
                reinterpret_cast<const float*>(Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8) + 0xf0);
            alignas(16) float force[4] = {gravity[0], gravity[1], gravity[2], gravity[3]};
            const float invMass = Field<float>(Motion(entity), 0x2c);
            float mass = K(0x0056c05c);
            if (invMass != K(0x0056c05c))
                mass = K(0x005bcb2c) / invMass;
            const float k = (scale - K(0x005bcb2c)) * mass;
            force[0] = force[0] * k;
            force[1] = force[1] * k;
            force[2] = force[2] * k;
            force[3] = force[3] * k;
            WakeEntity(entity);
            VCall<void>(Motion(entity), 0x6c / 4, force);
        }
    }
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t Physics_ApplyGravityScale_Abi()
{
    return Physics_ApplyGravityScale();
}
ABI_ENTRY_00403c70(Physics_ApplyGravityScale_Entry, Physics_ApplyGravityScale_Abi)
HOOK(0x00403c70, "Physics_ApplyGravityScale", Physics_ApplyGravityScale_Entry, nullptr);

// ApplyForceAction pool (strings "ApplyForceAction: m_elements" / "ApplyForceAction: m_freeList"):
//   +0x00 capacity, +0x04 used count, +0x08 next free-list read index, +0x0c free-list write index,
//   +0x10 u16 free list[capacity], +0x14 elements[capacity] of 0x30 bytes:
//   +0x00 flags (1 in use, 2 guess: kept alive, 4 active, 8 started, 0x10 impulse at end, 0x20 angular), +0x04 owner
//   object (reference count +0x28), +0x08 entity, +0x10 vector (4 floats), +0x20 delay / start time, +0x24
//   duration, +0x28 elapsed time.

// 0x00403ef0 (eax = capacity, esi = pool; caller 0x0045a4e0): allocates the element array (capacity * 0x30, tag
// 0x13, "ApplyForceAction: m_elements") and the free list (capacity * 2, "ApplyForceAction: m_freeList"), clears
// each element's flags and owner, fills the free list with 0..capacity-1. Returns the pool.
// Enhancement hook ([limits] physics_solver_iterations): the Havok world's solver iterations (world cinfo +0x3c;
// +0x34 tau 0.6, +0x38 damping 1.0, +0x3c 4 match Havok's "4 iterations, medium" solver preset). 0 = the original.
uint32_t g_solverIterations = 0;

// Enhancement hooks (hook/enhance/limits.cpp, [limits] force_pool): a larger capacity (0 = the caller's, 512; the free
// list holds 16-bit indices) and a count of forces dropped because the pool was full.
uint32_t g_forcePoolCapacity = 0;
uint32_t g_forcePoolDrops = 0;

uint32_t ForceActionPool_Init(uint32_t capacity, uint32_t* pool)
{
    if (g_forcePoolCapacity > capacity)
        capacity = g_forcePoolCapacity < 0xffffu ? g_forcePoolCapacity : 0xffffu;
    const uint32_t elements = Alloc(0x13, capacity * 0x30, 2, 0x005bc06c);
    pool[5] = elements;
    pool[4] = Alloc(0x13, capacity * 2, 2, 0x005bc04c);
    uint32_t* e = reinterpret_cast<uint32_t*>(pool[5]);
    pool[0] = capacity;
    for (uint32_t n = capacity; n != 0; --n, e += 0x30 / 4)
    {
        e[0] = 0;
        e[1] = 0;
    }
    pool[2] = 0;
    int32_t i = 0;
    for (; i < static_cast<int32_t>(pool[0]); ++i)
        reinterpret_cast<uint16_t*>(pool[4])[i] = static_cast<uint16_t>(i);
    pool[3] = static_cast<uint32_t>(i);
    pool[1] = 0;
    return u(pool);
}

static uint32_t ForceActionPool_Init_Abi(uint32_t eax, uint32_t esi)
{
    return ForceActionPool_Init(eax, reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_00403ef0(ForceActionPool_Init_Entry, ForceActionPool_Init_Abi)
HOOK(0x00403ef0, "ForceActionPool_Init", ForceActionPool_Init_Entry, nullptr);

// 0x00403f70 (esi = pool; caller 0x0045a6c0): releases every element (0x00404110) and frees the free list and the
// element array. Not executed in recorded runs. Returns eax as left (the last free's result, else the pointer).
uint32_t ForceActionPool_Destroy(uint32_t* pool)
{
    uint32_t result = ForceActionPool_ReleaseAll(pool);
    if (pool[4])
    {
        result = Free(pool[4]);
        pool[4] = 0;
    }
    if (pool[5])
    {
        result = Free(pool[5]);
        pool[5] = 0;
    }
    return result;
}

static uint32_t ForceActionPool_Destroy_Abi(uint32_t esi)
{
    return ForceActionPool_Destroy(reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_00403f70(ForceActionPool_Destroy_Entry, ForceActionPool_Destroy_Abi)
HOOK(0x00403f70, "ForceActionPool_Destroy", ForceActionPool_Destroy_Entry, nullptr);

// 0x00403fb0 (eax = previous handle, ecx = pool, esi = vector (4 dwords); stack: owner, bodyArg, delay, duration,
// flags; callee pops 0x14; callers 0x00455a30/0x00455aa0/0x00455b10/0x00455b80): releases the previous handle when
// >= 0 (0x00404060, force), takes the next free-list slot; fails with -1 when that element is still in use (flag 1).
// Otherwise advances the read index (wraps), counts it, stores flags | 5, the vector, duration (4th stack arg,
// +0x20), delay (3rd stack arg, +0x24), elapsed 0, and when an owner is given takes a reference (+0x28), stores it and resolves its body into
// +0x08 with 0x004574b0(eax = bodyArg, ecx = owner, edx = &element+8). Returns the element index.
uint32_t ForceActionPool_Add(int32_t previous, uint32_t* pool, const uint32_t* vector, uint8_t* owner,
                             uint32_t bodyArg, uint32_t delay, uint32_t duration, uint32_t flags)
{
    if (previous >= 0)
        ForceActionPool_Release(pool, previous, 1);
    const uint32_t next = pool[2];
    const uint32_t index = reinterpret_cast<uint16_t*>(pool[4])[next];
    uint32_t* e = reinterpret_cast<uint32_t*>(pool[5] + index * 0x30);
    if (e[0] & 1)
    {
        ++g_forcePoolDrops;
        return 0xffffffffu;
    }
    pool[2] = next + 1;
    if (static_cast<int32_t>(next + 1) >= static_cast<int32_t>(pool[0]))
        pool[2] = 0;
    ++pool[1];
    e[0] = flags | 5;
    e[4] = vector[0];
    e[5] = vector[1];
    e[6] = vector[2];
    e[7] = vector[3];
    e[8] = duration; // 0x0040401a: [esp+0x1c] after 3 pushes = 4th stack argument
    e[9] = delay;    // 0x00404021: [esp+0x18] = 3rd stack argument
    e[10] = 0;
    if (owner)
    {
        ++Field<int32_t>(owner, 0x28);
        e[1] = u(owner);
        orig::fn_004574b0(bodyArg, u(owner), u(&e[2]));
    }
    return index;
}

static uint32_t ForceActionPool_Add_Abi(uint32_t eax, uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1,
                                        uint32_t a2, uint32_t a3, uint32_t a4)
{
    return ForceActionPool_Add(static_cast<int32_t>(eax), reinterpret_cast<uint32_t*>(ecx),
                               reinterpret_cast<const uint32_t*>(esi), B(a0), a1, a2, a3, a4);
}
ABI_ENTRY_00403fb0(ForceActionPool_Add_Entry, ForceActionPool_Add_Abi)
HOOK(0x00403fb0, "ForceActionPool_Add", ForceActionPool_Add_Entry, nullptr);

// 0x00404060 (eax: not read, edi = pool; stack: index, force (byte); callee pops 8; callers 0x00403fb0, 0x00404110,
// 0x00404140, 0x00455a00): for index >= 0: clears flag 4 (active), drops the owner reference (+0x28; at zero, when
// owner virtual +0xd8 returns false, 0x00487da0(eax = owner)) and clears the owner. Unless force is set, an element
// with flag 2 stays allocated; otherwise flag 2 is cleared and, if in use (flag 1), flag 1 is cleared and the index
// is pushed back on the free list (write index wraps at capacity), used count decremented. Returns -1.
uint32_t ForceActionPool_Release(uint32_t* pool, int32_t index, uint8_t force)
{
    if (index < 0)
        return 0xffffffffu;
    uint32_t* e = reinterpret_cast<uint32_t*>(pool[5] + static_cast<uint32_t>(index) * 0x30);
    if (e[0] & 4)
        e[0] &= ~4u;
    uint8_t* owner = B(e[1]);
    if (owner)
    {
        if (--Field<int32_t>(owner, 0x28) == 0)
        {
            if (!VCall<uint8_t>(owner, 0xd8 / 4))
                orig::fn_00487da0(u(owner));
        }
        e[1] = 0;
    }
    if (!force && (e[0] & 2))
        return 0xffffffffu;
    const uint32_t flags = e[0] & ~2u;
    e[0] = flags;
    if (flags & 1)
    {
        e[0] = flags & ~1u;
        if (static_cast<int32_t>(pool[3]) >= static_cast<int32_t>(pool[0]))
            pool[3] = 0;
        reinterpret_cast<uint16_t*>(pool[4])[pool[3]] = static_cast<uint16_t>(index);
        ++pool[3];
        --pool[1];
    }
    return 0xffffffffu;
}

static uint32_t ForceActionPool_Release_Abi(uint32_t /*eax*/, uint32_t edi, uint32_t a0, uint32_t a1)
{
    return ForceActionPool_Release(reinterpret_cast<uint32_t*>(edi), static_cast<int32_t>(a0),
                                   static_cast<uint8_t>(a1));
}
ABI_ENTRY_00404060(ForceActionPool_Release_Entry, ForceActionPool_Release_Abi)
HOOK(0x00404060, "ForceActionPool_Release", ForceActionPool_Release_Entry, nullptr);

// 0x00404110 (eax = pool; callers 0x00403f70, 0x0045d670): releases every element with force (0x00404060).
// Returns the capacity as last read.
uint32_t ForceActionPool_ReleaseAll(uint32_t* pool)
{
    uint32_t capacity = pool[0];
    for (int32_t i = 0; i < static_cast<int32_t>(capacity); ++i)
    {
        ForceActionPool_Release(pool, i, 1);
        capacity = pool[0];
    }
    return capacity;
}

static uint32_t ForceActionPool_ReleaseAll_Abi(uint32_t eax)
{
    return ForceActionPool_ReleaseAll(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_00404110(ForceActionPool_ReleaseAll_Entry, ForceActionPool_ReleaseAll_Abi)
HOOK(0x00404110, "ForceActionPool_ReleaseAll", ForceActionPool_ReleaseAll_Entry, nullptr);

// 0x00404140 (stack: pool, dt (float); callee pops 8; caller 0x00411d40, once per substep): runs the active (flag 4)
// force actions. An element whose entity's pair array (+0x78/+0x7c) has, at its first null key, a value object with
// bit 8 of +0x424 is released. Otherwise elapsed (+0x28) += dt; nothing happens before the delay (+0x20). On the
// first run (flag 8 clear) the start time (+0x20) becomes the elapsed time and the remaining time is the duration;
// later remaining = duration + start - elapsed. While remaining > 0.2: flag 0x20 applies the vector as torque
// (0x004044b0) when its squared length exceeds FLT_EPSILON, else as a force (wake, motion virtual +0x6c). When
// 0 < remaining <= 0.2 with flag 0x10: vector * remaining as an angular impulse (0x00404460, only when its squared
// length > FLT_EPSILON; flag 0x20) or a linear impulse (0x00404410), then released. remaining <= 0: released.
// Releases are 0x00404060(index, 0). Returns 0 (the loop counter).
uint32_t ForceActionPool_Update(uint32_t* pool, float dt)
{
    uint8_t* element = B(pool[5]);
    uint32_t counter = pool[0];
    if (!counter)
        return 0;
    do
    {
        --counter;
        const uint32_t flags = Field<uint32_t>(element, 0);
        float* v = &Field<float>(element, 0x10);  // the original addresses the element through ebx = element + 0x18
        if (flags & 4)
        {
            uint8_t* entity = Field<uint8_t*>(element, 8);
            const int32_t n = Field<int32_t>(entity, 0x7c);
            bool released = false;
            for (int32_t i = 0; i < n; ++i)
            {
                uint32_t* pair = Field<uint32_t*>(entity, 0x78) + i * 2;
                if (pair[0] == 0)
                {
                    uint8_t* other = B(Field<uint32_t*>(entity, 0x78)[i * 2 + 1]);
                    if (other && (other[0x424] & 8))
                    {
                        ForceActionPool_Release(pool, static_cast<int32_t>(pool[0] - counter - 1), 0);
                        released = true;
                    }
                    break;
                }
            }
            if (!released)
            {
                const float elapsed = dt + Field<float>(element, 0x28);
                Field<float>(element, 0x28) = elapsed;
                if (elapsed >= Field<float>(element, 0x20))  // fcomp / test ah, 1: skipped when below or NaN
                {
                    float remaining = (Field<float>(element, 0x24) + Field<float>(element, 0x20)) - elapsed;
                    if (!(flags & 8))
                    {
                        Field<float>(element, 0x20) = elapsed;
                        remaining = Field<float>(element, 0x24);
                        Field<uint32_t>(element, 0) = flags | 8;
                    }
                    if (remaining > K(0x00589178))
                    {
                        if (element[0] & 0x20)
                        {
                            const float x = v[0], y = v[1], z = v[2];
                            if ((x * x + z * z) + y * y > K(0x00589174))
                            {
                                alignas(16) float torque[4];
                                torque[1] = v[1];
                                torque[0] = v[0];
                                torque[2] = v[2];
                                Entity_ApplyTorque(entity, torque);
                            }
                        }
                        else
                        {
                            alignas(16) float force[4];
                            force[0] = v[0];
                            force[1] = v[1];
                            force[2] = v[2];
                            WakeEntity(entity);
                            VCall<void>(Motion(entity), 0x6c / 4, force);
                        }
                    }
                    else if (remaining > K(0x0056c05c) && (Field<uint32_t>(element, 0) & 0x10))
                    {
                        alignas(16) float impulse[4];
                        const float x = v[0] * remaining;
                        if (Field<uint32_t>(element, 0) & 0x20)
                        {
                            impulse[0] = x;
                            impulse[2] = v[2];
                            impulse[3] = v[3];
                            impulse[1] = v[1] * remaining;
                            impulse[2] = impulse[2] * remaining;
                            impulse[3] = impulse[3] * remaining;
                            if ((impulse[2] * impulse[2] + impulse[1] * impulse[1]) + impulse[0] * impulse[0] >
                                K(0x00589174))
                                Entity_ApplyAngularImpulse(entity, impulse);
                        }
                        else
                        {
                            impulse[0] = x;
                            impulse[2] = v[2];
                            impulse[3] = v[3];
                            impulse[1] = v[1] * remaining;
                            impulse[2] = impulse[2] * remaining;
                            impulse[3] = impulse[3] * remaining;
                            Entity_ApplyLinearImpulse(entity, impulse);
                        }
                        ForceActionPool_Release(pool, static_cast<int32_t>(pool[0] - counter - 1), 0);
                    }
                    else if (remaining <= K(0x0056c05c))
                    {
                        // fcomp 0.0 / test ah, 0x41 / jp: released when remaining <= 0 (not for NaN).
                        ForceActionPool_Release(pool, static_cast<int32_t>(pool[0] - counter - 1), 0);
                    }
                }
            }
        }
        element += 0x30;
    } while (counter != 0);
    return 0;
}

static uint32_t ForceActionPool_Update_Abi(uint32_t a0, uint32_t a1)
{
    return ForceActionPool_Update(reinterpret_cast<uint32_t*>(a0), bf(a1));
}
ABI_ENTRY_00404140(ForceActionPool_Update_Entry, ForceActionPool_Update_Abi)
HOOK(0x00404140, "ForceActionPool_Update", ForceActionPool_Update_Entry, nullptr);

// 0x00404410 (stack: entity, impulse; callee pops 8; caller 0x00404140): wakes the body and calls motion virtual
// +0x5c (applyLinearImpulse, docs/subsystems/physics.md). Returns the virtual's eax.
uint32_t Entity_ApplyLinearImpulse(uint8_t* entity, const float* impulse)
{
    WakeEntity(entity);
    return VCall<uint32_t>(Motion(entity), 0x5c / 4, impulse);
}

static uint32_t Entity_ApplyLinearImpulse_Abi(uint32_t a0, uint32_t a1)
{
    return Entity_ApplyLinearImpulse(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00404410(Entity_ApplyLinearImpulse_Entry, Entity_ApplyLinearImpulse_Abi)
HOOK(0x00404410, "Entity_ApplyLinearImpulse", Entity_ApplyLinearImpulse_Entry, nullptr);

// 0x00404460 (stack: entity, impulse; callee pops 8; caller 0x00404140): wakes the body and calls motion virtual
// +0x64 (applyAngularImpulse).
uint32_t Entity_ApplyAngularImpulse(uint8_t* entity, const float* impulse)
{
    WakeEntity(entity);
    return VCall<uint32_t>(Motion(entity), 0x64 / 4, impulse);
}

static uint32_t Entity_ApplyAngularImpulse_Abi(uint32_t a0, uint32_t a1)
{
    return Entity_ApplyAngularImpulse(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00404460(Entity_ApplyAngularImpulse_Entry, Entity_ApplyAngularImpulse_Abi)
HOOK(0x00404460, "Entity_ApplyAngularImpulse", Entity_ApplyAngularImpulse_Entry, nullptr);

// 0x004044b0 (stack: entity, torque; callee pops 8; caller 0x00404140): wakes the body and calls motion virtual
// +0x70 (applyTorque). Not executed in recorded runs.
uint32_t Entity_ApplyTorque(uint8_t* entity, const float* torque)
{
    WakeEntity(entity);
    return VCall<uint32_t>(Motion(entity), 0x70 / 4, torque);
}

static uint32_t Entity_ApplyTorque_Abi(uint32_t a0, uint32_t a1)
{
    return Entity_ApplyTorque(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_004044b0(Entity_ApplyTorque_Entry, Entity_ApplyTorque_Abi)
HOOK(0x004044b0, "Entity_ApplyTorque", Entity_ApplyTorque_Entry, nullptr);

// 0x00404510 (eax = out, ecx = entity; stack: key; callee pops 4; callers 0x00411850, 0x00454940): looks the key up
// in the entity's pair array (+0x78 {key, value} x +0x7c) and stores the value, or 0, in *out. Returns out (eax is
// untouched).
uint32_t Entity_FindPairValue(uint32_t* out, uint8_t* entity, uint32_t key)
{
    const int32_t n = Field<int32_t>(entity, 0x7c);
    for (int32_t i = 0; i < n; ++i)
    {
        if (Field<uint32_t*>(entity, 0x78)[i * 2] == key)
        {
            *out = Field<uint32_t*>(entity, 0x78)[i * 2 + 1];
            return u(out);
        }
    }
    *out = 0;
    return u(out);
}

static uint32_t Entity_FindPairValue_Abi(uint32_t eax, uint32_t ecx, uint32_t a0)
{
    return Entity_FindPairValue(reinterpret_cast<uint32_t*>(eax), B(ecx), a0);
}
ABI_ENTRY_00404510(Entity_FindPairValue_Entry, Entity_FindPairValue_Abi)
HOOK(0x00404510, "Entity_FindPairValue", Entity_FindPairValue_Entry, nullptr);

// ================================================================================================================
// PeasantObject (vtable 0x005a8550, class name "CLASS(PeasantObject)" through slot 1)
// ================================================================================================================

// 0x004e7880 (ecx = in, edx = out): normalises a 3-vector and returns its length in st0. symbols/abi.csv lists an
// EAX return; the original callers pop st0 (fstp st(0)), so this wrapper pops it too (as src/renderer/render_r1.cpp).
static void Vec3_Normalize(uint32_t source, uint32_t dest)
{
    static const uint32_t target = 0x004e7880;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, source
        mov edx, dest
        call dword ptr [target]
        fstp st(0)
        pop ebp
        pop edi
        pop esi
        pop ebx
    }
}

// 0x00405d10 (ecx = PeasantObject; slot +0xbc of vtable 0x005a8550; no direct callers; not executed in recorded
// runs). guess: "follow in a queue" behaviour of a peasant. When +0x48c is set and +0x47c < 0 (first time): unless
// 0x004539e0(eax = this) returns true (then nothing else happens), physics is disabled (0x004577b0 0), 0x004058f0
// (edi = this) and 0x00454750 (esi = Game+0xa84 +0xefc, edi = this) run, the effect at +0x498 is released
// (Fx_Release 0x0041d030 with Game+0xa90), Game+0x26f4 += +0x484; a non-ragdolled object (+0x421 clear) goes through
// 0x00459000(1, 1, 0), Body_addConstraints, flag 0x10 on the +0x1c of the four records 0x00408800(eax = 0..3) and
// 0x00459000(1, 0, 0). +0x47c = the wrapping counter 0x0064a768 (always 0: it wraps at 1), +0x478 = 0, +0x48d = 1,
// +0x424 |= 0x8000, and each body record (+0x1bc, 0x4c bytes) gets entity +0x38 = 0 and, for a shape of type 1 (shape
// virtual +8; through the child +0x28 of a type-0x19 wrapper, virtual +4), shape +0xc = 1.0 (guess: radius).
// Then, while +0x478 >= 0: picks from the list Game+0xa84 +0xef8 the object with the largest +0x480 below this one's
// (else the overall largest when this one's is 0, else the player Game+0x748) and sets +0x480 to its +0x480 + 1 (or
// 1); a change resets +0x478 = 0 / +0x48d = 1. The target position is the player's virtual +0x90(&pos, names, 1) or
// the centre of mass (motion +0x70) of the body named "center" (0x004586a0). The distance to this body's centre of
// mass is stored into +0x478 when it was 0; when the distance exceeds 3 and +0x478, the body is moved towards the
// target by (distance - +0x478) through 0x004df0d0(ecx = new position; quaternion, 10.0, entity). While +0x48d is
// set, +0x478 shrinks by dt (0x00604a90) * 5; above 5 +0x48d is set again; below 3 it is clamped to 3 and +0x48d
// cleared. A ragdolled object (+0x421) finally goes through 0x00459000(0, 0, 0). Returns eax as left when cheap.
uint32_t fn_00405d10(uint8_t* self)
{
    uint32_t names[2] = {0x005a009c, 0x005a009c};  // "center"
    uint32_t result = 0;
    if (self[0x48c] && Field<int32_t>(self, 0x47c) < 0)
    {
        const uint32_t busy = orig::fn_004539e0(u(self));
        if (busy & 0xff)
            return busy;
        orig::fn_004577b0(u(self), 0);
        orig::fn_004058f0(u(self));
        orig::fn_00454750(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xefc), u(self));
        orig::fn_0041d030(Field<uint32_t>(g_game, 0xa90), u(self + 0x498));
        Field<int32_t>(g_game, 0x26f4) += Field<int32_t>(self, 0x484);
        if (!self[0x421])
        {
            // 0x00459000 reads only AL when it is non-zero (it then reuses EAX).
            orig::fn_00459000(1, u(self), 1, 1, 0);
            orig::fn_00456570(u(self));
            for (uint32_t i = 0; i < 4; ++i)
                Field<uint32_t>(P(orig::fn_00408800(i, u(self))), 0x1c) |= 0x10;
            orig::fn_00459000(1, u(self), 1, 0, 0);
        }
        uint32_t counter = GameGlobal<uint32_t>(0x0064a768) + 1;
        GameGlobal<uint32_t>(0x0064a768) = counter;
        if (counter >= 1)
        {
            counter = 0;
            GameGlobal<uint32_t>(0x0064a768) = 0;
        }
        Field<uint32_t>(self, 0x47c) = counter;
        Field<uint32_t>(self, 0x478) = 0;
        self[0x48d] = 1;
        Field<uint32_t>(self, 0x424) |= 0x8000;
        uint8_t* record = Field<uint8_t*>(self, 0x1bc);
        if (record && Field<uint32_t>(record, 0))
        {
            do
            {
                Field<uint32_t>(Field<uint8_t*>(record, 0), 0x38) = 0;
                uint8_t* shape = Field<uint8_t*>(Field<uint8_t*>(record, 0), 0x24);
                if (VCall<int32_t>(shape, 1) == 0x19)
                    shape = Field<uint8_t*>(shape, 0x28);
                if (VCall<int32_t>(shape, 2) == 1)
                    Field<uint32_t>(shape, 0xc) = 0x3f800000;  // 1.0f
                record += 0x4c;
            } while (Field<uint32_t>(record, 0));
        }
    }
    if (Field<float>(self, 0x478) >= K(0x0056c05c))  // fcomp / test ah, 1: skipped when below 0 or NaN
    {
        uint32_t iterator[2] = {0, 0};
        uint8_t* below = nullptr;
        uint8_t* highest = nullptr;
        int32_t belowValue = 0;
        int32_t highestValue = 0;
        for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xef8), u(iterator)));
             obj; obj = B(orig::fn_004e73c0(u(iterator))))
        {
            if (obj == self)
                continue;
            const int32_t value = Field<int32_t>(obj, 0x480);
            if (value < Field<int32_t>(self, 0x480) && belowValue < value)
            {
                below = obj;
                belowValue = value;
            }
            if (highestValue < value)
            {
                highest = obj;
                highestValue = value;
            }
        }
        const int32_t old = Field<int32_t>(self, 0x480);
        uint8_t* target;
        if (below)
        {
            Field<int32_t>(self, 0x480) = Field<int32_t>(below, 0x480) + 1;
            target = below;
        }
        else if (highest && old == 0)
        {
            Field<int32_t>(self, 0x480) = Field<int32_t>(highest, 0x480) + 1;
            target = highest;
        }
        else
        {
            Field<int32_t>(self, 0x480) = 1;
            target = Field<uint8_t*>(g_game, 0x748);
        }
        if (Field<int32_t>(self, 0x480) != old)
        {
            Field<uint32_t>(self, 0x478) = 0;
            self[0x48d] = 1;
        }
        if (target)
        {
            float goal[4];  // stack +0x54: x, y, z (and +0x60 scratch)
            const int32_t index = Field<int32_t>(self, 0x47c);
            if (target == Field<uint8_t*>(g_game, 0x748))
            {
                VCall<void>(target, 0x90 / 4, goal, names, 1u);
            }
            else
            {
                uint8_t* entity = B(orig::fn_004586a0(names[index], u(target)));
                const float* com = &Field<float>(Motion(entity), 0x70);
                goal[0] = com[0];
                goal[1] = com[1];
                goal[2] = com[2];
            }
            uint8_t* body = B(orig::fn_004586a0(names[index + 1], u(self)));
            const float* com = &Field<float>(Motion(body), 0x70);
            alignas(16) float own[4] = {com[0], com[1], com[2], com[3]};
            float delta[3];
            delta[0] = goal[0] - own[0];
            delta[1] = goal[1] - own[1];
            delta[2] = goal[2] - own[2];
            if (Field<float>(self, 0x478) == K(0x0056c05c))
                Field<float>(self, 0x478) =
                    std::sqrt((delta[2] * delta[2] + delta[1] * delta[1]) + delta[0] * delta[0]);
            const float dt = GameGlobal<float>(0x00604a90);
            const float distance = std::sqrt((delta[2] * delta[2] + delta[1] * delta[1]) + delta[0] * delta[0]);
            if (distance > K(0x005bcb7c) && distance > Field<float>(self, 0x478))
            {
                alignas(16) float direction[4];
                Vec3_Normalize(u(delta), u(direction));
                const float step = distance - Field<float>(self, 0x478);
                const float* position = &Field<float>(Motion(body), 0xb0);
                alignas(16) float moved[4] = {position[0], position[1], position[2], position[3]};
                const float dx = direction[0] * step;
                direction[1] = step * direction[1];
                direction[2] = step * direction[2];
                moved[0] = moved[0] + dx;
                moved[1] = moved[1] + direction[1];
                moved[2] = moved[2] + direction[2];
                const float* q = &Field<float>(Motion(body), 0x30);
                alignas(16) float rotation[4] = {q[0], q[1], q[2], q[3]};
                orig::fn_004df0d0(u(moved), u(rotation), 0x41200000u /* 10.0f */, u(body));
            }
            if (self[0x48d])
                Field<float>(self, 0x478) = Field<float>(self, 0x478) - dt * K(0x005bcbdc);
            if (Field<float>(self, 0x478) > K(0x005bcbdc))
                self[0x48d] = 1;
            if (Field<float>(self, 0x478) < K(0x005bcb7c))
            {
                Field<uint32_t>(self, 0x478) = 0x40400000;  // 3.0f
                self[0x48d] = 0;
            }
        }
        if (iterator[1])
            --Field<int32_t>(P(iterator[1]), 0x18);
    }
    if (self[0x421])
        result = orig::fn_00459000(1, u(self), 0, 0, 0);
    return result;
}

static uint32_t fn_00405d10_Abi(uint32_t ecx)
{
    return fn_00405d10(B(ecx));
}
ABI_ENTRY_00405d10(fn_00405d10_Entry, fn_00405d10_Abi)
HOOK(0x00405d10, "fn_00405d10", fn_00405d10_Entry, nullptr);

// 0x00406580 (stack: entity, position; callee pops 8; callers 0x00457870, 0x0048cec0): wakes the body and, unless
// it is flagged (+0xc bit 0) while in a world (+0x44), calls motion virtual +0x40 (setPosition). Returns the world
// pointer when skipped, else the virtual's eax.
uint32_t Entity_SetPosition(uint8_t* entity, const float* position)
{
    WakeEntity(entity);
    if ((entity[0xc] & 1) && Field<uint32_t>(entity, 0x44))
        return Field<uint32_t>(entity, 0x44);
    return VCall<uint32_t>(Motion(entity), 0x40 / 4, position);
}

static uint32_t Entity_SetPosition_Abi(uint32_t a0, uint32_t a1)
{
    return Entity_SetPosition(B(a0), reinterpret_cast<const float*>(a1));
}
ABI_ENTRY_00406580(Entity_SetPosition_Entry, Entity_SetPosition_Abi)
HOOK(0x00406580, "Entity_SetPosition", Entity_SetPosition_Entry, nullptr);

// ================================================================================================================
// Game collision listener (vtable 0x005baa48 {0x00413240, 0x004112d0, 0x004113e0}; added to world +0xd0 by
// 0x00411960, removed by 0x00411cb0)
// ================================================================================================================

// 0x004112c0 (eax = listener; callers 0x00411960, 0x00411cb0): constructor, stores the vtable 0x005baa48.
uint32_t CollisionListener_Construct(uint32_t* listener)
{
    listener[0] = 0x005baa48;
    return u(listener);
}

static uint32_t CollisionListener_Construct_Abi(uint32_t eax)
{
    return CollisionListener_Construct(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_004112c0(CollisionListener_Construct_Entry, CollisionListener_Construct_Abi)
HOOK(0x004112c0, "CollisionListener_Construct", CollisionListener_Construct_Entry, nullptr);

// Game object owning an entity: the value of the first pair-array entry (+0x78/+0x7c) with key 0, or null.
static uint8_t* OwnerOf(uint8_t* entity)
{
    const int32_t n = Field<int32_t>(entity, 0x7c);
    for (int32_t i = 0; i < n; ++i)
    {
        if (Field<uint32_t*>(entity, 0x78)[i * 2] == 0)
            return B(Field<uint32_t*>(entity, 0x78)[i * 2 + 1]);
    }
    return nullptr;
}

// 0x004112d0 (slot +4 of vtable 0x005baa48, thiscall with `this` unused; stack: event; callee pops 4; called by
// Havok). Event: +0 argument, +4 object whose virtual +0x18(event+0) returns a contact point (position +0,
// normal +0x10), +8 / +0xc entities A / B. The owners of A and B (OwnerOf) are notified through game object virtual
// +0xac(other, &position, &normal): A with the normal, then B with the negated normal.
uint32_t CollisionListener_OnContact(uint32_t* event)
{
    uint8_t* a = OwnerOf(B(event[2]));
    uint8_t* b = OwnerOf(B(event[3]));
    const float* point = VCall<const float*>(P(event[1]), 0x18 / 4, event[0]);
    float position[3] = {point[0], point[1], point[2]};
    float normal[3] = {point[4], point[5], point[6]};
    uint32_t result = u(point);
    if (a)
        result = VCall<uint32_t>(a, 0xac / 4, b, position, normal);
    if (b)
    {
        normal[0] = -normal[0];
        normal[1] = -normal[1];
        normal[2] = -normal[2];
        result = VCall<uint32_t>(b, 0xac / 4, a, position, normal);
    }
    return result;
}

static uint32_t CollisionListener_OnContact_Abi(uint32_t a0)
{
    return CollisionListener_OnContact(reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004112d0(CollisionListener_OnContact_Entry, CollisionListener_OnContact_Abi)
HOOK(0x004112d0, "CollisionListener_OnContact", CollisionListener_OnContact_Entry, nullptr);

// 0x004113e0 (slot +8 of vtable 0x005baa48, `this` unused; stack: event; callee pops 4; not executed in recorded
// runs). Event: +4 / +8 collidables A / B (each: +0x10 object whose virtual +4 is called first, +0x1c entity), +0xc
// manifold (+4 -> {points, count}, points of 0x30 bytes: position +0, normal +0x10), +0x10 entity. sign = -1 when
// A's entity is event +0x10, else +1. For each point: owner(A) virtual +0xb0(owner(B), &position, &(sign * normal),
// 0) and owner(B) virtual +0xb0(owner(A), &position, &-(sign * normal), 0).
uint32_t CollisionListener_OnManifold(uint32_t* event)
{
    uint8_t* collidableA = B(event[1]);
    uint8_t* manifold = B(event[3]);
    if (Field<void*>(collidableA, 0x10))
        VCall<void>(Field<void*>(collidableA, 0x10), 1);
    if (Field<void*>(B(event[2]), 0x10))
        VCall<void>(Field<void*>(B(event[2]), 0x10), 1);
    uint8_t* entityA = Field<uint8_t*>(B(event[1]), 0x1c);
    float sign = -1.0f;
    uint8_t* a = OwnerOf(entityA);
    if (u(entityA) != event[4])
        sign = 1.0f;
    uint8_t* b = OwnerOf(Field<uint8_t*>(B(event[2]), 0x1c));
    uint32_t result = 0;
    for (int32_t i = 0; i < Field<int32_t>(Field<uint8_t*>(manifold, 4), 4); ++i)
    {
        const uint32_t offset = static_cast<uint32_t>(i) * 0x30;
        float position[3];
        position[0] = Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset);
        position[1] = Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 4);
        position[2] = Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 8);
        float normal[3];
        if (a)
        {
            normal[0] = sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x10);
            normal[1] = sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x14);
            normal[2] = sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x18);
            result = VCall<uint32_t>(a, 0xb0 / 4, b, position, normal, 0u);
        }
        if (b)
        {
            normal[0] = -(sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x10));
            normal[1] = -(sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x14));
            normal[2] = -(sign * Field<float>(Field<uint8_t*>(Field<uint8_t*>(manifold, 4), 0), offset + 0x18));
            result = VCall<uint32_t>(b, 0xb0 / 4, a, position, normal, 0u);
        }
    }
    return result;
}

static uint32_t CollisionListener_OnManifold_Abi(uint32_t a0)
{
    return CollisionListener_OnManifold(reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004113e0(CollisionListener_OnManifold_Entry, CollisionListener_OnManifold_Abi)
HOOK(0x004113e0, "CollisionListener_OnManifold", CollisionListener_OnManifold_Entry, nullptr);

// ================================================================================================================
// Game shape subclass (vtable 0x005baa20, base Havok shape vtable 0x0058e3c4)
// ================================================================================================================

// 0x00411590 (eax = shape; callers 0x004527d0, 0x004574e0): constructor: u16 +6 = 1, +8 = 0, vtable 0x005baa20.
uint32_t GameShape_Construct(uint8_t* shape)
{
    Field<uint16_t>(shape, 6) = 1;
    Field<uint32_t>(shape, 8) = 0;
    Field<uint32_t>(shape, 0) = 0x005baa20;
    return u(shape);
}

static uint32_t GameShape_Construct_Abi(uint32_t eax)
{
    return GameShape_Construct(B(eax));
}
ABI_ENTRY_00411590(GameShape_Construct_Entry, GameShape_Construct_Abi)
HOOK(0x00411590, "GameShape_Construct", GameShape_Construct_Entry, nullptr);

// 0x004115e0 (eax = shape; caller 0x004115b0): restores the base Havok shape vtable 0x0058e3c4. Not executed in
// recorded runs.
uint32_t GameShape_DestructBase(uint32_t* shape)
{
    shape[0] = 0x0058e3c4;
    return u(shape);
}

static uint32_t GameShape_DestructBase_Abi(uint32_t eax)
{
    return GameShape_DestructBase(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_004115e0(GameShape_DestructBase_Entry, GameShape_DestructBase_Abi)
HOOK(0x004115e0, "GameShape_DestructBase", GameShape_DestructBase_Entry, nullptr);

// 0x004115b0 (ecx = shape; stack: flags; callee pops 4; slot 0 of vtable 0x005baa20, the scalar deleting
// destructor; not executed in recorded runs): base destructor (0x004115e0) and, with flags bit 0, hkMemory
// ([0x005fed84]) virtual +0x14 free(shape, u16 size +4, class 0x1d). Returns the shape.
uint32_t GameShape_Delete(uint8_t* shape, uint32_t flags)
{
    GameShape_DestructBase(reinterpret_cast<uint32_t*>(shape));
    if (flags & 1)
        VCall<void>(GameGlobal<void*>(0x005fed84), 0x14 / 4, shape, static_cast<uint32_t>(Field<uint16_t>(shape, 4)),
                    0x1du);
    return u(shape);
}

static uint32_t GameShape_Delete_Abi(uint32_t ecx, uint32_t a0)
{
    return GameShape_Delete(B(ecx), a0);
}
ABI_ENTRY_004115b0(GameShape_Delete_Entry, GameShape_Delete_Abi)
HOOK(0x004115b0, "GameShape_Delete", GameShape_Delete_Entry, nullptr);

// 0x004115f0 (slot +0x20 of vtable 0x005baa20, `this` unused; stack: collidable A, collidable B, unused; callee pops
// 0xc; not executed in recorded runs). guess: overlap begins (phantom callback). The owners (pair key 0 of the
// collidables' entities +0x1c) are told through game object virtual +0xb4(1, other): A first, then B.
uint32_t GameShape_OnOverlapBegin(uint8_t* a, uint8_t* b, uint32_t /*unused*/)
{
    uint8_t* ownerA = OwnerOf(Field<uint8_t*>(a, 0x1c));
    uint8_t* ownerB = OwnerOf(Field<uint8_t*>(b, 0x1c));
    uint32_t result = 0;
    if (ownerA)
        result = VCall<uint32_t>(ownerA, 0xb4 / 4, 1u, ownerB);
    if (ownerB)
        result = VCall<uint32_t>(ownerB, 0xb4 / 4, 1u, ownerA);
    return result;
}

static uint32_t GameShape_OnOverlapBegin_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return GameShape_OnOverlapBegin(B(a0), B(a1), a2);
}
ABI_ENTRY_004115f0(GameShape_OnOverlapBegin_Entry, GameShape_OnOverlapBegin_Abi)
HOOK(0x004115f0, "GameShape_OnOverlapBegin", GameShape_OnOverlapBegin_Entry, nullptr);

// 0x00411680 (slot +0x24 of vtable 0x005baa20; as 0x004115f0 with 0): guess: overlap ends. Not executed in recorded
// runs.
uint32_t GameShape_OnOverlapEnd(uint8_t* a, uint8_t* b, uint32_t /*unused*/)
{
    uint8_t* ownerA = OwnerOf(Field<uint8_t*>(a, 0x1c));
    uint8_t* ownerB = OwnerOf(Field<uint8_t*>(b, 0x1c));
    uint32_t result = 0;
    if (ownerA)
        result = VCall<uint32_t>(ownerA, 0xb4 / 4, 0u, ownerB);
    if (ownerB)
        result = VCall<uint32_t>(ownerB, 0xb4 / 4, 0u, ownerA);
    return result;
}

static uint32_t GameShape_OnOverlapEnd_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return GameShape_OnOverlapEnd(B(a0), B(a1), a2);
}
ABI_ENTRY_00411680(GameShape_OnOverlapEnd_Entry, GameShape_OnOverlapEnd_Abi)
HOOK(0x00411680, "GameShape_OnOverlapEnd", GameShape_OnOverlapEnd_Entry, nullptr);

// ================================================================================================================
// Collision filters
// ================================================================================================================

// 0x00411710 (slot 1 of vtable 0x005baa18, installed through 0x004ac700 at world +0xe8 -> +4; `this` unused;
// stack: collidable A, collidable B; callee pops 8; returns bool in AL; not executed in recorded runs).
// isCollisionEnabled: owners (pair key 0 of entities +0x1c) equal: rejected when null or owner virtual +0x44 is
// false. Rejected: a keyframed (motion type 4) body against a type 4 or 5 body; filter words (+0xc) that are both
// non-zero and whose high halves do not intersect the other's low bits. Both owners present: each owner's virtual
// +0xa0(other) must accept. Otherwise enabled.
uint32_t CollisionFilter_IsEnabled(uint8_t* a, uint8_t* b)
{
    uint8_t* entityB = Field<uint8_t*>(b, 0x1c);
    uint8_t* entityA = Field<uint8_t*>(a, 0x1c);
    uint8_t* ownerA = OwnerOf(entityA);
    uint8_t* ownerB = OwnerOf(entityB);
    if (ownerA == ownerB)
    {
        if (!ownerA || !VCall<uint8_t>(ownerA, 0x44 / 4))
            return 0;
    }
    const uint32_t filterA = Field<uint32_t>(a, 0xc);
    const uint32_t filterB = Field<uint32_t>(b, 0xc);
    const int32_t typeA = VCall<int32_t>(Motion(entityA), 0x18 / 4);
    const int32_t typeB = VCall<int32_t>(Motion(entityB), 0x18 / 4);
    if (typeA == 4 && (typeB == 4 || typeB == 5))
        return 0;
    if (typeB == 4 && (typeA == 4 || typeA == 5))
        return 0;
    if (!(filterA & (filterB >> 16)) && !(filterB & (filterA >> 16)) && filterA && filterB)
        return 0;
    if (ownerA && ownerB)
    {
        if (!VCall<uint8_t>(ownerA, 0xa0 / 4, ownerB))
            return 0;
        if (!VCall<uint8_t>(ownerB, 0xa0 / 4, ownerA))
            return 0;
    }
    return 1;
}

static uint32_t CollisionFilter_IsEnabled_Abi(uint32_t a0, uint32_t a1)
{
    return CollisionFilter_IsEnabled(B(a0), B(a1));
}
ABI_ENTRY_00411710(CollisionFilter_IsEnabled_Entry, CollisionFilter_IsEnabled_Abi)
HOOK(0x00411710, "CollisionFilter_IsEnabled", CollisionFilter_IsEnabled_Entry, nullptr);

// 0x00411850 (slot 1 of vtable 0x005baa00, installed at world +0x23c by 0x00411960; `this` unused; stack:
// collidable A, collidable B; callee pops 8; returns bool in AL; not executed in recorded runs). Two dynamic bodies
// (neither entity flagged +0xc bit 0, neither motion type 4) never pass. Otherwise passes when A's owner (pair key 0)
// is ragdolled (+0x421) or A has a pair key 1 value (0x00404510), else when B has an owner that is ragdolled or B has
// a pair key 1 value.
uint32_t CollisionFilter_IsEnabledFixed(uint8_t* a, uint8_t* b)
{
    uint8_t* entityB = Field<uint8_t*>(b, 0x1c);
    uint8_t* entityA = Field<uint8_t*>(a, 0x1c);
    if (!(entityA[0xc] & 1) && !(entityB[0xc] & 1) && VCall<int32_t>(Motion(entityA), 0x18 / 4) != 4 &&
        VCall<int32_t>(Motion(entityB), 0x18 / 4) != 4)
        return 0;
    uint32_t value;  // the original reuses the stack slot of argument A
    uint8_t* ownerA = OwnerOf(entityA);
    if (ownerA)
    {
        if (ownerA[0x421])
            return 1;
        if (*reinterpret_cast<uint32_t*>(Entity_FindPairValue(&value, entityA, 1)))
            return 1;
    }
    Entity_FindPairValue(&value, entityB, 0);
    if (!value)
        return 0;
    if (B(value)[0x421])
        return 1;
    return *reinterpret_cast<uint32_t*>(Entity_FindPairValue(&value, entityB, 1)) ? 1 : 0;
}

static uint32_t CollisionFilter_IsEnabledFixed_Abi(uint32_t a0, uint32_t a1)
{
    return CollisionFilter_IsEnabledFixed(B(a0), B(a1));
}
ABI_ENTRY_00411850(CollisionFilter_IsEnabledFixed_Entry, CollisionFilter_IsEnabledFixed_Abi)
HOOK(0x00411850, "CollisionFilter_IsEnabledFixed", CollisionFilter_IsEnabledFixed_Entry, nullptr);

// ================================================================================================================
// Physics manager (Game+0xa8c): +0 initialised, +4 game collision listener, +8 hkWorld*, +0xc shape, +0x10..+0x5c
// state cleared by 0x00411910 (built by 0x004a46c0), +0x60 deferred refilter flag.
// ================================================================================================================

// 0x00411910 (eax = manager; caller 0x004011e0): clears +0x10..+0x5c (in the original's store order) and the
// initialised byte +0.
uint32_t PhysicsManager_Clear(uint8_t* m)
{
    static const uint8_t order[] = {0x10, 0x14, 0x2c, 0x28, 0x24, 0x20, 0x3c, 0x38, 0x34,
                                    0x30, 0x4c, 0x48, 0x44, 0x40, 0x5c, 0x58, 0x54, 0x50};
    for (uint8_t offset : order)
        Field<uint32_t>(m, offset) = 0;
    m[0] = 0;
    return u(m);
}

static uint32_t PhysicsManager_Clear_Abi(uint32_t eax)
{
    return PhysicsManager_Clear(B(eax));
}
ABI_ENTRY_00411910(PhysicsManager_Clear_Entry, PhysicsManager_Clear_Abi)
HOOK(0x00411910, "PhysicsManager_Clear", PhysicsManager_Clear_Entry, nullptr);

uint32_t PhysicsManager_Shutdown(uint8_t* m);
uint32_t WorldCinfo_Init(uint32_t* cinfo);

// 0x00411950 (eax = manager; caller 0x00401710): PhysicsManager_Shutdown(manager). Not executed in recorded runs.
uint32_t PhysicsManager_ShutdownThunk(uint8_t* m)
{
    return PhysicsManager_Shutdown(m);
}

static uint32_t PhysicsManager_ShutdownThunk_Abi(uint32_t eax)
{
    return PhysicsManager_ShutdownThunk(B(eax));
}
ABI_ENTRY_00411950(PhysicsManager_ShutdownThunk_Entry, PhysicsManager_ShutdownThunk_Abi)
HOOK(0x00411950, "PhysicsManager_ShutdownThunk", PhysicsManager_ShutdownThunk_Entry, nullptr);

// hkMemory singleton [0x005fed84]: virtual +0x10 alloc(size, class).
static uint8_t* HkAlloc(uint32_t size, uint32_t cls)
{
    return VCall<uint8_t*>(GameGlobal<void*>(0x005fed84), 0x10 / 4, size, cls);
}
// hkReferencedObject::removeReference inlined: u16 +6 reference count; at zero, virtual 0 (deleting destructor)(1).
static void HkRelease(uint8_t* object)
{
    --Field<uint16_t>(object, 6);
    if (Field<uint16_t>(object, 6) == 0)
        VCall<void>(object, 0, 1u);
}

// 0x00411960 (ebx = manager; caller 0x0044a110; strings hk*.cpp): physics init. Shuts down first, marks +0
// initialised, calls hkBaseSystem init through [0x005cadc8], suppresses six Havok warnings (hkError [0x005fed80]
// virtual +0x10(file, line)), fills a world cinfo (0x00414370, then gravity (0, -64.35, 0), broadphase extents
// (-5000, -5000, -1100)..(5000, 5000, 2000), 0.6, 1.0, 4, 0.2), creates the world (0x250 bytes, class 0x25,
// hkWorld ctor 0x004addb0) into +8 and, when it exists: registers all agents (0x0050f090), the game's extension
// agents (0x004a3d80, 0x004a4280, 0x004a87e0) and the type-0x19 agent 0x004df500 for (1, 0x19), (0x19, 1),
// (0x19, 0x19); adds the default listener at world +0x240 once (inline Havok code, flag +0x22e/+0x22f); the game
// listener 0x004df780; replaces the world's collision filter (+0x23c) with 0x005baa00; adds the game collision
// listener (vtable 0x005baa48) into +4; installs filter 0x005baa18 (0x004ac700); creates a convex radius shape
// (0x0050f190, radius 0.1234) into +0xc with +8 = -5 and builds +0x10 from it (0x004a46c0(0.1234, 1.0)). Finally
// +0x60 = 0. Returns eax as left.
uint32_t PhysicsManager_Init(uint8_t* m)
{
    uint32_t cinfo[25];  // the original's frame from esp+0x10
    uint32_t result = 0;
    PhysicsManager_Shutdown(m);
    m[0] = 1;
    reinterpret_cast<uint32_t (*)()>(GameGlobal<uint32_t>(0x005cadc8))();
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005babf4u, 0xa8u);
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005babe0u, 0x3du);
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005babccu, 0x15u);
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005babb0u, 0x54u);
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005bab94u, 0x30u);
    VCall<void>(GameGlobal<void*>(0x005fed80), 0x10 / 4, 0x005bab94u, 0xd2u);
    WorldCinfo_Init(cinfo);
    cinfo[0x40 / 4] = 0x3e4ccccd;  // 0.2
    cinfo[0x34 / 4] = 0x3f19999a;  // 0.6
    cinfo[0x38 / 4] = 0x3f800000;  // 1.0
    cinfo[0x3c / 4] = 4;
    if (g_solverIterations)  // enhancement ([limits] physics_solver_iterations)
        cinfo[0x3c / 4] = g_solverIterations;
    cinfo[0x10 / 4] = 0xc59c4000;  // -5000
    cinfo[0x14 / 4] = 0xc59c4000;
    cinfo[0x18 / 4] = 0xc4898000;  // -1100
    cinfo[0x1c / 4] = 0;
    cinfo[0x20 / 4] = 0x459c4000;  // 5000
    cinfo[0x24 / 4] = 0x459c4000;
    cinfo[0x28 / 4] = 0x44fa0000;  // 2000
    cinfo[0x2c / 4] = 0;
    cinfo[0] = 0;
    cinfo[1] = 0;
    cinfo[2] = 0xc280b23a;  // -64.348
    cinfo[3] = 0;
    uint8_t* memory = HkAlloc(0x250, 0x25);
    Field<uint16_t>(memory, 4) = 0x250;
    uint8_t* world = B(orig::fn_004addb0(u(memory), u(cinfo), 0x30da6));
    Field<uint8_t*>(m, 8) = world;
    if (world)
    {
        orig::fn_0050f090(Field<uint32_t>(world, 0xe4));
        orig::fn_004a3d80(Field<uint32_t>(Field<uint8_t*>(m, 8), 0xe4));
        orig::fn_004a4280(Field<uint32_t>(Field<uint8_t*>(m, 8), 0xe4));
        orig::fn_004a87e0(Field<uint32_t>(Field<uint8_t*>(m, 8), 0xe4));
        const uint32_t dispatcher = Field<uint32_t>(Field<uint8_t*>(m, 8), 0xe4);
        orig::fn_00513270(dispatcher, 0x004df500, 0x19, 1);
        orig::fn_00513270(dispatcher, 0x004df500, 1, 0x19);
        orig::fn_00513270(dispatcher, 0x004df500, 0x19, 0x19);
        world = Field<uint8_t*>(m, 8);
        if (!world[0x22e])
        {
            world[0x22e] = 1;
            uint8_t* listener = HkAlloc(0x14, 0xe);
            Field<uint16_t>(listener, 4) = 0x14;
            Field<uint32_t>(listener, 8) = 0x005baa0c;
            Field<uint32_t>(listener, 0) = 0x0058e3c4;
            Field<uint32_t>(listener, 8) = 0x0058e3b8;
            Field<uint8_t*>(world, 0x240) = listener;
            orig::fn_004ac5c0(u(world), u(listener + 8));
            world[0x22f] = 1;
        }
        world = Field<uint8_t*>(m, 8);
        uint8_t* gameListener = HkAlloc(0x14, 0xe);
        Field<uint16_t>(gameListener, 4) = 0x14;
        orig::fn_004df780(u(gameListener), u(world));
        uint8_t* filter = HkAlloc(8, 0xe);
        Field<uint16_t>(filter, 4) = 8;
        Field<uint16_t>(filter, 6) = 1;
        Field<uint32_t>(filter, 0) = 0x005baa00;
        world = Field<uint8_t*>(m, 8);
        ++Field<uint16_t>(filter, 6);
        HkRelease(Field<uint8_t*>(world, 0x23c));
        Field<uint8_t*>(world, 0x23c) = filter;
        HkRelease(filter);
        uint8_t* listener = B(Alloc(0x10, 4, 2, 0x0058b5e8));  // "new"
        if (listener)
            listener = B(CollisionListener_Construct(reinterpret_cast<uint32_t*>(listener)));
        Field<uint8_t*>(m, 4) = listener;
        if (listener)
            orig::fn_004ac5c0(Field<uint32_t>(m, 8), u(listener));
        uint8_t* filter2 = HkAlloc(8, 0xe);
        Field<uint16_t>(filter2, 4) = 8;
        Field<uint16_t>(filter2, 6) = 1;
        Field<uint32_t>(filter2, 0) = 0x005baa18;
        orig::fn_004ac700(u(filter2), Field<uint32_t>(m, 8));
        HkRelease(filter2);
        uint8_t* shapeMemory = HkAlloc(0x10, 0x1d);
        Field<uint16_t>(shapeMemory, 4) = 0x10;
        uint8_t* shape = B(orig::fn_0050f190(u(shapeMemory), 0x3dfcb924));  // radius 0.1234
        Field<uint8_t*>(m, 0xc) = shape;
        if (shape)
        {
            Field<uint32_t>(shape, 8) = 0xfffffffb;
            result = orig::fn_004a46c0(u(m + 0x10), 0x3dfcb924, 0x3f800000);
        }
    }
    m[0x60] = 0;
    if (g_physicsAfterInit)  // enhancement point (physics_hooks.h); null = original
        g_physicsAfterInit(m);
    return result;
}

static uint32_t PhysicsManager_Init_Abi(uint32_t ebx)
{
    return PhysicsManager_Init(B(ebx));
}
ABI_ENTRY_00411960(PhysicsManager_Init_Entry, PhysicsManager_Init_Abi)
HOOK(0x00411960, "PhysicsManager_Init", PhysicsManager_Init_Entry, nullptr);

// 0x00411cb0 (stack: manager; callee pops 4; callers 0x00403940, 0x00411950, 0x00411960): when initialised (+0):
// releases the shape +0xc, removes the game listener +4 from the world (0x004ac620 esi = world, edi = listener),
// destroys (vtable reset 0x004112c0) and frees it, releases the world +8, calls hkBaseSystem quit through
// [0x005cadcc]; then clears +0. Returns eax as left.
uint32_t PhysicsManager_Shutdown(uint8_t* m)
{
    uint32_t result = 0;
    if (m[0])
    {
        if (g_physicsBeforeShutdown)  // enhancement point (physics_hooks.h); null = original
            g_physicsBeforeShutdown(m);
        if (Field<uint8_t*>(m, 0xc))
        {
            HkRelease(Field<uint8_t*>(m, 0xc));
            Field<uint32_t>(m, 0xc) = 0;
        }
        if (Field<uint8_t*>(m, 8))
        {
            orig::fn_004ac620(Field<uint32_t>(m, 8), Field<uint32_t>(m, 4));
            if (Field<uint32_t*>(m, 4))
            {
                CollisionListener_Construct(Field<uint32_t*>(m, 4));
                Free(Field<uint32_t>(m, 4));
            }
            Field<uint32_t>(m, 4) = 0;
            HkRelease(Field<uint8_t*>(m, 8));
            Field<uint32_t>(m, 8) = 0;
        }
        result = reinterpret_cast<uint32_t (*)()>(GameGlobal<uint32_t>(0x005cadcc))();
    }
    m[0] = 0;
    return result;
}

static uint32_t PhysicsManager_Shutdown_Abi(uint32_t a0)
{
    return PhysicsManager_Shutdown(B(a0));
}
ABI_ENTRY_00411cb0(PhysicsManager_Shutdown_Entry, PhysicsManager_Shutdown_Abi)
HOOK(0x00411cb0, "PhysicsManager_Shutdown", PhysicsManager_Shutdown_Entry, nullptr);

// 0x00411d40 (ecx = substeps, edi = manager; stack: dt (float); callee pops 4; callers 0x0044a670, 0x0048c9b0):
// counts the step (Game+0x700), raises dt below 1/60 to 1/60 (0x3c888889). With a world and substeps > 0, each
// substep: gravity scaling (0x00403c70), the force actions of Game+0xa84 +0xf30 (0x00404140), a deferred refilter of
// the whole world (0x004ac910 esi = world) when +0x60 is set, and hkWorld_stepDeltaTime (0x004ad7b0, ecx = &{dt,
// 1/dt}; stack: world, &{dt, 1/dt}). Each substep uses the full dt. Returns eax as left.
// Enhancement diagnostics (hook/enhance/limits.cpp): game time handed to the physics step and the time simulated.
double g_physicsGameTime = 0.0, g_physicsSimTime = 0.0;
// Enhancement hook ([limits] physics_fixed_step): the original raises steps under 1/60 s to 1/60 s, so above 60 fps
// every frame simulates 1/60 s (x2.41 at 144 Hz, measured with the playground physics_rate scene). With this set, short
// frames add their time to an accumulator and the world steps whole 1/60 s steps (at most 5) once enough has built up.
bool g_physicsFixedStep = false;
double g_physicsAccumulator = 0.0;

uint32_t PhysicsManager_Step(uint32_t substeps, uint8_t* m, float dt)
{
    ++Field<int32_t>(g_game, 0x700);
    g_physicsGameTime += static_cast<double>(dt) * substeps;
    // Frames of 1/60 s and longer step as the original (their full time); shorter ones go through the accumulator.
    // Frame times hover around 1/60 s at 60 fps, so a small tolerance keeps both paths from losing steps.
    if (g_physicsFixedStep && dt < K(0x005bcc98) * 0.999f)
    {
        const double step = static_cast<double>(bf(0x3c888889));
        g_physicsAccumulator += static_cast<double>(dt) * substeps;
        uint32_t n = static_cast<uint32_t>(g_physicsAccumulator / step + 1e-3);
        if (n == 0)
            return Field<uint32_t>(m, 8);
        if (n > 5)
            n = 5;
        g_physicsAccumulator -= n * step;
        if (g_physicsAccumulator > step)
            g_physicsAccumulator = step;  // after a hitch: do not try to catch up more than one step
        if (g_physicsAccumulator < -step)
            g_physicsAccumulator = 0.0;
        substeps = n;
    }
    if (dt < K(0x005bcc98))
        dt = bf(0x3c888889);
    if (Field<uint32_t>(m, 8))
        g_physicsSimTime += static_cast<double>(dt) * substeps;
    if (!Field<uint32_t>(m, 8))
        return 0;
    if (!substeps)
        return Field<uint32_t>(m, 8);
    const float inverse = K(0x005bcb2c) / dt;
    uint32_t result = 0;
    do
    {
        Physics_ApplyGravityScale();
        uint8_t* manager = Field<uint8_t*>(g_game, 0xa84);
        if (manager && Field<uint32_t*>(manager, 0xf30))
            ForceActionPool_Update(Field<uint32_t*>(manager, 0xf30), dt);
        if (m[0x60])
        {
            orig::fn_004ac910(Field<uint32_t>(m, 8));
            m[0x60] = 0;
        }
        float stepInfo[2];
        stepInfo[0] = dt;
        stepInfo[1] = inverse;
        // Enhancement points (physics_hooks.h); both null = original.
        if (!(g_physicsBeforeSubstep && g_physicsBeforeSubstep(m, dt)))
            result = orig::fn_004ad7b0(u(stepInfo), Field<uint32_t>(m, 8), u(stepInfo));
        if (g_physicsAfterSubstep)
            g_physicsAfterSubstep(m, dt);
    } while (--substeps);
    return result;
}

static uint32_t PhysicsManager_Step_Abi(uint32_t ecx, uint32_t edi, uint32_t a0)
{
    return PhysicsManager_Step(ecx, B(edi), bf(a0));
}
ABI_ENTRY_00411d40(PhysicsManager_Step_Entry, PhysicsManager_Step_Abi)
HOOK(0x00411d40, "PhysicsManager_Step", PhysicsManager_Step_Entry, nullptr);

// 0x00411e10 (esi = object; caller 0x00411f70): releases the Havok object at +0x60 (u16 reference count +6) and
// frees +0x64 with the game's free (also when null). Returns the free's eax.
uint32_t fn_00411e10(uint8_t* object)
{
    if (Field<uint8_t*>(object, 0x60))
    {
        HkRelease(Field<uint8_t*>(object, 0x60));
        Field<uint32_t>(object, 0x60) = 0;
    }
    const uint32_t result = Free(Field<uint32_t>(object, 0x64));
    Field<uint32_t>(object, 0x64) = 0;
    return result;
}

static uint32_t fn_00411e10_Abi(uint32_t esi)
{
    return fn_00411e10(B(esi));
}
ABI_ENTRY_00411e10(fn_00411e10_Entry, fn_00411e10_Abi)
HOOK(0x00411e10, "fn_00411e10", fn_00411e10_Entry, nullptr);

// ================================================================================================================
// Collision skin node arrays (built by 0x00412450). Arrays come from the game's new[] (tag 0x10, "new[]" 0x0058b5e0)
// with the element count in the dword 0x10 bytes before the data. Outer element 0x80 bytes, inner element 0x70 bytes;
// both: byte +0 = 1, +0x10/+0x14 = 0, three 0x10-byte members at +0x30 built by the vector-constructor iterator
// 0x00403c40 with constructor 0x00404500, +0x60 Havok object (released by 0x00411e10), +0x64 game block (freed).
// Outer element: +0x74 game block, +0x78 inner count, +0x7c inner array.
// ================================================================================================================

// The element initialisation shared by 0x00411e50 (0x70 bytes) and 0x00411fd0 (0x80 bytes, three more dwords), in
// the original's store order.
static void SkinNode_InitElement(uint8_t* e, bool outer)
{
    e[0] = 1;
    Field<uint32_t>(e, 0x10) = 0;
    Field<uint32_t>(e, 0x14) = 0;
    orig::fn_00403c40(u(e + 0x30), 0x10, 3, 0x00404500);
    static const uint8_t order[] = {0x2c, 0x28, 0x24, 0x20, 0x3c, 0x38, 0x34, 0x30, 0x4c,
                                    0x48, 0x44, 0x40, 0x5c, 0x58, 0x54, 0x50, 0x60, 0x64};
    for (uint8_t offset : order)
        Field<uint32_t>(e, offset) = 0;
    if (outer)
    {
        Field<uint32_t>(e, 0x7c) = 0;
        Field<uint32_t>(e, 0x78) = 0;
        Field<uint32_t>(e, 0x74) = 0;
    }
}

// 0x00411e50 (eax = count; stack: outer element; callee pops 4; caller 0x00411fd0): outer +0x78 = count, allocates
// and initialises count inner elements (0x70 bytes) into +0x7c (0 when the allocation fails). Returns eax as left.
uint32_t SkinNode_AllocChildren(int32_t count, uint8_t* node)
{
    Field<int32_t>(node, 0x78) = count;
    uint8_t* memory = B(Alloc(0x10, static_cast<uint32_t>(count) * 0x70 + 0x10, 2, 0x0058b5e0));
    if (!memory)
    {
        Field<uint32_t>(node, 0x7c) = 0;
        return 0;
    }
    uint8_t* data = memory + 0x10;
    Field<int32_t>(memory, 0) = count;
    int32_t remaining = count - 1;
    if (remaining >= 0)
    {
        remaining = count;
        uint8_t* e = data;
        do
        {
            SkinNode_InitElement(e, false);
            e += 0x70;
        } while (--remaining);
    }
    Field<uint8_t*>(node, 0x7c) = data;
    return static_cast<uint32_t>(remaining);
}

static uint32_t SkinNode_AllocChildren_Abi(uint32_t eax, uint32_t a0)
{
    return SkinNode_AllocChildren(static_cast<int32_t>(eax), B(a0));
}
ABI_ENTRY_00411e50(SkinNode_AllocChildren_Entry, SkinNode_AllocChildren_Abi)
HOOK(0x00411e50, "SkinNode_AllocChildren", SkinNode_AllocChildren_Entry, nullptr);

// 0x00411f70 (ecx = outer element; caller 0x004120f0): destroys the inner elements from the last (0x00411e10 each),
// frees the inner array, frees +0x74 (also when null) and runs 0x00411e10 on the outer element itself.
uint32_t SkinNode_Destroy(uint8_t* node)
{
    uint8_t* data = Field<uint8_t*>(node, 0x7c);
    if (data)
    {
        const int32_t count = Field<int32_t>(data - 0x10, 0);
        uint8_t* e = data + count * 0x70;
        if (count - 1 >= 0)
        {
            for (int32_t n = count; n; --n)
            {
                e -= 0x70;
                fn_00411e10(e);
            }
        }
        Free(u(data - 0x10));
    }
    const uint32_t block = Field<uint32_t>(node, 0x74);
    Field<uint32_t>(node, 0x7c) = 0;
    Free(block);
    Field<uint32_t>(node, 0x74) = 0;
    return fn_00411e10(node);
}

static uint32_t SkinNode_Destroy_Abi(uint32_t ecx)
{
    return SkinNode_Destroy(B(ecx));
}
ABI_ENTRY_00411f70(SkinNode_Destroy_Entry, SkinNode_Destroy_Abi)
HOOK(0x00411f70, "SkinNode_Destroy", SkinNode_Destroy_Entry, nullptr);

// 0x00411fd0 (stack: holder, source, last index; callee pops 0xc; caller 0x00412450): count = last + 1 into
// holder +0; allocates and initialises count outer elements (0x80 bytes) into holder +4 (0 when the allocation
// fails), then gives each outer element source +0x80 inner elements (0x00411e50). Returns the holder.
uint32_t SkinNode_AllocArray(uint32_t* holder, uint8_t* source, int32_t last)
{
    const int32_t count = last + 1;
    holder[0] = static_cast<uint32_t>(count);
    uint8_t* memory = B(Alloc(0x10, static_cast<uint32_t>(count) * 0x80 + 0x10, 2, 0x0058b5e0));
    uint8_t* data = nullptr;
    if (memory)
    {
        data = memory + 0x10;
        Field<int32_t>(memory, 0) = count;
        if (count - 1 >= 0)
        {
            uint8_t* e = data;
            for (int32_t n = count; n; --n)
            {
                SkinNode_InitElement(e, true);
                e += 0x80;
            }
        }
    }
    holder[1] = u(data);
    if (last > -1)
    {
        int32_t offset = -0x80;
        for (int32_t n = count; n; --n)
        {
            SkinNode_AllocChildren(Field<int32_t>(source, 0x80), B(holder[1] + offset + 0x80));
            offset += 0x80;
        }
    }
    return u(holder);
}

static uint32_t SkinNode_AllocArray_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SkinNode_AllocArray(reinterpret_cast<uint32_t*>(a0), B(a1), static_cast<int32_t>(a2));
}
ABI_ENTRY_00411fd0(SkinNode_AllocArray_Entry, SkinNode_AllocArray_Abi)
HOOK(0x00411fd0, "SkinNode_AllocArray", SkinNode_AllocArray_Entry, nullptr);

// 0x004120f0 (stack: holder; callee pops 4; callers 0x0045a860, 0x0045aad0): destroys the outer elements of holder
// +4 from the last (0x00411f70), frees the array and clears +4. Returns eax as left.
uint32_t SkinNode_FreeArray(uint32_t* holder)
{
    uint8_t* data = B(holder[1]);
    if (!data)
        return 0;
    const int32_t count = Field<int32_t>(data - 0x10, 0);
    uint8_t* e = data + count * 0x80;
    if (count - 1 >= 0)
    {
        for (int32_t n = count; n; --n)
        {
            e -= 0x80;
            SkinNode_Destroy(e);
        }
    }
    const uint32_t result = Free(u(data - 0x10));
    holder[1] = 0;
    return result;
}

static uint32_t SkinNode_FreeArray_Abi(uint32_t a0)
{
    return SkinNode_FreeArray(reinterpret_cast<uint32_t*>(a0));
}
ABI_ENTRY_004120f0(SkinNode_FreeArray_Entry, SkinNode_FreeArray_Abi)
HOOK(0x004120f0, "SkinNode_FreeArray", SkinNode_FreeArray_Entry, nullptr);

// ================================================================================================================
// Physics materials
// ================================================================================================================

// 0x00412140 (cdecl; stack: name, material, useDefaults (byte); callers 0x004122f0, 0x0042cb80, 0x004517b0;
// strings "%s_friction" ...): reads the "global_settings" object (0x0045abe0, ecx = name, esi = Game+0xa84) and
// its table (+0x10 +0x3c; none: nothing). For each property the name "<name>_<property>" (sprintf into a 0x40-byte
// buffer) is looked up with 0x0045df00 (floats) / 0x0045df30 (bool) (eax = table +0, ecx = key, esi = table +8;
// stack: Game+0xa84, &value); when absent and useDefaults is set, "default_<property>" is used:
// friction -> +8, restitution -> +4, density -> +0, rayCastDisable -> +0x10. Returns eax as left.
uint32_t PhysMaterial_Load(const char* name, uint8_t* material, uint32_t useDefaults)
{
    char key[0x40];
    uint8_t* settings = B(orig::fn_0045abe0(0x005bab84, Field<uint32_t>(g_game, 0xa84)));  // "global_settings"
    if (!settings)
        return 0;
    uint32_t* table = Field<uint32_t*>(Field<uint8_t*>(settings, 0x10), 0x3c);
    if (!table)
        return 0;
    struct Property
    {
        uint32_t format, fallback, offset;
        bool isBool;
    };
    static const Property properties[] = {
        {0x005bab78, 0x005bab64, 8, false},     // "%s_friction" / "default_friction"
        {0x005bab54, 0x005bab40, 4, false},     // "%s_restitution" / "default_restitution"
        {0x005bab34, 0x005bab24, 0, false},     // "%s_density" / "default_density"
        {0x005bab10, 0x005baaf8, 0x10, true}};  // "%s_rayCastDisable" / "default_rayCastDisable"
    uint32_t result = 0;
    for (const Property& p : properties)
    {
        orig::fn_0053814f(u(key), p.format, u(name));
        const uint32_t a84 = Field<uint32_t>(g_game, 0xa84);
        if (p.isBool)
            result = orig::fn_0045df30(table[0], u(key), table[2], a84, u(material + p.offset));
        else
            result = orig::fn_0045df00(table[0], u(key), table[2], a84, u(material + p.offset));
        if (!(result & 0xff) && static_cast<uint8_t>(useDefaults))
        {
            const uint32_t a84b = Field<uint32_t>(g_game, 0xa84);
            if (p.isBool)
                result = orig::fn_0045df30(table[0], p.fallback, table[2], a84b, u(material + p.offset));
            else
                result = orig::fn_0045df00(table[0], p.fallback, table[2], a84b, u(material + p.offset));
        }
    }
    return result;
}

static uint32_t PhysMaterial_Load_Abi(uint32_t a0, uint32_t a1, uint32_t a2)
{
    return PhysMaterial_Load(reinterpret_cast<const char*>(a0), B(a1), a2);
}
ABI_ENTRY_00412140(PhysMaterial_Load_Entry, PhysMaterial_Load_Abi)
HOOK(0x00412140, "PhysMaterial_Load", PhysMaterial_Load_Entry, nullptr);

// 0x004122f0 (ecx = mesh part; stack: owner object or 0; caller pops; callers 0x00412fd0, 0x00413450): the
// part's material (+8) is loaded once (bit 0 of its +0): friction / restitution / density = -1, +0x10 name = 0,
// rayCastDisable = 0. With a material index (u16 +0x14): name = 0x00489630(eax = index, ecx = part +4 +0x70) into
// material +0x10, its prefix up to '_' copied into a 0x80-byte buffer and loaded (PhysMaterial_Load, defaults
// only without owner); without an index and without owner, "default" is loaded. Values left negative come from
// the owner (friction 0x00458260, restitution 0x00458240, density owner +0x38c) or default to 0, 0 and 1.0.
// Returns the material.
uint32_t MeshPart_LoadMaterial(uint8_t* part, uint8_t* owner)
{
    uint8_t buffer[0x80];
    uint8_t* material = Field<uint8_t*>(part, 8);
    if (!material || (Field<uint32_t>(material, 0) & 1))
        return u(material);
    Field<uint32_t>(material, 0) |= 1;
    uint8_t* values = material + 4;
    Field<uint32_t>(values, 8) = 0xbf800000;  // -1.0f
    Field<uint32_t>(values, 4) = 0xbf800000;
    Field<uint32_t>(values, 0) = 0xbf800000;
    Field<uint32_t>(values, 0xc) = 0;
    values[0x10] = 0;
    const uint16_t index = Field<uint16_t>(part, 0x14);
    const char* name = nullptr;
    if (index)
    {
        const char* full = reinterpret_cast<const char*>(
            orig::fn_00489630(index, Field<uint32_t>(Field<uint8_t*>(part, 4), 0x70)));
        Field<const char*>(material, 0x10) = full;
        int32_t n = 0;
        for (char c = full[0]; c && c != '_'; c = full[++n])
            buffer[n] = static_cast<uint8_t>(c);
        buffer[n] = 0;
        name = reinterpret_cast<const char*>(buffer);
    }
    else if (!owner)
    {
        name = reinterpret_cast<const char*>(0x005baaf0);  // "default"
    }
    if (name)
        PhysMaterial_Load(name, values, owner == nullptr ? 1u : 0u);
    if (Field<float>(values, 8) < K(0x0056c05c))
    {
        if (owner)
            Field<float>(values, 8) = orig::fn_00458260(u(owner));
        else
            Field<uint32_t>(values, 8) = 0;
    }
    if (Field<float>(values, 4) < K(0x0056c05c))
    {
        if (owner)
            Field<float>(values, 4) = orig::fn_00458240(u(owner));
        else
            Field<uint32_t>(values, 4) = 0;
    }
    if (Field<float>(values, 0) < K(0x0056c05c))
    {
        if (owner)
            Field<uint32_t>(values, 0) = Field<uint32_t>(owner, 0x38c);
        else
            Field<uint32_t>(values, 0) = 0x3f800000;  // 1.0f
    }
    return u(material);
}

static uint32_t MeshPart_LoadMaterial_Abi(uint32_t ecx, uint32_t a0)
{
    return MeshPart_LoadMaterial(B(ecx), B(a0));
}
ABI_ENTRY_004122f0(MeshPart_LoadMaterial_Entry, MeshPart_LoadMaterial_Abi)
HOOK(0x004122f0, "MeshPart_LoadMaterial", MeshPart_LoadMaterial_Entry, nullptr);

// 0x00412420 (ecx = skin mesh shape; stack: a, b; callee pops 8; slot +0x28 of vtable 0x005ba9d0, the getTriangle
// override; not executed in recorded runs): calls the Havok mesh shape's getTriangle 0x0050f750 and tags the
// returned triangle: +0x48 = this shape, +0x4c = 0xf123abcf.
uint32_t SkinMeshShape_GetTriangle(uint8_t* shape, uint32_t a, uint32_t b)
{
    uint8_t* triangle = B(orig::fn_0050f750(u(shape), a, b));
    Field<uint8_t*>(triangle, 0x48) = shape;
    Field<uint32_t>(triangle, 0x4c) = 0xf123abcf;
    return u(triangle);
}

static uint32_t SkinMeshShape_GetTriangle_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return SkinMeshShape_GetTriangle(B(ecx), a0, a1);
}
ABI_ENTRY_00412420(SkinMeshShape_GetTriangle_Entry, SkinMeshShape_GetTriangle_Abi)
HOOK(0x00412420, "SkinMeshShape_GetTriangle", SkinMeshShape_GetTriangle_Entry, nullptr);

// ================================================================================================================
// Level collision skin build
// ================================================================================================================

// 0x004bc730 (qhull entry inside Havok, cdecl): the caller pushes five arguments and pops 0x14, although
// symbols/abi.csv lists four; this shim passes all five.
static uint32_t Qhull_Run(uint32_t points, uint32_t count, uint32_t stride, uint32_t outVertices, uint32_t outPlanes)
{
    static const uint32_t target = 0x004bc730;
    uint32_t result;
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        push outPlanes
        push outVertices
        push stride
        push count
        push points
        call dword ptr [target]
        add esp, 0x14
        pop ebp
        pop edi
        pop esi
        pop ebx
        mov result, eax
    }
    return result;
}

// 0x00412450 CollisionSkin_build (eax = mesh object (+0x40 template count, +0x48 per-template scales of 12 bytes);
// stack: unused, skin, part, layer, &hullFlag, makeHull (byte), unused, unused, template; callee pops 0x24; callers
// 0x00413410, 0x0045bf70). Skin: +0 node holder (0x00411fd0, created on first use with count = template count + 1;
// a different count prints "Error: Skin used by more than one template but with different scales"), +4 name object
// (+0x34 name), +0x10/+0x24/+0x28 vertex count / format / positions, +0x40/+0x44/+0x48 vertex count / deformer /
// index map, +0x80 part count, +0x84 parts (0x2c bytes). The node is the outer element of template + 1 (part < 0) or
// its inner element `part`; an already built node (+0x60) is returned as is. Collision filter info =
// layer << 16 | (part & 0xffff).
// makeHull: the parts' vertices (0x004ef990) scaled by the template scale go through the hull builder 0x004ba8d0 and
// qhull (0x004bc730) into a convex vertices shape (0x00510aa0, 0xe0 bytes, radius 0.1); no hull clears *hullFlag.
// Otherwise (or when no hull came out) a mesh shape (0x0050f900, vtable 0x005ba9d0, radius 0.1) gets the template's
// world vertices (cached in outer +0x74, 0x004effa0 or copied through the index map), the scaling and one subpart per
// part with the triangles 0x004efab0 accepts (16-bit indices in node +0x64); a MOPP code is taken from the level's
// cache (0x0044ad40 / 0x004479d0 / 0x00447cb0, key "*name*layer*part*template") or built (0x00510c90, 0x00510cf0),
// and wrapped in a MOPP bv-tree shape (0x00510f50, +8 = -5). The shape goes into node +0x60. With *hullFlag set the
// node's bounds (+0x10..+0x5c) come from 0x004a62f0 (identity transform 0x005cad50) and are cleared again when +0x10
// is 0; otherwise the shape's AABB (virtual +0xc) gives the half extents (0x004a5160 into +0x10) and the centre
// (+0x20). Returns the node (0 when the holder cannot be allocated).
uint32_t CollisionSkin_Build(uint8_t* meshObject, uint32_t /*unused0*/, uint8_t* skin, int32_t part, uint32_t layer,
                             uint8_t* hullFlag, uint32_t makeHull, uint32_t /*unused6*/, uint32_t /*unused7*/,
                             int32_t templateIndex)
{
    alignas(16) uint8_t f[0x200];  // the original's frame (offsets from esp after the three register pushes)
    auto L = [&f](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + offset); };
    auto LF = [&f](uint32_t offset) -> float& { return *reinterpret_cast<float*>(f + offset); };
    std::memset(f, 0, sizeof(f));
    void* const memory = GameGlobal<void*>(0x005fed84);  // hkMemory singleton

    int32_t templateCount = 0;
    L(0x24) = 0;
    L(0x58) = 0x3f800000;
    L(0x54) = 0x3f800000;
    L(0x50) = 0x3f800000;
    if (meshObject)
    {
        templateCount = Field<int32_t>(meshObject, 0x40);
        if (templateIndex >= 0)
        {
            const uint32_t* scale = reinterpret_cast<const uint32_t*>(Field<uint8_t*>(meshObject, 0x48) +
                                                                      templateIndex * 12);
            L(0x50) = scale[0];
            L(0x54) = scale[1];
            L(0x58) = scale[2];
        }
    }
    if (!Field<uint32_t>(skin, 0))
    {
        const uint32_t holderMemory = Alloc(0x10, 8, 2, 0x0058b5e8);  // "new"
        uint32_t holder = 0;
        if (holderMemory)
            holder = SkinNode_AllocArray(reinterpret_cast<uint32_t*>(holderMemory), skin, templateCount);
        Field<uint32_t>(skin, 0) = holder;
        if (!holder)
            return 0;
    }
    uint32_t* holder = Field<uint32_t*>(skin, 0);
    if (templateCount != static_cast<int32_t>(holder[0] - 1))
        reinterpret_cast<void(__cdecl*)(uint32_t, const char*, ...)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), reinterpret_cast<const char*>(0x005baaa8));
    uint8_t* outer = B(Field<uint32_t*>(skin, 0)[1] + static_cast<uint32_t>(templateIndex + 1) * 0x80);
    L(0x4c) = u(outer);
    uint8_t* node;
    if (part >= 0)
    {
        L(0x28) = static_cast<uint32_t>(part);
        L(0x2c) = static_cast<uint32_t>(part + 1);
        node = Field<uint8_t*>(outer, 0x7c) + part * 0x70;
    }
    else
    {
        L(0x28) = 0;
        L(0x2c) = Field<uint32_t>(skin, 0x80);
        node = outer;
    }
    if (Field<uint32_t>(node, 0x60) || !Field<uint32_t>(skin, 0x84))
        return u(node);
    const uint32_t filterInfo = (layer << 16) | (static_cast<uint32_t>(part) & 0xffff);
    uint8_t* shape = nullptr;  // ecx at 0x00412bb7
    bool haveShape = false;
    if (static_cast<uint8_t>(makeHull))
    {
        uint8_t* parts = Field<uint8_t*>(skin, 0x84);
        const uint32_t first = Field<uint32_t>(parts + L(0x28) * 0x2c, 0x14);
        uint8_t* lastPart = parts + L(0x2c) * 0x2c - 0x2c;
        const uint32_t count = Field<uint32_t>(lastPart, 0x18) + Field<uint32_t>(lastPart, 0x14) - first;
        L(0x40) = first;
        const uint32_t vertices = VCall<uint32_t>(memory, 0, count << 4, 8u);
        L(0x20) = vertices;
        if (vertices)
        {
            orig::fn_00403c40(vertices, 0x10, count, 0x00404500);
            L(0x1c) = L(0x20);
        }
        else
        {
            L(0x1c) = 0;
        }
        L(0x18) = 0;
        if (count)
        {
            L(0x20) = L(0x1c) + 8;
            do
            {
                orig::fn_004ef990(u(f + 0x30), u(skin), L(0x18) + L(0x40));
                float* v = reinterpret_cast<float*>(L(0x20) - 8);
                v[0] = LF(0x30) * LF(0x50);
                L(0x18) = L(0x18) + 1;
                L(0x20) = L(0x20) + 0x10;
                v[1] = LF(0x34) * LF(0x54);
                v[2] = LF(0x38) * LF(0x58);
            } while (L(0x18) < count);
        }
        L(0x30) = 0x3a83126f;  // 0.001
        L(0x34) = 0x3c23d70a;  // 0.01
        L(0x38) = 0x3ca3d70a;  // 0.02
        L(0x3c) = 0x3f7df3b6;  // 0.992
        uint8_t* hull = B(orig::fn_004ba8d0(count, L(0x1c), u(f + 0x30), 0x3c23d70a));
        if (!hull)
        {
            *hullFlag = 0;
        }
        else
        {
            // Two hkArrays {data, size, capacity | 0x80000000 (not owned)} for qhull's vertices and planes.
            L(0x38) = 0x80000000;
            L(0x48) = 0x80000000;
            L(0x30) = 0;
            L(0x34) = 0;
            L(0x40) = 0;
            L(0x44) = 0;
            Qhull_Run(Field<uint32_t>(hull, 0xc), Field<uint32_t>(hull, 8), 0x10, u(f + 0x30), u(f + 0x40));
            uint8_t* convex = VCall<uint8_t*>(memory, 0x10 / 4, 0xe0u, 0x1du);
            Field<uint16_t>(convex, 4) = 0xe0;
            convex = B(orig::fn_00510aa0(u(convex), L(0x40), 0x10, L(0x44), u(f + 0x30)));
            L(0x24) = u(convex);
            Field<uint32_t>(convex, 0xc) = 0x3dcccccd;  // 0.1
            Field<uint32_t>(convex, 8) = filterInfo;
            orig::fn_00412f00(u(hull), 1);
            if (static_cast<int32_t>(L(0x48)) >= 0)
                VCall<void>(memory, 0x14 / 4, L(0x40), (L(0x48) & 0x7fffffff) << 4, 0x10u);
            if (static_cast<int32_t>(L(0x38)) >= 0)
                VCall<void>(memory, 0x14 / 4, L(0x30), (L(0x38) & 0x7fffffff) << 4, 0x10u);
        }
        VCall<void>(memory, 1, L(0x1c));
        if (L(0x24))
        {
            shape = B(L(0x24));
            haveShape = true;
        }
    }
    if (!haveShape)
    {
        // World vertices of this template, cached in the outer node +0x74.
        if (!Field<uint32_t>(outer, 0x74))
        {
            Field<uint32_t>(outer, 0x74) = Alloc(0x10, Field<uint32_t>(skin, 0x40) * 12, 2, 0x0058b5e0);
            L(0x18) = 0;
            if (Field<int32_t>(skin, 0x40) > 0)
            {
                L(0x1c) = 0;
                do
                {
                    float* out = reinterpret_cast<float*>(Field<uint32_t>(B(L(0x4c)), 0x74) + L(0x1c));
                    uint8_t* deformer = Field<uint8_t*>(skin, 0x44);
                    if (deformer)
                    {
                        orig::fn_004effa0(u(out), u(deformer + 0xac), u(deformer + 0x20), u(deformer + 0x70), L(0x18),
                                          u(deformer + 0x10));
                    }
                    else
                    {
                        const uint32_t index = Field<uint32_t*>(skin, 0x48)[L(0x18)];
                        const uint32_t* source =
                            reinterpret_cast<const uint32_t*>(Field<uint8_t*>(skin, 0x28) + index * 12);
                        reinterpret_cast<uint32_t*>(out)[0] = source[0];
                        reinterpret_cast<uint32_t*>(out)[1] = source[1];
                        reinterpret_cast<uint32_t*>(out)[2] = source[2];
                    }
                    L(0x18) = L(0x18) + 1;
                    L(0x1c) = L(0x1c) + 12;
                } while (static_cast<int32_t>(L(0x18)) < Field<int32_t>(skin, 0x40));
            }
        }
        uint8_t* mesh = VCall<uint8_t*>(memory, 0x10 / 4, 0x50u, 0x1du);
        L(0x40) = u(mesh);
        if (mesh)
        {
            orig::fn_0050f900(u(mesh));
            mesh = B(L(0x40));
            Field<uint32_t>(mesh, 0) = 0x005ba9d0;  // skin mesh shape (getTriangle override 0x00412420)
            L(0x1c) = u(mesh);
        }
        else
        {
            L(0x1c) = 0;
        }
        Field<uint32_t>(mesh, 8) = filterInfo;
        Field<uint32_t>(mesh, 0x20) = 0x3dcccccd;  // 0.1
        L(0x24) = 0;
        if (static_cast<int32_t>(L(0x28)) < static_cast<int32_t>(L(0x2c)))
        {
            L(0x18) = L(0x28) * 0x2c + Field<uint32_t>(skin, 0x84);
            L(0x20) = L(0x2c) - L(0x28);
            do
            {
                const uint32_t triangles = orig::fn_004efa50(L(0x18));
                L(0x24) = triangles + L(0x24) + triangles * 2;
                L(0x18) = L(0x18) + 0x2c;
                L(0x20) = L(0x20) - 1;
            } while (L(0x20));
        }
        Field<uint32_t>(node, 0x64) = Alloc(0x10, L(0x24) * 2, 2, 0x0058b5e0);
        L(0x80) = L(0x50);
        L(0x84) = L(0x54);
        L(0x88) = L(0x58);
        L(0x8c) = 0x3f800000;
        orig::fn_0050f6f0(L(0x1c), u(f + 0x80));
        L(0x24) = 0;
        if (static_cast<int32_t>(L(0x28)) < static_cast<int32_t>(L(0x2c)))
        {
            L(0x20) = L(0x2c) - L(0x28);
            L(0x28) = L(0x28) * 0x2c;
            do
            {
                uint8_t* p = Field<uint8_t*>(skin, 0x84) + L(0x28);
                auto triangleCount = [p]() -> uint32_t {
                    if (Field<uint8_t*>(p, 0x24))
                        return Field<uint32_t>(Field<uint8_t*>(p, 0x24), 0x10) - 2;
                    return static_cast<uint32_t>(Field<int32_t>(p, 0x20) / 3);
                };
                L(0x2c) = 0;  // skipped triangles
                const uint32_t total = triangleCount();
                L(0x18) = 0;
                if (total)
                {
                    do
                    {
                        if (orig::fn_004efab0(u(p), u(f + 0x40), L(0x18)) & 0xff)
                        {
                            uint16_t* indices = Field<uint16_t*>(node, 0x64);
                            uint32_t i = L(0x24);
                            indices[i] = Field<uint16_t>(f, 0x40);
                            indices = Field<uint16_t*>(node, 0x64);
                            ++i;
                            indices[i] = Field<uint16_t>(f, 0x42);
                            indices = Field<uint16_t*>(node, 0x64);
                            ++i;
                            indices[i] = Field<uint16_t>(f, 0x44);
                            L(0x24) = i + 1;
                        }
                        else
                        {
                            L(0x2c) = L(0x2c) + 1;
                        }
                        L(0x18) = L(0x18) + 1;
                    } while (L(0x18) < total);
                }
                const uint32_t skipped = L(0x2c);
                // hkMeshShape::Subpart at +0x50: vertices, stride, count, indices, index type (low byte 0), index
                // stride 6, triangle count.
                uint32_t vertexCount;
                if (Field<uint32_t>(B(L(0x4c)), 0x74))
                {
                    L(0x50) = Field<uint32_t>(B(L(0x4c)), 0x74);
                    vertexCount = Field<uint32_t>(skin, 0x40);
                    L(0x54) = 0xc;
                }
                else
                {
                    L(0x50) = Field<uint32_t>(skin, 0x28);
                    L(0x54) = GameGlobal<uint32_t>(0x005fb8ac + Field<uint32_t>(skin, 0x24) * 4);
                    vertexCount = Field<uint32_t>(skin, 0x10);
                }
                L(0x58) = vertexCount;
                const uint32_t delta = skipped - triangleCount();
                L(0x5c) = Field<uint32_t>(node, 0x64) + (delta + (L(0x24) + delta * 2)) * 2;
                L(0x60) = L(0x60) & 0xffffff00;
                L(0x64) = 6;
                L(0x68) = triangleCount() - skipped;
                orig::fn_0050f8a0(L(0x1c), u(f + 0x50));
                L(0x28) = L(0x28) + 0x2c;
                L(0x20) = L(0x20) - 1;
            } while (L(0x20));
        }
        // MOPP code: from the level's cache, else built.
        L(0x20) = orig::fn_0044ad40(Field<uint32_t>(g_game, 0xa64));
        reinterpret_cast<int(__cdecl*)(char*, const char*, ...)>(0x0053814f)(
            reinterpret_cast<char*>(f + 0x100), reinterpret_cast<const char*>(0x005baa94),
            Field<uint32_t>(skin, 4) + 0x34, layer, static_cast<uint32_t>(part), templateIndex);
        L(0x50) = 0;
        L(0x54) = 0;
        L(0x58) = 0;
        L(0x5c) = u(f + 0x100);
        bool cached = false;
        if (!(orig::fn_004479d0(L(0x20)) & 0xff))
        {
            if (orig::fn_00447cb0(L(0x20), u(f + 0x50)) & 0xff)
            {
                const uint32_t code = L(0x54);
                L(0x18) = code;
                orig::fn_00510c10(code);
                if (code)
                    cached = true;
            }
        }
        if (!cached)
        {
            orig::fn_00510c90(u(f + 0x50));
            L(0x18) = orig::fn_00510cf0(L(0x1c), u(f + 0x50));
        }
        uint8_t* meshShape = B(L(0x1c));
        uint8_t* bvMemory = VCall<uint8_t*>(memory, 0x10 / 4, 0x18u, 0x1du);
        Field<uint16_t>(bvMemory, 4) = 0x18;
        uint8_t* bvTree = B(orig::fn_00510f50(u(bvMemory), u(meshShape), L(0x18)));
        L(0x40) = u(bvTree);
        Field<uint32_t>(bvTree, 8) = 0xfffffffb;
        orig::fn_00510c40(L(0x18));
        HkRelease(meshShape);
        L(0x24) = L(0x40);
        shape = B(L(0x40));
    }
    Field<uint8_t*>(node, 0x60) = shape;
    if (*hullFlag)
    {
        for (uint32_t i = 0; i < 12; ++i)
            L(0x80 + i * 4) = GameGlobal<uint32_t>(0x005cad50 + i * 4);
        static const uint8_t order[] = {0x2c, 0x28, 0x24, 0x20, 0x3c, 0x38, 0x34, 0x30,
                                        0x4c, 0x48, 0x44, 0x40, 0x5c, 0x58, 0x54, 0x50};
        for (uint8_t offset : order)
            Field<uint32_t>(node, offset) = 0;
        L(0xbc) = 0;
        L(0xb8) = 0;
        L(0xb4) = 0;
        L(0xb0) = 0;
        Field<uint32_t>(node, 0x14) = 0;
        Field<uint32_t>(node, 0x10) = 0;
        orig::fn_004a62f0(u(shape), u(f + 0x80), u(node + 0x10));
        if (Field<float>(node, 0x10) == K(0x0056c05c))
        {
            for (uint8_t offset : order)
                Field<uint32_t>(node, offset) = 0;
            Field<uint32_t>(node, 0x14) = 0;
            Field<uint32_t>(node, 0x10) = 0;
        }
        return u(node);
    }
    for (uint32_t i = 0; i < 12; ++i)
        L(0xc0 + i * 4) = GameGlobal<uint32_t>(0x005cad50 + i * 4);
    L(0xfc) = 0;
    L(0xf8) = 0;
    L(0xf4) = 0;
    L(0xf0) = 0;
    VCall<void>(shape, 0xc / 4, u(f + 0xc0), 0u, u(f + 0x80));  // getAabb(transform, tolerance 0, &aabb)
    const float d0 = LF(0x90) - LF(0x80);
    const float d1 = LF(0x94) - LF(0x84);
    LF(0x38) = LF(0x98) - LF(0x88);
    LF(0x3c) = LF(0x9c) - LF(0x8c);
    LF(0x30) = d0 * K(0x005bcb4c);
    LF(0x34) = d1 * K(0x005bcb4c);
    LF(0x38) = LF(0x38) * K(0x005bcb4c);
    LF(0x3c) = LF(0x3c) * K(0x005bcb4c);
    LF(0x50) = LF(0x80) + LF(0x30);
    LF(0x54) = LF(0x84) + LF(0x34);
    LF(0x58) = LF(0x88) + LF(0x38);
    LF(0x5c) = LF(0x8c) + LF(0x3c);
    orig::fn_004a5160(u(node + 0x10), u(f + 0x30), 0x3f800000);
    Field<uint32_t>(node, 0x20) = L(0x50);
    Field<uint32_t>(node, 0x24) = L(0x54);
    Field<uint32_t>(node, 0x28) = L(0x58);
    Field<uint32_t>(node, 0x2c) = L(0x5c);
    return u(node);
}

static uint32_t CollisionSkin_Build_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                        uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8)
{
    return CollisionSkin_Build(B(eax), a0, B(a1), static_cast<int32_t>(a2), a3, B(a4), a5, a6, a7,
                               static_cast<int32_t>(a8));
}
ABI_ENTRY_00412450(CollisionSkin_Build_Entry, CollisionSkin_Build_Abi)
HOOK(0x00412450, "CollisionSkin_Build", CollisionSkin_Build_Entry, nullptr);

// 0x00412f00 (esi = hull from 0x004ba8d0; stack: flags; callee pops 4; caller 0x00412450): frees the owned arrays
// (+0xc when byte +0x18, +0x14 when byte +0x19) with hkMemory virtual +4 and, with flags bit 0, the hull itself
// (virtual +0x14(hull, 0x1c, 6)). Returns the hull.
uint32_t Hull_Delete(uint8_t* hull, uint32_t flags)
{
    void* memory = GameGlobal<void*>(0x005fed84);
    if (hull[0x18])
        VCall<void>(memory, 1, Field<uint32_t>(hull, 0xc));
    if (hull[0x19])
        VCall<void>(GameGlobal<void*>(0x005fed84), 1, Field<uint32_t>(hull, 0x14));
    if (flags & 1)
        VCall<void>(GameGlobal<void*>(0x005fed84), 0x14 / 4, hull, 0x1cu, 6u);
    return u(hull);
}

static uint32_t Hull_Delete_Abi(uint32_t esi, uint32_t a0)
{
    return Hull_Delete(B(esi), a0);
}
ABI_ENTRY_00412f00(Hull_Delete_Entry, Hull_Delete_Abi)
HOOK(0x00412f00, "Hull_Delete", Hull_Delete_Entry, nullptr);

// 0x00412f50 (ecx = skin mesh shape; stack: flags; callee pops 4; slot 0 of vtable 0x005ba9d0, the scalar deleting
// destructor; listed caller 0x00412450): frees the subpart hkArray (+0x24 data, +0x2c capacity, 0x1c-byte
// elements, class 0x10) unless it is not owned (sign bit), restores the base vtable 0x0058e3c4 and, with flags bit 0,
// frees the shape (0x50 bytes, class 0x1d). Returns the shape.
uint32_t SkinMeshShape_Delete(uint8_t* shape, uint32_t flags)
{
    const int32_t capacity = Field<int32_t>(shape, 0x2c);
    if (capacity >= 0)
        VCall<void>(GameGlobal<void*>(0x005fed84), 0x14 / 4, Field<uint32_t>(shape, 0x24),
                    (static_cast<uint32_t>(capacity) & 0x7fffffff) * 0x1c, 0x10u);
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    if (flags & 1)
        VCall<void>(GameGlobal<void*>(0x005fed84), 0x14 / 4, shape, 0x50u, 0x1du);
    return u(shape);
}

static uint32_t SkinMeshShape_Delete_Abi(uint32_t ecx, uint32_t a0)
{
    return SkinMeshShape_Delete(B(ecx), a0);
}
ABI_ENTRY_00412f50(SkinMeshShape_Delete_Entry, SkinMeshShape_Delete_Abi)
HOOK(0x00412f50, "SkinMeshShape_Delete", SkinMeshShape_Delete_Entry, nullptr);

// 0x00412fa0 (ecx = mesh shape; caller: Havok 0x0050f940): the mesh shape destructor body (subpart array free as in
// 0x00412f50, base vtable 0x0058e3c4). Shared with Havok's hkMeshShape through identical-code folding (guess); not
// executed in recorded runs. Returns eax as left.
uint32_t MeshShape_Destruct(uint8_t* shape)
{
    const int32_t capacity = Field<int32_t>(shape, 0x2c);
    uint32_t result = static_cast<uint32_t>(capacity);
    if (capacity >= 0)
        result = VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, Field<uint32_t>(shape, 0x24),
                                 (static_cast<uint32_t>(capacity) & 0x7fffffff) * 0x1c, 0x10u);
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    return result;
}

static uint32_t MeshShape_Destruct_Abi(uint32_t ecx)
{
    return MeshShape_Destruct(B(ecx));
}
ABI_ENTRY_00412fa0(MeshShape_Destruct_Entry, MeshShape_Destruct_Abi)
HOOK(0x00412fa0, "MeshShape_Destruct", MeshShape_Destruct_Entry, nullptr);

// 0x00412fd0 (eax = triangle, ecx = part; stack: owner object, model index, subpart; caller pops; callers
// 0x00413120, 0x004138c0): finds the collision material of a contact. Model list: the owner's (0x00453100 then
// 0x00453120(eax = owner, edx = that; 0)) or the level's (Game+0xa60 +0xf4c). The model is list +0x18[index] +4;
// its part is `part`, else `subpart`, else the part containing triangle * 3 (summing part +0x20 index counts).
// Returns MeshPart_LoadMaterial(model +0x7c + part +0x10 * 0x64, owner), or 0. Missing data prints "Error: can't
// figure collision properties" (not for part = model index = -1).
uint32_t CollisionMaterial_Find(int32_t triangle, int32_t part, uint8_t* owner, int32_t model, int32_t subpart)
{
    auto error = []() {
        reinterpret_cast<void(__cdecl*)(uint32_t, const char*, ...)>(0x00407ff0)(
            Field<uint32_t>(g_game, 0x34), reinterpret_cast<const char*>(0x005baa68));
    };
    uint8_t* list;
    if (owner)
    {
        const uint32_t edx = orig::fn_00453100(u(owner));
        list = B(orig::fn_00453120(u(owner), edx, 0));
    }
    else
    {
        list = Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa60), 0xf4c);
    }
    if (!list)
    {
        error();
        return 0;
    }
    if (model < 0)
    {
        if (model == -1 && part == -1)
            return 0;
        error();
        return 0;
    }
    uint8_t* mesh = Field<uint8_t*>(Field<uint8_t*>(list, 0x18 + model * 4), 4);
    uint8_t* parts = Field<uint8_t*>(mesh, 0x84);
    if (!parts)
    {
        error();
        return 0;
    }
    uint8_t* found;
    if (part >= 0)
    {
        found = parts + part * 0x2c;
    }
    else if (subpart >= 0)
    {
        found = parts + subpart * 0x2c;
    }
    else
    {
        if (triangle < 0)
            return 0;
        const int32_t count = Field<int32_t>(mesh, 0x80);
        const int32_t index = triangle * 3;
        int32_t sum = 0;
        found = parts;
        bool hit = false;
        for (int32_t i = 0; i < count; ++i, found += 0x2c)
        {
            sum += Field<int32_t>(found, 0x20);
            if (index < sum)
            {
                hit = true;
                break;
            }
        }
        if (!hit && index >= sum)
            return 0;
    }
    if (!found)
        return 0;
    uint8_t* materials = Field<uint8_t*>(mesh, 0x7c);
    if (!materials)
    {
        error();
        return 0;
    }
    return MeshPart_LoadMaterial(materials + Field<uint32_t>(found, 0x10) * 0x64, owner);
}

static uint32_t CollisionMaterial_Find_Abi(uint32_t eax, uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return CollisionMaterial_Find(static_cast<int32_t>(eax), static_cast<int32_t>(ecx), B(a0),
                                  static_cast<int32_t>(a1), static_cast<int32_t>(a2));
}
ABI_ENTRY_00412fd0(CollisionMaterial_Find_Entry, CollisionMaterial_Find_Abi)
HOOK(0x00412fd0, "CollisionMaterial_Find", CollisionMaterial_Find_Entry, nullptr);

// 0x00413120 (esi = contact side info (0x30 bytes); stack: collidable; callee pops 4; caller 0x00413240): +0x10
// entity (collidable +0x1c), +0x14 owner (pair key 0), +0x18 shape (collidable +0x10), +0x1c / +0x20 shape virtuals
// +4 / +8 (type, guess). Triangle shapes (type 7) tagged by 0x00412420: +0xc / +8 from triangle +0x40 / +0x44, +0 /
// +4 = layer / part of the mesh shape's filter word (triangle +0x48, int16 +0xa / +8); other triangles 0, 0, 0, -1.
// Other shapes: +8 = +0xc = -1, +0 / +4 from the shape's filter word. Then +0x24 = CollisionMaterial_Find(+0xc, +4,
// owner, +0, +8): friction / restitution (+0x28 / +0x2c) = material +0xc / +8, else from the owner (0x00458260 /
// 0x00458240), else 0 and 0.5. Returns the info.
uint32_t ContactSide_Fill(uint32_t* info, uint8_t* collidable)
{
    uint8_t* entity = Field<uint8_t*>(collidable, 0x1c);
    info[4] = u(entity);
    info[5] = u(OwnerOf(entity));
    uint8_t* shape = Field<uint8_t*>(collidable, 0x10);
    info[6] = u(shape);
    info[7] = VCall<uint32_t>(shape, 1);
    info[8] = VCall<uint32_t>(B(info[6]), 2);
    if (info[7] == 7)
    {
        uint8_t* triangle = B(info[6]);
        if (Field<uint32_t>(triangle, 0x4c) == 0xf123abcf)
        {
            uint8_t* mesh = Field<uint8_t*>(triangle, 0x48);
            info[3] = Field<uint32_t>(triangle, 0x40);
            info[2] = Field<uint32_t>(triangle, 0x44);
            info[0] = static_cast<uint32_t>(static_cast<int32_t>(Field<int16_t>(mesh, 0xa)));
            info[1] = static_cast<uint32_t>(static_cast<int32_t>(Field<int16_t>(mesh, 8)));
        }
        else
        {
            info[3] = 0;
            info[2] = 0;
            info[0] = 0;
            info[1] = 0xffffffffu;
        }
    }
    else
    {
        uint8_t* s = B(info[6]);
        info[2] = 0xffffffffu;
        info[3] = 0xffffffffu;
        info[0] = static_cast<uint32_t>(static_cast<int32_t>(Field<int16_t>(s, 0xa)));
        info[1] = static_cast<uint32_t>(static_cast<int32_t>(Field<int16_t>(s, 8)));
    }
    const uint32_t material = CollisionMaterial_Find(static_cast<int32_t>(info[3]), static_cast<int32_t>(info[1]),
                                                     B(info[5]), static_cast<int32_t>(info[0]),
                                                     static_cast<int32_t>(info[2]));
    info[9] = material;
    if (material)
    {
        info[10] = Field<uint32_t>(B(material), 0xc);
        info[11] = Field<uint32_t>(B(material), 8);
        return u(info);
    }
    if (info[5])
    {
        // 0x00458260 leaves EAX (the owner) for 0x00458240.
        reinterpret_cast<float*>(info)[10] = orig::fn_00458260(info[5]);
        reinterpret_cast<float*>(info)[11] = orig::fn_00458240(info[5]);
        return u(info);
    }
    info[10] = 0;
    info[11] = 0x3f000000;  // 0.5f
    return u(info);
}

static uint32_t ContactSide_Fill_Abi(uint32_t esi, uint32_t a0)
{
    return ContactSide_Fill(reinterpret_cast<uint32_t*>(esi), B(a0));
}
ABI_ENTRY_00413120(ContactSide_Fill_Entry, ContactSide_Fill_Abi)
HOOK(0x00413120, "ContactSide_Fill", ContactSide_Fill_Entry, nullptr);

// 0x00413240 (slot 0 of the collision listener vtable 0x005baa48, `this` unused; stack: event; callee pops 4;
// called by Havok). Event: +8 / +0xc collidables A / B, +0x14 contact point (position +0, normal +0x10), +0x18
// contact properties, +0x1c reject flag, +0x20 argument. Fills both sides (0x00413120), sets the contact's friction
// (+8) = sqrt(fA * fB), restitution (+0xc) = sqrt(rA * rB) and material (+0x10) = A's or B's. Owners that are not
// flagged (+0x424 bit 8, on either side) are asked with virtual +0xa8(other, &position, &normal, event +0x20,
// other side's material): A with the normal, B with the negated normal. A refusal sets +0x1c = 1. Returns eax as
// left (the accept byte).
uint32_t CollisionListener_OnContactAdded(uint8_t* event)
{
    alignas(16) uint8_t f[0xa0];  // the original's frame: side A at +0x70, side B at +0x40
    uint32_t* a = reinterpret_cast<uint32_t*>(f + 0x70);
    uint32_t* b = reinterpret_cast<uint32_t*>(f + 0x40);
    ContactSide_Fill(a, Field<uint8_t*>(event, 8));
    ContactSide_Fill(b, Field<uint8_t*>(event, 0xc));
    uint8_t* properties = Field<uint8_t*>(event, 0x18);
    Field<float>(properties, 8) = std::sqrt(bf(b[10]) * bf(a[10]));
    Field<float>(Field<uint8_t*>(event, 0x18), 0xc) = std::sqrt(bf(b[11]) * bf(a[11]));
    if (a[9])
        Field<uint32_t>(Field<uint8_t*>(event, 0x18), 0x10) = a[9];
    else
        Field<uint32_t>(Field<uint8_t*>(event, 0x18), 0x10) = b[9];
    uint8_t* ownerA = OwnerOf(B(a[4]));
    uint8_t* ownerB = OwnerOf(B(b[4]));
    const float* point = Field<const float*>(event, 0x14);
    float position[3] = {point[0], point[1], point[2]};
    float normal[3] = {point[4], point[5], point[6]};
    uint8_t accept = 1;
    if (ownerA && !(ownerA[0x424] & 8) && !(ownerB && (ownerB[0x424] & 8)))
    {
        if (!VCall<uint8_t>(ownerA, 0xa8 / 4, ownerB, position, normal, Field<uint32_t>(event, 0x20), b[9]))
            accept = 0;
    }
    if (ownerB)
    {
        normal[0] = -normal[0];
        normal[1] = -normal[1];
        normal[2] = -normal[2];
        if (!(ownerB[0x424] & 8) && !(ownerA && (ownerA[0x424] & 8)))
        {
            if (!VCall<uint8_t>(ownerB, 0xa8 / 4, ownerA, position, normal, Field<uint32_t>(event, 0x20), a[9]))
            {
                Field<uint32_t>(event, 0x1c) = 1;
                return 0;
            }
        }
    }
    if (!accept)
        Field<uint32_t>(event, 0x1c) = 1;
    return accept;
}

static uint32_t CollisionListener_OnContactAdded_Abi(uint32_t a0)
{
    return CollisionListener_OnContactAdded(B(a0));
}
ABI_ENTRY_00413240(CollisionListener_OnContactAdded_Entry, CollisionListener_OnContactAdded_Abi)
HOOK(0x00413240, "CollisionListener_OnContactAdded", CollisionListener_OnContactAdded_Entry, nullptr);

// ================================================================================================================
// Rigid body shape / mass properties from a model
// ================================================================================================================

uint32_t MassProperties_Copy(uint32_t* destination, const uint32_t* source);
uint32_t Matrix_Copy(uint32_t* destination, const uint32_t* source);
uint32_t MassEntry_Init(uint32_t* entry);
uint32_t MassProperties_Clear(uint32_t* properties);

// 0x00413410 (ecx: saved only, edx = owner object or 0, esi = &hullFlag (byte); stack: unused, skin, part, layer;
// callee pops 0x10; caller 0x00413450): CollisionSkin_Build(eax = owner +0x414 (mesh object) or 0; unused, skin,
// part, layer, &hullFlag, *hullFlag, 1, 0, template = owner +0x388 or -1).
uint32_t CollisionSkin_BuildForOwner(uint32_t /*ecx*/, uint8_t* owner, uint8_t* hullFlag, uint32_t unused,
                                     uint32_t skin, uint32_t part, uint32_t layer)
{
    uint32_t meshObject = 0;
    uint32_t templateIndex = 0xffffffffu;
    if (owner)
    {
        meshObject = Field<uint32_t>(owner, 0x414);
        templateIndex = Field<uint32_t>(owner, 0x388);
    }
    return CollisionSkin_Build(B(meshObject), unused, B(skin), static_cast<int32_t>(part), layer, hullFlag,
                               *hullFlag, 1, 0, static_cast<int32_t>(templateIndex));
}

static uint32_t CollisionSkin_BuildForOwner_Abi(uint32_t ecx, uint32_t edx, uint32_t esi, uint32_t a0, uint32_t a1,
                                                uint32_t a2, uint32_t a3)
{
    return CollisionSkin_BuildForOwner(ecx, B(edx), B(esi), a0, a1, a2, a3);
}
ABI_ENTRY_00413410(CollisionSkin_BuildForOwner_Entry, CollisionSkin_BuildForOwner_Abi)
HOOK(0x00413410, "CollisionSkin_BuildForOwner", CollisionSkin_BuildForOwner_Entry, nullptr);

// 0x00413450 (eax = model list; stack: unused, owner, model index, hullFlag (byte, passed by address), perPart
// (byte), out; callee pops 0x18; callers 0x00456a90, 0x00456c40). The model is list +0x18[index] +4. Without
// perPart, one node for the whole skin (0x00413410 part -1) with density owner +0x38c (else 1.0); with perPart, one
// node per part (from the last) with the density of the part's material (MeshPart_LoadMaterial, +4) when it has one.
// Each node shape (+0x60): mass properties computed once (0x004a6950 on node +0x10 with the density, node +0 cleared);
// filter word = index << 16 | (part or 0xffff). perPart: the shapes go into an hkArray (data, size, capacity; reserve
// 0x004aa830) and their mass properties with identity transforms (0x004142b0, 0x00413770, 0x00414240) into a second
// array of 0x90-byte entries; out +0x74 = list shape (0x00511350, 0x28 bytes, +8 = -4) and out +0x20 = combined mass
// properties (0x004a5ed0) or cleared ones. Otherwise out +0x74 = the node shape (reference +1) and out +0x20 = its mass
// properties. out +0x70 = 2; both arrays are freed unless not owned. Returns eax as left.
uint32_t Body_BuildShape(uint8_t* list, uint32_t unused, uint8_t* owner, uint32_t modelIndex, uint32_t hullFlag,
                         uint32_t perPart, uint8_t* out)
{
    alignas(16) uint8_t f[0xd0];  // the original's frame (offsets from esp after the three register pushes)
    auto L = [&f](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + offset); };
    std::memset(f, 0, sizeof(f));
    uint8_t flag = static_cast<uint8_t>(hullFlag);  // the original passes the address of its stack argument
    uint8_t* mesh = Field<uint8_t*>(Field<uint8_t*>(list, 0x18 + modelIndex * 4), 4);
    L(0x28) = 0x80000000;
    L(0x3c) = 0x80000000;
    uint32_t entries = 0;  // edi: size of the 0x90-byte entry array
    L(0x30) = u(mesh);
    L(0x18) = 0x3f800000;  // density 1.0
    L(0x20) = 0;
    L(0x24) = 0;
    L(0x34) = 0;
    L(0x38) = 0;
    uint8_t* node = B(L(0x1c));
    uint32_t shape = L(0x2c);
    int32_t index;
    if (static_cast<uint8_t>(perPart))
    {
        index = Field<int32_t>(mesh, 0x80);
        L(0x14) = static_cast<uint32_t>(index);
        if (!index)
            goto done;
    }
    else
    {
        L(0x14) = 1;
        node = B(CollisionSkin_BuildForOwner(modelIndex, owner, &flag, unused, u(mesh), 0xffffffffu, modelIndex));
        if (owner)
            L(0x18) = Field<uint32_t>(owner, 0x38c);
        index = static_cast<int32_t>(L(0x14));
    }
    L(0x1c) = static_cast<uint32_t>(index) * 0x2c;
    do
    {
        L(0x1c) = L(0x1c) - 0x2c;
        --index;
        L(0x14) = static_cast<uint32_t>(index);
        if (static_cast<uint8_t>(perPart))
        {
            node = B(CollisionSkin_BuildForOwner(modelIndex, owner, &flag, unused, L(0x30), static_cast<uint32_t>(index),
                                                 modelIndex));
            uint8_t* m = B(L(0x30));
            uint8_t* parts = Field<uint8_t*>(m, 0x84);
            if (parts && Field<uint8_t*>(m, 0x7c))
            {
                L(0x18) = 0x3f800000;
                const uint32_t material = MeshPart_LoadMaterial(
                    Field<uint8_t*>(m, 0x7c) + Field<uint32_t>(parts + L(0x1c), 0x10) * 0x64, owner);
                if (material)
                    L(0x18) = Field<uint32_t>(B(material), 4);
            }
        }
        shape = Field<uint32_t>(node, 0x60);
        L(0x2c) = shape;
        if (!shape)
            continue;
        if (node[0])
        {
            orig::fn_004a6950(u(node + 0x10), L(0x18));
            node[0] = 0;
        }
        uint32_t low;
        if (static_cast<uint8_t>(perPart))
        {
            if (L(0x24) == (L(0x28) & 0x7fffffff))
            {
                orig::fn_004aa830(u(f + 0x20), L(0x24) ? L(0x24) * 2 : 1, 4);
                shape = L(0x2c);
            }
            reinterpret_cast<uint32_t*>(L(0x20))[L(0x24)] = shape;
            L(0x24) = L(0x24) + 1;
            MassEntry_Init(reinterpret_cast<uint32_t*>(f + 0x40));
            MassProperties_Copy(reinterpret_cast<uint32_t*>(f + 0x40), reinterpret_cast<uint32_t*>(node + 0x10));
            if (entries == (L(0x3c) & 0x7fffffff))
            {
                orig::fn_004aa830(u(f + 0x34), entries ? entries * 2 : 1, 0x90);
                entries = L(0x38);
            }
            uint32_t* entry = reinterpret_cast<uint32_t*>(L(0x34) + entries * 0x90);
            ++entries;
            L(0x38) = entries;
            MassProperties_Copy(entry, reinterpret_cast<uint32_t*>(f + 0x40));
            Matrix_Copy(entry + 0x50 / 4, reinterpret_cast<uint32_t*>(f + 0x90));
            low = L(0x14);
            shape = L(0x2c);
        }
        else
        {
            low = 0xffff;
        }
        Field<uint32_t>(B(shape), 8) = (modelIndex << 16) | low;
    } while (L(0x14));
done:
    Field<uint32_t>(out, 0x74) = 0;
    if (static_cast<uint8_t>(perPart))
    {
        if (L(0x24))
        {
            uint8_t* memory = VCall<uint8_t*>(GameGlobal<void*>(0x005fed84), 0x10 / 4, 0x28u, 0x1du);
            Field<uint16_t>(memory, 4) = 0x28;
            uint8_t* listShape = B(orig::fn_00511350(u(memory), L(0x20), L(0x24)));
            Field<uint8_t*>(out, 0x74) = listShape;
            Field<uint32_t>(listShape, 8) = 0xfffffffc;
            if (entries)
            {
                orig::fn_004a5ed0(u(out + 0x20), u(f + 0x34));
            }
            else
            {
                MassProperties_Clear(reinterpret_cast<uint32_t*>(f + 0x40));
                MassProperties_Copy(reinterpret_cast<uint32_t*>(out + 0x20), reinterpret_cast<uint32_t*>(f + 0x40));
            }
        }
    }
    else if (shape)
    {
        Field<uint32_t>(out, 0x74) = shape;
        ++Field<uint16_t>(B(shape), 6);
        MassProperties_Copy(reinterpret_cast<uint32_t*>(out + 0x20), reinterpret_cast<uint32_t*>(node + 0x10));
    }
    Field<uint32_t>(out, 0x70) = 2;
    uint32_t result = 0;
    if (static_cast<int32_t>(L(0x3c)) >= 0)
        result = VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, L(0x34), (L(0x3c) & 0x7fffffff) * 0x90,
                                 0x10u);
    if (static_cast<int32_t>(L(0x28)) >= 0)
        result = VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, L(0x20), (L(0x28) & 0x7fffffff) << 2,
                                 0x10u);
    return result;
}

static uint32_t Body_BuildShape_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                    uint32_t a5)
{
    return Body_BuildShape(B(eax), a0, B(a1), a2, a3, a4, B(a5));
}
ABI_ENTRY_00413450(Body_BuildShape_Entry, Body_BuildShape_Abi)
HOOK(0x00413450, "Body_BuildShape", Body_BuildShape_Entry, nullptr);

// 0x00413770 (eax = destination, ecx = source; returns nothing (EAX kept); called by game and Havok code): copies
// the 0x50-byte mass properties except the words +8 and +0xc.
uint32_t MassProperties_Copy(uint32_t* destination, const uint32_t* source)
{
    destination[0] = source[0];
    destination[1] = source[1];
    for (uint32_t i = 4; i < 0x50 / 4; ++i)
        destination[i] = source[i];
    return u(destination);
}

static uint32_t MassProperties_Copy_Abi(uint32_t eax, uint32_t ecx)
{
    return MassProperties_Copy(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<const uint32_t*>(ecx));
}
ABI_ENTRY_00413770(MassProperties_Copy_Entry, MassProperties_Copy_Abi)
HOOK(0x00413770, "MassProperties_Copy", MassProperties_Copy_Entry, nullptr);

// 0x004137e0 (ecx, esi passed through; stack: a, b, c; callee pops 0xc; callers 0x00436bb0, 0x0043ee90,
// 0x00455860, 0x0047fc90, 0x004822b0): 0x00414110(eax = b, esi; a, c, ecx, 0, &c, 0, 0, 0, 0, 0, 0, 1, 0). The
// original passes the address of its own stack argument c; this passes the address of a copy.
uint32_t fn_004137e0(uint32_t ecx, uint32_t esi, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t slot = c;
    return orig::fn_00414110(b, esi, a, c, ecx, 0, u(&slot), 0, 0, 0, 0, 0, 0, 1, 0);
}

static uint32_t fn_004137e0_Abi(uint32_t ecx, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return fn_004137e0(ecx, esi, a0, a1, a2);
}
ABI_ENTRY_004137e0(fn_004137e0_Entry, fn_004137e0_Abi)
HOOK(0x004137e0, "fn_004137e0", fn_004137e0_Entry, nullptr);

// 0x00413810 (eax = hit output template, ecx = to, edx = from, esi = memory; caller 0x00413e10): Havok ray collector
// base constructor 0x00511550(ecx = memory; from, to, template) (docs/subsystems/physics.md calls 0x00511550 a
// transformed shape; here it builds the ray collector, guess) then the game vtable 0x005baa08 (slot 0
// RayCollector_AddBroadphaseHit). Returns the collector.
uint32_t RayCollector_Construct(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t* memory)
{
    orig::fn_00511550(u(memory), edx, ecx, eax);
    memory[0] = 0x005baa08;
    return u(memory);
}

static uint32_t RayCollector_Construct_Abi(uint32_t eax, uint32_t ecx, uint32_t edx, uint32_t esi)
{
    return RayCollector_Construct(eax, ecx, edx, reinterpret_cast<uint32_t*>(esi));
}
ABI_ENTRY_00413810(RayCollector_Construct_Entry, RayCollector_Construct_Abi)
HOOK(0x00413810, "RayCollector_Construct", RayCollector_Construct_Entry, nullptr);

// 0x00413830 (eax = &shape (updated), edi: saved only; caller 0x00413a90): unwraps container shapes while the type
// (virtual +4) is in 0x12..0x19: types 0x12 and 0x13 -> +0xc, 0x19 -> +0x28 (types 0x14..0x18 would loop forever).
// Then classifies the filter word (+8): high half != 0xffff -> 2; low half -2 -> 0, -1 -> 1; otherwise (or no
// shape) -1.
uint32_t Shape_Classify(uint8_t** shape, uint32_t /*edi*/)
{
    int32_t type = VCall<int32_t>(*shape, 1);
    if (type >= 0x12)
    {
        while (type <= 0x19)
        {
            if (type == 0x12)
                *shape = Field<uint8_t*>(*shape, 0xc);
            else if (type == 0x13)
                *shape = Field<uint8_t*>(*shape, 0xc);
            else if (type == 0x19)
                *shape = Field<uint8_t*>(*shape, 0x28);
            type = VCall<int32_t>(*shape, 1);
            if (type < 0x12)
                break;
        }
    }
    if (!*shape)
        return 0xffffffffu;
    const uint32_t info = Field<uint32_t>(*shape, 8);
    if (static_cast<uint16_t>(static_cast<int32_t>(info) >> 16) != 0xffff)
        return 2;
    const int32_t low = static_cast<int16_t>(info & 0xffff);
    if (low == -2)
        return 0;
    if (low == -1)
        return 1;
    return 0xffffffffu;
}

static uint32_t Shape_Classify_Abi(uint32_t eax, uint32_t edi)
{
    return Shape_Classify(reinterpret_cast<uint8_t**>(eax), edi);
}
ABI_ENTRY_00413830(Shape_Classify_Entry, Shape_Classify_Abi)
HOOK(0x00413830, "Shape_Classify", Shape_Classify_Entry, nullptr);

// ================================================================================================================
// Game ray cast (collector vtable 0x005baa08, built by 0x00413810 in 0x00413e10): +0x10..+0x28 hit record (Havok
// writes it; +0x24 / +0x28 triangle / subpart), +0x20 hit fraction (early out), +0x50 / +0x60 / +0x64 / +0x68 hit
// argument / shape / owner / material, +0x6c detail level, +0x70 / +0x74 include / exclude stamps of a query
// (0x00413cc0), +0x78 accept bodies without owner, +0x79 test attached objects (0x004539d0 chain).
// ================================================================================================================

// 0x004138c0 (esi = collector; stack: owner, argument, shape, saved hit record (7 dwords); callee pops 0x10;
// caller 0x00413970): looks up the material of the hit (CollisionMaterial_Find: triangle / subpart = collector
// +0x24 / +0x28 for shape type 2, else -1; part / model = int16 halves of the shape's filter word). A material
// named "invisible_material" (_stricmp) restores the saved hit record (the hit is ignored, returns it); otherwise the
// hit is recorded (+0x68 material, +0x60 shape, +0x50 argument, +0x64 owner; returns the argument).
uint32_t RayCollector_AddHit(uint8_t* collector, uint8_t* owner, uint32_t argument, uint8_t* shape,
                             const uint32_t* saved)
{
    const uint32_t info = Field<uint32_t>(shape, 8);
    const int32_t model = static_cast<int16_t>(static_cast<int32_t>(info) >> 16);
    const int32_t part = static_cast<int16_t>(info & 0xffff);
    int32_t subpart = -1;
    int32_t triangle = -1;
    if (VCall<int32_t>(shape, 1) == 2)
    {
        subpart = Field<int32_t>(collector, 0x28);
        triangle = Field<int32_t>(collector, 0x24);
    }
    uint8_t* material = B(CollisionMaterial_Find(triangle, part, owner, model, subpart));
    if (material && Field<const char*>(material, 0x10) &&
        reinterpret_cast<int(__cdecl*)(const char*, const char*)>(0x005448ee)(
            Field<const char*>(material, 0x10), reinterpret_cast<const char*>(0x005baa54)) == 0)
    {
        for (uint32_t i = 0; i < 7; ++i)
            Field<uint32_t>(collector, 0x10 + i * 4) = saved[i];
        return u(saved);
    }
    Field<uint8_t*>(collector, 0x68) = material;
    Field<uint8_t*>(collector, 0x60) = shape;
    Field<uint32_t>(collector, 0x50) = argument;
    Field<uint8_t*>(collector, 0x64) = owner;
    return argument;
}

static uint32_t RayCollector_AddHit_Abi(uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return RayCollector_AddHit(B(esi), B(a0), a1, B(a2), reinterpret_cast<const uint32_t*>(a3));
}
ABI_ENTRY_004138c0(RayCollector_AddHit_Entry, RayCollector_AddHit_Abi)
HOOK(0x004138c0, "RayCollector_AddHit", RayCollector_AddHit_Entry, nullptr);

// 0x00413970 (edi = shape; stack: collector, owner, collidable; callee pops 0xc; caller 0x00413a90): casts the
// collector's ray (from +0x30, to +0x40, transformed by Havok 0x004de720 with collidable +0x14 +0x20) against the
// shape (virtual +0x18(from, to, &hit record +0x10)); a list shape (type 0x10) casts against each child (count virtual
// +0x20, children +0xc). Each hit goes through RayCollector_AddHit with a copy of the hit record taken before.
uint32_t RayCollector_CastShape(uint8_t* shape, uint8_t* collector, uint8_t* owner, uint8_t* collidable)
{
    if (!shape)
        return 0;
    alignas(16) float to[4];
    alignas(16) float from[4];
    uint32_t saved[7];
    uint8_t* transform = Field<uint8_t*>(collidable, 0x14) + 0x20;
    uint8_t* record = collector + 0x10;
    for (uint32_t i = 0; i < 7; ++i)
        saved[i] = Field<uint32_t>(record, i * 4);
    orig::fn_004de720(u(from), u(transform), u(collector + 0x30));
    orig::fn_004de720(u(to), u(transform), u(collector + 0x40));
    uint32_t result = 0;
    if (VCall<int32_t>(shape, 1) == 0x10)
    {
        int32_t i = 0;
        if (VCall<int32_t>(shape, 0x20 / 4) <= 0)
            return 0;
        do
        {
            uint8_t* child = Field<uint8_t**>(shape, 0xc)[i];
            if (child && VCall<uint32_t>(child, 0x18 / 4, from, to, record))
                result = RayCollector_AddHit(collector, owner, u(collidable), child, saved);
            ++i;
        } while (i < VCall<int32_t>(shape, 0x20 / 4));
        return result;
    }
    if (VCall<uint32_t>(shape, 0x18 / 4, from, to, record))
        result = RayCollector_AddHit(collector, owner, u(collidable), shape, saved);
    return result;
}

static uint32_t RayCollector_CastShape_Abi(uint32_t edi, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return RayCollector_CastShape(B(edi), B(a0), B(a1), B(a2));
}
ABI_ENTRY_00413970(RayCollector_CastShape_Entry, RayCollector_CastShape_Abi)
HOOK(0x00413970, "RayCollector_CastShape", RayCollector_CastShape_Entry, nullptr);

// 0x00413a90 (ecx = collector; esi: not read; stack: collidable; callee pops 4; returns the early-out fraction +0x20
// in st0; slot 0 of vtable 0x005baa08; not executed in recorded runs). Owner (pair key 0 of collidable +0x1c): none
// -> only when +0x78; otherwise it must carry the include stamp (+0x2c == +0x70) and not the exclude stamp (+0x30 !=
// +0x74), nor (with +0x79) any attached object (0x004539d0 chain). The detail level comes from Shape_Classify on the
// collidable's shape; for the game's list shape (type 0x10, filter word -1:-4) the children are classified (level 1
// counts only when the owner's mesh object +0x414 has bit 0 of +0x4c) and the deepest level not above +0x6c wins,
// else the lowest. With +0x6c >= 2 and an owner with bodies (+0x1bc): a non-ragdolled owner is ragdolled (0x00458970)
// and every body is cast (RayCollector_CastShape on entity +0x24 / +0x14); a ragdolled one is cast when the level
// reaches +0x6c. Otherwise a level > 0 casts the chosen shape.
float RayCollector_AddBroadphaseHit(uint8_t* collector, uint32_t /*esi*/, uint8_t* collidable)
{
    uint8_t* owner = OwnerOf(Field<uint8_t*>(collidable, 0x1c));
    if (!owner)
    {
        if (!collector[0x78])
            return Field<float>(collector, 0x20);
    }
    else
    {
        if (Field<uint32_t>(owner, 0x30) == Field<uint32_t>(collector, 0x74))
            return Field<float>(collector, 0x20);
        if (Field<uint32_t>(owner, 0x2c) != Field<uint32_t>(collector, 0x70))
            return Field<float>(collector, 0x20);
    }
    if (collector[0x79] && owner)
    {
        for (uint8_t* p = B(orig::fn_004539d0(u(owner))); p; p = B(orig::fn_004539d0(u(p))))
        {
            if (Field<uint32_t>(p, 0x30) == Field<uint32_t>(collector, 0x74))
                return Field<float>(collector, 0x20);
        }
    }
    uint8_t* shape = Field<uint8_t*>(collidable, 0x10);
    uint8_t* current = shape;  // edi
    uint8_t* probe = shape;    // stack slot passed to Shape_Classify
    int32_t best = -1;         // ebx
    int32_t lowest = -1;       // esi
    uint8_t* lowestShape = nullptr;
    const uint32_t info = Field<uint32_t>(shape, 8);
    if (VCall<int32_t>(shape, 1) == 0x10 && static_cast<int16_t>(static_cast<int32_t>(info) >> 16) == -1 &&
        static_cast<uint16_t>(info) == 0xfffc)
    {
        uint8_t* list = shape;
        int32_t i = 0;
        if (VCall<int32_t>(list, 0x20 / 4) > 0)
        {
            do
            {
                probe = Field<uint8_t**>(list, 0xc)[i];
                int32_t level = static_cast<int32_t>(Shape_Classify(&probe, u(current)));
                if (level == 1)
                {
                    uint8_t* meshObject = Field<uint8_t*>(owner, 0x414);
                    if (meshObject && !(meshObject[0x4c] & 1))
                        level = -1;
                }
                if (!lowestShape || lowest > level)
                {
                    lowest = level;
                    lowestShape = current;
                }
                if (level <= Field<int32_t>(collector, 0x6c) && best < level)
                {
                    current = probe;
                    best = level;
                }
                ++i;
            } while (i < VCall<int32_t>(list, 0x20 / 4));
            if (best < lowest)
            {
                current = lowestShape;
                best = lowest;
            }
        }
    }
    else
    {
        best = static_cast<int32_t>(Shape_Classify(&probe, u(current)));
        current = probe;
    }
    const int32_t detail = Field<int32_t>(collector, 0x6c);
    if (detail >= 2 && owner && Field<uint8_t*>(owner, 0x1bc))
    {
        if (!owner[0x421])
        {
            orig::fn_00458970(u(owner), 0, 0);
            for (uint8_t* record = Field<uint8_t*>(owner, 0x1bc); Field<uint8_t*>(record, 0); record += 0x4c)
            {
                uint8_t* entity = Field<uint8_t*>(record, 0);
                RayCollector_CastShape(Field<uint8_t*>(entity, 0x24), collector, owner, entity + 0x14);
            }
            return Field<float>(collector, 0x20);
        }
        if (best >= detail)
            RayCollector_CastShape(current, collector, owner, collidable);
    }
    else if (best > 0)
    {
        RayCollector_CastShape(current, collector, owner, collidable);
    }
    return Field<float>(collector, 0x20);
}

static float RayCollector_AddBroadphaseHit_Abi(uint32_t ecx, uint32_t esi, uint32_t a0)
{
    return RayCollector_AddBroadphaseHit(B(ecx), esi, B(a0));
}
ABI_ENTRY_00413a90(RayCollector_AddBroadphaseHit_Entry, RayCollector_AddBroadphaseHit_Abi)
HOOK(0x00413a90, "RayCollector_AddBroadphaseHit", RayCollector_AddBroadphaseHit_Entry, nullptr);

// ================================================================================================================
// Query stamps: a query (+0 include stamp, +4 exclude stamp) marks objects with +0x2c / +0x30.
// ================================================================================================================

uint32_t Stamp_Next(uint32_t unused);
uint32_t Query_NewStamps(uint32_t* query);

// 0x00413cc0 (ecx = query, edx: dl = reset; callers 0x00406f50, 0x0040e350, 0x00414110, ...): new stamps
// (0x00413d20); with reset: +8..+0x20 = 0, byte +0x3c = 1, +0x40 = 1, +0x44 = 0, byte +0x3d = 1, byte +0x3e = 0.
// Returns the query.
uint32_t Query_Init(uint8_t* query, uint32_t edx)
{
    Query_NewStamps(reinterpret_cast<uint32_t*>(query));
    if (static_cast<uint8_t>(edx))
    {
        Field<uint32_t>(query, 0x10) = 0;
        Field<uint32_t>(query, 0xc) = 0;
        Field<uint32_t>(query, 8) = 0;
        Field<uint32_t>(query, 0x1c) = 0;
        Field<uint32_t>(query, 0x18) = 0;
        Field<uint32_t>(query, 0x14) = 0;
        Field<uint32_t>(query, 0x20) = 0;
        query[0x3c] = 1;
        Field<uint32_t>(query, 0x40) = 1;
        Field<uint32_t>(query, 0x44) = 0;
        query[0x3d] = 1;
        query[0x3e] = 0;
    }
    return u(query);
}

static uint32_t Query_Init_Abi(uint32_t ecx, uint32_t edx)
{
    return Query_Init(B(ecx), edx);
}
ABI_ENTRY_00413cc0(Query_Init_Entry, Query_Init_Abi)
HOOK(0x00413cc0, "Query_Init", Query_Init_Entry, nullptr);

// 0x00413d00 (stack: unused; callee pops 4; caller 0x00413d20): increments the global stamp counter 0x0064a760,
// skipping 0, and returns it.
uint32_t Stamp_Next(uint32_t /*unused*/)
{
    if (++GameGlobal<uint32_t>(0x0064a760) == 0)
    {
        GameGlobal<uint32_t>(0x0064a760) = 1;
        return 1;
    }
    return GameGlobal<uint32_t>(0x0064a760);
}

static uint32_t Stamp_Next_Abi(uint32_t a0)
{
    return Stamp_Next(a0);
}
ABI_ENTRY_00413d00(Stamp_Next_Entry, Stamp_Next_Abi)
HOOK(0x00413d00, "Stamp_Next", Stamp_Next_Entry, nullptr);

// 0x00413d20 (ecx = query; caller 0x00413cc0): two new stamps into +0 and +4 (the original pushes ecx as the unused
// argument). Returns the second.
uint32_t Query_NewStamps(uint32_t* query)
{
    query[0] = Stamp_Next(u(query));
    query[1] = Stamp_Next(u(query));
    return query[1];
}

static uint32_t Query_NewStamps_Abi(uint32_t ecx)
{
    return Query_NewStamps(reinterpret_cast<uint32_t*>(ecx));
}
ABI_ENTRY_00413d20(Query_NewStamps_Entry, Query_NewStamps_Abi)
HOOK(0x00413d20, "Query_NewStamps", Query_NewStamps_Entry, nullptr);

// 0x00413d40 (eax = object, ecx = query; not executed in recorded runs): stamps the object and its attached chain
// (0x004539d0) with the exclude stamp (+0x30 = query +4). Returns 0.
uint32_t Query_ExcludeChain(uint8_t* object, const uint32_t* query)
{
    while (object)
    {
        Field<uint32_t>(object, 0x30) = query[1];
        object = B(orig::fn_004539d0(u(object)));
    }
    return 0;
}

static uint32_t Query_ExcludeChain_Abi(uint32_t eax, uint32_t ecx)
{
    return Query_ExcludeChain(B(eax), reinterpret_cast<const uint32_t*>(ecx));
}
ABI_ENTRY_00413d40(Query_ExcludeChain_Entry, Query_ExcludeChain_Abi)
HOOK(0x00413d40, "Query_ExcludeChain", Query_ExcludeChain_Entry, nullptr);

// 0x00413d60 (eax = object or 0, ecx = query; callers 0x00413dc0, 0x00414110): include stamp (+0x2c = query +0).
uint32_t Query_Include(uint8_t* object, const uint32_t* query)
{
    if (object)
        Field<uint32_t>(object, 0x2c) = query[0];
    return u(object);
}

static uint32_t Query_Include_Abi(uint32_t eax, uint32_t ecx)
{
    return Query_Include(B(eax), reinterpret_cast<const uint32_t*>(ecx));
}
ABI_ENTRY_00413d60(Query_Include_Entry, Query_Include_Abi)
HOOK(0x00413d60, "Query_Include", Query_Include_Entry, nullptr);

// 0x00413d70 (eax = object list or 0, ebx = query): Query_ExcludeChain for every object of the list (iterator
// 0x004e7340 / 0x004e73c0). Returns the iterator node after dropping its reference (+0x18), or 0.
uint32_t Query_ExcludeList(uint32_t list, const uint32_t* query)
{
    if (!list)
        return 0;
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(list, u(iterator))); obj; obj = B(orig::fn_004e73c0(u(iterator))))
        Query_ExcludeChain(obj, query);
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t Query_ExcludeList_Abi(uint32_t eax, uint32_t ebx)
{
    return Query_ExcludeList(eax, reinterpret_cast<const uint32_t*>(ebx));
}
ABI_ENTRY_00413d70(Query_ExcludeList_Entry, Query_ExcludeList_Abi)
HOOK(0x00413d70, "Query_ExcludeList", Query_ExcludeList_Entry, nullptr);

// 0x00413dc0 (eax = object list or 0, ebx = query): Query_Include for every object of the list. Returns the
// iterator node after dropping its reference, or 0.
uint32_t Query_IncludeList(uint32_t list, const uint32_t* query)
{
    if (!list)
        return 0;
    uint32_t iterator[2] = {0, 0};
    for (uint8_t* obj = B(orig::fn_004e7340(list, u(iterator))); obj; obj = B(orig::fn_004e73c0(u(iterator))))
        Query_Include(obj, query);
    if (iterator[1])
        --Field<int32_t>(P(iterator[1]), 0x18);
    return iterator[1];
}

static uint32_t Query_IncludeList_Abi(uint32_t eax, uint32_t ebx)
{
    return Query_IncludeList(eax, reinterpret_cast<const uint32_t*>(ebx));
}
ABI_ENTRY_00413dc0(Query_IncludeList_Entry, Query_IncludeList_Abi)
HOOK(0x00413dc0, "Query_IncludeList", Query_IncludeList_Entry, nullptr);

// 0x00413e10 (edi = query; callers 0x00406f50, 0x0040e350, 0x00414110, 0x00426fd0, 0x00477570, 0x0047e580): casts a
// ray. Query: +0 / +4 stamps, +8 from, +0x14 direction, +0x20 length, +0x3c / +0x3d / +0x3e flags, +0x40 detail.
// to = from + direction * length; a collector (0x00413810, on the stack) gets the stamps, flags and detail and the
// world's ray caster (hkWorld +0xe0, virtual +0x28(from, to, collector)) runs when the ray is longer than
// FLT_EPSILON. Hit (0x005114c0): +0x20 = distance from `from` to the interpolated point, +0x28..+0x34 normalised
// normal (0 when null), +0x38 = int16 +0xa of the hit shape's filter word, +0x44 material, +0x34 owner (with +0x3d:
// walked up the attachment chain (0x004539d0) until 0x004559f0 accepts, +0x38 = -1 then). No hit: +0x34 = 0, +0x38 = -1.
// +0x24 = +0x20 in both cases. Returns the hit flag in AL (EAX upper bytes from +0x20 as in the original).
uint32_t Ray_Cast(uint8_t* q)
{
    alignas(16) uint8_t f[0xe0];  // the original's frame (offsets from esp after the two register pushes)
    auto L = [&f](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + offset); };
    auto LF = [&f](uint32_t offset) -> float& { return *reinterpret_cast<float*>(f + offset); };
    std::memset(f, 0, sizeof(f));
    LF(0x44) = Field<float>(q, 0xc);
    L(0xc) = Field<uint32_t>(q, 0x20);
    LF(0x48) = Field<float>(q, 0x10);
    const float dx = Field<float>(q, 0x14);
    const float dy = Field<float>(q, 0x18);
    L(0x40) = Field<uint32_t>(q, 8);
    LF(0x18) = Field<float>(q, 0x1c);
    L(0x34) = 0xffffffffu;
    L(0x38) = 0xffffffffu;
    LF(0x10) = LF(0xc) * dx;
    const float y = dy * LF(0xc);
    L(0x4c) = 0;
    LF(0x18) = LF(0x18) * LF(0xc);
    L(0x30) = 0x3f800000;
    const float w = LF(0xc) * K(0x0056c05c);
    LF(0xc) = (LF(0x18) * LF(0x18) + y * y) + LF(0x10) * LF(0x10);
    LF(0x10) = LF(0x10) + LF(0x40);
    LF(0x14) = y + LF(0x44);
    LF(0x18) = LF(0x18) + LF(0x48);
    LF(0x1c) = w;
    uint8_t* collector = f + 0x60;
    RayCollector_Construct(u(f + 0x20), u(f + 0x10), u(f + 0x40), reinterpret_cast<uint32_t*>(collector));
    uint8_t hit = 0;
    if (std::sqrt(LF(0xc)) > K(0x00589088))
    {
        uint8_t* caster =
            Field<uint8_t*>(Field<uint8_t*>(Field<uint8_t*>(g_game, 0xa8c), 8), 0xe0);
        L(0xd0) = Field<uint32_t>(q, 0);
        L(0xcc) = Field<uint32_t>(q, 0x40);
        f[0xd9] = q[0x3e];
        L(0xd4) = Field<uint32_t>(q, 4);
        f[0xd8] = q[0x3c];
        VCall<void>(caster, 0x28 / 4, f + 0x40, f + 0x10, collector);
        const uint32_t* record = reinterpret_cast<const uint32_t*>(orig::fn_005114b0(u(collector)));
        for (uint32_t i = 0; i < 7; ++i)
            L(0x20 + i * 4) = record[i];
        hit = orig::fn_005114c0(u(collector)) != 0;
        if (hit)
        {
            const float t = LF(0x30);
            const float s = K(0x005bcb2c) - t;
            const float px = LF(0x40) * s + LF(0x10) * t;
            LF(0x54) = LF(0x44) * s + LF(0x14) * t;
            LF(0x58) = LF(0x48) * s + LF(0x18) * t;
            const float ex = px - LF(0x40);
            const float ey = LF(0x54) - LF(0x44);
            const float ez = LF(0x58) - LF(0x48);
            Field<float>(q, 0x20) = std::sqrt((ez * ez + ey * ey) + ex * ex);
            const float n2 = (LF(0x28) * LF(0x28) + LF(0x24) * LF(0x24)) + LF(0x20) * LF(0x20);
            float inverse;
            if (n2 == K(0x0056c05c))
                inverse = K(0x0056c05c);
            else
                inverse = K(0x005bcb2c) / std::sqrt(n2);
            uint8_t* shape = B(L(0xc0));
            const float nx = LF(0x20) * inverse;
            LF(0x20) = nx;
            LF(0x24) = LF(0x24) * inverse;
            Field<uint32_t>(q, 0x2c) = L(0x24);
            LF(0x28) = LF(0x28) * inverse;
            Field<uint32_t>(q, 0x30) = L(0x28);
            LF(0x2c) = LF(0x2c) * inverse;
            Field<float>(q, 0x28) = nx;
            Field<int32_t>(q, 0x38) = Field<int16_t>(shape, 0xa);
            uint8_t* owner = B(L(0xc4));
            Field<uint32_t>(q, 0x44) = L(0xc8);
            if (q[0x3d] && owner)
            {
                do
                {
                    if (orig::fn_004559f0(u(owner)) & 0xff)
                        break;
                    if (!orig::fn_004539d0(u(owner)))
                        break;
                    owner = B(orig::fn_004539d0(u(owner)));
                    Field<int32_t>(q, 0x38) = -1;
                } while (owner);
            }
            Field<uint32_t>(q, 0x24) = Field<uint32_t>(q, 0x20);
            Field<uint8_t*>(q, 0x34) = owner;
            return (Field<uint32_t>(q, 0x20) & 0xffffff00) | hit;
        }
    }
    Field<uint32_t>(q, 0x24) = Field<uint32_t>(q, 0x20);
    Field<uint32_t>(q, 0x34) = 0;
    Field<int32_t>(q, 0x38) = -1;
    return (Field<uint32_t>(q, 0x20) & 0xffffff00) | hit;
}

static uint32_t Ray_Cast_Abi(uint32_t edi)
{
    return Ray_Cast(B(edi));
}
ABI_ENTRY_00413e10(Ray_Cast_Entry, Ray_Cast_Abi)
HOOK(0x00413e10, "Ray_Cast", Ray_Cast_Entry, nullptr);

// 0x00414110 (eax = from, esi = direction (3 floats each); stack: unused, length, &distance, &normal or 0, &owner,
// &model or 0, exclude list, exclude object, include list, include object, use level list (byte), flag +0x3c (byte),
// detail; callee pops 0x34; callers 0x00404d00, 0x00406f50, 0x004137e0, ...): builds a query on the stack
// (Query_Init without reset), excludes the object chain and list, includes the object and list (the level's list
// Game+0xa84 +0xe70 when neither include is given and the byte is set), casts (Ray_Cast) and stores distance, owner,
// normal and model. Returns Ray_Cast's result.
uint32_t Ray_CastQuery(const uint32_t* from, const uint32_t* direction, uint32_t /*unused*/, uint32_t length,
                       uint32_t* distance, uint32_t* normal, uint32_t* owner, uint32_t* model, uint32_t excludeList,
                       uint32_t excludeObject, uint32_t includeList, uint32_t includeObject, uint32_t useLevelList,
                       uint32_t flag3c, uint32_t detail)
{
    alignas(4) uint8_t query[0x48];
    std::memset(query, 0, sizeof(query));
    Query_Init(query, 0);
    Field<uint32_t>(query, 8) = from[0];
    Field<uint32_t>(query, 0xc) = from[1];
    Field<uint32_t>(query, 0x10) = from[2];
    Field<uint32_t>(query, 0x14) = direction[0];
    Field<uint32_t>(query, 0x18) = direction[1];
    Field<uint32_t>(query, 0x1c) = direction[2];
    Field<uint32_t>(query, 0x20) = length;
    if (!includeList && !includeObject && static_cast<uint8_t>(useLevelList))
        includeList = Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe70);
    const uint32_t* stamps = reinterpret_cast<const uint32_t*>(query);
    Query_ExcludeChain(B(excludeObject), stamps);
    Query_ExcludeList(excludeList, stamps);
    Query_Include(B(includeObject), stamps);
    Query_IncludeList(includeList, stamps);
    query[0x3c] = static_cast<uint8_t>(flag3c);
    Field<uint32_t>(query, 0x40) = detail;
    const uint32_t result = Ray_Cast(query);
    *distance = Field<uint32_t>(query, 0x24);
    *owner = Field<uint32_t>(query, 0x34);
    if (normal)
    {
        normal[0] = Field<uint32_t>(query, 0x28);
        normal[1] = Field<uint32_t>(query, 0x2c);
        normal[2] = Field<uint32_t>(query, 0x30);
    }
    if (model)
        *model = Field<uint32_t>(query, 0x38);
    return result;
}

static uint32_t Ray_CastQuery_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3,
                                  uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                  uint32_t a10, uint32_t a11, uint32_t a12)
{
    return Ray_CastQuery(reinterpret_cast<const uint32_t*>(eax), reinterpret_cast<const uint32_t*>(esi), a0, a1,
                         reinterpret_cast<uint32_t*>(a2), reinterpret_cast<uint32_t*>(a3),
                         reinterpret_cast<uint32_t*>(a4), reinterpret_cast<uint32_t*>(a5), a6, a7, a8, a9, a10, a11,
                         a12);
}
ABI_ENTRY_00414110(Ray_CastQuery_Entry, Ray_CastQuery_Abi)
HOOK(0x00414110, "Ray_CastQuery", Ray_CastQuery_Entry, nullptr);

// 0x00414220 (no inputs; slots 1 and 2 of the skin mesh shape vtable 0x005ba9d0, also in Havok vtables 0x0056b2c0 /
// 0x0056b384 through identical-code folding; not executed in recorded runs): returns 0x11 (guess: the mesh shape
// type).
uint32_t Shape_Type11()
{
    return 0x11;
}

static uint32_t Shape_Type11_Abi()
{
    return Shape_Type11();
}
ABI_ENTRY_00414220(Shape_Type11_Entry, Shape_Type11_Abi)
HOOK(0x00414220, "Shape_Type11", Shape_Type11_Entry, nullptr);

// 0x00414230 (no inputs; slots +0x10 / +0x14 of the game shape vtable 0x005baa20; not executed in recorded runs):
// returns 0x1b (guess: the shape type).
uint32_t Shape_Type1b()
{
    return 0x1b;
}

static uint32_t Shape_Type1b_Abi()
{
    return Shape_Type1b();
}
ABI_ENTRY_00414230(Shape_Type1b_Entry, Shape_Type1b_Abi)
HOOK(0x00414230, "Shape_Type1b", Shape_Type1b_Entry, nullptr);

// 0x00414240 (ecx = destination; stack: source; callee pops 4; called by game and Havok code): copies 0x40 bytes
// (a transform). Returns the destination.
uint32_t Matrix_Copy(uint32_t* destination, const uint32_t* source)
{
    for (uint32_t i = 0; i < 0x40 / 4; ++i)
        destination[i] = source[i];
    return u(destination);
}

static uint32_t Matrix_Copy_Abi(uint32_t ecx, uint32_t a0)
{
    return Matrix_Copy(reinterpret_cast<uint32_t*>(ecx), reinterpret_cast<const uint32_t*>(a0));
}
ABI_ENTRY_00414240(Matrix_Copy_Entry, Matrix_Copy_Abi)
HOOK(0x00414240, "Matrix_Copy", Matrix_Copy_Entry, nullptr);

// 0x004142b0 (eax = 0x90-byte entry; returns nothing; called by game and Havok code): clears the mass properties
// (0x50 bytes except +8 / +0xc, in the original's order), copies the identity rotation 0x005cad50 (12 dwords) into
// +0x50 and clears the translation +0x80..+0x8c.
uint32_t MassEntry_Init(uint32_t* entry)
{
    MassProperties_Clear(entry);
    for (uint32_t i = 0; i < 12; ++i)
        entry[0x50 / 4 + i] = GameGlobal<uint32_t>(0x005cad50 + i * 4);
    entry[0x8c / 4] = 0;
    entry[0x88 / 4] = 0;
    entry[0x84 / 4] = 0;
    entry[0x80 / 4] = 0;
    return u(entry);
}

static uint32_t MassEntry_Init_Abi(uint32_t eax)
{
    return MassEntry_Init(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_004142b0(MassEntry_Init_Entry, MassEntry_Init_Abi)
HOOK(0x004142b0, "MassEntry_Init", MassEntry_Init_Entry, nullptr);

// 0x00414370 (eax = hkWorld cinfo, guess; returns nothing; caller 0x00411960): fills the defaults: +0x30 = 0x200,
// +0x40 0.1, +0x44 0.05, +0x48 0.2, byte +0x4c = 1, +0x50 -0.03, +0x54 0.3, +0x58 0.001, broadphase min +0x10..+0x1c
// = -500, max +0x20..+0x2c = 500, gravity (0, -9.8, 0, 0), +0x34 0.6, +0x38 1.0, +0x3c 4.
uint32_t WorldCinfo_Init(uint32_t* cinfo)
{
    cinfo[0x30 / 4] = 0x200;
    cinfo[0x40 / 4] = 0x3dcccccd;
    cinfo[0x44 / 4] = 0x3d4ccccd;
    cinfo[0x48 / 4] = 0x3e4ccccd;
    reinterpret_cast<uint8_t*>(cinfo)[0x4c] = 1;
    cinfo[0x50 / 4] = 0xbcf5c28f;
    cinfo[0x54 / 4] = 0x3e99999a;
    cinfo[0x58 / 4] = 0x3a83126f;
    for (uint32_t i = 0x10 / 4; i < 0x20 / 4; ++i)
        cinfo[i] = 0xc3fa0000;
    for (uint32_t i = 0x20 / 4; i < 0x30 / 4; ++i)
        cinfo[i] = 0x43fa0000;
    cinfo[0] = 0;
    cinfo[1] = 0xc11ccccd;
    cinfo[2] = 0;
    cinfo[3] = 0;
    cinfo[0x34 / 4] = 0x3f19999a;
    cinfo[0x38 / 4] = 0x3f800000;
    cinfo[0x3c / 4] = 4;
    return u(cinfo);
}

static uint32_t WorldCinfo_Init_Abi(uint32_t eax)
{
    return WorldCinfo_Init(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_00414370(WorldCinfo_Init_Entry, WorldCinfo_Init_Abi)
HOOK(0x00414370, "WorldCinfo_Init", WorldCinfo_Init_Entry, nullptr);

// 0x004143f0 (eax = mass properties; returns nothing; callers 0x00413450, 0x004527d0, Havok 0x004a47a0; not executed
// in recorded runs): clears the 0x50 bytes except +8 / +0xc, in the original's order.
uint32_t MassProperties_Clear(uint32_t* properties)
{
    static const uint8_t order[] = {0x00, 0x04, 0x1c, 0x18, 0x14, 0x10, 0x2c, 0x28, 0x24,
                                    0x20, 0x3c, 0x38, 0x34, 0x30, 0x4c, 0x48, 0x44, 0x40};
    for (uint8_t offset : order)
        properties[offset / 4] = 0;
    return u(properties);
}

static uint32_t MassProperties_Clear_Abi(uint32_t eax)
{
    return MassProperties_Clear(reinterpret_cast<uint32_t*>(eax));
}
ABI_ENTRY_004143f0(MassProperties_Clear_Entry, MassProperties_Clear_Abi)
HOOK(0x004143f0, "MassProperties_Clear", MassProperties_Clear_Entry, nullptr);

// 0x00414430 (ecx = object; stack: flags; callee pops 4; slot 0 of vtable 0x005ba9c8, the base the game listener
// 0x004df780 derives from; not executed in recorded runs): scalar deleting destructor: vtable 0x005ba9c8 and, with
// flags bit 0, the game's free. Returns the object.
uint32_t GameListenerBase_Delete(uint32_t* object, uint32_t flags)
{
    object[0] = 0x005ba9c8;
    if (flags & 1)
        Free(u(object));
    return u(object);
}

static uint32_t GameListenerBase_Delete_Abi(uint32_t ecx, uint32_t a0)
{
    return GameListenerBase_Delete(reinterpret_cast<uint32_t*>(ecx), a0);
}
ABI_ENTRY_00414430(GameListenerBase_Delete_Entry, GameListenerBase_Delete_Abi)
HOOK(0x00414430, "GameListenerBase_Delete", GameListenerBase_Delete_Entry, nullptr);

// ================================================================================================================
// Character physics (materials, launch, ragdoll)
// ================================================================================================================

// 0x0042cb80 (ebx = object; callers 0x0042cc10, 0x00433c70; not executed in recorded runs): friction of the body
// (0x00458250(eax = object; friction)): 1.0 for object types (+0x418) 0x48 and 0x59; otherwise the material named by
// the object's "material" property (0x004533a0, default "default", into a 0x20-byte buffer) is loaded
// (PhysMaterial_Load with defaults) and its friction overridden by the "friction" property (0x004534a0). Returns the
// object (EAX as 0x00458250 leaves it, guess).
uint32_t Object_ApplyMaterialFriction(uint8_t* object)
{
    alignas(4) uint8_t f[0x3c];  // the original's frame (offsets from esp after pushing esi and edi)
    std::memset(f, 0, sizeof(f));
    Field<uint32_t>(f, 0x10) = 0;  // friction (shares the material's +8)
    const uint32_t type = Field<uint32_t>(object, 0x418);
    if (type == 0x48 || type == 0x59)
    {
        Field<uint32_t>(f, 0x10) = 0x3f800000;
        orig::fn_00458250(u(object), Field<uint32_t>(f, 0x10));
        return u(object);
    }
    orig::fn_004533a0(0x005baaf0, u(f + 0x1c), u(object), 0x005b92ec, 0, 0, 1);  // "default", "material"
    PhysMaterial_Load(reinterpret_cast<const char*>(f + 0x1c), f + 8, 1);
    orig::fn_004534a0(0, u(f + 0x10), u(object), 0x005b67ec, 0, Field<uint32_t>(f, 0x10), 1);  // "friction"
    orig::fn_00458250(u(object), Field<uint32_t>(f, 0x10));
    return u(object);
}

static uint32_t Object_ApplyMaterialFriction_Abi(uint32_t ebx)
{
    return Object_ApplyMaterialFriction(B(ebx));
}
ABI_ENTRY_0042cb80(Object_ApplyMaterialFriction_Entry, Object_ApplyMaterialFriction_Abi)
HOOK(0x0042cb80, "Object_ApplyMaterialFriction", Object_ApplyMaterialFriction_Entry, nullptr);

// 0x0042cc10 (eax = object; callers 0x0042fb60, 0x00431720, 0x00433a90, 0x00433c70, 0x0043ee90): +0x574 = 0; when
// +0x9bc holds a reference: friction restored (0x0042cb80), the reference dropped (+0x28; at zero and virtual +0xd8
// false: 0x00487da0) and cleared. Returns eax as left.
uint32_t Object_ReleaseHolder(uint8_t* object)
{
    Field<uint32_t>(object, 0x574) = 0;
    if (!Field<uint32_t>(object, 0x9bc))
        return 0;
    Object_ApplyMaterialFriction(object);
    uint8_t* held = Field<uint8_t*>(object, 0x9bc);
    uint32_t result = 0;
    if (--Field<int32_t>(held, 0x28) == 0)
    {
        result = VCall<uint8_t>(held, 0xd8 / 4);
        if (!(result & 0xff))
            result = orig::fn_00487da0(u(held));
    }
    Field<uint32_t>(object, 0x9bc) = 0;
    return result;
}

static uint32_t Object_ReleaseHolder_Abi(uint32_t eax)
{
    return Object_ReleaseHolder(B(eax));
}
ABI_ENTRY_0042cc10(Object_ReleaseHolder_Entry, Object_ReleaseHolder_Abi)
HOOK(0x0042cc10, "Object_ReleaseHolder", Object_ReleaseHolder_Entry, nullptr);

// 0x00431000 (eax = object; caller 0x00431310): true when +0x460 is 400 or the object is ragdolled (+0x421).
// Returns the bool in AL with EAX's upper bytes from the object pointer, as the original.
uint32_t Object_IsDownOrRagdoll(uint8_t* object)
{
    if (Field<int32_t>(object, 0x460) == 400)
        return (u(object) & 0xffffff00) | 1;
    return (u(object) & 0xffffff00) | (object[0x421] != 0 ? 1u : 0u);
}

static uint32_t Object_IsDownOrRagdoll_Abi(uint32_t eax)
{
    return Object_IsDownOrRagdoll(B(eax));
}
ABI_ENTRY_00431000(Object_IsDownOrRagdoll_Entry, Object_IsDownOrRagdoll_Abi)
HOOK(0x00431000, "Object_IsDownOrRagdoll", Object_IsDownOrRagdoll_Entry, nullptr);

// x87 fsin / fcos of a single-precision angle, as the original computes them.
static void SinCos(float angle, float* s, float* c)
{
    float sv, cv;
    __asm {
        fld angle
        fsin
        fstp sv
        fld angle
        fcos
        fstp cv
    }
    *s = sv;
    *c = cv;
}

// 0x00431020 (eax = attacker; stack: target, speed (float), spread in degrees (float), keepSide (byte); callee pops
// 0x10; callers 0x0040d390, 0x00431310): launches the target. Target virtual +0x1ac(1), +0x154 / +0x160; Game+0xb34 =
// the current time 0x00604a98. Direction = attacker -> target in the horizontal plane (positions virtual +0x54,
// normalised by 0x004e7780), rotated about z by a random angle in [-spread, spread] degrees (rand, matrix with
// cos/sin, 0x004e36a0) and normalised. The attacker point is moved sideways by 40 + rand * 1/1638.4 along it (+ on
// a 50% roll without keepSide, else -). Launch velocity from that point to the target (0x00471a00, speed * target
// +0x948, gravity -64.35); a downward result clears bits 0x30000000 of +0x484 and retries at half speed. Then
// 0x00454c90(cl = 0; 1), physics off (0x004577b0 0), the two force-action handles +0x39c / +0x398 released
// (0x00455a00), the velocity set (0x00458510), virtual +0xf8(0x170000, 0), +0x490 = 8.0, virtual +0x19c(0x49 +
// rand / 16384, 1, 0, 0, 0, 0) and +0x1b8(0x195). Returns the last virtual's eax.
uint32_t Character_Launch(uint8_t* attacker, uint8_t* target, float speed, float spread, uint32_t keepSide)
{
    alignas(16) uint8_t f[0x90];  // the original's frame (offsets from esp after the four register pushes)
    auto L = [&f](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + offset); };
    auto LF = [&f](uint32_t offset) -> float& { return *reinterpret_cast<float*>(f + offset); };
    std::memset(f, 0, sizeof(f));
    VCall<void>(target, 0x1ac / 4, 1u);
    if (!VCall<uint8_t>(target, 0x154 / 4))
        VCall<void>(target, 0x160 / 4);
    Field<uint32_t>(g_game, 0xb34) = GameGlobal<uint32_t>(0x00604a98);
    VCall<void>(target, 0x54 / 4, f + 0x40);
    VCall<void>(attacker, 0x54 / 4, f + 0x1c);
    LF(0x34) = LF(0x40) - LF(0x1c);
    LF(0x38) = LF(0x44) - LF(0x20);
    orig::fn_004e7780(u(f + 0x34), u(f + 0x34));
    L(0x3c) = 0;
    const float k = spread * K(0x005bcd60);  // degrees / 180
    LF(0x10) = k;
    LF(0x14) = k * K(0x005bce38);            // -pi
    const int32_t r1 = static_cast<int32_t>(orig::fn_005391cd());
    L(0x18) = static_cast<uint32_t>(r1);
    const float range = k * K(0x005bcb70) - LF(0x14);
    std::memset(f + 0x4c, 0, 0x44);
    L(0x74) = 0x3f800000;
    L(0x88) = 0x3f800000;
    L(0x8c) = 1;
    LF(0x14) = static_cast<float>(r1) * range * K(0x005bcbc0) + LF(0x14);
    float s, c;
    SinCos(LF(0x14), &s, &c);
    LF(0x4c) = c;
    LF(0x50) = -s;
    LF(0x5c) = s;
    LF(0x60) = c;
    orig::fn_004e36a0(u(f + 0x34), u(f + 0x4c), 1, u(f + 0x28));
    orig::fn_004e7780(u(f + 0x28), u(f + 0x28));
    const int32_t r2 = static_cast<int32_t>(orig::fn_005391cd());
    LF(0x10) = static_cast<float>(r2) * K(0x005bce34) + K(0x005bcb30);
    const int32_t r3 = static_cast<int32_t>(orig::fn_005391cd());
    if (static_cast<int32_t>((r3 * 100) & 0xffff8000) < 0x190000 && !static_cast<uint8_t>(keepSide))
    {
        LF(0x1c) = LF(0x28) * LF(0x10) + LF(0x1c);
        LF(0x20) = LF(0x2c) * LF(0x10) + LF(0x20);
    }
    else
    {
        LF(0x1c) = LF(0x1c) - LF(0x28) * LF(0x10);
        LF(0x20) = LF(0x20) - LF(0x2c) * LF(0x10);
    }
    orig::fn_00471a00(u(f + 0x34), u(f + 0x1c), u(f + 0x40), fb(speed * Field<float>(target, 0x948)), 0xc280b23a,
                      0);
    if (LF(0x3c) < K(0x0056c05c))
    {
        Field<uint32_t>(target, 0x484) &= 0xcfffffff;
        orig::fn_00471a00(u(f + 0x34), u(f + 0x1c), u(f + 0x40),
                          fb(speed * K(0x005bcb4c) * Field<float>(target, 0x948)), 0xc280b23a, 0);
    }
    orig::fn_00454c90(u(target), 0, 1);
    orig::fn_004577b0(u(target), 0);
    Field<uint32_t>(target, 0x39c) = orig::fn_00455a00(Field<uint32_t>(target, 0x39c));
    Field<uint32_t>(target, 0x398) = orig::fn_00455a00(Field<uint32_t>(target, 0x398));
    orig::fn_00458510(u(f + 0x34), u(target));
    VCall<void>(target, 0xf8 / 4, 0x170000u, 0u);
    Field<uint32_t>(target, 0x490) = 0x41000000;  // 8.0f
    const int32_t r4 = static_cast<int32_t>(orig::fn_005391cd());
    VCall<void>(target, 0x19c / 4, static_cast<uint32_t>(((r4 << 1) >> 15) + 0x49), 1u, 0u, 0u, 0u, 0u);
    return VCall<uint32_t>(target, 0x1b8 / 4, 0x195u);
}

static uint32_t Character_Launch_Abi(uint32_t eax, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Character_Launch(B(eax), B(a0), bf(a1), bf(a2), a3);
}
ABI_ENTRY_00431020(Character_Launch_Entry, Character_Launch_Abi)
HOOK(0x00431020, "Character_Launch", Character_Launch_Entry, nullptr);

// 0x00431310 Character_GoRagdoll (ecx = character; stack: damage (or 0), quiet (byte); callee pops 8; caller
// 0x004353b0). Damage: +0 flags (bit 7), +0x10 id, +0x18 direction, +0x24 amount, +0x2c attacker. 0x00431720(this,
// damage), virtual +0xf8(0x60000, 0), removed from list Game+0xa84 +0xe8c when in it (0x00454690 / 0x00454750).
// A flagged hit from an attacker at least 10 s after the last launch (Game+0xb34) launches the character
// (Character_Launch, speed rand 2.5..3) when virtual +0x214 allows. Counts the down objects of list Game+0xa84 +0xe7c
// (virtual +0x10c and 0x00431000) unless 0x00426630 or quiet. A hit over 10050 from an attacker (not quiet, +0x214):
// at the launch time itself a 25% roll launches (spread 45) else the impulse is the direction * amount * rand 2..5
// * 0.15; otherwise direction * amount * 0.15; the impulse is applied (virtual +0x1ac(1), +0x154 / +0x158,
// 0x00458440), physics off, +0x490 = 8.0, virtual +0x19c(0x49 + rand / 16384, 1, 0, 0, 0, 0), +0x48c |= 0x80,
// +0x1b8(0x195). Otherwise (after +0x154 / +0x158): with bodies (+0x1bc) and at most 3 down objects, the ragdoll
// impulse (+0x9f8 direction, +0xa08 amount, +0xa04 id; zeros and -1 without damage), +0x9f0 = 5.0, +0x9f4 = 0.5,
// physics on, added to list +0xe9c, virtual +0x1b8(0x190); else virtual +0x210 ? 0x00427100 : Object_Kill
// 0x00455640(damage).
// Enhancement point: true for a dead character kept past its removal (hook/enhance/persistence.cpp). Null = original.
bool (*g_isHeldBody)(void* character) = nullptr;
// Enhancement point: how many characters may be down or ragdolled before a death is animated instead (original 3, i.e.
// at most four ragdolls; a 2003 CPU budget). Set by hook/enhance/persistence.cpp ([persistence] ragdoll_limit).
uint32_t g_ragdollLimit = 3;

uint32_t Character_GoRagdoll(uint8_t* self, uint8_t* damage, uint32_t quiet)
{
    alignas(4) uint8_t f[0x10];  // esp+0x10..+0x1c of the original (iterator, then impulse)
    auto LF = [&f](uint32_t offset) -> float& { return *reinterpret_cast<float*>(f + offset - 0x10); };
    auto L = [&f](uint32_t offset) -> uint32_t& { return *reinterpret_cast<uint32_t*>(f + offset - 0x10); };
    std::memset(f, 0, sizeof(f));
    const uint8_t isQuiet = static_cast<uint8_t>(quiet);
    orig::fn_00431720(u(self), u(damage));
    VCall<void>(self, 0xf8 / 4, 0x60000u, 0u);
    if (orig::fn_00454690(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe8c), u(self)) & 0xff)
        orig::fn_00454750(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe8c), u(self));
    if (!isQuiet && damage && Field<uint32_t>(damage, 0x2c) && static_cast<int8_t>(damage[0]) < 0 &&
        GameGlobal<float>(0x00604a98) - Field<float>(g_game, 0xb34) >= K(0x005bcb68) &&
        (VCall<uint8_t>(self, 0x214 / 4)))
    {
        const float launchSpeed = orig::fn_0046e2c0(0, 0x40200000, 0x40400000);  // 2.5 .. 3.0
        return Character_Launch(Field<uint8_t*>(damage, 0x2c), self, launchSpeed, 0.0f, 0);
    }
    uint32_t downCount = 0;  // the original keeps it in its damage argument slot
    if (!(orig::fn_00426630(u(self)) & 0xff) && !isQuiet && Field<uint32_t>(self, 0x1bc))
    {
        L(0x10) = 0;
        L(0x14) = 0;
        for (uint8_t* obj = B(orig::fn_004e7340(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe7c), u(f)));
             obj; obj = B(orig::fn_004e73c0(u(f))))
        {
            // Remaster: bodies held by the persistence enhancement (g_isHeldBody) do not count, or after four held
            // bodies every later death was animated without physics and stayed in the air (2026-10-12).
            if ((VCall<uint8_t>(obj, 0x10c / 4)) && (Object_IsDownOrRagdoll(obj) & 0xff) &&
                !(g_isHeldBody && g_isHeldBody(obj)))
                ++downCount;
        }
        if (L(0x14))
            --Field<int32_t>(P(L(0x14)), 0x18);
    }
    if (damage && Field<uint32_t>(damage, 0x2c) && Field<float>(damage, 0x24) > K(0x005bcf34) && !isQuiet &&
        (VCall<uint8_t>(self, 0x214 / 4)))
    {
        if (GameGlobal<float>(0x00604a98) == Field<float>(g_game, 0xb34))
        {
            const int32_t roll = static_cast<int32_t>(orig::fn_005391cd());
            if (static_cast<int32_t>((roll * 100) & 0xffff8000) < 0xc8000 && (VCall<uint8_t>(self, 0x214 / 4)))
            {
                const float launchSpeed = orig::fn_0046e2c0(0, 0x40200000, 0x40400000);
                return Character_Launch(Field<uint8_t*>(damage, 0x2c), self, launchSpeed, bf(0x42340000), 0);
            }
            const float scale = orig::fn_0046e2c0(0, 0x40000000, 0x40a00000);  // 2.0 .. 5.0
            LF(0x10) = Field<float>(damage, 0x18) * Field<float>(damage, 0x24) * scale * K(0x005bcedc);
            LF(0x14) = Field<float>(damage, 0x1c) * Field<float>(damage, 0x24) * scale * K(0x005bcedc);
            LF(0x18) = Field<float>(damage, 0x20) * Field<float>(damage, 0x24) * scale * K(0x005bcedc);
        }
        else
        {
            LF(0x10) = Field<float>(damage, 0x18) * Field<float>(damage, 0x24) * K(0x005bcedc);
            LF(0x14) = Field<float>(damage, 0x1c) * Field<float>(damage, 0x24) * K(0x005bcedc);
            LF(0x18) = Field<float>(damage, 0x20) * Field<float>(damage, 0x24) * K(0x005bcedc);
        }
        VCall<void>(self, 0x1ac / 4, 1u);
        if (!VCall<uint8_t>(self, 0x154 / 4))
            VCall<void>(self, 0x158 / 4);
        orig::fn_00458440(0, u(f), u(self));
        orig::fn_00454c90(u(self), 0, 1);
        orig::fn_004577b0(u(self), 0);
        Field<uint32_t>(self, 0x490) = 0x41000000;  // 8.0f
        const int32_t r = static_cast<int32_t>(orig::fn_005391cd());
        VCall<void>(self, 0x19c / 4, static_cast<uint32_t>(((r << 1) >> 15) + 0x49), 1u, 0u, 0u, 0u, 0u);
        Field<uint32_t>(self, 0x48c) |= 0x80;
        return VCall<uint32_t>(self, 0x1b8 / 4, 0x195u);
    }
    if (!VCall<uint8_t>(self, 0x154 / 4))
        VCall<void>(self, 0x158 / 4);
    if (Field<uint32_t>(self, 0x1bc) && downCount <= g_ragdollLimit)
    {
        if (damage)
        {
            Field<uint32_t>(self, 0x9f8) = Field<uint32_t>(damage, 0x18);
            Field<uint32_t>(self, 0x9fc) = Field<uint32_t>(damage, 0x1c);
            Field<uint32_t>(self, 0xa00) = Field<uint32_t>(damage, 0x20);
            Field<uint32_t>(self, 0xa08) = Field<uint32_t>(damage, 0x24);
            Field<uint32_t>(self, 0xa04) = Field<uint32_t>(damage, 0x10);
        }
        else
        {
            L(0x10) = 0;
            Field<uint32_t>(self, 0x9f8) = 0;
            L(0x14) = 0;
            L(0x18) = 0;
            Field<uint32_t>(self, 0x9fc) = 0;
            Field<uint32_t>(self, 0xa00) = 0;
            Field<uint32_t>(self, 0xa04) = 0xffffffffu;
        }
        Field<uint32_t>(self, 0x9f0) = 0x40a00000;  // 5.0f
        Field<uint32_t>(self, 0x9f4) = 0x3f000000;  // 0.5f
        orig::fn_004577b0(u(self), 1);
        orig::fn_00454750(Field<uint32_t>(Field<uint8_t*>(g_game, 0xa84), 0xe9c), u(self));
        return VCall<uint32_t>(self, 0x1b8 / 4, 0x190u);
    }
    if (VCall<uint8_t>(self, 0x210 / 4))
        return orig::fn_00427100(u(self));
    return orig::fn_00455640(u(self), u(damage));
}

static uint32_t Character_GoRagdoll_Abi(uint32_t ecx, uint32_t a0, uint32_t a1)
{
    return Character_GoRagdoll(B(ecx), B(a0), a1);
}
ABI_ENTRY_00431310(Character_GoRagdoll_Entry, Character_GoRagdoll_Abi)
HOOK(0x00431310, "Character_GoRagdoll", Character_GoRagdoll_Entry, nullptr);

// ================================================================================================================
// Data loading
// ================================================================================================================

// 0x00447660 (eax = context {+4 MOPP code, +8 data}, edi = load descriptor {table, done, swap}; no direct callers: a
// fix-up callback, guess). 0x00510c20(ecx = code; data + 0x20) stores the code's data pointer (+0x10) and +0x14 = 1;
// then, unless the descriptor is done, the dwords +0, +4, +8, +0xc of the code are located in the descriptor's
// table (binary search 0x004e0f40, as 0x00447920) and byte-swapped (0x004f60b0) when the descriptor says so.
uint32_t fn_00447660(uint8_t* context, uint32_t* descriptor)
{
    uint8_t* code = Field<uint8_t*>(context, 4);
    uint32_t result = orig::fn_00510c20(u(code), Field<uint32_t>(context, 8) + 0x20);
    for (uint32_t offset = 0; offset < 0x10; offset += 4)
    {
        if (descriptor[1] != 0)
            continue;
        uint32_t key = u(code + offset);
        uint8_t* table = Field<uint8_t*>(B(descriptor[0]), 8);
        uint8_t* entry = nullptr;
        const uint32_t first = Field<uint32_t>(table, 0x1c);
        if (first != 0 && Field<uint32_t>(table, 0x20) != 0)
        {
            uint8_t* search = Field<uint8_t*>(table, 0x24);
            Field<uint32_t>(search, 0) = first;
            Field<uint32_t>(search, 0x18) = u(&key);
            orig::fn_004e0f40(u(search));
            if (Field<uint32_t>(search, 0xc) == 0)
                entry = Field<uint8_t**>(search, 0x10)[Field<uint32_t>(search, 8)];
        }
        uint8_t* range = entry + Field<uint16_t>(entry, 2);
        const uint32_t target = Field<uint32_t>(range, 4) - Field<uint32_t>(range, 0) + u(code + offset);
        result = descriptor[2];
        if (descriptor[2] != 0)
            result = orig::fn_004f60b0(target, 0, 0, 0);
    }
    return result;
}

static uint32_t fn_00447660_Abi(uint32_t eax, uint32_t edi)
{
    return fn_00447660(B(eax), reinterpret_cast<uint32_t*>(edi));
}
ABI_ENTRY_00447660(fn_00447660_Entry, fn_00447660_Abi)
HOOK(0x00447660, "fn_00447660", fn_00447660_Entry, nullptr);
