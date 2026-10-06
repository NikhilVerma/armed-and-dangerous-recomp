# Physics: replacing Havok with Jolt

Decision: `docs/decision.md` 2026-10-06. Havok boundary: `docs/subsystems/physics.md`. Havok data layouts:
`docs/subsystems/havok-layouts.md` (in progress).

## Why Jolt

Jolt Physics v5.6.0 (MIT; Jorrit Rouwe, used by Guerrilla in Horizon Forbidden West) is the closest open-source
match to Havok 2's model:

- static, keyframed and dynamic bodies, which map to Jolt's Static, Kinematic and Dynamic motion types;
- shapes: sphere, box, convex hull (Havok builds these with qhull), triangle mesh (in place of Havok's MOPP), a
  transformed shape and a compound;
- a contact listener, collision filtering through layers, and constraints;
- it builds for 32-bit x86.

Bullet and PhysX were the alternatives, but Jolt has the cleaner API and deterministic stepping.

## Build layout

- Jolt is fetched by CMake (`FetchContent`, tag `v5.6.0`) into the build directory. It is not vendored in git.
- It is compiled into the static library `and_physics` with `/arch:SSE2`. SSE4/AVX/LZCNT/TZCNT/F16C/FMA are off so
  that it runs on any x86 CPU the game runs on. `CROSS_PLATFORM_DETERMINISTIC` is on and the CRT is static.
- The hook DLL itself stays `/arch:IA32` (x87, for fidelity), so Jolt headers never reach it. The only interface is
  the plain C API in `hook/physics/jolt_bridge.h`, with handles as `uint32_t` and vectors as `float[3]` and
  quaternions as `float[4]` in (x, y, z, w) order.
- `hook/physics/jolt_bridge.cpp` is the only file that includes Jolt.
- Headless test with no game: `pwsh tools/build.ps1 -Target jolt_bridge_test`, then run
  `build/hook-release/jolt_bridge_test.exe`. It drops a box onto a mesh floor under the game's gravity and expects
  PASS. Result 2026-10-06: `PASS: box at (0, 0, 0.980) speed 0.0000`.

## Hook points (recovered glue, faithful when unset)

`src/physics/physics_hooks.h`; all four are null by default, which is the original behaviour:

| Pointer | Called from |
| --- | --- |
| `g_physicsAfterInit(manager)` | end of `PhysicsManager_Init` 0x00411960, after the hkWorld (manager +8) exists |
| `g_physicsBeforeShutdown(manager)` | start of `PhysicsManager_Shutdown` 0x00411cb0 |
| `g_physicsBeforeSubstep(manager, dt)` | `PhysicsManager_Step` 0x00411d40, before each `hkWorld_stepDeltaTime` 0x004ad7b0; returning true skips Havok's step |
| `g_physicsAfterSubstep(manager, dt)` | after each substep, whether or not Havok's step ran |

## Stages

Select a stage with `[physics] engine=` in `and.ini`. The default is `havok`, and tests always run with it.

1. **`jolt_shadow`** (skeleton done in `hook/enhance/physics_jolt.cpp`). Havok still simulates. Every Havok entity
   gets a Jolt mirror body. Before each substep, Jolt is seeded with Havok's position, rotation and velocities;
   after it, Jolt steps the same dt, and the per-body position and velocity differences are logged once a second.
   This "one-step agreement" check avoids chaotic drift: a large error points to a wrong shape, mass, gravity or
   unit, not to chaos. The game is unchanged.
2. **`jolt`**: Jolt simulates and writes position, rotation and velocities back into each Havok motion (+0xb0,
   +0x30, +0x40, +0x50), and Havok's step is skipped. Keyframed bodies are driven from Havok's fields into Jolt.
3. Contacts: Havok's collision listeners (the game's damage, sound and effects callbacks) are fed from Jolt's
   `ContactListener`. Collision filter groups become object layers. Constraints and ragdolls are mapped.
4. Remove Havok. The recovered glue calls the bridge directly, and the fenced Havok ranges are no longer reached.

## Havok fields used (evidence in physics.md section 4)

| Offset | Meaning |
| --- | --- |
| Game+0xa8c | physics manager; +8 is the hkWorld |
| object +0x1c4 | the object's Havok entity |
| entity +0x08 | motion |
| entity +0x44 | world (null when the entity is not in a world) |
| entity +0x38 | friction |
| entity +0x3c | restitution |
| motion +0x2c | inverse mass (0 means keyframed or fixed) |
| motion +0x30 | rotation quaternion (x y z w) |
| motion +0x40 | linear velocity |
| motion +0x50 | angular velocity |
| motion +0xb0 | position |
| gravity | (0, 0, -64.348): Z up, in feet (2 x 32.174 ft/s^2) |

## Open work (next session)

- `TranslateShape` in `physics_jolt.cpp` is a stub that returns 0, so every body currently counts as
  "unsupported". It needs the shape type IDs and layouts from `havok-layouts.md`: sphere, box, convex vertices,
  MOPP/mesh, transform, list, and the game's custom shapes 0x12, 0x13 and 0x19.
- `ForEachEntity` walks the game's object list and so misses level collision and ragdoll parts. It should walk the
  hkWorld's island entity arrays instead (`havok-layouts.md`).
- Level collision (static mesh) has to be mirrored once per level load.
- Not yet run in game. The first run uses `engine=jolt_shadow`, and its log should be read before trying stage 2.
