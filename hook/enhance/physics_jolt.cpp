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
// through hook/physics/jolt_bridge.h (Jolt lives in the SSE2 static library and_physics). Havok is only read, never
// called: types are recognised by vtable (docs/subsystems/havok-layouts.md).
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "game.h"
#include "log.h"
#include "gen/orig_calls.h"
#include "physics/jolt_bridge.h"
#include "physics/physics_hooks.h"

namespace
{
enum class Mode
{
    Havok,
    JoltShadow
};
Mode g_mode = Mode::Havok;

// Havok data layouts used here (docs/subsystems/havok-layouts.md; evidence addresses there).
constexpr uint32_t kWorldActiveIslands = 0x08;    // hkArray<island*>: data, count +0x0c
constexpr uint32_t kWorldInactiveIslands = 0x14;  // hkArray<island*>: data, count +0x18
constexpr uint32_t kWorldFixedIsland = 0x2c;      // island*: every fixed entity
constexpr uint32_t kIslandEntities = 0x3c;        // hkArray<entity*>: data, count +0x40
constexpr uint32_t kEntityMotion = 0x08;          // entity -> motion*
constexpr uint32_t kEntityUid = 0x18;             // unique per add (world +0x140 counter)
constexpr uint32_t kEntityShape = 0x24;           // root shape
constexpr uint32_t kEntityFriction = 0x38;
constexpr uint32_t kEntityRestitution = 0x3c;
constexpr uint32_t kMotionInvInertiaScalar = 0x10;  // motion types 0 / 1
constexpr uint32_t kMotionLinDamping = 0x14;
constexpr uint32_t kMotionAngDamping = 0x18;
constexpr uint32_t kMotionComLocal = 0x20;
constexpr uint32_t kMotionInvMass = 0x2c;
constexpr uint32_t kMotionQuat = 0x30;  // x y z w
constexpr uint32_t kMotionLinVel = 0x40;
constexpr uint32_t kMotionAngVel = 0x50;
constexpr uint32_t kMotionPos = 0xb0;             // body transform translation (+0x80 rotation columns)
constexpr uint32_t kMotionInvInertiaDiag = 0xe0;  // motion types 2 / 3

// Motion type by vtable (havok-layouts.md s3): 0..3 dynamic, 4 keyframed, 5 fixed; -1 unknown.
int MotionType(const uint8_t* motion)
{
    switch (Field<uint32_t>(motion, 0))
    {
    case 0x0058e278: return 0;
    case 0x0058e1f8: return 1;
    case 0x0058dfc8: return 2;  // the game's dynamic bodies
    case 0x0058df50: return 3;
    case 0x0058ded8: return 4;
    case 0x0058e040: return 5;
    default: return -1;
    }
}
int JoltMotion(int havokType)
{
    return havokType == 5 ? JB_Static : havokType == 4 ? JB_Kinematic : JB_Dynamic;
}

// Shape vtables (havok-layouts.md s4).
namespace Shape
{
constexpr uint32_t kMopp = 0x0056b384;            // type 2: +0xc mesh child (MOPP code is not mirrored)
constexpr uint32_t kBv = 0x005b0af8;              // type 3: bounding volume + phantom callback (trigger)
constexpr uint32_t kSphere = 0x0056b28c;          // type 5: +0xc radius
constexpr uint32_t kOffsetSphere = 0x0058e760;    // type 5 (game): +0x10 centre, +0x20 outer radius
constexpr uint32_t kBox = 0x0056b3e4;             // type 8: +0xc radius, +0x10 half extents
constexpr uint32_t kConvex = 0x0056b324;          // type 0xb: +0xc radius, +0x30 4-vertex blocks
constexpr uint32_t kMultiRay = 0x0056b438;        // type 0xd: rays, not solid
constexpr uint32_t kList = 0x0056b3b0;            // type 0x10: +0xc children, count +0x10
constexpr uint32_t kMeshBase = 0x0056b2f0;        // type 0x11
constexpr uint32_t kMeshGame = 0x005ba9d0;        // type 0x11, the game's skin mesh
constexpr uint32_t kSpeedTrap = 0x005b0ad8;       // type 0x12 (game): +0xc child
constexpr uint32_t kFilterOverride = 0x0058e704;  // type 0x13 (game): +0xc child
constexpr uint32_t kSweptConvex = 0x0058b760;     // type 0x19 (game): +0x28 child
constexpr uint32_t kTransform = 0x0056b414;       // type 0x1a: +0xc child, +0x10 hkTransform
constexpr uint32_t kPhantom = 0x005baa20;         // type 0x1b: no geometry
}  // namespace Shape

struct Mirror
{
    uint32_t body = 0;  // Jolt body handle; 0 = not mirrored (no shape or unsupported)
    uint32_t uid = 0;   // entity +0x18 when mirrored (a reused entity address gets a new mirror)
    int type = -1;      // Havok motion type when mirrored
    uint32_t lastSeen = 0;
};
std::unordered_map<const void*, Mirror> g_mirrors;       // Havok entity -> mirror
std::unordered_map<const void*, uint32_t> g_shapes;      // Havok shape -> Jolt shape (0 = nothing solid)
std::unordered_map<uint32_t, uint32_t> g_unknownShapes;  // vtable -> times seen
struct Seen
{
    const uint8_t* entity;
    bool active;
    float before[6];  // Havok position and linear velocity before the step
};
std::vector<Seen> g_seen;
struct Stats
{
    uint32_t steps = 0, compared = 0, unsupported = 0, mirrored[3] = {};
    double posError = 0, maxPosError = 0, velError = 0;
    // Free flight: bodies whose Havok velocity changed by exactly gravity * dt (no contact, no damping effect) test
    // the state mapping itself (position, rotation, centre of mass, gravity, integration) without solver differences.
    uint32_t freeCompared = 0;
    double freePosError = 0, freeMaxPosError = 0;
    // ... of which Jolt also saw no contact (its velocity changed by gravity * dt too), and those where only Jolt
    // had a contact (shape mismatch or a collision Havok's filter excludes).
    uint32_t bothFree = 0, joltOnlyContact = 0;
    char bothFreeWorst[512] = {};
    double bothFreePosError = 0, bothFreeMaxPosError = 0;
    // The body with the largest position error since the last log line, for diagnosis.
    const uint8_t* worst = nullptr;
    uint32_t worstUid = 0, worstShapeVtable = 0;
    int worstMotion = -1;
    float worstInvMass = 0;
    float worstHavok[6] = {}, worstJolt[6] = {}, worstBefore[6] = {};
    DWORD lastLog = 0;
} g_stats;

void ReadState(const uint8_t* motion, float pos[3], float rot[4], float lin[3], float ang[3])
{
    std::memcpy(pos, motion + kMotionPos, 12);
    std::memcpy(rot, motion + kMotionQuat, 16);
    std::memcpy(lin, motion + kMotionLinVel, 12);
    std::memcpy(ang, motion + kMotionAngVel, 12);
}

// Every entity in the Havok world: active islands, inactive islands, the fixed island (havok-layouts.md s1).
// Bodies on the game's pending-add list (Game+0xa84 +0xf20) are not in an island yet; they are picked up once added.
template <class F>
void ForEachEntity(const uint8_t* world, F&& f)
{
    auto island = [&](const uint8_t* is, bool active) {
        if (!is)
            return;
        uint8_t* const* entities = Field<uint8_t**>(is, kIslandEntities);
        const int32_t count = Field<int32_t>(is, kIslandEntities + 4);
        for (int32_t i = 0; i < count; ++i)
            if (entities[i])
                f(entities[i], active);
    };
    for (const uint32_t array : {kWorldActiveIslands, kWorldInactiveIslands})
    {
        uint8_t* const* islands = Field<uint8_t**>(world, array);
        const int32_t count = Field<int32_t>(world, array + 4);
        for (int32_t i = 0; i < count; ++i)
            island(islands[i], array == kWorldActiveIslands);
    }
    island(Field<uint8_t*>(world, kWorldFixedIsland), false);
}

// hkTransform rotation columns -> quaternion (x y z w).
void MatrixToQuat(const float* c0, const float* c1, const float* c2, float q[4])
{
    const float m00 = c0[0], m10 = c0[1], m20 = c0[2], m01 = c1[0], m11 = c1[1], m21 = c1[2], m02 = c2[0],
                m12 = c2[1], m22 = c2[2];
    const float trace = m00 + m11 + m22;
    if (trace > 0.0f)
    {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q[3] = 0.25f * s, q[0] = (m21 - m12) / s, q[1] = (m02 - m20) / s, q[2] = (m10 - m01) / s;
    }
    else if (m00 > m11 && m00 > m22)
    {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q[3] = (m21 - m12) / s, q[0] = 0.25f * s, q[1] = (m01 + m10) / s, q[2] = (m02 + m20) / s;
    }
    else if (m11 > m22)
    {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q[3] = (m02 - m20) / s, q[0] = (m01 + m10) / s, q[1] = 0.25f * s, q[2] = (m12 + m21) / s;
    }
    else
    {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q[3] = (m10 - m01) / s, q[0] = (m02 + m20) / s, q[1] = (m12 + m21) / s, q[2] = 0.25f * s;
    }
}

// hkMeshShape subparts (havok-layouts.md s4, type 0x11; 0x1c bytes): +0 vertex base, +4 vertex stride, +8 vertex
// count, +0xc index base, byte +0x10 index type (0 = 16-bit, guess), +0x14 index stride, +0x18 triangle count.
// Mesh +0x10 is the scaling. The mesh radius (+0x20, game 0.1) has no Jolt equivalent and is not mirrored.
uint32_t TranslateMesh(const uint8_t* mesh)
{
    const float* scale = &Field<float>(mesh, 0x10);
    const uint8_t* subparts = Field<uint8_t*>(mesh, 0x24);
    const int32_t subpartCount = Field<int32_t>(mesh, 0x28);
    std::vector<float> vertices;
    std::vector<uint32_t> triangles;
    if (subpartCount <= 0 || subpartCount > 4096 || IsBadReadPtr(subparts, subpartCount * 0x1c))
    {
        Log("physics(jolt): mesh %p rejected: subparts %p count %d", mesh, subparts, subpartCount);
        return 0;
    }
    for (int32_t s = 0; s < subpartCount; ++s)
    {
        const uint8_t* sp = subparts + s * 0x1c;
        const uint8_t* vbase = Field<uint8_t*>(sp, 0);
        const int32_t vstride = Field<int32_t>(sp, 4), vcount = Field<int32_t>(sp, 8);
        const uint8_t* ibase = Field<uint8_t*>(sp, 0xc);
        const uint8_t itype = Field<uint8_t>(sp, 0x10);
        const int32_t istride = Field<int32_t>(sp, 0x14), tcount = Field<int32_t>(sp, 0x18);
        // The subpart layout is read from Havok's addSubpart (0x0050f8a0); anything outside these limits means a
        // layout or index-type guess is wrong for this mesh: skip it and say so rather than read wild memory.
        const bool sane = vbase && ibase && vcount > 0 && vcount <= (1 << 20) && tcount > 0 && tcount <= (1 << 21) &&
                          vstride >= 12 && vstride <= 256 && istride >= (itype == 0 ? 6 : 12) && istride <= 64 &&
                          !IsBadReadPtr(vbase, static_cast<UINT_PTR>(vcount - 1) * vstride + 12) &&
                          !IsBadReadPtr(ibase, static_cast<UINT_PTR>(tcount - 1) * istride + (itype == 0 ? 6 : 12));
        if (!sane)
        {
            Log("physics(jolt): mesh %p subpart %d rejected: vertices %p stride %d count %d, indices %p type %u "
                "stride %d triangles %d",
                mesh, s, vbase, vstride, vcount, ibase, itype, istride, tcount);
            continue;
        }
        const uint32_t first = static_cast<uint32_t>(vertices.size() / 3);
        for (int32_t v = 0; v < vcount; ++v)
        {
            const float* p = reinterpret_cast<const float*>(vbase + v * vstride);
            vertices.insert(vertices.end(), {p[0] * scale[0], p[1] * scale[1], p[2] * scale[2]});
        }
        for (int32_t t = 0; t < tcount; ++t)
        {
            const uint8_t* tri = ibase + t * istride;
            for (int k = 0; k < 3; ++k)
            {
                const uint32_t index = itype == 0 ? reinterpret_cast<const uint16_t*>(tri)[k]
                                                  : reinterpret_cast<const uint32_t*>(tri)[k];
                triangles.push_back(first + (index < static_cast<uint32_t>(vcount) ? index : 0));
            }
        }
    }
    if (triangles.empty())
        return 0;
    if (triangles.size() / 3 >= 10000)
    {
        MEMORYSTATUSEX ms{sizeof(ms)};
        GlobalMemoryStatusEx(&ms);
        Log("physics(jolt): mesh %p: %zu vertices, %zu triangles; free address space %llu MB", mesh,
            vertices.size() / 3, triangles.size() / 3, ms.ullAvailVirtual >> 20);
    }
    return JB_ShapeMesh(vertices.data(), static_cast<uint32_t>(vertices.size() / 3), triangles.data(),
                        static_cast<uint32_t>(triangles.size() / 3));
}

// Havok shape -> Jolt shape, cached per Havok shape (level geometry is shared). 0 = nothing solid to mirror.
// Wrappers 0x12 / 0x13 / 0x19 mirror their child's geometry only; their game behaviour (velocity clamp, filter
// override, sweep) is stage 3 work. Triggers (bv 3 + 0x1b) and rays (0xd) are not solid.
// Havok's convex radius pads hulls outwards, while Jolt's rounds corners inside the same outline: hulls are about one
// radius (0.1) smaller in Jolt. Expected in the shadow compare.
// The cache assumes a Havok shape is not freed and its address reused within a level (cleared at shutdown).
uint32_t TranslateShape(const uint8_t* shape, int depth = 0)
{
    if (!shape || depth > 16)
        return 0;
    if (auto it = g_shapes.find(shape); it != g_shapes.end())
        return it->second;
    const uint32_t vtable = Field<uint32_t>(shape, 0);
    const float identity[4] = {0, 0, 0, 1};
    uint32_t result = 0;
    switch (vtable)
    {
    case Shape::kSphere:
        result = JB_ShapeSphere(Field<float>(shape, 0xc));
        break;
    case Shape::kOffsetSphere:
        result = JB_ShapeTransformed(JB_ShapeSphere(Field<float>(shape, 0x20)), &Field<float>(shape, 0x10), identity);
        break;
    case Shape::kBox:
        result = JB_ShapeBox(&Field<float>(shape, 0x10), Field<float>(shape, 0xc));
        break;
    case Shape::kConvex:
    {
        // 4-vertex blocks of 0x30 bytes: x0..x3, y0..y3, z0..z3; the last block is padded with its last vertex.
        const float* blocks = Field<float*>(shape, 0x30);
        const int32_t count = Field<int32_t>(shape, 0x34);
        std::vector<float> points;
        for (int32_t b = 0; b < count; ++b)
            for (int k = 0; k < 4; ++k)
                points.insert(points.end(), {blocks[b * 12 + k], blocks[b * 12 + 4 + k], blocks[b * 12 + 8 + k]});
        if (!points.empty())
            result = JB_ShapeConvexHull(points.data(), static_cast<uint32_t>(points.size() / 3),
                                        Field<float>(shape, 0xc));
        break;
    }
    case Shape::kMopp:
        result = TranslateShape(Field<uint8_t*>(shape, 0xc), depth + 1);
        break;
    case Shape::kMeshBase:
    case Shape::kMeshGame:
        result = TranslateMesh(shape);
        break;
    case Shape::kList:
    {
        uint8_t* const* children = Field<uint8_t**>(shape, 0xc);
        const int32_t count = Field<int32_t>(shape, 0x10);
        std::vector<uint32_t> parts;
        for (int32_t i = 0; i < count; ++i)
            if (const uint32_t part = TranslateShape(children[i], depth + 1))
                parts.push_back(part);
        if (parts.size() == 1)
            result = parts[0];
        else if (!parts.empty())
        {
            const std::vector<float> positions(parts.size() * 3, 0.0f);
            std::vector<float> rotations;
            for (size_t i = 0; i < parts.size(); ++i)
                rotations.insert(rotations.end(), identity, identity + 4);
            result = JB_ShapeCompound(parts.data(), positions.data(), rotations.data(),
                                      static_cast<uint32_t>(parts.size()));
        }
        break;
    }
    case Shape::kTransform:
        if (const uint32_t child = TranslateShape(Field<uint8_t*>(shape, 0xc), depth + 1))
        {
            float q[4];
            MatrixToQuat(&Field<float>(shape, 0x10), &Field<float>(shape, 0x20), &Field<float>(shape, 0x30), q);
            result = JB_ShapeTransformed(child, &Field<float>(shape, 0x40), q);
        }
        break;
    case Shape::kSpeedTrap:
    case Shape::kFilterOverride:
        result = TranslateShape(Field<uint8_t*>(shape, 0xc), depth + 1);
        break;
    case Shape::kSweptConvex:
        result = TranslateShape(Field<uint8_t*>(shape, 0x28), depth + 1);
        break;
    case Shape::kBv:
    case Shape::kPhantom:
    case Shape::kMultiRay:
        break;
    default:
        ++g_unknownShapes[vtable];
        break;
    }
    g_shapes.emplace(shape, result);
    return result;
}

void DestroyMirror(Mirror& mirror)
{
    if (mirror.body)
    {
        JB_BodyDestroy(mirror.body);
        --g_stats.mirrored[JoltMotion(mirror.type)];
    }
    mirror = Mirror{};
}

// A Jolt body for one Havok entity; dynamic bodies get Havok's mass, inertia, centre of mass and damping.
void CreateMirror(const uint8_t* entity, const uint8_t* motion, int type, Mirror& mirror)
{
    mirror.uid = Field<uint32_t>(entity, kEntityUid);
    mirror.type = type;
    uint32_t shape = TranslateShape(Field<uint8_t*>(entity, kEntityShape));
    if (!shape)
    {
        ++g_stats.unsupported;
        return;
    }
    const int jolt = JoltMotion(type);
    float pos[3], rot[4], lin[3], ang[3];
    ReadState(motion, pos, rot, lin, ang);
    const float invMass = Field<float>(motion, kMotionInvMass);
    uint32_t comShape = 0;
    if (jolt == JB_Dynamic)
        shape = comShape = JB_ShapeWithCenterOfMass(shape, &Field<float>(motion, kMotionComLocal));
    mirror.body = JB_BodyCreate(shape, pos, rot, jolt, jolt == JB_Dynamic && invMass > 0.0f ? 1.0f / invMass : 0.0f,
                                Field<float>(entity, kEntityFriction), Field<float>(entity, kEntityRestitution),
                                reinterpret_cast<uintptr_t>(entity));
    if (comShape)
        JB_ShapeRelease(comShape);  // the body holds its own reference
    if (!mirror.body)
    {
        ++g_stats.unsupported;
        return;
    }
    ++g_stats.mirrored[jolt];
    if (jolt == JB_Dynamic)
    {
        float invInertia[3];
        if (type >= 2)
            std::memcpy(invInertia, motion + kMotionInvInertiaDiag, 12);
        else
            invInertia[0] = invInertia[1] = invInertia[2] = Field<float>(motion, kMotionInvInertiaScalar);
        JB_BodySetMassProperties(mirror.body, invMass, invInertia, Field<float>(motion, kMotionLinDamping),
                                 Field<float>(motion, kMotionAngDamping));
    }
}

// The game's Havok collision filter for a pair, asked by Jolt for each new body pair (during JB_Step, same thread).
// 0x00411710 (the collision input's filter, world +0xe8 -> +4: group bits of entity +0x20, same owner, keyframed
// vs keyframed/fixed, owner virtual +0xa0 veto) takes the two collidables (entity +0x14). Calling the game's own
// function keeps the mirror exact without re-deriving the owner virtuals.
int PairFilter(uint64_t userA, uint64_t userB)
{
    uint8_t* a = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(userA));
    uint8_t* b = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(userB));
    if (!a || !b)
        return 1;
    return orig::fn_00411710(reinterpret_cast<uint32_t>(a + 0x14), reinterpret_cast<uint32_t>(b + 0x14)) & 0xff;
}

void BeforeShutdown(uint8_t*)
{
    Log("physics(jolt): shutdown after %u steps; mirrors static %u kinematic %u dynamic %u, %u unsupported, %zu "
        "Havok shapes seen",
        g_stats.steps, g_stats.mirrored[JB_Static], g_stats.mirrored[JB_Kinematic], g_stats.mirrored[JB_Dynamic],
        g_stats.unsupported, g_shapes.size());
    for (const auto& [vtable, count] : g_unknownShapes)
        Log("physics(jolt): unknown Havok shape vtable 0x%08x seen %u times", vtable, count);
    g_mirrors.clear();
    g_shapes.clear();
    g_unknownShapes.clear();
    g_stats = Stats{};
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
    JB_SetPairFilter(PairFilter);
    Log("physics(jolt): Jolt ready (shadow mode: Havok simulates, Jolt is compared; game collision filter mirrored)");
}

// Shadow mode, before Havok's substep: mirror new entities, drop removed ones, seed Jolt with Havok's state.
bool BeforeSubstep(uint8_t* m, float)
{
    if (g_mode != Mode::JoltShadow || !JB_IsReady())
        return false;
    const uint8_t* world = Field<uint8_t*>(m, 8);
    if (!world)
        return false;
    const uint32_t step = g_stats.steps + 1;
    g_seen.clear();
    ForEachEntity(world, [step](const uint8_t* entity, bool active) {
        const uint8_t* motion = Field<uint8_t*>(entity, kEntityMotion);
        const int type = motion ? MotionType(motion) : -1;
        if (type < 0)
            return;
        Mirror& mirror = g_mirrors[entity];
        // New entity, a reused address, or a change of motion class (keyframing toggled): rebuild the mirror.
        if (mirror.lastSeen == 0 || mirror.uid != Field<uint32_t>(entity, kEntityUid) ||
            JoltMotion(mirror.type) != JoltMotion(type))
        {
            DestroyMirror(mirror);
            CreateMirror(entity, motion, type, mirror);
        }
        mirror.lastSeen = step;
        if (!mirror.body)
            return;
        if (JoltMotion(type) != JB_Static)
        {
            float pos[3], rot[4], lin[3], ang[3];
            ReadState(motion, pos, rot, lin, ang);
            JB_BodySetState(mirror.body, pos, rot, lin, ang);
        }
        Seen seen{entity, active, {}};
        std::memcpy(seen.before, motion + kMotionPos, 12);
        std::memcpy(seen.before + 3, motion + kMotionLinVel, 12);
        g_seen.push_back(seen);
    });
    // Entities that left the world.
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();)
        if (it->second.lastSeen != step)
        {
            DestroyMirror(it->second);
            it = g_mirrors.erase(it);
        }
        else
            ++it;
    return false;  // Havok still simulates
}

// Shadow mode, after Havok's substep: step Jolt from the same start state and compare the active dynamic bodies.
void AfterSubstep(uint8_t*, float dt)
{
    if (g_mode != Mode::JoltShadow || !JB_IsReady())
        return;
    JB_Step(dt, 1);
    ++g_stats.steps;
    for (const Seen& seen : g_seen)
    {
        const Mirror& mirror = g_mirrors[seen.entity];
        if (!seen.active || JoltMotion(mirror.type) != JB_Dynamic)
            continue;
        const uint8_t* motion = Field<uint8_t*>(seen.entity, kEntityMotion);
        float hp[3], hr[4], hl[3], ha[3], jp[3], jr[4], jl[3], ja[3];
        ReadState(motion, hp, hr, hl, ha);
        JB_BodyGetState(mirror.body, jp, jr, jl, ja);
        const double dp = std::sqrt(double(hp[0] - jp[0]) * (hp[0] - jp[0]) + double(hp[1] - jp[1]) * (hp[1] - jp[1]) +
                                    double(hp[2] - jp[2]) * (hp[2] - jp[2]));
        const double dv = std::sqrt(double(hl[0] - jl[0]) * (hl[0] - jl[0]) + double(hl[1] - jl[1]) * (hl[1] - jl[1]) +
                                    double(hl[2] - jl[2]) * (hl[2] - jl[2]));
        g_stats.posError += dp, g_stats.velError += dv, ++g_stats.compared;
        const float* b = seen.before;
        const float expectedVz = b[5] - 64.348f * dt;
        if (std::fabs(hl[0] - b[3]) < 0.01f && std::fabs(hl[1] - b[4]) < 0.01f && std::fabs(hl[2] - expectedVz) < 0.01f &&
            std::fabs(b[5]) + std::fabs(b[3]) + std::fabs(b[4]) > 0.1f)
        {
            g_stats.freePosError += dp, ++g_stats.freeCompared;
            if (dp > g_stats.freeMaxPosError)
                g_stats.freeMaxPosError = dp;
            if (std::fabs(jl[0] - b[3]) < 0.01f && std::fabs(jl[1] - b[4]) < 0.01f && std::fabs(jl[2] - expectedVz) < 0.01f)
            {
                g_stats.bothFreePosError += dp, ++g_stats.bothFree;
                if (dp > g_stats.bothFreeMaxPosError)
                {
                    g_stats.bothFreeMaxPosError = dp;
                    float jc[3];
                    JB_BodyGetCenterOfMass(mirror.body, jc);
                    const float* hc = &Field<float>(motion, 0x70);  // Havok centre of mass, world
                    const float* cl = &Field<float>(motion, kMotionComLocal);
                    snprintf(g_stats.bothFreeWorst, sizeof(g_stats.bothFreeWorst),
                             "motion %d dp(%.4f %.4f %.4f) com Havok(%.3f %.3f %.3f) Jolt(%.3f %.3f %.3f) local com "
                             "(%.3f %.3f %.3f) w Havok(%.3f %.3f %.3f) Jolt(%.3f %.3f %.3f) q Havok(%.4f %.4f %.4f %.4f) "
                             "Jolt(%.4f %.4f %.4f %.4f)",
                             MotionType(motion), hp[0] - jp[0], hp[1] - jp[1], hp[2] - jp[2], hc[0], hc[1], hc[2],
                             jc[0], jc[1], jc[2], cl[0], cl[1], cl[2], ha[0], ha[1], ha[2], ja[0], ja[1], ja[2], hr[0],
                             hr[1], hr[2], hr[3], jr[0], jr[1], jr[2], jr[3]);
                }
            }
            else
                ++g_stats.joltOnlyContact;
        }
        if (dp > g_stats.maxPosError)
        {
            g_stats.maxPosError = dp;
            g_stats.worst = seen.entity;
            const uint8_t* shape = Field<uint8_t*>(seen.entity, kEntityShape);
            g_stats.worstUid = Field<uint32_t>(seen.entity, kEntityUid);
            g_stats.worstShapeVtable = shape ? Field<uint32_t>(shape, 0) : 0;
            g_stats.worstMotion = MotionType(motion);
            g_stats.worstInvMass = Field<float>(motion, kMotionInvMass);
            std::memcpy(g_stats.worstHavok, hp, 12), std::memcpy(g_stats.worstHavok + 3, hl, 12);
            std::memcpy(g_stats.worstJolt, jp, 12), std::memcpy(g_stats.worstJolt + 3, jl, 12);
            std::memcpy(g_stats.worstBefore, seen.before, 24);
        }
    }
    const DWORD now = GetTickCount();
    if (now - g_stats.lastLog >= 1000)
    {
        g_stats.lastLog = now;
        Log("physics(jolt): %u steps; mirrors static %u kinematic %u dynamic %u (%u unsupported); one-step error: pos "
            "mean %.4f max %.4f, vel mean %.4f (%u samples)",
            g_stats.steps, g_stats.mirrored[JB_Static], g_stats.mirrored[JB_Kinematic], g_stats.mirrored[JB_Dynamic],
            g_stats.unsupported, g_stats.compared ? g_stats.posError / g_stats.compared : 0.0, g_stats.maxPosError,
            g_stats.compared ? g_stats.velError / g_stats.compared : 0.0, g_stats.compared);
        if (const uint8_t* w = g_stats.worst)
        {
            const float* b = g_stats.worstBefore;
            const float* h = g_stats.worstHavok;
            const float* j = g_stats.worstJolt;
            Log("physics(jolt):   worst entity %p (uid %u, motion %d, shape vtable 0x%08x, inv mass %.4f): before "
                "p(%.3f %.3f %.3f) v(%.3f %.3f %.3f); Havok p(%.3f %.3f %.3f) v(%.3f %.3f %.3f); Jolt p(%.3f %.3f "
                "%.3f) v(%.3f %.3f %.3f)",
                w, g_stats.worstUid, g_stats.worstMotion, g_stats.worstShapeVtable, g_stats.worstInvMass,
                b[0], b[1], b[2], b[3], b[4], b[5], h[0], h[1], h[2], h[3], h[4], h[5], j[0], j[1], j[2], j[3], j[4],
                j[5]);
        }
        if (g_stats.freeCompared)
            Log("physics(jolt):   free flight: pos error mean %.5f max %.5f (%u samples)",
                g_stats.freePosError / g_stats.freeCompared, g_stats.freeMaxPosError, g_stats.freeCompared);
        if (g_stats.freeCompared)
            Log("physics(jolt):   of which Jolt free too: pos error mean %.6f max %.6f (%u); Jolt-only contact: %u",
                g_stats.bothFree ? g_stats.bothFreePosError / g_stats.bothFree : 0.0, g_stats.bothFreeMaxPosError,
                g_stats.bothFree, g_stats.joltOnlyContact);
        if (g_stats.bothFreeWorst[0])
            Log("physics(jolt):     worst contact-free: %s", g_stats.bothFreeWorst);
        g_stats.bothFreeWorst[0] = 0;
        g_stats.freeCompared = 0, g_stats.freePosError = g_stats.freeMaxPosError = 0;
        g_stats.bothFree = g_stats.joltOnlyContact = 0, g_stats.bothFreePosError = g_stats.bothFreeMaxPosError = 0;
        g_stats.worst = nullptr;
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
