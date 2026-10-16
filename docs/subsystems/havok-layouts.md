# Havok 2.x data layouts (for Jolt shadow mode)

Status 2026-10-06: static only (no runs, no builds). Purpose: everything a per-step "mirror every Havok rigid body into
Jolt" pass has to read. Havok code stays fenced (docs/subsystems/physics.md section 1); this file describes only data
layouts, established from the constructors / accessors in `orig/pc/GameData/game.unpacked.exe` (base 0x400000,
capstone) and from the recovered glue in `src/physics/physics_01..04.cpp`. Every row gives an address as evidence.
**guess** = inferred from data flow, not proven. Names are suggested (no symbols in the binary).

Conventions used throughout (all evidence-backed):

- **hkArray** = `{T* data; int32 size; uint32 capacityAndFlags}`; bit 31 of the third word = storage not owned
  (in-place / borrowed buffer, never freed). Grow helper 0x004aa830(array, newCap, elemSize). Evidence: world ctor
  0x004addb0 writes `0x80000000` into every third word; 0x004adff5 skips the free when that word is negative; in-place
  arrays are initialised as `data = &inline, cap = 0x80000000 | n` (entity +0x50 at 0x004b01b5, island +0x3c at
  0x004b168f).
- **hkReferencedObject** header on every Havok object: +0 vtable, u16 +4 allocation size, u16 +6 reference count
  (0x004b0170, 0x0050f190; release = decrement +6, at zero virtual 0 (deleting dtor) with 1, e.g. 0x004b0080).
- **hkTransform** (64 bytes) = rotation as three **column** vec4s at +0x00 / +0x10 / +0x20, translation vec4 at
  +0x30. Evidence: 0x004de720 (world point -> local) computes `local.x = dot(p - t, [+0..+8])`, `local.y = dot(.., [+0x10..])`,
  `local.z = dot(.., [+0x20..])` with `t = +0x30`; identity constant 0x005cad50 = 1,0,0,0 / 0,1,0,0 / 0,0,1,0 / 0,0,0,1.
- **Quaternion** = (x, y, z, w), w last: identity built as {0, 0, 0, 1.0} at 0x004b00ee..0x004b0106; the game's
  rotate-by-quaternion code in 0x00459810 uses `q[3]` as w.
- **World frame is Z-up, units are game units (feet, guess)**: the game overrides gravity to (0, 0, -64.348)
  (PhysicsManager_Init 0x00411960; Havok's default from 0x00414370 is (0, -9.8, 0)). -64.348 = 2 x 32.174 ft/s^2
  (guess at the reason). Jolt must be given the same up axis and gravity.

Pointer chain: `g_game [0x005fe490] + 0xa8c` = physics manager; manager +8 = **hkWorld***; game object +0x1c4 = its
**entity** (rigid body); ragdoll records at object +0x1bc (0x4c bytes each, entity at +0), Object_GetBody 0x004574b0.

## 1. hkWorld (0x250 bytes, class 0x25, ctor 0x004addb0, cinfo defaults 0x00414370)

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x00 | vtable | 0x0058e3c8 | 0x004addce |
| +0x08 / +0x0c / +0x10 | hkArray<island*> | **active simulation islands** (data, count, capacity) | 0x004ad130 activateIsland appends the island here (`lea ebx,[esi+8]`), sets island +0x29 = 1; 0x004aba90 addEntity appends new islands here |
| +0x14 / +0x18 / +0x1c | hkArray<island*> | **inactive simulation islands** | 0x004ad130 removes the island from `[world+0x14]` at index island +0x24 (swap with last, `dec [esi+0x18]`) |
| +0x20 / +0x24 / +0x28 | hkArray | unknown (third island-level array; **guess**: phantoms or dirty islands) | zeroed by 0x004addf6..0x004addfc; not traced |
| +0x2c | island* | **fixed island** (holds every entity with flags bit 0, see entity +0x0c) | created at 0x004ae215 (island ctor 0x004b1650, +0x24 = -1, +0x29 = 0, +0x30 = 1); 0x004abad6 addEntity uses it for fixed entities |
| +0x30 / +0x34 | float | 0.2 / 10.0 (unknown) | 0x004ae04a / 0x004ae051 |
| +0x40 | vec4 | **broadphase AABB min** (cinfo +0x10) | 0x004ae058..0x004ae070 |
| +0x50 | vec4 | **broadphase AABB max** (cinfo +0x20) | 0x004ae073..0x004ae08c; both passed to the broadphase ctor 0x00531e60 |
| +0x60 | entity* | the world's own **fixed rigid body** (motion type 5, added to the world) | 0x004ae236..0x004ae278; the game uses it as the "world" side of pin constraints (0x004594b0 reads world +0x60) |
| +0x64..+0xd8 | 11 hkArrays | listener / action arrays; +0x7c / +0x80 = constraint listeners (notified by 0x004b0590); +0xd0 = world/collision listeners (0x004ac5c0) | 0x004ade02..0x004ade98, 0x004b0596 |
| +0xdc | u32 | 0 | 0x004ae368 |
| +0xe0 | ptr | broadphase (0x00531e60(min, max)) | 0x004ae094; addEntity calls its virtual +4 (0x004abc29) |
| +0xe4 | ptr | collision dispatcher (0x114 bytes, 0x00513620) | 0x004ae17e; agents registered on it by 0x00411960 |
| +0xe8 | ptr | collision input (0x1c bytes): +0 dispatcher, +4 collision filter, +0xc tolerance (cinfo +0x40; game 0.2), +0x10 cinfo +0x44, +0x14 cinfo +0x48, byte +0x18 cinfo +0x4c | 0x004ae1a6..0x004ae1ec; filter replaced by 0x004ac700 |
| +0xf0 | vec4 | **gravity** (cinfo +0x00) | 0x004ae027..0x004ae044 |
| +0x100..+0x138 | 4 hkArrays | +0x118 = pending-operation list (entities with a reference, 0x004ad070); others unknown | 0x004ad073 |
| +0x13c | byte | locked (inside step; add/remove deferred) | addEntity 0x004abaa0 saves and sets it |
| +0x140 | u32 | entity uid counter (written to entity +0x18 on add) | 0x004abaab..0x004abac9 |
| +0x144 / +0x148 | ptr | collision filter / broadphase-pair helper objects (0x24 bytes 0x0052d1c0; 4 bytes vtable 0x0058e394) (**guess**) | 0x004ae0cf, 0x004ae0fc |
| +0x150..+0x16b | 7 dwords | copy of the collision input | 0x004ae286 `rep movsd` |
| +0x16c | float | step delta time (**guess**: read by the type-0x19 shape's getAabb 0x004dfbf0) | zeroed 0x004ae2fa |
| +0x180..+0x1cc | floats | solver parameters: +0x184 = cinfo +0x34 (0.6, **guess** tau), +0x188 = cinfo +0x38 (1.0, **guess** damping), +0x19c = cinfo +0x3c (4, **guess** iterations), +0x1a0 = 1 / iterations, +0x198 = 0.99 | 0x004ae293..0x004ae2f4 |
| +0x224 / +0x228 | int | cinfo +0x30 (0x200) and half of it (**guess**: broadphase/agent budget) | 0x004ae09a..0x004ae0ab |
| +0x22c | byte | 1 = one island per entity (else a single shared island at active[0]) | 0x004ae35a; branch 0x004abade |
| +0x22e / +0x22f | byte | default-listener installed flags | 0x004ae31f, PhysicsManager_Init 0x00411960 |
| +0x230 / +0x234 / +0x238 | float | cinfo +0x50 (-0.03), +0x50 * +0x54, +0x58 (0.001) (unknown; **guess**: deactivation / contact thresholds) | 0x004ae300..0x004ae319 |
| +0x23c | ptr | collision filter (game replaces it with vtable 0x005baa00, isCollisionEnabled 0x00411850) | 0x004ae34d, 0x00411960 |
| +0x240 | ptr | default listener (game installs one) | 0x004ae375, 0x00411960 |

World cinfo (0x00414370 then 0x00411960): +0x00 gravity (game: 0, 0, -64.348, 0); +0x10 / +0x20 broadphase min / max
(game: (-5000, -5000, -1100) / (5000, 5000, 2000)); +0x30 0x200; +0x34 0.6; +0x38 1.0; +0x3c 4; +0x40 0.2 (game,
default 0.1); +0x44 0.05; +0x48 0.2; byte +0x4c 1; +0x50 -0.03; +0x54 0.3; +0x58 0.001. Ctor called with version
0x30da6 (0x004adeec; any other value logs an hkError).

### Simulation island (0xa8 bytes, class 0x27, ctor 0x004b1650 (eax = island, edx = world))

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x00 / +0x1c | vtables | 0x0058e0d4 / 0x0058e0c8 | 0x004b166b, 0x004b1671 |
| +0x20 | hkWorld* | owner world | 0x004b1668 |
| +0x24 | int | index in the world's active or inactive island array (-1 for the fixed island) | 0x004ad172..0x004ad18e, 0x004abb12, 0x004ae21d |
| +0x28 | byte | 0 (unknown) | 0x004b1678 |
| +0x29 | byte | **active** (1 = in world +0x08 array) | 0x004b1686 (new islands start active), 0x004ad191 |
| +0x30 | byte | 1 at creation, cleared on activation (**guess**: needs-split/dirty) | 0x004b1689, 0x004ad19d |
| +0x3c / +0x40 / +0x44 | hkArray<entity*> | **entities in this island** (in-place capacity 1 at +0x48) | 0x004b1696; island addEntity 0x004b1850 appends and sets entity +0x10 = island, entity +0x48 = index |
| +0x58 / +0x5c / +0x60 | hkArray<constraint*> | **constraints in this island** (in-place capacity 1 at +0x64) | 0x004b18b0 (called by addConstraint 0x004ac3e0) appends and sets constraint +0x14 = island |
| +0x4c, +0x68..+0x94 | hkArrays | unknown (actions etc.) | 0x004b169c..0x004b16eb |

### Enumerating every body (algorithm, evidence above)

```
world = *(g_game + 0xa8c) -> +8
for arr in (world+0x08 active, world+0x14 inactive):         // hkArray<island*>
    for i < arr.size: visit(arr.data[i])
visit(world+0x2c fixed island)
visit(island): for j < [island+0x40]: entity = ([island+0x3c])[j]
constraints:   same islands, array at island +0x58 (count +0x5c)
```
Every added entity lands in exactly one of these islands (addEntity 0x004aba90: fixed flag -> world +0x2c; else a new
island appended to the active array, or the single island active[0] when world +0x22c is 0). The world's own fixed
body (+0x60) is in the fixed island. Activity: `[entity+0x10]+0x29`. Entities with `+0x44 == 0` are not in a world.
Bodies queued by the game but not yet added (pending list Game+0xa84 +0xf20, Body_Create 0x00457030) are not in any
island.

## 2. Entity / rigid body (0x84 bytes, class 0x23; ctor 0x004afd80, base ctor 0x004b0170)

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x00 | vtable | 0x0058e270 (rigid body; base 0x0058e4e8) | 0x004afd90, 0x004b0176 |
| +0x04 / +0x06 | u16 | size / reference count | 0x004b0170, Body_RemoveAndRelease 0x00457000 |
| +0x08 | motion* | **motion** (section 3); replaced when keyframing is toggled (0x004aff70) | 0x004afde1, 0x004afed8, 0x004affe0 |
| +0x0c | byte flags | bit 0 = **fixed** (set for motion type 5); fixed entities go to the fixed island | 0x004afda1, 0x004b01f5, 0x004abacc |
| +0x10 | island* | simulation island | 0x004b1859 |
| +0x14 | collidable | start of the embedded collidable (passed as `lea edi,[esi+0x14]` to the broadphase, 0x004abbd6; game filters read collidable +0xc / +0x1c) | 0x004abbd6, 0x00411710 |
| +0x18 | u32 | unique id (world +0x140 counter) | 0x004abac9 |
| +0x20 | u32 | **collision filter info** (collidable +0xc), cinfo +8 | 0x004b0209; written by the game in 0x00459260; semantics below |
| +0x24 | shape* | **root shape** (collidable +0x10), cinfo +4, reference held | 0x004b017f..0x004b021c |
| +0x28 | ptr | motion state = motion + 0x60 (collidable +0x14; agents read `[c+0x14]+0x20` = transform, `+0x50` = translation) | 0x004afde7, 0x004aff23, 0x004affe3; speed-trap agent 0x004a88a0 |
| +0x2c | ptr | -> +0x34 (broadphase handle) | 0x004b01f2 |
| +0x30 | ptr | owner = the entity itself (collidable +0x1c) | 0x004b01e9; filters 0x00411710 / 0x00411850 read `collidable+0x1c` |
| +0x34 / +0x36 | u16 | broadphase handle: +0x34 = cinfo u16 +0 (unknown, **guess**: handle type), +0x36 = 0 | 0x004b01ec |
| +0x38 | float | **friction** (default 0.5; cinfo +0x7c) | 0x004b01a4, 0x004aff3f; game 0x00458250 / 0x00458260 |
| +0x3c | float | **restitution** (default 0.4; cinfo +0x80) | 0x004b01ab, 0x004aff47; game 0x00458230 / 0x00458240 |
| +0x40 | ptr | deactivator (cinfo +0x88: 0 none, 1 = 0x60-byte object 0x004b5ac0); game drops it when its cinfo +0xf is set | 0x004b0030, Body_Create 0x00457030 |
| +0x44 | hkWorld* | world, 0 when not added | 0x004abad1; game readers 0x004577b0 etc. |
| +0x48 | int | index in island +0x3c array | 0x004b0221 (-1), 0x004b1894 |
| +0x4c | u16 | cinfo u16 +2 (default 0xffff; game sets 0) (unknown, **guess**: contact-callback delay) | 0x004b0205, 0x00457030 |
| +0x50 / +0x54 / +0x58 | hkArray | unknown (in-place capacity 1 at +0x5c) | 0x004b01b2..0x004b01c3 |
| +0x60 / +0x64 / +0x68 | hkArray<ptr> | constraints attached to this entity (each entry = constraint + 8) | 0x004b33fa..0x004b342e (constraint ctors call it for A and B) |
| +0x6c / +0x70 / +0x74 | hkArray | unknown (**guess**: entity listeners) | 0x004b01d4..0x004b01da |
| +0x78 / +0x7c / +0x80 | hkArray<{u32 key, u32 value}> | **properties**. Key 0 = the owning **game object** (written by Body_Create `0x004aaf20(body, 0, object)`); key 1 = 1 marks a type-0x19-wrapped body. This is the entity -> game-object back pointer | 0x00404510 Entity_FindPairValue, 0x00457030, filters 0x00411710 / 0x00411850 |

Collision filter info (+0x20): low 16 bits = membership bits, high 16 bits = collide-with mask; two bodies are
rejected when both words are non-zero and neither `A & (B >> 16)` nor `B & (A >> 16)` is non-zero (game filter
0x00411710). Same filter also rejects: equal owners unless owner virtual +0x44 allows it; keyframed (motion type 4)
vs keyframed or fixed; owner virtual +0xa0(other) veto. World filter 0x00411850 additionally rejects dynamic vs
dynamic unless an owner is ragdolled (+0x421) or has property key 1. Body_Create defaults the word to 0x4000 for
bodies without a shape. Mesh and hull shapes built by 0x00412450 carry `layer << 16 | part` in shape +8.
(2026-10-16, agent-charbody) 0x00411850 is not a pair filter for the simulation: the game calls it from the
swept-body contact listener Type19Listener_OnContact 0x004df8a0 to decide whether to clamp speeds, and as a pair filter
it would refuse a character against the landscape (no owner, not ragdolled). Characters do collide with each other:
oracle lev_2_m2, grunt 16 (radius 4) walks round grunt 15 at 7.7..7.97 apart. The AI owner's +0xa0 veto is
GenericAIObject_Slot40 0x00432590 (+0x50c / +0x510, grunt-ai.md section 14).

Rigid body cinfo (Havok, 0x8c bytes; defaults 0x00459730, filled by Body_Create 0x00457030, consumed by 0x004afd80):
u16 +0 (-> entity +0x34), u16 +2 (-> +0x4c), +4 shape, +8 filter info, +0x10 position, +0x20 rotation quaternion,
+0x30 inertia tensor (3 x vec4, local), +0x60 centre of mass (local), +0x70 mass, +0x74 linear damping (0),
+0x78 angular damping (0.05), +0x7c friction, +0x80 restitution, +0x84 motion type, +0x88 deactivator type.
Evidence: 0x004afd96 (+0x84), 0x004afee9 (+0x30 -> motion virtual +0x2c), 0x004afef5 (+0x60 -> +0x3c), 0x004afefc
(+0x70 -> +0x1c), 0x004aff0b / 0x004aff14 (+0x74 / +0x78 -> motion +0x14 / +0x18), 0x004afdb9 (+0x10, +0x20 to
the motion base ctor).

## 3. Motion (owned by entity +0x08)

Base ctor 0x004afbc0 (edx = motion, ecx = position, eax = quaternion), vtable slots in physics.md section 2.

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| +0x0c | float | radius of the shape's bounding sphere about the centre of mass (|sphere centre - COM local| + sphere radius; default 1.0) (**guess**: used for deactivation/CCD) | 0x004afbf0, 0x004b00e0 |
| +0x10 | float | sphere-inertia motions: **scalar inverse inertia** = 1 / max(Ixx, Iyy, Izz); box-inertia motions: -1.0 at creation (unknown) | 0x004b5e60, 0x004b5f00 (getInertiaInvLocal returns diag(+0x10)), 0x004b37fc |
| +0x14 | float | **linear damping** | 0x004aff11 |
| +0x18 | float | **angular damping** | 0x004aff1a |
| +0x20 | vec3 | **centre of mass, local** (setCenterOfMassLocal slot 0x3c 0x004b6ff0) | 0x004b7000..0x004b701d |
| +0x2c | float | **inverse mass** (w of +0x20; slot 0x20 setMassInv; 0 for fixed / keyframed) | 0x004b3ae6, 0x004b6fe4, 0x004afdd7 |
| +0x30 | quat | **rotation** (x, y, z, w) | 0x004afc5e..0x004afc72 |
| +0x40 | vec4 | **linear velocity** (world) | zeroed 0x004afbd5; slot 0x50 0x004b7200; game 0x00458000 |
| +0x50 | vec4 | **angular velocity** (world frame: **guess**) | zeroed 0x004afbe1; slot 0x54 0x004b7220; game 0x00457fc0 |
| +0x60 | vec4 | motion-state header: position at creation (**guess**: the swept-transform start); +0x6c is bumped by 1e6 in setCenterOfMassLocal (unknown) | 0x004afc0c, 0x004b6ff2 |
| +0x70 | vec4 | **centre of mass, world** (+0x7c preserved) | 0x004afc26, 0x004b7031 |
| +0x80 | hkTransform | **body transform**: rotation columns +0x80 / +0x90 / +0xa0, **translation +0xb0** (rotation rebuilt from +0x30 by 0x004de840) | 0x004afc3d, 0x004afc75; game readers 0x00457930 / 0x00457c20; the inverse-transform helper 0x004de720 is called on motion + 0x80 by 0x00459810 / 0x0045a1e0 |
| +0xc0 | ptr | keyframed motion: the saved original motion (restored on un-keyframe) | 0x004b5529, 0x004aff70 / 0x004b59a0, game 0x004581c0 / 0x004582d0 |
| +0xe0 / +0xe4 / +0xe8 | float | box-inertia motions: **inverse inertia diagonal, local** (setInertiaLocal slot 0x2c 0x004b3880 stores 1/Ixx, 1/Iyy, 1/Izz) | 0x004b38aa..0x004b38be, 0x004b38d0 |
| +0xec | float | box-inertia motions: inverse mass copy | 0x004b3ae9 |
| +0xc0..+0xfc / +0xf0..+0x10c | | sphere / box motions: other solver state (zeroed / unit) | 0x004afcdf, 0x004b377d |

Motion types (cinfo +0x84, jump table 0x004aff58; getType = vtable slot 0x18):

| Type | Size | Ctor | Vtable | getType body | Inertia storage | Name |
| ---: | ---: | --- | --- | --- | --- | --- |
| 0 | 0x100 | 0x004afcd0 | 0x0058e278 | 0x00418600 (0) | scalar +0x10 | sphere inertia (**guess** on the name) |
| 1 | 0x100 | 0x004afcd0 + vtable | 0x0058e1f8 | 0x004648a0 (1) | scalar +0x10 | stabilised sphere inertia (**guess**) |
| 2 | 0x110 | 0x004b3770 | 0x0058dfc8 | 0x00465350 (2) | diagonal +0xe0 | box inertia (**guess** on the name). **The game's dynamic bodies use 2** (0x004527d0 desc +0x70 = 2, 0x00413450) |
| 3 | 0x110 | 0x004b3770 + vtable | 0x0058df50 | 0x004563c0 (3) | diagonal +0xe0 | stabilised box inertia (**guess**) |
| 4 | 0xd0 | 0x004b5520 | 0x0058ded8 | 0x004b5990 (4) | none (+0x2c = 0) | **keyframed**; set at run time by 0x004aff70 (replaces entity +8, keeps the old one in +0xc0) |
| 5 | 0xc0 | 0x004afbc0 + vtable | 0x0058e040 | 0x004a3c40 (5) | none (+0x2c = 0) | **fixed**; entity +0xc bit 0 set |

Types 0/1 and 2/3 differ only in vtable slot 4 (integrator: 0x004b6580 / 0x004b7380, 0x004b4680 / 0x004b5120).
Jolt mapping: 0..3 -> Dynamic, 4 -> Kinematic, 5 -> Static. Mass = 1 / +0x2c (0 means infinite). Inertia: diagonal
1 / +0xe0.. for 2/3, isotropic 1 / +0x10 for 0/1, in the body frame whose origin is the local COM (+0x20).

## 4. Shapes

Common header: +0 vtable, u16 +4 size, u16 +6 references, **+8 user data** (game tags: -1, -2, -3, -5, or a filter
word `layer << 16 | part` for level skins; Body_Create copies it as two shorts into the 0x19 wrapper). Vtable slot 1
(+4) = **getType**, slot 2 (+8) = agent dispatch type (1 for every convex shape), slot 3 (+0xc) = getAabb(transform,
tolerance, out), slot 4 (+0x10) = bounding sphere (used by 0x004563d0, 0x004b00e0). Evidence: Body_Create 0x00457030
tests slot 1 == 0x10 and slot 2 == 1; getType bodies listed below.

| Type id | Shape | Size | Ctor | Vtable | Parameters | Evidence |
| ---: | --- | ---: | --- | --- | --- | --- |
| 2 | MOPP bv-tree shape | 0x18 | 0x00510f50 | 0x0056b384 (base 0x0056b358) | +0xc child **mesh shape** (reference), +0x10 = 2.0 (unknown), +0x14 MOPP code (0x00510cf0 / level cache; reference via 0x00510c10) | 0x00510f50..0x00510f85; getType 0x00510ef0; built in 0x00412450 |
| 3 | bounding-volume / pair shape (**strong guess** hkBvShape) | 0x14 | inline in 0x004527d0 and 0x004574e0 | 0x005b0af8 | +0xc bounding-volume shape, +0x10 phantom-callback shape (type 0x1b) | physics_02.cpp (0x004527d0 desc +0x78 branch); getType 0x004563c0 (3); getAabb shared with MOPP 0x00510e50 |
| 5 | sphere (convex radius base) | 0x10 | 0x0050f190 | 0x0056b28c | +0xc **radius** | 0x0050f1a3; getType 0x004a3c40. The manager's default shape (radius 0.1234, +8 = -5) is one |
| 5 | game offset sphere | 0x30 | 0x004a3c50 | 0x0058e760 | +0xc radius (core), +0x10 **centre offset** vec4 (0x004a3d50), +0x20 **outer radius** (0x004a3d70; the AABB uses +0x20 + centre) | physics_03.cpp 0x004a3c50 / 0x004a3cb0 |
| 8 | box | 0x20 | 0x005120f0 (halfExtents) | 0x0056b3e4 | +0xc convex radius (0), +0x10 **half extents** vec4 | 0x005120fd..0x0051211e; getType 0x00512130 |
| 0xb | convex vertices (hull) | 0xe0 | 0x00510aa0 (vertices, stride, count, planes) | 0x0056b324 | +0xc radius (game 0.1), +0x10 / +0x20 AABB centre / half extents (**guess**), +0x30 hkArray of 4-vertex blocks (0x30 bytes: x0..x3, y0..y3, z0..z3; count +0x34 = ceil(n / 4); last block padded with the last vertex; in-place capacity 3 at +0x40), +0xd0 hkArray of **plane equations** vec4 (count +0xd4) | 0x00510aa0, 0x005101e0 (packing 0x00510272..0x005102b6), 0x005103e8; built from qhull output in 0x00412450 |
| 0xd | multi-ray (**guess** hkMultiRayShape; physics.md called it multi-sphere) | 0xa0 | 0x00512cc0 (rays, count) | 0x0056b438 | +0x10 hkArray of 0x20-byte rays {start xyz, length (w), end xyz, w} (in-place capacity 4 at +0x20); length = |end - start| | 0x00512d40..0x00512da1; getType 0x00528c40; the game adds one downward ray (0, 0, -(r + 0.1)) under character spheres (0x004527d0) |
| 0x10 | list | 0x28 | 0x00511350 (children, count) | 0x0056b3b0 | +0xc hkArray<shape*> children (count +0x10, in-place capacity 4 at +0x18); numChildren = slot 8 (+0x20) returning +0x10 | 0x00511356..0x005113f4, getType 0x00511410, 0x00511330 |
| 0x11 | mesh (game subclass "skin mesh") | 0x50 | 0x0050f900, then the game's vtable | 0x0056b2f0 -> 0x005ba9d0 (getTriangle override 0x00412420) | +0x10 **scaling** vec4 (default 1,1,1,0; set by 0x0050f6f0), +0x20 radius (default 0.05, game 0.1), +0x24 hkArray of **subparts** (count +0x28, in-place capacity 1 at +0x30), each 0x1c bytes: +0 vertex base, +4 vertex stride (12 here), +8 vertex count, +0xc index base, byte +0x10 index type (0 = 16-bit, **guess**), +0x14 index stride (6 = three u16), +0x18 triangle count (addSubpart 0x0050f8a0 copies 7 dwords) | 0x0050f900..0x0050f934, 0x0050f8a0, 0x0050f6f0; game fill in 0x00412450 |
| 0x12 | game speed trap / velocity clamp wrapper | 0x28 | 0x004563d0 | 0x005b0ad8 | +0xc child, +0x10 25.0, +0x14 child bounding radius (+0.3 for characters), +0x18 0.016, +0x1c sweep, byte +0x20 mode, +0x24 0.8 (speed-trap agent 0x004a88a0 scales the body's velocity) | physics_02.cpp 0x004563d0, physics_04.cpp 0x004a85a0 / 0x004a88a0 |
| 0x13 | game filter-override wrapper | 0x18 | 0x004a4530 (eax = shape, ecx = child, edx = filter word; stack: object) | 0x0058e704 | +0xc child, +0x10 **filter info used for the child** (agents rewrite the collidable copy's +0xc with it), +0x14 referenced object (collision input, **guess**) | physics_03.cpp 0x004a4530, agents 0x004a3fd0..0x004a4470 (registered for 0x13 vs 1, 5..0xe, 0x12, 0x13) |
| 0x19 | game swept-convex wrapper (CCD helper, **guess**) | 0x30 | 0x004dfaa0 (eax = child, esi = memory; stack: entity, 20.0) | 0x0058b760 | +0xc radius (copied from child +0xc), +0x10 sweep vec4 (linear velocity x dt, updated by the agent), +0x20 = 20^2, +0x24 = 20.0, +0x28 child, +0x2c entity. Body_Create wraps every body whose root is convex (after collapsing a 1-child list) when cinfo +0x11 is set | 0x004dfaa0..0x004dfaf3, physics_04.cpp 0x004dfb70 / 0x004dfbf0 / 0x004df570 |
| 0x1a | transform shape | 0x50 | 0x00512ab0 (child) | 0x0056b414 | +0xc child (reference), +0x10 **hkTransform** (columns +0x10 / +0x20 / +0x30, translation +0x40); the game writes it with the 64-byte copy 0x00414240 | 0x00512ab6..0x00512b45, getType 0x00512940 |
| 0x1b | game phantom-callback shape (**strong guess** hkPhantomCallbackShape) | 0xc | 0x00411590 | 0x005baa20 | no geometry; slots +0x20 / +0x24 = overlap begin / end (0x004115f0 / 0x00411680) notify the owners | physics_01.cpp 0x00411590..0x00411680, getType 0x00414230 |

Correction to physics.md: **0x00511550 is not a shape**. Its only caller is RayCollector_Construct 0x00413810 (call at
0x00413815), it sets no reference count, and vtable 0x0056b3e0 has 0x0050f720 in slot 1: it is a ray-cast
input/collector object.

Root shapes the game builds (from the callers above): sphere / offset sphere / box / convex vertices / list, each
usually under a transform shape; level geometry = MOPP(mesh) or convex vertices (0x00412450); optional wrappers
0x12 / 0x13 / 0x19 / bv(3)+0x1b on top; character feet add a multi-ray (0xd) inside a list. Jolt mapping: sphere ->
SphereShape (offset sphere -> RotatedTranslated sphere of the outer radius), box -> BoxShape(half extents,
convexRadius 0), hull -> ConvexHullShape(vertices, radius), mesh -> MeshShape (scale applied to vertices, MOPP
ignored), list -> StaticCompoundShape, transform -> RotatedTranslatedShape, 0x12 / 0x13 / 0x19 -> their child
(plus a game contact listener / per-sub-shape filter / LinearCast motion quality), bv(3) + 0x1b -> sensor body,
multi-ray -> no shape (a ray query).

## 5. Constraints (ragdolls 0x00456570, pins 0x004594b0)

Common header (every ctor): +0 vtable, u16 +6 references, **+8 interface vtable 0x0058e2f0** (this address is what
entity +0x60 stores), **+0xc entity A**, **+0x10 entity B** (0x004b33e0 / 0x004b3460 add a reference and append
`constraint + 8` to each entity's +0x60 array), +0x14 island (0x004b1912), +0x18 0. addConstraint 0x004ac3e0
(world, constraint; ret 8) puts it in A's island (B's when A is fixed), merging islands (0x004ab080) and activating.
Pivots and axes are stored in **each body's local frame** (the game converts world values with 0x004de720 /
0x004de7e0 against motion +0x80 before construction).

| Ctor | Size | Vtable | Game use | Stored fields (constraint offset <- cinfo offset) | Evidence |
| --- | ---: | --- | --- | --- | --- |
| 0x004b0230 **ball-and-socket** (point-to-point) | 0x60 | 0x0058e164 | 0x004594b0: pins an object to the world's fixed body (world +0x60) at the point 10 units above it; stored in object +0x1c8 | +0x20 pivot in A (<- +0x10), +0x30 pivot in B (<- +0x20); +0x40..+0x54 zeroed (runtime) | 0x004b028e..0x004b02bb |
| 0x004af7c0 **hinge** | 0xa0 | 0x0058e300 | 0x00456570 type 2 with no limits and zero strength (cinfo 0x00459810) | +0x20 pivot A (<- +0x10), +0x30 pivot B (<- +0x20), +0x40 perpendicular 1 in A (<- +0x40), +0x50 perpendicular 2 in A = axis x perp1 (<- +0x50), +0x60 **hinge axis in B** (<- +0x30); the axis in A is implied by the two perpendiculars | 0x004af740..0x004af7b5; cinfo builder 0x00459810 |
| 0x004af020 **limited hinge** | 0xe0 | 0x0058e31c | 0x00456570 type 2 with limits or non-zero strength (cinfo 0x0045a1b0 defaults, 0x00459bb0) | +0x20 pivot A (<- +0x10), +0x30 pivot B (<- +0x20), +0x40 perp A (<- +0x50), +0x50 perp2 A (<- +0x60), +0x60 **axis A** (<- +0x30), +0x70 **axis B** (<- +0x40), +0x80 perp2 in B (<- +0x70), +0xc8 **min angle** (<- +0x80, default -pi), +0xcc **max angle** (<- +0x84, default pi), +0xd0 strength (<- +0x88; the game overwrites +0xd0 with (mass A + mass B) * 20, **guess**: max friction torque), +0xd4 = 0 (runtime) | 0x004aef30..0x004af00a |
| 0x004ae980 **ragdoll** (cone + twist) | 0xe0 | 0x0058e364 | 0x00456570 type 0 (cinfo 0x0045a3d0 defaults, 0x0045a1e0 frames) | +0x20 pivot A (<- +0x10), +0x30 pivot B (<- +0x20), +0x40 / +0x50 axis2 in A / B (<- +0x30 / +0x40; from descriptor +0x28), +0x60 / +0x70 axis in A / B (<- +0x50 / +0x60; from descriptor +0x34), +0x80 **cos(cone angle)** (descriptor +0x4c), +0x84 / +0x88 sin of descriptor +0x50 / +0x54, +0x8c / +0x90 sin of descriptor +0x40 / +0x44, +0x94 strength (mass A + mass B) * 20. Which axis is twist and which is plane, and which sin pair is plane vs twist limits, is a **guess** (defaults: +0x74/+0x78 = -/+0.45 pi, +0x7c/+0x80 = -/+ pi/6) | 0x004ae740..0x004ae806; Body_addConstraints 0x00456570 |

Type 1 descriptors ("LimitedPoint2Point") are not supported by the game (logged, 0x00456570). Jolt mapping:
ball-socket -> PointConstraint, hinge -> HingeConstraint (axis B, normal from perp A), limited hinge -> HingeConstraint
with limits and max friction torque, ragdoll -> SwingTwistConstraint (cone from +0x80, twist/plane limits from the
sines).

## Gaps (for the shadow mirror)

- World +0x20 array, the +0x100..+0x138 arrays and the solver parameters are not identified; phantoms (if Havok
  phantoms exist at all in this game) were not located: the game's triggers found so far are bodies with bv(3) +
  0x1b or 0x13 shapes.
- Angular velocity frame (world vs local) is a guess; confirm with a run (compare +0x50 against finite differences
  of +0x30).
- Ragdoll axis / limit naming and the limited-hinge +0xd0 meaning need Havok behaviour, not layout.
- Motion types 0/1/3 were not seen being used by the game; only 2, 4, 5 are confirmed callers.
- Shape +8 user data semantics for mesh / hull filter words (who reads it) were not traced.
