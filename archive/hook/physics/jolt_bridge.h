#pragma once
// Plain C interface to Jolt Physics (docs/decision.md 2026-10-06, docs/subsystems/physics-jolt.md).
//
// Jolt needs SSE2 and its math types depend on the instruction set, while the hook DLL is built /arch:IA32 (x87,
// faithful float behaviour). So Jolt is only ever included by jolt_bridge.cpp, which is compiled with SSE2 into the
// separate static library and_physics; everything else talks to it through this header: handles, floats, no Jolt types.
//
// Conventions: positions are float[3]; rotations are quaternions float[4] in (x, y, z, w) order; the world is the
// game's (Z up, Havok units). Handles are small integers; 0 = none / failure.

#include <cstdint>

extern "C" {

// System. One Jolt world at a time. Returns 0 on failure. Single-threaded and deterministic.
int JB_Init(const float gravity[3], uint32_t maxBodies);
void JB_Shutdown();
int JB_IsReady();
// Advance the world by dt seconds in `collisionSteps` sub-steps (Jolt's collision steps).
void JB_Step(float dt, int collisionSteps);
void JB_SetGravity(const float gravity[3]);

// Contact filter: called during JB_Step (same thread) for each new body pair with the two bodies' userData; return 0
// to reject every contact between them (the game's Havok collision filters, mirrored). Null accepts everything.
typedef int (*JB_PairFilter)(uint64_t userDataA, uint64_t userDataB);
void JB_SetPairFilter(JB_PairFilter filter);

// Shapes (reference counted inside the bridge; a body keeps its shape alive). Return 0 on failure.
uint32_t JB_ShapeSphere(float radius);
uint32_t JB_ShapeBox(const float halfExtents[3], float convexRadius);
uint32_t JB_ShapeConvexHull(const float* points, uint32_t count, float convexRadius);
uint32_t JB_ShapeMesh(const float* vertices, uint32_t vertexCount, const uint32_t* triangles, uint32_t triangleCount);
uint32_t JB_ShapeTransformed(uint32_t child, const float position[3], const float rotation[4]);
uint32_t JB_ShapeCompound(const uint32_t* children, const float* positions, const float* rotations, uint32_t count);
// The same shape with its centre of mass moved to `centerOfMass` (shape space), so a dynamic mirror rotates about
// the same point as the Havok body (motion +0x20) rather than Jolt's computed centre.
uint32_t JB_ShapeWithCenterOfMass(uint32_t shape, const float centerOfMass[3]);
void JB_ShapeRelease(uint32_t shape);

// Bodies. motion: 0 static (Havok fixed), 1 kinematic (Havok keyframed), 2 dynamic. mass <= 0 lets Jolt compute it
// from the shape (density 1). layer: 0 non-moving, 1 moving.
enum JB_Motion : int { JB_Static = 0, JB_Kinematic = 1, JB_Dynamic = 2 };
uint32_t JB_BodyCreate(uint32_t shape, const float position[3], const float rotation[4], int motion, float mass,
                       float friction, float restitution, uint64_t userData);
void JB_BodyDestroy(uint32_t body);
void JB_BodySetState(uint32_t body, const float position[3], const float rotation[4], const float linearVelocity[3],
                     const float angularVelocity[3]);
void JB_BodyGetState(uint32_t body, float position[3], float rotation[4], float linearVelocity[3],
                     float angularVelocity[3]);
void JB_BodyAddImpulse(uint32_t body, const float impulse[3]);
void JB_BodySetMotion(uint32_t body, int motion);
// Dynamic bodies: take Havok's mass properties as they are. invInertia is the diagonal of the inverse inertia in the
// body frame about the centre of mass (zeros allowed: locked axis).
void JB_BodySetMassProperties(uint32_t body, float invMass, const float invInertia[3], float linearDamping,
                              float angularDamping);
// Centre of mass in world space (diagnostics: compare with Havok motion +0x70).
void JB_BodyGetCenterOfMass(uint32_t body, float centerOfMass[3]);
uint32_t JB_BodyCount();

}  // extern "C"
