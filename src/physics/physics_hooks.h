#pragma once
// Enhancement points of the recovered physics glue (src/physics/physics_01.cpp) for the Jolt replacement
// (hook/enhance/physics_jolt.cpp, docs/subsystems/physics-jolt.md). All null by default: the original behaviour.

#include <cstdint>

// After PhysicsManager_Init (0x00411960) created the Havok world (manager +8).
extern void (*g_physicsAfterInit)(uint8_t* manager);
// At the start of PhysicsManager_Shutdown (0x00411cb0), while the Havok world still exists.
extern void (*g_physicsBeforeShutdown)(uint8_t* manager);
// In PhysicsManager_Step (0x00411d40), around each substep's hkWorld_stepDeltaTime (0x004ad7b0). Before: returning true
// skips the Havok step for this substep (the replacement simulated it instead). After: called either way.
extern bool (*g_physicsBeforeSubstep)(uint8_t* manager, float dt);
extern void (*g_physicsAfterSubstep)(uint8_t* manager, float dt);
