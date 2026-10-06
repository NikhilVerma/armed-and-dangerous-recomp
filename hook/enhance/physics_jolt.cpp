// Havok -> Jolt Physics replacement, staged (docs/decision.md 2026-10-06, docs/subsystems/physics-jolt.md).
//
//   [physics] engine=havok         the original (default; tests always run with it)
//   [physics] engine=jolt_shadow   Havok still simulates; every Havok rigid body is mirrored into Jolt, and each
//                                  substep Jolt is seeded with Havok's state before the step and compared with
//                                  Havok's state after it ("one-step agreement": no chaotic drift). Logs the error
//                                  once per second; the game is unchanged.
//   [physics] engine=jolt          (not yet) Jolt simulates and writes the results back into Havok's entity/motion
//                                  fields, Havok's step is skipped.
//
// Uses the enhancement points of the recovered physics glue (src/physics/physics_hooks.h) and talks to Jolt only
// through hook/physics/jolt_bridge.h (Jolt lives in the SSE2 static library and_physics).
#include <windows.h>

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "game.h"
#include "game/game_object.h"
#include "log.h"
#include "physics/jolt_bridge.h"
#include "gen/orig_calls.h"
#include "physics/physics_hooks.h"

namespace
{
enum class Mode
{
    Havok,
    JoltShadow
};
Mode g_mode = Mode::Havok;

// Havok data layouts used here (docs/subsystems/physics.md s4, docs/subsystems/havok-layouts.md).
constexpr uint32_t kEntityMotion = 0x08;     // entity -> motion*
constexpr uint32_t kEntityWorld = 0x44;      // entity -> hkWorld* (null when not in a world)
constexpr uint32_t kEntityFriction = 0x38;
constexpr uint32_t kEntityRestitution = 0x3c;
constexpr uint32_t kMotionInvMass = 0x2c;
constexpr uint32_t kMotionQuat = 0x30;       // x y z w
constexpr uint32_t kMotionLinVel = 0x40;
constexpr uint32_t kMotionAngVel = 0x50;
constexpr uint32_t kMotionPos = 0xb0;
constexpr uint32_t kMotionGetType = 0x18;    // motion virtual: 4 = keyframed (physics.md)

struct Mirror
{
    uint32_t body = 0;   // Jolt body handle
    uint32_t shape = 0;  // Jolt shape handle
    bool dynamic = false;
};
std::unordered_map<const void*, Mirror> g_mirrors;  // Havok entity -> mirror
std::vector<const void*> g_seen;
struct Stats
{
    uint32_t steps = 0, compared = 0, unsupported = 0;
    double posError = 0, maxPosError = 0, velError = 0;
    DWORD lastLog = 0;
} g_stats;

void ReadState(const uint8_t* motion, float pos[3], float rot[4], float lin[3], float ang[3])
{
    std::memcpy(pos, motion + kMotionPos, 12);
    std::memcpy(rot, motion + kMotionQuat, 16);
    std::memcpy(lin, motion + kMotionLinVel, 12);
    std::memcpy(ang, motion + kMotionAngVel, 12);
}

// Every rigid body in the Havok world: islands -> entity arrays (docs/subsystems/havok-layouts.md). Until that
// layout is confirmed, the game's own objects are used: the object list Game+0xa84 +0xe78, each object's entity at
// +0x1c4 (docs/subsystems/physics.md s4). That misses bodies the game owns elsewhere (level collision, ragdoll parts).
template <class F>
void ForEachEntity(uint8_t* world, F&& f)
{
    (void)world;  // TODO(havok-layouts): walk the world's island entity arrays instead
    uint8_t* manager = Field<uint8_t*>(g_game, 0xa84);
    if (!manager)
        return;
    // The game object list (as Shadows_Update, src/game/shadows.cpp): iterator begin 0x004e7340 (eax = list, esi =
    // iterator), next 0x004e73c0 (edi = iterator); the iterator's node (second dword) loses a reference at the end.
    uint32_t iterator[2] = {0, 0};
    const uint32_t list = Field<uint32_t>(manager, 0xe78);
    for (uint8_t* obj = reinterpret_cast<uint8_t*>(orig::fn_004e7340(list, reinterpret_cast<uint32_t>(iterator)));
         obj; obj = reinterpret_cast<uint8_t*>(orig::fn_004e73c0(reinterpret_cast<uint32_t>(iterator))))
    {
        uint8_t* entity = Field<uint8_t*>(obj, 0x1c4);
        if (entity && Field<uint32_t>(entity, kEntityWorld))
            f(entity);
    }
    if (iterator[1])
        --Field<int32_t>(reinterpret_cast<void*>(iterator[1]), 0x18);
}

// Havok shape -> Jolt shape (docs/subsystems/havok-layouts.md). Returns 0 for shapes not translated yet.
uint32_t TranslateShape(const uint8_t* entity)
{
    (void)entity;
    // TODO(havok-layouts): read the entity's collidable shape and its type; build sphere / box / convex hull / mesh /
    // transformed / list (compound) shapes with JB_Shape*.
    return 0;
}

void BeforeShutdown(uint8_t*)
{
    Log("physics(jolt): shutdown after %u steps; mirrored %zu bodies, %u unsupported shapes", g_stats.steps,
        g_mirrors.size(), g_stats.unsupported);
    g_mirrors.clear();
    JB_Shutdown();
}

void AfterInit(uint8_t*)
{
    const float gravity[3] = {0.0f, 0.0f, -64.348f};  // PhysicsManager_Init's world cinfo (Z up)
    if (!JB_Init(gravity, 8192))
    {
        Log("physics(jolt): Jolt init failed; staying on Havok only");
        g_mode = Mode::Havok;
        return;
    }
    Log("physics(jolt): Jolt %s ready (shadow mode: Havok simulates, Jolt is compared)",
        g_mode == Mode::JoltShadow ? "shadow" : "");
}

// Shadow mode, before Havok's substep: make sure every entity has a mirror, and seed Jolt with Havok's state.
bool BeforeSubstep(uint8_t* m, float)
{
    if (g_mode != Mode::JoltShadow || !JB_IsReady())
        return false;
    g_seen.clear();
    ForEachEntity(Field<uint8_t*>(m, 8), [](uint8_t* entity) {
        g_seen.push_back(entity);
        const uint8_t* motion = Field<uint8_t*>(entity, kEntityMotion);
        if (!motion)
            return;
        float pos[3], rot[4], lin[3], ang[3];
        ReadState(motion, pos, rot, lin, ang);
        auto it = g_mirrors.find(entity);
        if (it == g_mirrors.end())
        {
            Mirror mirror;
            mirror.shape = TranslateShape(entity);
            if (!mirror.shape)
            {
                ++g_stats.unsupported;
                g_mirrors.emplace(entity, mirror);  // remember, do not retry every step
                return;
            }
            const float invMass = Field<float>(motion, kMotionInvMass);
            mirror.dynamic = invMass > 0.0f;
            mirror.body = JB_BodyCreate(mirror.shape, pos, rot, mirror.dynamic ? JB_Dynamic : JB_Kinematic,
                                        mirror.dynamic ? 1.0f / invMass : 0.0f, Field<float>(entity, kEntityFriction),
                                        Field<float>(entity, kEntityRestitution), reinterpret_cast<uintptr_t>(entity));
            it = g_mirrors.emplace(entity, mirror).first;
        }
        if (it->second.body)
            JB_BodySetState(it->second.body, pos, rot, lin, ang);
    });
    return false;  // Havok still simulates
}

// Shadow mode, after Havok's substep: step Jolt from the same start state and compare.
void AfterSubstep(uint8_t*, float dt)
{
    if (g_mode != Mode::JoltShadow || !JB_IsReady())
        return;
    JB_Step(dt, 1);
    ++g_stats.steps;
    for (const void* entity : g_seen)
    {
        auto it = g_mirrors.find(entity);
        if (it == g_mirrors.end() || !it->second.body || !it->second.dynamic)
            continue;
        const uint8_t* motion = Field<uint8_t*>(entity, kEntityMotion);
        if (!motion)
            continue;
        float hp[3], hr[4], hl[3], ha[3], jp[3], jr[4], jl[3], ja[3];
        ReadState(motion, hp, hr, hl, ha);
        JB_BodyGetState(it->second.body, jp, jr, jl, ja);
        const double dp = std::sqrt(double(hp[0] - jp[0]) * (hp[0] - jp[0]) + double(hp[1] - jp[1]) * (hp[1] - jp[1]) +
                                    double(hp[2] - jp[2]) * (hp[2] - jp[2]));
        const double dv = std::sqrt(double(hl[0] - jl[0]) * (hl[0] - jl[0]) + double(hl[1] - jl[1]) * (hl[1] - jl[1]) +
                                    double(hl[2] - jl[2]) * (hl[2] - jl[2]));
        g_stats.posError += dp, g_stats.velError += dv, ++g_stats.compared;
        if (dp > g_stats.maxPosError)
            g_stats.maxPosError = dp;
    }
    const DWORD now = GetTickCount();
    if (now - g_stats.lastLog >= 1000)
    {
        g_stats.lastLog = now;
        Log("physics(jolt): %u steps, %zu mirrors (%u unsupported), one-step error: pos mean %.4f max %.4f, vel mean %.4f "
            "(%u samples)", g_stats.steps, g_mirrors.size(), g_stats.unsupported,
            g_stats.compared ? g_stats.posError / g_stats.compared : 0.0, g_stats.maxPosError,
            g_stats.compared ? g_stats.velError / g_stats.compared : 0.0, g_stats.compared);
        g_stats.posError = g_stats.velError = g_stats.maxPosError = 0, g_stats.compared = 0;
    }
}
}  // namespace

void PhysicsJoltInit()
{
    char engine[32];
    ConfigString("physics", "engine", "havok", engine, sizeof(engine));
    if (!_stricmp(engine, "jolt_shadow"))
        g_mode = Mode::JoltShadow;
    else
    {
        if (_stricmp(engine, "havok"))
            Log("physics(jolt): [physics] engine=%s not supported yet; using havok", engine);
        return;  // original: no enhancement points installed
    }
    g_physicsAfterInit = AfterInit;
    g_physicsBeforeShutdown = BeforeShutdown;
    g_physicsBeforeSubstep = BeforeSubstep;
    g_physicsAfterSubstep = AfterSubstep;
    Log("physics(jolt): engine=%s", engine);
}
