# Physics (Havok 2.x) and its boundary with the game

Status 2026-10-05: static map only (no runs). Havok is proprietary and is not decompiled: it is fenced off by address
range (symbols/functions.csv subsystems `havok`, `qhull`; game-written code inside or next to it is `havok-ext` and
`physics-glue`, which ARE game code to recover). Choosing the replacement engine is the human's decision
(docs/decision.md); section 6 below only lists what any replacement must provide. Source write-up and per-function
rows: work/classify/havok_boundary.md, work/classify/havok.csv (not committed; regenerate with the classifier).

Binary: `orig/pc/GameData/game.unpacked.exe` (image base 0x400000). Evidence comes from `work/exports/functions.json`,
capstone disassembly, and a `.rdata` vtable scan. Havok internals are described by role only. Labels such as
`hkWorld_addEntity` are suggested names: the binary has no symbols. Anything marked **guess** is inferred from data flow.
Per-function rows are in `work/classify/havok.csv` (1370 rows: 885 havok, 306 qhull, 72 havok-ext, 107 physics-glue;
62 of them are code-pointer targets that are missing from functions.json).

## 1. Library ranges

| Range | Label | First / last function | Edge evidence |
| --- | --- | --- | --- |
| 0x004a3c40-0x004a919f | **havok-ext** (game-specific Havok extensions, authorship **guess**: game-written) | 0x4a3c40 / 0x4a9180 | Below the start: 0x4a3bf0 is UI code (calls 0x4a2de0, uses g_game 0x5fe490+0xab4) and 0x4a3be0 jumps to game 0x4927f0. 0x4a3c40 is slot 1 of hk shape vtables 0x56b28c/0x58e760. The block defines custom shape types **0x12 and 0x13** and their agents. These agents are registered by the game's init (0x411960 -> 0x4a3d80, 0x4a4280, 0x4a87e0), not by Havok's register-all 0x50f090. The speed-trap agent 0x4a88a0 ("Ttspeedtrap shape") calls game function 0x456250 directly, which ICF folding cannot explain. The vtables (0x58e674-0x58e760) sit contiguously with Havok's rdata. |
| 0x004a91a0-0x004bc4bf | **havok** block 1 (hkbase, world, islands, motions, constraints, broadphase, some shapes, keycode) | 0x4a91a0 / 0x4bbee0 | 0x4a91a0 is the pool-memory size-class helper for hkPoolMemory ctor 0x4a9410 (vtable 0x58e5d4, which also holds 0x4a9540 "hkConfigMemoryStats.h"). Inside the block: hkError.cpp 0x4aa690, hkWorld.cpp 0x4addb0, timers 0x4ad7b0/0x4ad830/0x4ada90/0x4b25b0/0x4b2940, keycode 0x4b8000. The last function 0x4bbee0 is called only by 0x4ba8d0. |
| 0x004bc4c0-0x004de50f | **qhull** (copy embedded in Havok) | 0x4bc4c0 / 0x4de490 | The entry 0x4bc4c0 has "qhull Qt Pp W1e-7 E1e-7" and is called from Havok 0x4babf0. String 0x58b82c reads "HK_NULL new facets deleted", so qhull was compiled with Havok's macros. 0x4de040-0x4de490 have only qhull callers. |
| 0x004de510-0x004df0cf | **havok** (hkmath tail) | 0x4de510 / 0x4df050 | Pure vector/matrix/quaternion code. Callers are mostly Havok blocks 1 and 2 (for example, 0x4de6b0 has 18 Havok callers). Game calls a few of these (0x4de720/0x4de7e0/0x4de840/0x4de920/0x4dea40), probably through hk math headers. |
| 0x004df0d0-0x004dff5f | **physics-glue** (game-authored Havok-derived classes) | 0x4df0d0 / 0x4dff20 | 0x4df8a0 calls game 0x456250 and reads game data `[[ecx+8]+0x23c]`. Ctor 0x4df780 stores vtables 0x58b7a4/798/790 over game base vtables 0x5baa0c/0x5ba9c8 and is called by game init 0x411960. The type-0x19 agent create fn 0x4df500 is registered by 0x411960. Upper edge: 0x4dff60 uses the /GS cookie 0x5bf424 and is called by 0x4011e0 (PMS engine start, "AppAccel" at 0x4e0300). |
| 0x0050eb20-0x0053814e | **havok** block 2 (hkcollide: shapes, agents, dispatcher, MOPP, keycode decode) | 0x50eb20 / 0x536cc0 | Below the start: zlib inflate_fast 0x50e780 ("invalid distance code") ends at 0x50eb15. 0x50eb20 is called by Havok 0x4ac990 and calls hkArray 0x4aa830. Agent timer strings in rdata 0x56b500-0x56b968 cover 0x5154e0-0x51c160. The last function 0x536cc0 (5263 B) ends at 0x53814f, where the CRT starts (0x53814f, `CorExitProcess` page 0x539000). |

Exceptions inside the ranges:
- 0x004a4600-0x004a69ff inside the havok-ext range is Havok's own mass-properties library (hkInertiaTensorComputer:
  sphere / box / triangle / capsule / convex-hull mass properties, inertia shifts, Mirtich projection integrals; data
  "computeConvexHullTriangles 3.1 2001/10/19" at 0x005cad94; callers Havok). Fenced as `havok` (2026-10-05, found by
  the physics_03 recovery, whose copies were removed). The rest of havok-ext (custom shape types 0x12/0x13, their
  agents, the speed trap) stays game code.
- ICF-folded tiny game functions are called from Havok and sit in game space: 0x403c40 (vector-ctor iterator), 0x413770/0x414240 (64-byte copy), 0x4142b0/0x4143f0 (zero/identity), 0x459730, 0x412fa0 and 0x407ff0. These are not callbacks.
- Some game-space functions are vtable slots of Havok classes because of ICF (`ret`, `xor eax,eax`, `mov eax,1`): 0x418600, 0x4548c0, 0x4648a0, 0x4270f0, 0x4563c0, 0x433e70.
- Correction to an earlier classifier note: 0x4a3d80 is not a collision-layer filter. It registers collision agents (`dispatcher.registerAgent(createFn, typeA, typeB)` = 0x513270).
- 0x411960 references the hk*.cpp strings to pass (file, line) pairs to hkError vfunc+0x10 on singleton 0x5fed80, which suppresses five Havok warnings. It is not a statistics table.

## 2. Entry points (game -> Havok)

There are 173 direct call sites from 59 game functions into 69 library functions (`entries.json` scan). There are also indirect entries through function pointers and vtables, listed at the end of this section.

| Role | Library function (suggested name) | Game callers (sites) |
| --- | --- | --- |
| Base system init / quit | 0x4aa270 `hkBaseSystem_init` and 0x4aa3c0 `quit`, both through the fn-ptr table at 0x5cadc0 (0x4aa750, 0x4aa760, 0x4aa270, 0x4aa3c0) | 0x411960 (`call [0x5cadc8]`), 0x411cb0 (`call [0x5cadcc]`) |
| Memory / error | hkMemory singleton `[0x5fed84]`: vfunc +0x10 alloc(size, class), +0x14 free(p, size, class). hkError singleton `[0x5fed80]`: vfunc +0x10 | game allocates hk objects inline: 0x411960, 0x456300, 0x412fa0 |
| World create / step / destroy | 0x4addb0 hkWorld ctor (0x250 B, cinfo built by game 0x414370); 0x4ad7b0 `stepDeltaTime` (step info {dt, 1/dt}); world released by refcount in 0x411cb0 | 0x411960, 0x411d40 |
| Agent registration | 0x50f090 register-all (16 agents); 0x513270 `registerAgent`; ext registrars 0x4a3d80 / 0x4a4280 / 0x4a87e0 | 0x411960 |
| Listeners / filters | 0x4ac5c0 add listener (world+0xd0); 0x4ac620 remove; 0x4ac700 set collision filter (world+0xe8 -> +4); world+0x23c filter written directly | 0x411960, 0x411cb0 |
| Rigid body create / add / remove | 0x4afd80 rigid body ctor; 0x4aba90 `addEntity`; 0x4ac0e0 `removeEntity`; 0x4aff70 `setKeyframed` (motion type 4); 0x4b5a00 sync collidable transform | 0x457030, 0x457870, 0x457990, 0x458970, 0x458dd0, 0x45da90, 0x48cec0, 0x456fa0, 0x457000, 0x458db0, 0x4577b0, 0x4582d0, 0x487a90 |
| Wake / activation | 0x4ad130 `activateIsland(esi=world, edi=island)` (24 sites); 0x4ad070 / 0x4aafa0 deactivation bookkeeping (**guess**) | 0x403c70, 0x404140, 0x404410/60/b0, 0x406580, 0x455f10, 0x456250, 0x4583d0, 0x458440, 0x458510, 0x458580, 0x458970, 0x458dd0, 0x459680, 0x4596d0, 0x48e700 |
| Collision filtering | 0x4ac850 update filter on entity; 0x4ac910 on the whole world; 0x4aaf20 add pair exclusion (entity +0x78, **guess**) | 0x4577b0, 0x459260, 0x411d40, 0x457030 |
| Shape creation | 0x50f190 convex (radius) base; 0x5120f0 box(halfExtents); 0x512ab0 transform shape; 0x511350 list shape; 0x511550 transformed shape; 0x512cc0 (vtable 0x56b438, **guess** multi-sphere); ext 0x4a3c50 / 0x4a3d50 / 0x4a3d70 / 0x4a4530 | 0x4517b0, 0x4527d0, 0x4574e0, 0x475220, 0x413450, 0x456c40, 0x413810, 0x411960 |
| Mesh / MOPP | 0x50f900 mesh shape ctor, 0x50f8a0 addSubpart, 0x50f6f0 scaling, 0x50f750 getTriangle, 0x510aa0 / 0x510c90 / 0x510cf0 / 0x510f50 MOPP code, input, build, bv-tree; 0x510c10 / 0x510c40 add / remove ref | 0x412450, 0x412420 |
| Convex hull / decomposition | 0x4bc730 qhull; 0x4ba8d0 hull builder; ext 0x4a62f0, 0x4a5160, 0x4a5ed0, 0x4a6950, 0x4a46c0 (**guess**: convex decomposition) | 0x412450, 0x413450, 0x4527d0, 0x456c40, 0x411960 |
| Constraints / ragdoll | 0x4ae980, 0x4af020, 0x4af7c0, 0x4b0230 (four constraint ctors); 0x4ac3e0 `addConstraint`; 0x4ac460 `addAction` (**guess**) | 0x456570 ("RigidBodiesAddConstraints"), 0x4594b0, 0x459630 |
| Collision queries | 0x513420 dispatcher pair query with collector (**guess**: getClosestPoints); 0x4ae6c0 for-each contact of entity | 0x4562a0, 0x454b20, 0x478200 |
| Math | 0x4de720, 0x4de7e0, 0x4de840, 0x4de920, 0x4dea40; hkArray grow 0x4aa830 (11 sites) | 0x413970, 0x457ab0, 0x457c20, 0x459000, 0x459810, 0x459bb0, 0x45a1e0 and others |
| Keycode / licensing | 0x4b8000 (+ 0x531ef0) runs inside the library. The game makes no direct call. | none |

Indirect entry points through the **motion vtable**, reached via entity+8. The vtables are 0x58df50, 0x58dfc8, 0x58e1f8 and 0x58e278 (30 slots each):

| Slot | Role | Body | Game users |
| --- | --- | --- | --- |
| 0x18 | getType (4 = keyframed) | ICF consts | 0x457790, 0x4581c0, 0x4582d0, 0x411850 (filter) |
| 0x2c | setInertiaLocal | 0x4b3880 | 0x4582d0 |
| 0x40 | setPosition | 0x4b7050 | 0x406580 |
| 0x44 | setRotation (quat) | 0x4b70c0 | 0x48e700 |
| 0x48 | setPositionAndRotation | 0x4b7140 | 0x4596d0, 0x458970 |
| 0x50 | setLinearVelocity | 0x4b7200 | 0x458510, 0x456250, 0x455f10, 0x458970, 0x458dd0 |
| 0x54 | setAngularVelocity | 0x4b7220 | 0x458580, 0x459680, 0x4594b0, 0x458dd0 |
| 0x5c | applyLinearImpulse | 0x4b72d0 | 0x458440, 0x404410 |
| 0x60 | applyPointImpulse | 0x4b3b00 / 0x4b5f40 | 0x458440, 0x456570, 0x458970 |
| 0x64 | applyAngularImpulse | 0x4b3c80 / 0x4b5ff0 | 0x4583d0, 0x404460 |
| 0x6c | applyForce | 0x4b3d90 | 0x403c70, 0x404140 |
| 0x70 | applyTorque | 0x4b3eb0 | 0x4044b0 |
| 0x74 | 1.6 KB, unknown | 0x4b4ae0 | 0x458970 |

## 3. Callbacks (Havok -> game)

| Kind | Game code | Role / evidence |
| --- | --- | --- |
| Allocator | 0x4e07f0 alloc, 0x4e08d0 free (PMS heap object 0x63a210) | Reached through the game's global operator new/delete (tag string "new" at 0x58b5e8). Callers: 0x4aa270 (new hkPoolMemory 0x4340 B), 0x4a94f0, 0x4aa660, 0x4aeed0. All other Havok allocation goes through hkPoolMemory. |
| Direct call | 0x456250 `Body_wakeAndSetLinearVelocity` | Called from 0x4a88a0 (speed-trap agent) and 0x4df8a0 |
| Collision filter | vtable 0x5baa00 {0x4ac960 dtor, **0x411850** isCollisionEnabled} | Installed at world+0x23c by 0x411960. Reads entity +0xc flags, motion type, and the exclusion list +0x78/+0x7c. |
| Collision filter 2 | vtable 0x5baa18 {0x4ac960, **0x411710**} | Installed through 0x4ac700 (world+0xe8 -> +4) |
| World / collision listener | vtable 0x5baa48 {**0x413240**, **0x4112d0**} (ctor 0x4112c0) | Added with 0x4ac5c0 (world+0xd0) and removed in 0x411cb0 |
| Game-authored listener | 0x4df780 (vtables 0x58b7a4 / 0x58b798 / 0x58b790: 0x4df7d0, 0x4df840, **0x4df8a0**, ...) | Registered through 0x4ac5c0 / 0x4ac660 |
| Game-authored action / phantom (**guess**) | 0x4dfaa0 (vtable 0x58b760: 0x4dfb20, 0x4dfbf0, 0x4dfb70, ...) | Created by body create 0x457030 |
| Custom agents | type 0x19 create 0x4df500 (game init); ext types 0x12/0x13 (0x4a3f10, 0x4a3f40, 0x4a4290, 0x4a8810, 0x4a8840) | Called by the dispatcher during the narrowphase |
| Game shape subclasses | 0x5ba9d0 skin mesh shape (override **0x412420** getTriangle, dtor 0x412f50); 0x5baa20 (0x4115b0, **0x4115f0**, **0x411680**); 0x5b0ad8 (0x456450); 0x5b0af8 (0x456360) | Havok calls the overridden slots during collision |
| Point collector | vtable 0x5b0b18 {0x456300 dtor, 0x5136d0 Havok addPoint} | 0x4562a0 query |
| Pre-step "actions" (game side, before each `stepDeltaTime`) | 0x403c70, 0x404140 | Not called by Havok. They run inside the game's substep loop 0x411d40. |

## 4. Havok data the game reads and writes directly

Pointer chain: `g_game [0x5fe490] + 0xa8c` = physics manager {+4 game listener, +8 **hkWorld***, +0xc shape, +0x60 deferred-filter flag}. Game object +0x1c4 = **entity (rigid body)**, +0x1c8 = action. Field scan: `hv/flow.py` (taint-style per function).

| Object | Offset | Meaning | Game readers (R) / writers (W) |
| --- | --- | --- | --- |
| entity | +0x08 | motion* (vtable above) | 23 functions, for example 0x457790 and 0x458000 |
| entity | +0x0c | flags (bit 0 tested before physics ops; **guess**: fixed/no-world) | R 0x4577b0, 0x457870, 0x457990, 0x459260, 0x411850 |
| entity | +0x10 | simulation island* (island +0x24 list index, +0x29 active) | R 0x458510, 0x458440, 0x4583d0, 0x458580, 0x45da90 |
| entity | +0x20 | collision filter info (**guess**: layer) | W 0x459260 |
| entity | +0x28 | pointer to motion position copy (motion+0x60) | W 0x4582d0 |
| entity | +0x38 / +0x3c | friction / restitution (0x004517b0 stores the "friction" property through 0x00458250, "restitution" through 0x00458230) | W 0x458250 / 0x458230, R 0x458260 / 0x458240 |
| entity | +0x44 | hkWorld* (null when not in world) | R 0x453750, 0x454b20, 0x4577b0 and 11 more |
| entity | +0x78 / +0x7c | pair exclusion array {other, value} | 0x4aaf20 (Havok side), R game filter 0x411850 / 0x411710 |
| motion | +0x2c | inverse mass | R 10 functions (0x4056b0, 0x4247f0, 0x432cb0, ...) |
| motion | +0x30 | rotation quaternion | R 0x457c20 |
| motion | +0x40 / +0x50 | linear / angular velocity | R 0x458000 (26 callers) / 0x457fc0 |
| motion | +0x70 | center of mass (world) | R 0x457dc0 |
| motion | +0x80..+0xbc | transform (3x4 rotation, translation at **+0xb0**) | R 0x457930 `Body_getPosition`, 0x457c20, 0x457dc0 |
| motion | +0xc0 | keyframed / original motion link | R 0x4581c0, W 0x4582d0 |
| world | +0xd0 / +0xe4 / +0xe8 / +0x23c / +0x240 | listeners / dispatcher / collision input (filter at +4) / filter / default listener | 0x411960 and 0x4517b0 / 0x4527d0 / 0x4574e0 (R +0xe8) |
| world | +0x13c | "locked during step" flag (add / remove are deferred) | Havok side; game R +0x13d in 0x459260 |

This partly corrects an earlier classifier note: the body position is at motion+0xb0. Motion +0x40 holds linear velocity.

## 5. Game-side physics layer (top functions; full list in havok.csv, subsystem `physics-glue`)

| Addr | Size | Role | Havok entries used |
| --- | ---: | --- | --- |
| 0x411960 | 837 | Physics init: hkBaseSystem, error suppression, world, agents, filters, listeners | 0x4aa270*, 0x4addb0, 0x50f090, 0x513270, 0x4a3d80, 0x4a4280, 0x4a87e0, 0x4ac5c0, 0x4ac700, 0x4df780, 0x50f190, 0x4a46c0 |
| 0x411cb0 | 132 | Physics shutdown | 0x4ac620, 0x4aa3c0* |
| 0x411d40 | 178 | Per-frame step: substeps, dt clamp 1/60, pre-step forces | 0x4ad7b0, 0x4ac910 |
| 0x403c70 / 0x404140 | 626 / 709 | Per-substep force application (object forces, force-action list) | 0x4ad130, motion 0x6c / 0x5c / 0x64 / 0x70 |
| 0x412450 | 2721 | Level collision skin build (mesh, MOPP, hulls) | 0x50f900, 0x50f8a0, 0x510aa0, 0x510c90, 0x510cf0, 0x510f50, 0x4bc730, 0x4ba8d0, 0x4a62f0 |
| 0x4517b0 | 4047 | Object property loader: builds collision shapes | 0x5120f0, 0x512ab0, 0x511350, 0x50f190, 0x4a3d50 / 70, 0x4a4530 |
| 0x4527d0 | 1897 | Collision from "collision_radius" | 0x4a3c50, 0x512cc0, 0x512ab0, 0x511350, 0x4a5ed0 |
| 0x457030 | 1140 | Rigid body create and add to world | 0x4afd80, 0x4aba90, 0x4aff70, 0x4aaf20, 0x4dfaa0 |
| 0x4577b0 | 89 | Enable / disable physics for an object (72 callers) | 0x4aff70, 0x4ac850 |
| 0x457870 / 0x457990 | 182 / 257 | Teleport: remove, set position / transform, re-add | 0x4ac0e0, 0x4aba90, motion 0x40 / 0x48 |
| 0x458510 / 0x458580 | 106 / 106 | Set linear / angular velocity (wake first) | 0x4ad130, motion 0x50 / 0x54 |
| 0x458440 / 0x4583d0 | 203 / 106 | Apply impulse / angular impulse | 0x4ad130, motion 0x5c / 0x60 / 0x64 |
| 0x456570 | 1301 | Ragdoll / object constraints ("RigidBodiesAddConstraints") | 0x4ae980, 0x4af020, 0x4af7c0, 0x4ac3e0 |
| 0x458970 | 1079 | Ragdoll build / start | 0x4aba90, 0x4ac0e0, 0x4ad130, motion slots |
| 0x458dd0 | 556 | Body motion-mode change and removal | 0x4aba90, 0x4ac0e0, 0x4aff70, 0x4b5a00, 0x4ad070 |
| 0x458000 / 0x457930 | 50 / 86 | Get velocity / position (direct field reads) | none (fields) |
| 0x4562a0 | 84 | Closest-points query with a game collector | 0x513420 |
| 0x454b20 | 156 | Contact walk (with callback 0x454940) | 0x4ae6c0 |

`*` marks a call through a function pointer. I found no Havok ray-cast API (castRay) called directly from game code. Weapon and AI line of sight are probably the game's own raycast against its collision data (**guess**). The only Havok queries the game uses directly are 0x513420 and 0x4ae6c0.

## 6. Replacement plan (minimal interface for Jolt or Bullet)

Implement these behind the glue functions above. Do not try to emulate Havok objects.
1. **System**: `init / shutdown / step(dt, substeps)` covers 0x411960, 0x411cb0, 0x411d40. Keep the game's pre-step force callbacks (0x403c70, 0x404140) running before each substep.
2. **Shapes**: sphere/convex radius, box, transform, list/compound, triangle mesh (with a BVH replacing MOPP), and convex hull from points (replacing qhull and the decomposition chain). The custom types 0x12 (speed trap), 0x13 and 0x19 need equivalent contact callbacks.
3. **Bodies**: create (mass, inertia, motion type dynamic/keyframed/fixed), add, remove, set keyframed, set filter info + refilter, and per-pair exclusion.
4. **State access**: get/set position, rotation, transform, linear/angular velocity, center of mass, inverse mass, plus apply force/torque/impulse/point impulse/angular impulse, and wake. The game reads position, rotation, velocity and inverse mass straight from motion offsets (+0x2c, +0x30, +0x40, +0x50, +0x70, +0xb0). Those reads need a shim: either a mirror struct that is updated after each step, or patching the roughly 30 reader functions.
5. **Constraints**: four constraint types (0x4ae980, 0x4af020, 0x4af7c0, 0x4b0230) used for ragdolls.
6. **Callbacks**: a collision filter (0x411850 semantics), a contact listener (0x413240 / 0x4112d0), contact iteration (0x4ae6c0), and closest points (0x513420).

## Risks

- **Direct field access**: about 40 game functions dereference entity and motion offsets, and inline Havok idioms such as island activation (`[island+0x29]`, `[island+0x24]`) and hkArray growth. A drop-in replacement must keep these layouts or patch every reader.
- **LTCG register ABIs and inlining**: many entries take arguments in esi/edi/eax (0x4ad130, 0x4ac5c0, 0x4aba90 and others), and hkMemory / hkArray code is inlined into game functions. Hooks must match each custom ABI.
- **Game-authored extensions inside the library ranges** (0x4a3c40-0x4a919f, 0x4df0d0-0x4dff5f) hold game behaviour: speed traps, custom agents and decomposition. They must be recovered and kept, not discarded with Havok.
- **Offline data**: collision skins are built at load from game meshes (MOPP and qhull). Determinism and collision feel depend on Havok's solver and agent tolerances. Expect gameplay drift and a rebalance of ragdolls and vehicles.
- **Keycode check**: 0x4b8000 / 0x531ef0 is licensing code inside the library. Leave it untouched; replacing the library removes it.
