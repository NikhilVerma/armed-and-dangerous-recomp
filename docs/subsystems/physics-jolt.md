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

## Shadow mirror (stage 1, implemented 2026-10-06; not yet run in game)

- **Enumeration**: hkWorld active islands (+0x08), inactive islands (+0x14) and the fixed island (+0x2c), each
  island's entity array at +0x3c (havok-layouts.md s1). Level collision lives in the fixed island and is mirrored as
  static bodies; no separate level loader is needed. Bodies on the game's pending-add list are picked up once added.
- **Types by vtable**: motion type and shape type are read from the vtable address, so Havok code is never called.
  Motion types 0..3 map to Dynamic, 4 to Kinematic and 5 to Static.
- **Shapes** (cached per Havok shape pointer):
  - sphere maps to a sphere;
  - the game's offset sphere maps to a transformed sphere of its outer radius;
  - box maps to a box;
  - convex vertices (the 4-vertex SoA blocks) map to a convex hull;
  - MOPP maps to its child mesh, and a mesh (subparts, u16 or u32 indices, scaling) maps to a MeshShape;
  - list maps to a StaticCompound, with children that have nothing solid dropped;
  - transform maps to a RotatedTranslated shape (the hkTransform columns converted to a quaternion);
  - the 0x12, 0x13 and 0x19 wrappers map to their child;
  - triggers (bv 3 + 0x1b) and multi-ray have no solid shape;
  - unknown vtables are counted and logged at shutdown.
- **Mass properties** (dynamic bodies): the shape is wrapped so that Jolt's centre of mass equals Havok's (motion
  +0x20). Inverse mass is +0x2c. Inverse inertia is the diagonal at +0xe0 (types 2/3) or the scalar at +0x10
  (types 0/1). Damping is +0x14/+0x18. The velocity caps are raised to 10000 and 1000 because the game's units are
  feet.
- **Lifecycle**: a mirror is rebuilt when the entity's unique id (+0x18) or its motion class changes, and destroyed
  when the entity leaves the world.
- **Compare**: only bodies in active islands with dynamic motion. Static mirrors are not re-seeded.
- **Known differences** (expect a small error, not zero):
  - Havok's convex radius pads hulls outwards, while Jolt's stays inside the hull, so hulls are about 0.1 smaller.
  - Havok's mesh radius (0.1) has no Jolt equivalent.
  - Havok's solver parameters (tau 0.6, damping 1.0, 4 iterations) are not mapped to Jolt's.
  - Collision filtering is not mirrored yet, so in Jolt everything moving collides with everything.
- Headless bridge test (2026-10-06): `PASS: box at (0, 0, 0.980) speed 0.0000; free fall speed at 0.5 s 32.174`.

## First in-game runs (2026-10-06, `python tools/jolt_shadow_run.py --scenario play`)

Only the three PhysicsManager hooks are on, so everything else is original code. The runs are deterministic: the logs
are identical from run to run.

- **Mirrors:** lev_1_m1 gives 240 static mirrors (level collision, 441 Havok shapes), 21 kinematic and 33-35 dynamic.
  12 entities are unsupported: triggers, and the world's fixed body, which has no shape.
- **Error before the filter was mirrored:** the one-step position error averaged 0.3-0.4 (max 1-4.5), and the
  velocity error 0.2-2.9.
- **Breakdown** (log lines "free flight" and "worst contact-free"):
  - **Bodies with no contact in either engine:** the rotation, angular velocity and centre of mass agree exactly,
    including a body whose local centre of mass is 2.56 off its origin. The state mapping is right.
  - **Most of the error: contacts only Jolt had.** Some are Jolt's position solver separating overlapping bodies,
    which moves and rotates them without any velocity change. Both are pairs that Havok's collision filter rejects.
    Fix: Jolt's `OnContactValidate` now calls the game's own filter 0x00411710 for each new body pair
    (`JB_SetPairFilter`).
  - **Fast hull contacts** (about 47 ft/s): the two solvers resolve them differently. That is expected until
    stage 2 tuning.
- **One crash inside JB_ShapeMesh**, reproduced twice and then gone after the vertex arrays were moved into
  `MeshShapeSettings` instead of copied. No mesh exceeds 10k triangles. Mesh subparts are now validated and
  rejects are logged.

## Open work (next session)

- Measure the error with the filter mirrored. Check, with hook coverage, whether Havok calls the world filter
  0x00411850 (which rejects dynamic-vs-dynamic pairs); if so, mirror it too.
- Confirm that mesh index type 0 means 16-bit indices. Angular velocity is in the world frame: confirmed by the
  contact-free comparison.
- Stage 2 write-back. Stage 3: contact callbacks, triggers (sensor bodies for bv 3 + 0x1b), the behaviour of the
  0x12, 0x13 and 0x19 wrappers, and constraints (havok-layouts.md s5 maps each one to a Jolt constraint).
