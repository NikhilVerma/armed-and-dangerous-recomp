// Recovered: game-written Havok extensions (batch physics_04: the speed-trap shape and agents 0x004a85a0-0x004a9180 of
// the havok-ext block, the type-0x19 shape / agent / listener classes 0x004df540-0x004dfda0 of the physics-glue block)
// and the PMS heap allocator 0x004e07f0 / 0x004e08d0. Static recovery 2026-10-05, compile-checked only (the game was
// not run); the oracle (tools/compare.py) is still to do.
//
// Havok itself is not decompiled: Havok functions are called through orig::fn_XXXXXXXX exactly as the original does,
// and Havok objects are accessed with Field<T> at the offsets documented in docs/subsystems/physics.md (entity +0x08
// motion, +0x44 world; motion +0x40 linear velocity; hkReferencedObject u16 +4 memory size, u16 +6 reference count;
// hkMemory singleton [0x005fed84] virtual +0x10 alloc(size, class), +0x14 free(p, size, class)). Havok library code
// found inside the batch was skipped (see the batch report): 0x004a7080-0x004a7a60 (polyhedron mass integrals, the
// tail of hkInertiaTensorComputer), 0x004a7b70 / 0x004a7ba0 / 0x004a83f0 (shape display builder), 0x004a84a0 /
// 0x004a84e0 / 0x004a8530 (user shape display builder singleton), 0x004df0d0 (hard key-frame utility), 0x004dfdb0 /
// 0x004dfe00 / 0x004dfe40 (debug display singleton), 0x004dfe80 / 0x004dfeb0 / 0x004dff20 (third singleton).
//
// Classes identified here (vtable scan of orig/pc/GameData/game.unpacked.exe):
//   0x005b0ad8 SpeedTrapShape (shape type 0x12: slots 1/2 0x004564a0 return 0x12; class name from the timer string
//              "TthkSpeedTrapShape shape/are penetrating" at 0x0058e69c): +0x0c child shape, +0x10 / +0x14 floats,
//              +0x18 velocity scale, +0x1c entity whose velocity sweeps the trap, +0x20 byte mode, +0x24 float.
//   0x0058e674 SpeedTrapAgent (slots 0x004a3fa0, 0x004a88a0, 0x004a9020, 0x004a8f70, 0x004a8880; ctor 0x004a8790) and
//   0x0058e688 the swapped (shape vs speed trap) agent (0x004a3fa0, 0x004a90e0, 0x004a9180, 0x004a9160, 0x004a90d0).
//   0x0058b760 Type19Shape (getType 0x004dfbe0 returns 0x19; ctor 0x004dfaa0): wraps a child shape (+0x28) and an
//              entity (+0x2c); +0x10..+0x1c a sweep vector, +0x20 = limit^2, +0x24 = limit (guess: a speed limit).
//   0x0058b7a8 Type19Agent (Havok convex-convex agent subclass: ctor 0x0051a5b0, size 0x210, +0x200 flags).
//   0x0058b7a4 / +8 0x0058b798 / +0xc 0x0058b790 Type19Listener (ctor 0x004df780, called by PhysicsManager_Init).
//
// Conventions: every function's comment gives its address, its register/stack ABI (as the original reads it), what it
// does and the evidence. "guess" marks interpretations not proven by strings or behaviour. Large functions keep the
// original stack frame as one byte array (Frame), addressed with the original's esp offsets after the register pushes.
#include <intrin.h>

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
inline uint8_t* B(uint32_t v)
{
    return reinterpret_cast<uint8_t*>(v);
}
inline float bf(uint32_t v)
{
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
// A float constant of the game's .rdata (0x0056c05c = 0.0, 0x005bcb2c = 1.0, 0x005bcb5c = -1.0, 0x005bcbb0 = 1e-4,
// 0x005bcb68 = 10.0, 0x005bcbec = 0.01, 0x005bce08 = 25.0, 0x005bcbdc = 5.0).
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
    uint32_t A(uint32_t o)
    {
        return reinterpret_cast<uint32_t>(b + o);
    }
};
// hkMemory singleton [0x005fed84]: virtual +0x14 free(p, size, class).
inline uint32_t HkFree(uint32_t p, uint32_t size, uint32_t cls)
{
    return VCall<uint32_t>(GameGlobal<void*>(0x005fed84), 0x14 / 4, p, size, cls);
}
// The inlined Havok monitor-stream timer (HK_TIMER_BEGIN / END): when the write pointer [0x0063a130] is below the end
// [0x0063a134], stores {tag, rdtsc low dword} and advances the pointer by 0xc. Returns whether it wrote (eax then holds
// the rdtsc low dword in the original).
inline bool MonitorTimer(uint32_t tag, uint32_t& tsc)
{
    uint8_t* p = GameGlobal<uint8_t*>(0x0063a130);
    if (p >= GameGlobal<uint8_t*>(0x0063a134))
        return false;
    Field<uint32_t>(p, 0) = tag;
    tsc = static_cast<uint32_t>(__rdtsc());
    Field<uint32_t>(p, 4) = tsc;
    GameGlobal<uint8_t*>(0x0063a130) = p + 0xc;
    return true;
}
// The inlined vector normalisation factor: 0 when the squared length equals 0.0, else 1 / sqrt(squared length).
inline float InvSqrtOrZero(float sq)
{
    if (sq == K(0x0056c05c))
        return K(0x0056c05c);
    return K(0x005bcb2c) / std::sqrt(sq);
}
// The collidable copy the agents pass on (8 dwords; +0x10 = shape).
struct Collidable8
{
    uint32_t d[8];
};
}  // namespace

// Tail-jump forwarders (the jump leaves the caller's frame intact; the target's ret N pops): ecx and N stack arguments
// in, eax out, ret 4N.
#define P4_ENTRY_ECX_1(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 28] __asm push ecx __asm call impl __asm add esp, 8 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 4 }
#define P4_ENTRY_ECX_3(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 36] __asm push dword ptr [esp + 36] __asm push dword ptr [esp + 36] __asm push ecx __asm call impl __asm add esp, 16 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 12 }
#define P4_ENTRY_ECX_5(stub, impl) static __declspec(naked) void stub() { __asm push ecx __asm push edx __asm push ebx __asm push esi __asm push edi __asm push ebp __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push dword ptr [esp + 44] __asm push ecx __asm call impl __asm add esp, 24 __asm pop ebp __asm pop edi __asm pop esi __asm pop ebx __asm pop edx __asm pop ecx __asm ret 20 }

// ================================================================================================================
// havok-ext: SpeedTrapShape (vtable 0x005b0ad8, shape type 0x12)
// ================================================================================================================

// 0x004a8720 (eax = out (4 floats), edx = speed-trap shape; plain ret; no result): the sweep vector. When the shape has
// an entity (+0x1c): out = the entity's linear velocity (motion +0x40, 4 floats) scaled by +0x18; else out = 0.
// Callers 0x004a85a0, 0x004a88a0.
void SpeedTrapShape_GetSweep(float* out, uint8_t* shape)
{
    uint8_t* entity = Field<uint8_t*>(shape, 0x1c);
    if (!entity)
    {
        reinterpret_cast<uint32_t*>(out)[3] = 0;
        reinterpret_cast<uint32_t*>(out)[2] = 0;
        reinterpret_cast<uint32_t*>(out)[1] = 0;
        reinterpret_cast<uint32_t*>(out)[0] = 0;
        return;
    }
    std::memcpy(out, Field<uint8_t*>(entity, 8) + 0x40, 16);
    const float s = Field<float>(shape, 0x18);
    out[0] = s * out[0];
    out[1] = s * out[1];
    out[2] = s * out[2];
    out[3] = s * out[3];
}

static void SpeedTrapShape_GetSweep_Abi(uint32_t eax, uint32_t edx)
{
    SpeedTrapShape_GetSweep(reinterpret_cast<float*>(eax), B(edx));
}
ABI_ENTRY_004a8720(SpeedTrapShape_GetSweep_Entry, SpeedTrapShape_GetSweep_Abi)
HOOK(0x004a8720, "SpeedTrapShape_GetSweep", SpeedTrapShape_GetSweep_Entry, nullptr);

// 0x004a85a0 (ecx = shape; stack: transform, tolerance, out; ret 0xc): vtable 0x005b0ad8 slot 3 (getAabb): the child
// shape's (+0x0c) getAabb (virtual +0xc); when +0x18 > 1e-4 ([0x005bcbb0]) the box (min out[0..3], max out[4..7]) is
// extended by the sweep (0x004a8720): min = min(min, min + sweep), max = max(max, max + sweep), with the original's
// NaN handling. Returns the child's eax (the original leaves an x87 status word there on the long path; getAabb
// callers ignore it). Executed in recorded runs (no direct callers).
uint32_t SpeedTrapShape_GetAabb(uint8_t* shape, uint32_t transform, uint32_t tolerance, float* out)
{
    uint8_t* child = Field<uint8_t*>(shape, 0xc);
    if (!(Field<float>(shape, 0x18) > K(0x005bcbb0)))
        return VCall<uint32_t>(child, 0xc / 4, transform, tolerance, u(out));
    const uint32_t r = VCall<uint32_t>(child, 0xc / 4, transform, tolerance, u(out));
    alignas(16) float v[4];
    SpeedTrapShape_GetSweep(v, shape);
    float t0 = v[0] + out[0];
    float t1 = v[1] + out[1];
    float t2 = v[2] + out[2];
    float t3 = v[3] + out[3];
    out[0] = (out[0] < t0) ? out[0] : t0;
    out[1] = (out[1] < t1) ? out[1] : t1;
    out[2] = (out[2] < t2) ? out[2] : t2;
    out[3] = (out[3] < t3) ? out[3] : t3;
    t0 = v[0] + out[4];
    t1 = v[1] + out[5];
    t2 = v[2] + out[6];
    t3 = v[3] + out[7];
    out[4] = (t0 < out[4]) ? out[4] : t0;
    out[5] = (out[5] > t1) ? out[5] : t1;
    out[6] = (out[6] > t2) ? out[6] : t2;
    out[7] = (out[7] > t3) ? out[7] : t3;
    return r;
}

static uint32_t SpeedTrapShape_GetAabb_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapShape_GetAabb(B(ecx), a0, a1, reinterpret_cast<float*>(a2));
}
ABI_ENTRY_004a85a0(SpeedTrapShape_GetAabb_Entry, SpeedTrapShape_GetAabb_Abi)
HOOK(0x004a85a0, "SpeedTrapShape_GetAabb", SpeedTrapShape_GetAabb_Entry, nullptr);

// 0x004a8780 (ecx = shape; tail jump with the caller's frame intact): vtable 0x005b0ad8 slot 6 (castRay, guess: the
// speed-trap agent calls it with (from, to, result)): a tail jump to the child shape's (+0x0c) virtual +0x18. Slot 6
// takes 3 stack arguments (0x004a45e0 ret 0xc), so it is reproduced as a 3-argument call. Not executed in recorded
// runs.
uint32_t SpeedTrapShape_Slot6(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0xc), 0x18 / 4, a0, a1, a2);
}

static uint32_t SpeedTrapShape_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapShape_Slot6(B(ecx), a0, a1, a2);
}
P4_ENTRY_ECX_3(SpeedTrapShape_Slot6_Entry, SpeedTrapShape_Slot6_Abi)
HOOK(0x004a8780, "SpeedTrapShape_Slot6", SpeedTrapShape_Slot6_Entry, nullptr);

// ================================================================================================================
// havok-ext: speed-trap collision agents
// ================================================================================================================

// 0x004a8790 (eax = input, ecx = speed-trap collidable A, ebx = agent memory; stack: B; ret 4): agent constructor:
// vtable 0x0058e674; copies A (8 dwords) with its shape (+0x10) replaced by the speed-trap shape's child (+0x0c), and
// stores in +8 the child agent the dispatcher 0x00513420 (ecx = [input]; &copy, B, input) creates. Returns the agent.
// Callers: the create functions 0x004a8810 / 0x004a8840 (registered by 0x004a87e0).
uint32_t SpeedTrapAgent_Construct(uint32_t* input, const uint32_t* a, uint8_t* agent, uint32_t b)
{
    Field<uint32_t>(agent, 0) = 0x0058e674;
    const uint32_t child = Field<uint32_t>(B(a[4]), 0xc);
    Collidable8 copy;
    std::memcpy(copy.d, a, sizeof(copy.d));
    copy.d[4] = child;
    Field<uint32_t>(agent, 8) = orig::fn_00513420(input[0], u(&copy), b, u(input));
    return u(agent);
}

static uint32_t SpeedTrapAgent_Construct_Abi(uint32_t eax, uint32_t ecx, uint32_t ebx, uint32_t a0)
{
    return SpeedTrapAgent_Construct(reinterpret_cast<uint32_t*>(eax), reinterpret_cast<const uint32_t*>(ecx), B(ebx),
                                    a0);
}
ABI_ENTRY_004a8790(SpeedTrapAgent_Construct_Entry, SpeedTrapAgent_Construct_Abi)
HOOK(0x004a8790, "SpeedTrapAgent_Construct", SpeedTrapAgent_Construct_Entry, nullptr);

// 0x004a87e0 (esi = collision dispatcher; plain ret; caller PhysicsManager_Init 0x00411960):
// hkCollisionDispatcher_registerAgent 0x00513270 (ecx = dispatcher; createFn, typeA, typeB) for 0x004a8810 (0x12, 0)
// and 0x004a8840 (0, 0x12). Returns the second registration's eax.
uint32_t registerAgents_speedTrap(uint32_t dispatcher)
{
    orig::fn_00513270(dispatcher, 0x004a8810u, 0x12, 0);
    return orig::fn_00513270(dispatcher, 0x004a8840u, 0, 0x12);
}

static uint32_t registerAgents_speedTrap_Abi(uint32_t esi)
{
    return registerAgents_speedTrap(esi);
}
ABI_ENTRY_004a87e0(registerAgents_speedTrap_Entry, registerAgents_speedTrap_Abi)
HOOK(0x004a87e0, "registerAgents_speedTrap", registerAgents_speedTrap_Entry, nullptr);

// 0x004a8880 (ecx = agent; stack: a0; ret 4): vtable 0x0058e674 slot 4 (cleanup, guess; also reached through the
// thunk 0x004a90d0 of 0x0058e688): the child agent's (+8, not null-checked) virtual +0x10(a0), then the agent's
// virtual 0 (deleting destructor)(1). Executed in recorded runs.
uint32_t SpeedTrapAgent_Cleanup(uint8_t* agent, uint32_t a0)
{
    VCall<uint32_t>(Field<uint8_t*>(agent, 8), 0x10 / 4, a0);
    return VCall<uint32_t>(agent, 0, 1u);
}

static uint32_t SpeedTrapAgent_Cleanup_Abi(uint32_t ecx, uint32_t a0)
{
    return SpeedTrapAgent_Cleanup(B(ecx), a0);
}
ABI_ENTRY_004a8880(SpeedTrapAgent_Cleanup_Entry, SpeedTrapAgent_Cleanup_Abi)
HOOK(0x004a8880, "SpeedTrapAgent_Cleanup", SpeedTrapAgent_Cleanup_Entry, nullptr);

// 0x004a88a0 (ecx = agent; stack: A (speed-trap collidable), B, input, collector; ret 0x10): vtable 0x0058e674 slot 1
// (processCollision), timer "Ttspeedtrap shape" (0x0058e6ec) / "Et" (0x0058e6e8). With the shape (A +0x10) sweep
// (0x004a8720) v: when +0x18 > 1e-4 and |v|^2 > +0x10 * +0x18^2, a ray is cast from A's position p ([A +0x14] +0x50)
// to end = p + v + normalize(v) * ((input +0xc * 0 + shape +0x14) * 10), both moved into B's frame ([B +0x14] +0x20,
// Havok 0x004de720), against B's shape (virtual +0x18(from, to, result {normal, fraction 1, -1, -1})); a debug line
// p..end is drawn (debug display [0x00600204], 0x004dfe00, colour 0xff0000ff, tag [0x0063a124]). On a hit, with the
// world hit normal n (Havok 0x004de780, normalised), d = normalize(p - end), T = max(0, fraction * |p - end| -
// (shape +0x14) / (n.d)): when T <= |v| the trap entity's (+0x1c) velocity is rescaled: mode byte +0x20 == 0: v *= T /
// |v|; else the component along -n is scaled by (1 - q) * +0x24 + q (q = T / |v|) and the whole velocity by +0x24 ...
// exactly as the disassembly (see the code); it is set through Body_wakeAndSetLinearVelocity 0x00456250 and a second
// debug line (colour 0xff00ff00) shows it. Always: the child agent's (+8) processCollision (virtual +4) with A's copy
// whose shape is the trap's child (+0x0c). Returns the child's eax, or the rdtsc low dword when the end timer wrote.
// Executed in recorded runs.
uint32_t SpeedTrapAgent_processCollision(uint8_t* agent, const uint32_t* a, uint8_t* b, uint8_t* input,
                                         uint32_t collector)
{
    Frame<0xf0> f;  // esp offsets after the three register pushes
    uint32_t tsc = 0;
    MonitorTimer(0x0058e6ec, tsc);
    f.L(0x6c) = u(agent);
    std::memcpy(f.b + 0xb0, a, 0x20);
    uint8_t* shape = B(a[4]);
    SpeedTrapShape_GetSweep(&f.F(0x40), shape);
    if (Field<float>(shape, 0x18) > K(0x005bcbb0))
    {
        const float sq = (f.F(0x48) * f.F(0x48) + f.F(0x44) * f.F(0x44)) + f.F(0x40) * f.F(0x40);
        f.F(0x68) = sq;
        const float s18 = Field<float>(shape, 0x18);
        if (sq > (s18 * Field<float>(shape, 0x10)) * s18)
        {
            std::memcpy(f.b + 0x70, B(a[5]) + 0x50, 16);
            uint8_t* bshape = Field<uint8_t*>(b, 0x10);
            orig::fn_004de720(f.A(0xd0), Field<uint32_t>(b, 0x14) + 0x20, f.A(0x70));
            f.F(0x3c) = Field<float>(input, 0xc) * K(0x0056c05c);
            const float e0 = f.F(0x70) + f.F(0x40);
            const float e1 = f.F(0x74) + f.F(0x44);
            f.F(0x88) = f.F(0x78) + f.F(0x48);
            f.F(0x8c) = f.F(0x4c) + f.F(0x7c);
            f.F(0x20) = e0 - f.F(0x70);
            const float d1 = e1 - f.F(0x74);
            f.F(0x28) = f.F(0x88) - f.F(0x78);
            f.F(0x2c) = f.F(0x8c) - f.F(0x7c);
            const float inv = InvSqrtOrZero((f.F(0x20) * f.F(0x20) + f.F(0x28) * f.F(0x28)) + d1 * d1);
            const float n0 = f.F(0x20) * inv;
            f.F(0x24) = d1 * inv;
            f.F(0x28) = f.F(0x28) * inv;
            f.F(0x2c) = f.F(0x2c) * inv;
            const float m = (f.F(0x3c) + Field<float>(shape, 0x14)) * K(0x005bcb68);
            f.F(0x1c) = m;
            const float n0m = n0 * m;
            const float n1m = f.F(0x24) * f.F(0x1c);
            f.F(0x28) = f.F(0x28) * f.F(0x1c);
            f.F(0x2c) = f.F(0x2c) * f.F(0x1c);
            f.F(0x80) = n0m + e0;
            f.F(0x84) = n1m + e1;
            f.F(0x88) = f.F(0x28) + f.F(0x88);
            f.F(0x8c) = f.F(0x2c) + f.F(0x8c);
            orig::fn_004de720(f.A(0xe0), Field<uint32_t>(b, 0x14) + 0x20, f.A(0x80));
            uint32_t tag = GameGlobal<uint32_t>(0x0063a124);
            uint32_t display = GameGlobal<uint32_t>(0x00600204);
            orig::fn_004dfe00(display, f.A(0x70), f.A(0x80), 0xff0000ffu, tag);
            f.L(0xa0) = 0x3f800000;
            f.L(0xa4) = 0xffffffffu;
            f.L(0xa8) = 0xffffffffu;
            if (VCall<uint32_t>(bshape, 0x18 / 4, f.A(0xd0), f.A(0xe0), f.A(0x90)) != 0)
            {
                const float a0 = f.F(0x70) - f.F(0x80);
                const float a1 = f.F(0x74) - f.F(0x84);
                const uint32_t bt = Field<uint32_t>(b, 0x14) + 0x20;
                f.F(0x48) = f.F(0x78) - f.F(0x88);
                f.F(0x1c) = std::sqrt((a0 * a0 + f.F(0x48) * f.F(0x48)) + a1 * a1);
                const float il = K(0x005bcb2c) / f.F(0x1c);
                f.F(0x18) = il;
                f.F(0x40) = il * a0;
                f.F(0x44) = a1 * f.F(0x18);
                f.F(0x48) = f.F(0x48) * f.F(0x18);
                orig::fn_004de780(f.A(0x90), bt, f.A(0x90));
                const float ni =
                    InvSqrtOrZero((f.F(0x90) * f.F(0x90) + f.F(0x98) * f.F(0x98)) + f.F(0x94) * f.F(0x94));
                f.F(0x20) = f.F(0x90) * ni;
                f.F(0x24) = f.F(0x94) * ni;
                f.F(0x28) = f.F(0x98) * ni;
                f.F(0x2c) = f.F(0x9c) * ni;
                const float speed = std::sqrt(f.F(0x68));
                const float is = K(0x005bcb2c) / speed;
                f.F(0x68) = is;
                const float big = ((is * f.F(0x1c)) * f.F(0xa0)) * speed;
                const float zs = f.F(0x3c) + Field<float>(shape, 0x14);
                const float dot = (f.F(0x20) * f.F(0x40) + f.F(0x28) * f.F(0x48)) + f.F(0x24) * f.F(0x44);
                const float t = big - zs / dot;
                f.F(0x1c) = t;
                if (t < K(0x0056c05c))
                    f.L(0x1c) = 0;
                if (!(f.F(0x1c) > speed))
                {
                    uint8_t* entity = Field<uint8_t*>(shape, 0x1c);
                    std::memcpy(f.b + 0x50, Field<uint8_t*>(entity, 8) + 0x40, 16);
                    if (shape[0x20])
                    {
                        f.F(0x20) = -f.F(0x20);
                        const float ny = -f.F(0x24);
                        f.F(0x28) = -f.F(0x28);
                        f.F(0x2c) = -f.F(0x2c);
                        const float i2 =
                            InvSqrtOrZero((f.F(0x20) * f.F(0x20) + f.F(0x28) * f.F(0x28)) + ny * ny);
                        const float nx = f.F(0x20) * i2;
                        f.F(0x24) = ny * i2;
                        f.F(0x28) = f.F(0x28) * i2;
                        f.F(0x2c) = f.F(0x2c) * i2;
                        const float vn = (f.F(0x50) * nx + f.F(0x58) * f.F(0x28)) + f.F(0x54) * f.F(0x24);
                        f.F(0x18) = vn;
                        const float p0 = nx * vn;
                        const float p1 = f.F(0x24) * f.F(0x18);
                        f.F(0x48) = f.F(0x28) * f.F(0x18);
                        f.F(0x4c) = f.F(0x2c) * f.F(0x18);
                        f.F(0x3c) = f.F(0x68) * f.F(0x1c);
                        const float omq = K(0x005bcb2c) - f.F(0x3c);
                        f.F(0x68) = omq;
                        f.F(0x18) = omq;
                        f.F(0x40) = p0 * f.F(0x18);
                        const float p1q = p1 * f.F(0x18);
                        const float p2q = f.F(0x48) * f.F(0x18);
                        f.F(0x4c) = f.F(0x4c) * f.F(0x18);
                        const float g = Field<float>(shape, 0x24);
                        f.F(0x50) = f.F(0x50) * g;
                        f.F(0x54) = f.F(0x54) * g;
                        f.F(0x58) = f.F(0x58) * g;
                        f.F(0x5c) = f.F(0x5c) * g;
                        const float h = g * f.F(0x68) + f.F(0x3c);
                        f.F(0x18) = h;
                        f.F(0x40) = h * f.F(0x40);
                        f.F(0x44) = p1q * f.F(0x18);
                        const float p2h = p2q * f.F(0x18);
                        const float p3h = f.F(0x4c) * f.F(0x18);
                        f.F(0x50) = f.F(0x50) - f.F(0x40);
                        f.F(0x54) = f.F(0x54) - f.F(0x44);
                        f.F(0x58) = f.F(0x58) - p2h;
                        f.F(0x5c) = f.F(0x5c) - p3h;
                    }
                    else
                    {
                        const float k = f.F(0x68) * f.F(0x1c);
                        f.F(0x50) = f.F(0x50) * k;
                        f.F(0x54) = f.F(0x54) * k;
                        f.F(0x58) = f.F(0x58) * k;
                        f.F(0x5c) = f.F(0x5c) * k;
                    }
                    orig::fn_00456250(u(entity), f.A(0x50));
                    const float x = f.F(0x50) + f.F(0x70);
                    tag = GameGlobal<uint32_t>(0x0063a124);
                    const float y = f.F(0x54) + f.F(0x74);
                    display = GameGlobal<uint32_t>(0x00600204);
                    f.F(0x44) = y;
                    f.F(0x48) = f.F(0x58) + f.F(0x78);
                    f.F(0x4c) = f.F(0x5c) + f.F(0x7c);
                    f.F(0x40) = x + K(0x005bcbec);
                    orig::fn_004dfe00(display, f.A(0x70), f.A(0x40), 0xff00ff00u, tag);
                }
            }
        }
    }
    f.L(0xc0) = Field<uint32_t>(shape, 0xc);
    uint8_t* self = B(f.L(0x6c));
    uint32_t r = VCall<uint32_t>(Field<uint8_t*>(self, 8), 4 / 4, f.A(0xb0), u(b), u(input), collector);
    if (MonitorTimer(0x0058e6e8, tsc))
        r = tsc;
    return r;
}

static uint32_t SpeedTrapAgent_processCollision_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SpeedTrapAgent_processCollision(B(ecx), reinterpret_cast<const uint32_t*>(a0), B(a1), B(a2), a3);
}
ABI_ENTRY_004a88a0(SpeedTrapAgent_processCollision_Entry, SpeedTrapAgent_processCollision_Abi)
HOOK(0x004a88a0, "SpeedTrapAgent_processCollision", SpeedTrapAgent_processCollision_Entry, nullptr);

// 0x004a8f70 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e674 slot 3 (getClosestPoints),
// timer "Ttbv shape/get closest points" (0x0058e6c8) / "Et": the child agent's (+8) virtual +0xc with A's copy whose
// shape is the trap's child (+0x0c). Returns the child's eax, or the rdtsc low dword when the end timer wrote.
// Executed in recorded runs.
uint32_t SpeedTrapAgent_getClosestPoints(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t input,
                                         uint32_t collector)
{
    uint32_t tsc = 0;
    MonitorTimer(0x0058e6c8, tsc);
    Collidable8 copy;
    std::memcpy(copy.d, a, sizeof(copy.d));
    copy.d[4] = Field<uint32_t>(B(a[4]), 0xc);
    uint32_t r = VCall<uint32_t>(Field<uint8_t*>(agent, 8), 0xc / 4, u(&copy), b, input, collector);
    if (MonitorTimer(0x0058e6e8, tsc))
        r = tsc;
    return r;
}

static uint32_t SpeedTrapAgent_getClosestPoints_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return SpeedTrapAgent_getClosestPoints(B(ecx), reinterpret_cast<const uint32_t*>(a0), a1, a2, a3);
}
ABI_ENTRY_004a8f70(SpeedTrapAgent_getClosestPoints_Entry, SpeedTrapAgent_getClosestPoints_Abi)
HOOK(0x004a8f70, "SpeedTrapAgent_getClosestPoints", SpeedTrapAgent_getClosestPoints_Entry, nullptr);

// 0x004a9020 (ecx = agent; stack: A, B, input; ret 0xc): vtable 0x0058e674 slot 2 (arePenetrating), timer
// "TthkSpeedTrapShape shape/are penetrating" (0x0058e69c) / "Et": the child agent's (+8) virtual +8 with A's copy whose
// shape is the trap's child. Returns the child's al, upper eax bytes from the rdtsc when the end timer wrote, else
// the child's. Executed in recorded runs.
uint32_t SpeedTrapAgent_arePenetrating(uint8_t* agent, const uint32_t* a, uint32_t b, uint32_t input)
{
    uint32_t tsc = 0;
    MonitorTimer(0x0058e69c, tsc);
    Collidable8 copy;
    std::memcpy(copy.d, a, sizeof(copy.d));
    copy.d[4] = Field<uint32_t>(B(a[4]), 0xc);
    uint32_t r = VCall<uint32_t>(Field<uint8_t*>(agent, 8), 8 / 4, u(&copy), b, input);
    const uint32_t bl = r & 0xff;
    if (MonitorTimer(0x0058e6e8, tsc))
        r = tsc;
    return (r & 0xffffff00u) | bl;
}

static uint32_t SpeedTrapAgent_arePenetrating_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapAgent_arePenetrating(B(ecx), reinterpret_cast<const uint32_t*>(a0), a1, a2);
}
ABI_ENTRY_004a9020(SpeedTrapAgent_arePenetrating_Entry, SpeedTrapAgent_arePenetrating_Abi)
HOOK(0x004a9020, "SpeedTrapAgent_arePenetrating", SpeedTrapAgent_arePenetrating_Entry, nullptr);

// 0x004a90e0 (ecx = agent; stack: A, B (speed-trap collidable), input, collector; ret 0x10): vtable 0x0058e688 slot 1
// (shape vs speed trap): remembers the collector's point count (collector +4 -> +4), calls
// SpeedTrapAgent_processCollision with A and B swapped, then multiplies the normal (+0x10, 4 floats) of every point
// added (0x30 bytes each, array collector +4 -> +0) by -1 ([0x005bcb5c]). Returns collector +4 (eax as left by the
// loop test). Not executed in recorded runs.
uint32_t SpeedTrapSwappedAgent_processCollision(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input,
                                                uint8_t* collector)
{
    int32_t i = Field<int32_t>(Field<uint8_t*>(collector, 4), 4);
    SpeedTrapAgent_processCollision(agent, reinterpret_cast<const uint32_t*>(b), B(a), B(input), u(collector));
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

static uint32_t SpeedTrapSwappedAgent_processCollision_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2,
                                                           uint32_t a3)
{
    return SpeedTrapSwappedAgent_processCollision(B(ecx), a0, a1, a2, B(a3));
}
ABI_ENTRY_004a90e0(SpeedTrapSwappedAgent_processCollision_Entry, SpeedTrapSwappedAgent_processCollision_Abi)
HOOK(0x004a90e0, "SpeedTrapSwappedAgent_processCollision", SpeedTrapSwappedAgent_processCollision_Entry, nullptr);

// 0x004a9160 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058e688 slot 3:
// SpeedTrapAgent_getClosestPoints with A and B swapped. Not executed in recorded runs.
uint32_t SpeedTrapSwappedAgent_getClosestPoints(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input,
                                                uint32_t collector)
{
    return SpeedTrapAgent_getClosestPoints(agent, reinterpret_cast<const uint32_t*>(b), a, input, collector);
}

static uint32_t SpeedTrapSwappedAgent_getClosestPoints_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2,
                                                           uint32_t a3)
{
    return SpeedTrapSwappedAgent_getClosestPoints(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004a9160(SpeedTrapSwappedAgent_getClosestPoints_Entry, SpeedTrapSwappedAgent_getClosestPoints_Abi)
HOOK(0x004a9160, "SpeedTrapSwappedAgent_getClosestPoints", SpeedTrapSwappedAgent_getClosestPoints_Entry, nullptr);

// 0x004a9180 (ecx = agent; stack: A, B, input; ret 0xc): vtable 0x0058e688 slot 2: SpeedTrapAgent_arePenetrating with
// A and B swapped. Not executed in recorded runs.
uint32_t SpeedTrapSwappedAgent_arePenetrating(uint8_t* agent, uint32_t a, uint32_t b, uint32_t input)
{
    return SpeedTrapAgent_arePenetrating(agent, reinterpret_cast<const uint32_t*>(b), a, input);
}

static uint32_t SpeedTrapSwappedAgent_arePenetrating_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return SpeedTrapSwappedAgent_arePenetrating(B(ecx), a0, a1, a2);
}
ABI_ENTRY_004a9180(SpeedTrapSwappedAgent_arePenetrating_Entry, SpeedTrapSwappedAgent_arePenetrating_Abi)
HOOK(0x004a9180, "SpeedTrapSwappedAgent_arePenetrating", SpeedTrapSwappedAgent_arePenetrating_Entry, nullptr);

// ================================================================================================================
// physics-glue: Type19Agent (vtable 0x0058b7a8, a Havok convex-convex agent subclass)
// ================================================================================================================

// 0x004df540 (ecx = agent; stack: flags; ret 4): vtable 0x0058b7a8 slot 0, deleting destructor: the Havok
// convex-convex agent destructor body 0x0051a5f0 (ecx = agent), and when flags bit 0 is set hkMemory free(agent, u16
// +4, class 0x15). Returns the agent. Executed in recorded runs.
uint32_t Type19Agent_DeletingDestructor(uint8_t* agent, uint8_t flags)
{
    orig::fn_0051a5f0(u(agent));
    if (flags & 1)
        HkFree(u(agent), Field<uint16_t>(agent, 4), 0x15);
    return u(agent);
}

static uint32_t Type19Agent_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Agent_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004df540(Type19Agent_DeletingDestructor_Entry, Type19Agent_DeletingDestructor_Abi)
HOOK(0x004df540, "Type19Agent_DeletingDestructor", Type19Agent_DeletingDestructor_Entry, nullptr);

namespace
{
// The shared block of 0x004df570 for one collidable c whose shape (+0x10) is a Type19Shape: when the shape's +0x20
// (limit^2) < |v|^2 (v = the owner's (+0x1c) linear velocity, motion +0x40), the shape's sweep +0x10..+0x1c = v rotated
// into the frame at [c +0x14] +0x20 (transposed 3x3: dot with each column) times input +0x1c, w = 0, agent +0xa0 = 0
// and the result is true; otherwise the sweep is zeroed and the result is false.
bool Type19Agent_UpdateSweep(uint8_t* agent, uint8_t* c, uint8_t* input)
{
    uint8_t* motion = Field<uint8_t*>(Field<uint8_t*>(c, 0x1c), 8);
    uint8_t* shape = Field<uint8_t*>(c, 0x10);
    {
        const float x = Field<float>(motion, 0x40);
        const float y = Field<float>(motion, 0x44);
        const float z = Field<float>(motion, 0x48);
        const float sq = (x * x + y * y) + z * z;
        if (!(Field<float>(shape, 0x20) < sq))
        {
            Field<uint32_t>(shape, 0x1c) = 0;
            Field<uint32_t>(shape, 0x18) = 0;
            Field<uint32_t>(shape, 0x14) = 0;
            Field<uint32_t>(shape, 0x10) = 0;
            return false;
        }
    }
    const float x = Field<float>(motion, 0x40);
    const float y = Field<float>(motion, 0x44);
    const float z = Field<float>(motion, 0x48);
    const float* m = reinterpret_cast<const float*>(Field<uint8_t*>(c, 0x14) + 0x20);
    Field<float>(shape, 0x10) = (z * m[2] + y * m[1]) + x * m[0];
    Field<float>(shape, 0x14) = (z * m[6] + y * m[5]) + x * m[4];
    Field<float>(shape, 0x18) = (z * m[10] + y * m[9]) + x * m[8];
    Field<uint32_t>(shape, 0x1c) = 0;
    const float t = Field<float>(input, 0x1c);
    Field<float>(shape, 0x10) = t * Field<float>(shape, 0x10);
    Field<float>(shape, 0x14) = t * Field<float>(shape, 0x14);
    Field<float>(shape, 0x18) = t * Field<float>(shape, 0x18);
    Field<float>(shape, 0x1c) = t * Field<float>(shape, 0x1c);
    Field<uint32_t>(agent, 0xa0) = 0;
    return true;
}
}  // namespace

// 0x004df570 (ecx = agent; stack: A, B, input, collector; ret 0x10): vtable 0x0058b7a8 slot 1 (processCollision): for
// A then B, when the collidable's shape type (virtual +4) is 0x19, Type19Agent_UpdateSweep (flags |= 1 for A, += 2 for
// B when the sweep is live). When the flags differ from +0x200: agent +0xa0 = 0 and input byte +0x18 = 0 (guess: drop
// the cached separation). +0x200 = flags; the Havok convex-convex processCollision 0x0051a8c0 (ecx = agent; A, B, input,
// collector); input byte +0x18 is restored. Returns that eax with al = the saved byte. Executed in recorded runs.
uint32_t Type19Agent_ProcessCollision(uint8_t* agent, uint8_t* a, uint8_t* b, uint8_t* input, uint32_t collector)
{
    const uint8_t saved = input[0x18];
    uint32_t flags = 0;
    if (VCall<uint32_t>(Field<uint8_t*>(a, 0x10), 4 / 4) == 0x19 && Type19Agent_UpdateSweep(agent, a, input))
        flags = 1;
    if (VCall<uint32_t>(Field<uint8_t*>(b, 0x10), 4 / 4) == 0x19 && Type19Agent_UpdateSweep(agent, b, input))
        flags += 2;
    if (flags != Field<uint32_t>(agent, 0x200))
    {
        Field<uint32_t>(agent, 0xa0) = 0;
        input[0x18] = 0;
    }
    Field<uint32_t>(agent, 0x200) = flags;
    const uint32_t r = orig::fn_0051a8c0(u(agent), u(a), u(b), u(input), collector);
    input[0x18] = saved;
    return (r & 0xffffff00u) | saved;
}

static uint32_t Type19Agent_ProcessCollision_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return Type19Agent_ProcessCollision(B(ecx), B(a0), B(a1), B(a2), a3);
}
ABI_ENTRY_004df570(Type19Agent_ProcessCollision_Entry, Type19Agent_ProcessCollision_Abi)
HOOK(0x004df570, "Type19Agent_ProcessCollision", Type19Agent_ProcessCollision_Entry, nullptr);

// ================================================================================================================
// physics-glue: Type19Listener (vtables +0 0x0058b7a4, +8 0x0058b798 collision listener, +0xc 0x0058b790 world
// deletion listener)
// ================================================================================================================

// 0x004df780 (esi = memory; stack: world; ret 4; caller PhysicsManager_Init 0x00411960): constructor: reference count
// u16 +6 = 1, base vtables +8 0x005baa0c / +0xc 0x005ba9c8, then vtables +0 0x0058b7a4, +8 0x0058b798, +0xc
// 0x0058b790, +0x10 = world; registers +0xc with Havok 0x004ac660 (eax = world, edi = +0xc; guess: add world deletion
// listener) and +8 with 0x004ac5c0 (add collision listener). Returns the object.
uint32_t Type19Listener_Construct(uint8_t* self, uint32_t world)
{
    Field<uint16_t>(self, 6) = 1;
    Field<uint32_t>(self, 8) = 0x005baa0c;
    Field<uint32_t>(self, 0xc) = 0x005ba9c8;
    Field<uint32_t>(self, 0) = 0x0058b7a4;
    Field<uint32_t>(self, 8) = 0x0058b798;
    Field<uint32_t>(self, 0xc) = 0x0058b790;
    Field<uint32_t>(self, 0x10) = world;
    orig::fn_004ac660(world, u(self) + 0xc);
    orig::fn_004ac5c0(world, u(self) + 8);
    return u(self);
}

static uint32_t Type19Listener_Construct_Abi(uint32_t esi, uint32_t a0)
{
    return Type19Listener_Construct(B(esi), a0);
}
ABI_ENTRY_004df780(Type19Listener_Construct_Entry, Type19Listener_Construct_Abi)
HOOK(0x004df780, "Type19Listener_Construct", Type19Listener_Construct_Entry, nullptr);

// 0x004df7d0 (ecx = object; stack: flags; ret 4): vtable 0x0058b7a4 slot 0, deleting destructor: vtables reset; when
// still attached to a world (+0x10): Havok 0x004ac6c0 (esi = world, edi = +0xc) and 0x004ac620 (esi = world, edi = +8)
// (guess: remove the two listeners); then +0xc = 0x005ba9c8, +0 = 0x0058e3c4 (+8 is left at 0x0058b798), and when
// flags bit 0 is set hkMemory free(object, u16 +4, class 0xe). Returns the object. Not executed in recorded runs.
uint32_t Type19Listener_DeletingDestructor(uint8_t* self, uint8_t flags)
{
    uint8_t* world = Field<uint8_t*>(self, 0x10);
    Field<uint32_t>(self, 0) = 0x0058b7a4;
    Field<uint32_t>(self, 8) = 0x0058b798;
    Field<uint32_t>(self, 0xc) = 0x0058b790;
    if (world)
    {
        orig::fn_004ac6c0(u(world), u(self) + 0xc);
        orig::fn_004ac620(Field<uint32_t>(self, 0x10), u(self) + 8);
    }
    Field<uint32_t>(self, 0xc) = 0x005ba9c8;
    Field<uint32_t>(self, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(self), Field<uint16_t>(self, 4), 0xe);
    return u(self);
}

static uint32_t Type19Listener_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Listener_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004df7d0(Type19Listener_DeletingDestructor_Entry, Type19Listener_DeletingDestructor_Abi)
HOOK(0x004df7d0, "Type19Listener_DeletingDestructor", Type19Listener_DeletingDestructor_Entry, nullptr);

// 0x004df840 (ecx = the +0xc sub-object; stack: world (unused); ret 4): vtable 0x0058b790 slot 1 (guess: world deleted
// callback): Havok 0x004ac6c0 (esi = world [+0x10], edi = the sub-object, or 0 for a null object) and 0x004ac620
// (esi = world, edi = the +8 sub-object or 0), world = 0, then removeReference on the object (u16 +6; at zero its
// virtual 0 (deleting destructor)(1) as a tail call). Returns that call's eax, else 0x004ac620's. Not executed in
// recorded runs.
uint32_t Type19Listener_OnWorldDeleted(uint8_t* sub, uint32_t /*world*/)
{
    const uint32_t selfAddress = u(sub) - 0xc;  // integer arithmetic: the original tests it for null
    uint8_t* self = B(selfAddress);
    orig::fn_004ac6c0(Field<uint32_t>(sub, 4), selfAddress ? u(sub) : 0u);
    uint32_t r = orig::fn_004ac620(Field<uint32_t>(sub, 4), selfAddress ? u(sub) - 4 : 0u);
    Field<uint32_t>(sub, 4) = 0;
    --Field<uint16_t>(self, 6);
    if (Field<uint16_t>(self, 6) == 0)
        r = VCall<uint32_t>(self, 0, 1u);
    return r;
}

static uint32_t Type19Listener_OnWorldDeleted_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Listener_OnWorldDeleted(B(ecx), a0);
}
ABI_ENTRY_004df840(Type19Listener_OnWorldDeleted_Entry, Type19Listener_OnWorldDeleted_Abi)
HOOK(0x004df840, "Type19Listener_OnWorldDeleted", Type19Listener_OnWorldDeleted_Entry, nullptr);

namespace
{
// The 0x004df8a0 block for one collidable c: when its shape (+0x10) is a Type19Shape and the owner's (+0x1c) speed^2
// exceeds shape +0x20 - 25 ([0x005bce08]), the owner's velocity (motion +0x40, 4 floats) is rescaled to length
// shape +0x24 - 5 ([0x005bcbdc]) through Body_wakeAndSetLinearVelocity 0x00456250, and the event's +0x18 -> +0xc is
// cleared (guess: a contact property). Returns eax as the original leaves it where cheap (the type or the call).
uint32_t Type19Listener_ClampSpeed(uint8_t* event, uint32_t side)
{
    uint8_t* c = Field<uint8_t*>(event, side);
    const uint32_t type = VCall<uint32_t>(Field<uint8_t*>(c, 0x10), 4 / 4);
    if (type != 0x19)
        return type;
    c = Field<uint8_t*>(event, side);
    uint8_t* entity = Field<uint8_t*>(c, 0x1c);
    uint8_t* motion = Field<uint8_t*>(entity, 8);
    uint8_t* shape = Field<uint8_t*>(c, 0x10);
    const float x = Field<float>(motion, 0x40);
    const float y = Field<float>(motion, 0x44);
    const float z = Field<float>(motion, 0x48);
    const float sq = (x * x + y * y) + z * z;
    if (!(sq > Field<float>(shape, 0x20) - K(0x005bce08)))
        return 0;
    alignas(16) float v[4];
    std::memcpy(v, motion + 0x40, 12);
    const float w = Field<float>(motion, 0x4c);
    const float k = (Field<float>(shape, 0x24) - K(0x005bcbdc)) * (K(0x005bcb2c) / std::sqrt(sq));
    v[0] = v[0] * k;
    v[1] = v[1] * k;
    v[2] = v[2] * k;
    v[3] = w * k;
    const uint32_t r = orig::fn_00456250(u(entity), u(v));
    Field<uint32_t>(Field<uint8_t*>(event, 0x18), 0xc) = 0;
    return r;
}
}  // namespace

// 0x004df8a0 (ecx = the +8 sub-object; stack: event; ret 4): vtable 0x0058b798 slot 0 (contact callback): when the
// world's (+8 -> [+0x10 of the object]) collision filter (world +0x23c) exists and its virtual +4(event +8, event +0xc)
// allows the pair, Type19Listener_ClampSpeed for collidable A (event +8) then B (event +0xc). Evidence: calls the game's
// Body_wakeAndSetLinearVelocity 0x00456250 directly. Returns the world when there is no filter, else eax as left
// (approximate where the original leaves an x87 status word). Executed in recorded runs.
uint32_t Type19Listener_OnContact(uint8_t* sub, uint8_t* event)
{
    uint8_t* world = Field<uint8_t*>(sub, 8);
    uint8_t* filter = Field<uint8_t*>(world, 0x23c);
    if (!filter)
        return u(world);
    const uint32_t ok =
        VCall<uint32_t>(filter, 4 / 4, Field<uint32_t>(event, 8), Field<uint32_t>(event, 0xc));
    if (!static_cast<uint8_t>(ok))
        return ok;
    Type19Listener_ClampSpeed(event, 8);
    return Type19Listener_ClampSpeed(event, 0xc);
}

static uint32_t Type19Listener_OnContact_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Listener_OnContact(B(ecx), B(a0));
}
ABI_ENTRY_004df8a0(Type19Listener_OnContact_Entry, Type19Listener_OnContact_Abi)
HOOK(0x004df8a0, "Type19Listener_OnContact", Type19Listener_OnContact_Entry, nullptr);

// 0x004dfa70 (ecx = the +0xc sub-object; stack: flags; ret 4): vtable 0x0058b790 slot 0, adjustor thunk: ecx -= 0xc
// and a tail jump to Type19Listener_DeletingDestructor 0x004df7d0 (ret 4). Not executed in recorded runs.
uint32_t Type19Listener_DeletingDestructorThunk(uint8_t* sub, uint32_t flags)
{
    return Type19Listener_DeletingDestructor(sub - 0xc, static_cast<uint8_t>(flags));
}

static uint32_t Type19Listener_DeletingDestructorThunk_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Listener_DeletingDestructorThunk(B(ecx), a0);
}
ABI_ENTRY_004dfa70(Type19Listener_DeletingDestructorThunk_Entry, Type19Listener_DeletingDestructorThunk_Abi)
HOOK(0x004dfa70, "Type19Listener_DeletingDestructorThunk", Type19Listener_DeletingDestructorThunk_Entry, nullptr);

// ================================================================================================================
// physics-glue: Type19Shape (vtable 0x0058b760, shape type 0x19)
// ================================================================================================================

// 0x004dfaa0 (eax = child shape, esi = memory; stack: entity, limit; ret 8; caller Body create 0x00457030):
// constructor: +0x28 = child (reference count u16 +6 incremented), +0x0c = 0.05 ([0x005bf140]) then the child's +0x0c,
// +8 = 0, vtable 0x0058b760, u16 +6 = 1, +0x10..+0x1c = 0, +0x20 = limit * limit, +0x24 = limit, +0x2c = entity. The
// first time (byte [0x0063a128]) it registers the display builder 0x004dfa80 for shape type 0x19 with the Havok user
// shape display builder ([0x005fed78], 0x004a8530; eax = builder, edi = 0x19). Returns the shape.
uint32_t Type19Shape_Construct(uint8_t* child, uint8_t* shape, uint32_t entity, uint32_t limit)
{
    const float sq = bf(limit) * bf(limit);
    Field<uint32_t>(shape, 0x28) = u(child);
    Field<uint32_t>(shape, 0xc) = GameGlobal<uint32_t>(0x005bf140);
    Field<uint32_t>(shape, 8) = 0;
    Field<uint32_t>(shape, 0) = 0x0058b760;
    Field<uint16_t>(shape, 6) = 1;
    Field<uint32_t>(shape, 0x1c) = 0;
    Field<uint32_t>(shape, 0x18) = 0;
    Field<uint32_t>(shape, 0x14) = 0;
    Field<uint32_t>(shape, 0x10) = 0;
    ++Field<uint16_t>(child, 6);
    Field<float>(shape, 0x20) = sq;
    Field<uint32_t>(shape, 0x2c) = entity;
    Field<uint32_t>(shape, 0x24) = limit;
    Field<float>(shape, 0xc) = Field<float>(child, 0xc);
    if (GameGlobal<uint8_t>(0x0063a128) == 0)
    {
        const uint32_t builder = GameGlobal<uint32_t>(0x005fed78);
        GameGlobal<uint8_t>(0x0063a128) = 1;
        orig::fn_004a8530(builder, 0x19, 0x004dfa80u);
    }
    return u(shape);
}

static uint32_t Type19Shape_Construct_Abi(uint32_t eax, uint32_t esi, uint32_t a0, uint32_t a1)
{
    return Type19Shape_Construct(B(eax), B(esi), a0, a1);
}
ABI_ENTRY_004dfaa0(Type19Shape_Construct_Entry, Type19Shape_Construct_Abi)
HOOK(0x004dfaa0, "Type19Shape_Construct", Type19Shape_Construct_Entry, nullptr);

// 0x004dfb20 (ecx = shape; stack: flags; ret 4): vtable 0x0058b760 slot 0, deleting destructor: removeReference on the
// child (+0x28; u16 +6, at zero its virtual 0 (1)), base vtable 0x0058e3c4, and when flags bit 0 is set hkMemory
// free(shape, u16 +4, class 0x1d). Returns the shape. Executed in recorded runs.
uint32_t Type19Shape_DeletingDestructor(uint8_t* shape, uint8_t flags)
{
    uint8_t* child = Field<uint8_t*>(shape, 0x28);
    Field<uint32_t>(shape, 0) = 0x0058b760;
    --Field<uint16_t>(child, 6);
    if (Field<uint16_t>(child, 6) == 0)
        VCall<uint32_t>(child, 0, 1u);
    Field<uint32_t>(shape, 0) = 0x0058e3c4;
    if (flags & 1)
        HkFree(u(shape), Field<uint16_t>(shape, 4), 0x1d);
    return u(shape);
}

static uint32_t Type19Shape_DeletingDestructor_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Shape_DeletingDestructor(B(ecx), static_cast<uint8_t>(a0));
}
ABI_ENTRY_004dfb20(Type19Shape_DeletingDestructor_Entry, Type19Shape_DeletingDestructor_Abi)
HOOK(0x004dfb20, "Type19Shape_DeletingDestructor", Type19Shape_DeletingDestructor_Entry, nullptr);

// 0x004dfb70 (ecx = shape; stack: direction, out, a2; ret 0xc): vtable 0x0058b760 slot 9 (guess: getSupportingVertex):
// the child's (+0x28) virtual +0x24(direction, out, a2); when the sweep (+0x10..+0x18) points along the direction
// (dot > 0) the sweep (4 floats) is added to out. Returns the child's eax (the original leaves an x87 status word in
// ax). Not executed in recorded runs.
uint32_t Type19Shape_GetSupportingVertex(uint8_t* shape, const float* direction, float* out, uint32_t a2)
{
    const uint32_t r = VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0x24 / 4, u(direction), u(out), a2);
    const float dot = (Field<float>(shape, 0x18) * direction[2] + Field<float>(shape, 0x14) * direction[1]) +
                      Field<float>(shape, 0x10) * direction[0];
    if (dot > K(0x0056c05c))
    {
        out[0] = Field<float>(shape, 0x10) + out[0];
        out[1] = Field<float>(shape, 0x14) + out[1];
        out[2] = Field<float>(shape, 0x18) + out[2];
        out[3] = Field<float>(shape, 0x1c) + out[3];
    }
    return r;
}

static uint32_t Type19Shape_GetSupportingVertex_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type19Shape_GetSupportingVertex(B(ecx), reinterpret_cast<const float*>(a0), reinterpret_cast<float*>(a1),
                                           a2);
}
ABI_ENTRY_004dfb70(Type19Shape_GetSupportingVertex_Entry, Type19Shape_GetSupportingVertex_Abi)
HOOK(0x004dfb70, "Type19Shape_GetSupportingVertex", Type19Shape_GetSupportingVertex_Entry, nullptr);

// 0x004dfbe0 (no inputs; plain ret): vtable 0x0058b760 slot 1 (getType): returns 0x19. Not executed in recorded runs.
uint32_t Type19Shape_GetType()
{
    return 0x19;
}

static uint32_t Type19Shape_GetType_Abi()
{
    return Type19Shape_GetType();
}
ABI_ENTRY_004dfbe0(Type19Shape_GetType_Entry, Type19Shape_GetType_Abi)
HOOK(0x004dfbe0, "Type19Shape_GetType", Type19Shape_GetType_Entry, nullptr);

// 0x004dfbf0 (ecx = shape; stack: transform, tolerance, out; ret 0xc): vtable 0x0058b760 slot 3 (getAabb): the child's
// (+0x28) getAabb (virtual +0xc), then the box is extended by the entity's (+0x2c) linear velocity (motion +0x40)
// times the world's (entity +0x44) +0x16c (guess: the step time): max = max(max, max + sweep) first, then
// min = min(min, min + sweep), with the original's NaN handling. Returns the child's eax (the original leaves an x87
// status word or min.w there). Executed in recorded runs.
uint32_t Type19Shape_GetAabb(uint8_t* shape, uint32_t transform, uint32_t tolerance, float* out)
{
    const uint32_t r = VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0xc / 4, transform, tolerance, u(out));
    uint8_t* entity = Field<uint8_t*>(shape, 0x2c);
    uint8_t* world = Field<uint8_t*>(entity, 0x44);
    const float* v = reinterpret_cast<const float*>(Field<uint8_t*>(entity, 8) + 0x40);
    const float s = Field<float>(world, 0x16c);
    alignas(16) float w[4];
    w[0] = s * v[0];
    w[1] = s * v[1];
    w[2] = s * v[2];
    w[3] = s * v[3];
    float t0 = w[0] + out[4];
    float t1 = w[1] + out[5];
    float t2 = w[2] + out[6];
    float t3 = w[3] + out[7];
    out[4] = (t0 > out[4]) ? t0 : out[4];
    out[5] = (t1 > out[5]) ? t1 : out[5];
    out[6] = (t2 > out[6]) ? t2 : out[6];
    out[7] = (t3 > out[7]) ? t3 : out[7];
    t0 = w[0] + out[0];
    t1 = w[1] + out[1];
    t2 = w[2] + out[2];
    t3 = w[3] + out[3];
    out[0] = (t0 < out[0]) ? t0 : out[0];
    out[1] = (t1 < out[1]) ? t1 : out[1];
    out[2] = (t2 < out[2]) ? t2 : out[2];
    out[3] = (t3 < out[3]) ? t3 : out[3];
    return r;
}

static uint32_t Type19Shape_GetAabb_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type19Shape_GetAabb(B(ecx), a0, a1, reinterpret_cast<float*>(a2));
}
ABI_ENTRY_004dfbf0(Type19Shape_GetAabb_Entry, Type19Shape_GetAabb_Abi)
HOOK(0x004dfbf0, "Type19Shape_GetAabb", Type19Shape_GetAabb_Entry, nullptr);

// 0x004dfd70 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058b760 slot 11: a tail jump to the
// child's (+0x28) virtual +0x2c. Slot 11 takes 3 stack arguments (0x00477c10, slot 11 of the sibling vtable
// 0x0058e760, is `ret 0xc`), so it is reproduced as a 3-argument call. Not executed in recorded runs.
uint32_t Type19Shape_Slot11(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0x2c / 4, a0, a1, a2);
}

static uint32_t Type19Shape_Slot11_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type19Shape_Slot11(B(ecx), a0, a1, a2);
}
P4_ENTRY_ECX_3(Type19Shape_Slot11_Entry, Type19Shape_Slot11_Abi)
HOOK(0x004dfd70, "Type19Shape_Slot11", Type19Shape_Slot11_Entry, nullptr);

// 0x004dfd80 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058b760 slot 6: a tail jump to the
// child's (+0x28) virtual +0x18; slot 6 takes 3 stack arguments (0x004a45e0 ret 0xc). Not executed in recorded runs.
uint32_t Type19Shape_Slot6(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0x18 / 4, a0, a1, a2);
}

static uint32_t Type19Shape_Slot6_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2)
{
    return Type19Shape_Slot6(B(ecx), a0, a1, a2);
}
P4_ENTRY_ECX_3(Type19Shape_Slot6_Entry, Type19Shape_Slot6_Abi)
HOOK(0x004dfd80, "Type19Shape_Slot6", Type19Shape_Slot6_Entry, nullptr);

// 0x004dfd90 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058b760 slot 7: a tail jump to the
// child's (+0x28) virtual +0x1c; slot 7 takes 5 stack arguments (0x005115f0 ret 0x14). Not executed in recorded runs.
uint32_t Type19Shape_Slot7(uint8_t* shape, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0x1c / 4, a0, a1, a2, a3, a4);
}

static uint32_t Type19Shape_Slot7_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    return Type19Shape_Slot7(B(ecx), a0, a1, a2, a3, a4);
}
P4_ENTRY_ECX_5(Type19Shape_Slot7_Entry, Type19Shape_Slot7_Abi)
HOOK(0x004dfd90, "Type19Shape_Slot7", Type19Shape_Slot7_Entry, nullptr);

// 0x004dfda0 (ecx = shape; tail jump with the caller's frame intact): vtable 0x0058b760 slot 8: a tail jump to the
// child's (+0x28) virtual +0x20; slot 8 takes 1 stack argument (0x0050f1d0, slot 8 of 0x0058e760, ends in ret 4). Not
// executed in recorded runs.
uint32_t Type19Shape_Slot8(uint8_t* shape, uint32_t a0)
{
    return VCall<uint32_t>(Field<uint8_t*>(shape, 0x28), 0x20 / 4, a0);
}

static uint32_t Type19Shape_Slot8_Abi(uint32_t ecx, uint32_t a0)
{
    return Type19Shape_Slot8(B(ecx), a0);
}
P4_ENTRY_ECX_1(Type19Shape_Slot8_Entry, Type19Shape_Slot8_Abi)
HOOK(0x004dfda0, "Type19Shape_Slot8", Type19Shape_Slot8_Entry, nullptr);

// ================================================================================================================
// PMS heap (object 0x0063a210; also reached through the game's operator new / delete)
// ================================================================================================================

// 0x004e07f0 (ecx = heap; stack: unused, size, alignment shift, name; ret 0x10): rounds size up to 1 << shift; sizes up
// to 0x100 come from the first size-class pool (heap +4 list, next at +0) whose block size (+0x14) and alignment (u16
// +0x1c) fit: pop its free list (+8; refilled by 0x004f6150 when empty). Larger requests: a descriptor from 0x004e0a60
// (eax = 0x0063a210, the global heap, not ecx), filled by the heap's virtual +4(descriptor, size, shift); when it got
// memory (+4) the descriptor is linked into heap +0xc and its +8 returned. On failure the descriptor goes back to the
// free list at [0x0064a220] +8, "Failed to allocate %d bytes for %s\n" (0x0058b60c) or "Failed to allocate %d
// bytes\n" (0x0058b5ec) is logged, the handler [0x0064a224](size, name) runs and the function hangs (infinite loop).
uint32_t PMS_heapAlloc(uint8_t* heap, uint32_t /*a0*/, uint32_t size, uint32_t shift, uint32_t name)
{
    const uint32_t mask = (1u << (shift & 31)) - 1;
    const uint32_t rounded = (mask + size) & ~mask;
    if (rounded <= 0x100)
    {
        for (uint8_t* pool = Field<uint8_t*>(heap, 4); pool; pool = Field<uint8_t*>(pool, 0))
        {
            if (Field<uint32_t>(pool, 0x14) >= rounded && Field<uint16_t>(pool, 0x1c) >= shift)
            {
                if (!Field<uint32_t>(pool, 8))
                    orig::fn_004f6150(u(pool));
                uint8_t* block = Field<uint8_t*>(pool, 8);
                Field<uint32_t>(pool, 8) = Field<uint32_t>(block, 0);
                return u(block);
            }
        }
    }
    uint8_t* d = B(orig::fn_004e0a60(0x0063a210u));
    VCall<uint32_t>(heap, 4 / 4, u(d), size, shift);
    if (Field<uint32_t>(d, 4))
    {
        Field<uint32_t>(d, 0) = Field<uint32_t>(heap, 0xc);
        Field<uint32_t>(heap, 0xc) = u(d);
        return Field<uint32_t>(d, 8);
    }
    uint8_t* g = GameGlobal<uint8_t*>(0x0064a220);
    Field<uint32_t>(d, 0) = Field<uint32_t>(g, 8);
    Field<uint32_t>(g, 8) = u(d);
    if (name)
        GameLog(reinterpret_cast<const char*>(0x0058b60c), size, name);
    else
        GameLog(reinterpret_cast<const char*>(0x0058b5ec), size);
    using Handler = void(__cdecl*)(uint32_t, uint32_t);
    const Handler handler = GameGlobal<Handler>(0x0064a224);
    if (handler)
        handler(size, name);
    for (;;)
        GameGlobal<volatile uint8_t>(0x0063a128);  // the original spins on `jmp $`
}

static uint32_t PMS_heapAlloc_Abi(uint32_t ecx, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    return PMS_heapAlloc(B(ecx), a0, a1, a2, a3);
}
ABI_ENTRY_004e07f0(PMS_heapAlloc_Entry, PMS_heapAlloc_Abi)
HOOK(0x004e07f0, "PMS_heapAlloc", PMS_heapAlloc_Entry, nullptr);

// 0x004e08d0 (ecx = heap; stack: block; ret 4): null blocks are ignored. A block inside one of a pool's chunks (pools:
// heap +4 list, next +0; chunks from the pool itself, next +4; chunk start < block < chunk + chunk +0x10) is pushed on
// the pool's free list (+8); returns the old head. Otherwise the large-block descriptor whose +8 is the block (list
// heap +0xc) is unlinked, released through the heap's virtual +8(descriptor) and pushed on the free descriptor list
// [0x0064a220] +8; returns [0x0064a220] + 8. Not found: returns 0 (the original leaves 0 or its incoming eax).
uint32_t PMS_heapFree(uint8_t* heap, uint8_t* block)
{
    if (!block)
        return 0;
    for (uint8_t* pool = Field<uint8_t*>(heap, 4); pool; pool = Field<uint8_t*>(pool, 0))
    {
        for (uint8_t* chunk = pool; chunk; chunk = Field<uint8_t*>(chunk, 4))
        {
            if (block > chunk && block < chunk + Field<uint32_t>(chunk, 0x10))
            {
                const uint32_t head = Field<uint32_t>(pool, 8);
                Field<uint32_t>(block, 0) = head;
                Field<uint32_t>(pool, 8) = u(block);
                return head;
            }
        }
    }
    uint8_t* link = heap + 0xc;
    for (uint8_t* d = Field<uint8_t*>(heap, 0xc); d; link = d, d = Field<uint8_t*>(d, 0))
    {
        if (Field<uint8_t*>(d, 8) == block)
        {
            VCall<uint32_t>(heap, 8 / 4, u(d));
            Field<uint32_t>(link, 0) = Field<uint32_t>(d, 0);
            uint8_t* g = GameGlobal<uint8_t*>(0x0064a220);
            Field<uint32_t>(d, 0) = Field<uint32_t>(g, 8);
            Field<uint32_t>(g, 8) = u(d);
            return u(g) + 8;
        }
    }
    return 0;
}

static uint32_t PMS_heapFree_Abi(uint32_t ecx, uint32_t a0)
{
    return PMS_heapFree(B(ecx), B(a0));
}
ABI_ENTRY_004e08d0(PMS_heapFree_Entry, PMS_heapFree_Abi)
HOOK(0x004e08d0, "PMS_heapFree", PMS_heapFree_Entry, nullptr);
