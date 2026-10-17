# Decals and corpses: limits, timers, hook points

Status: 2026-10-04, static reconnaissance only (no runtime check yet). Source: `work/decomp/`, `tools/recover.py
--asm`, constants read from `game.unpacked.exe`. Goal: docs/goals/remaster-basics.md item 3 (persistent decals
and bodies). Anything marked **guess** has not been confirmed.

## 1. Decals

### Owner: the decal manager (`g_game+0xa7c`, 0x28 bytes)

Created in 0x004011e0 (`FUN_004e07f0(heap 0x0063a210, 0x10, 0x28)` then ctor **0x00416050**). Fields:

| Off | Meaning | Evidence |
| ---: | --- | --- |
| +0x04 | u8 shadows enabled (copy of `m_options.m_enableShadows`, game+0x4be) | 0x00401ce0 |
| +0x05 | u8 decals enabled (copy of `m_options.m_enableDecals`, game+0x4bf, default 1) | 0x00401a80, 0x00401ce0 |
| +0x08 | free decal list head | 0x00417110 relinks to it; 0x004165c0 takes from it |
| +0x0c | active decal list, sorted by decal+0x0c (texture handle) | 0x00417260 insert loop |
| +0x10 / +0x14 | shadow free / active lists (not decals) | 0x00417b90, 0x00416290 |
| +0x18 | decals allocated (incremented in decal ctor) | 0x00417010 |
| +0x1c | decals active | 0x00417260 (+1), 0x00417110 (-1) |
| +0x20 | decals currently fading | 0x00418640, 0x00418580 (+1), 0x00417110 (-1) |
| +0x24 | float: fade duration given to the next forced fade, init 1.0 | 0x00416050, 0x00416440 |

`m_enableDecals` only gates **drawing**: 0x00416330 draws a decal if manager+5 != 0 or decal age < 0.
Decals are still created, aged and recycled with the option off.

### Pool: fixed, allocated per level

- **0x00416150** allocates **160** decals (`00416152 mov ebx, 0xa0`), each `0xf6c` = **3,948 bytes**
  (`FUN_004e07f0(0x0063a210, 0x10, 0xf6c)`), ctor **0x00417010** puts it on the free list. 160 x 3,948 = 631,680
  bytes (plus allocator headers).
- Called from **0x00416130** (level setup, caller 0x00403690), which first destroys every decal and shadow
  (**0x004160d0**, also called at level unload from 0x0044a670) and then allocates shadows (0x00416190, table at
  0x005b96f8: 10, 10, 4 objects, not decals).
- The size is hard-coded. Nothing in the fx bank sets it (docs/formats/big-fx.md has no pool or budget field for
  decals; 0x00416150 reads no data).
- Per-decal geometry (decal ctor 0x00417010): +0x3c = 0x80, +0x40 = 0x180 (**guess**: max 128 vertices and 384
  indices), buffers at +0x58 (0x800 bytes), +0x858 (0x400), +0xc58 (0x300).

Decal object fields: +0x08 colour (alpha in the top byte, written while fading, 0x00417180), +0x0c texture,
+0x50/+0x54 list links, +0xf58 u8 active, +0xf59 u8 kill-now, +0xf5c float age, +0xf60 float lifetime,
+0xf64 float fade remaining, +0xf68 float fade duration (0 = not fading). Vtable 0x005b9690 (slot +0x1c =
0x00418580, slot +0x20 = draw).

### Creation

- **0x004165c0** `Decals_Spawn(mgr, xform, size, texture, target, flag)`: takes the free-list head; if the free
  list is empty it kills (0x00417110, no fade) the active decal with the **largest age** and reuses it. Then
  **0x00417260** projects and clips it (0x004175c0) and links it into the active list.
- Callers: the fx interpreter 0x00420770 case 0x62 (= fx opcode **0x6a**, operands `w s` (jump tables 0x00423250 / 0x00423088 read from the image; opcode 0x6c goes to 0x0041d700): size, texture name);
  and 0x0040cb10 (the "Fx_bullseye" target marker, which sets age to **-1.0** so it never ages and always draws).
- Surfaces opt in through the object property `decal_receive` (default 1) -> object+0x3ec bit 0x8 (0x00451360).
  0x00416e90 clears bits 0x18 on objects whose model has animated parts (**guess** on what the test means).

### Receivers: placed objects (2026-10-14, static reading of src/renderer/render_r0.cpp, ported to engine/src/decals.rs)

- **0x00417260** Decal_Build makes a projector {decal, flags 0x40000008, 6 planes, planes, point}: the box of
  +-size along the placement's three (unnormalised) axes. With no target entity it gathers the world grid cells
  against the six planes (0x0048eab0) and walks them (0x0048eb60) with the landscape cell callback 0x00449010 and,
  unless the `single` byte is set, the object callback **0x00416f50**. The fx op 0x6a passes target 0 and single 0
  (render_r2b.cpp:1020), so fx decals land on objects; the bullseye marker (game_01.cpp:1861) passes its own target.
- **0x00416f50** Decals_ProjectOnEntity takes an object when (1) its +0x3ec shares a bit with 0x40000008: bit 8 is
  `decal_receive` (default on, Object_LoadBaseProps 0x00451360, game_03.cpp:1294); (2) it is not excluded:
  **0x00416e90** caches in +0x3ec bits 30/31 whether the current LOD has a mesh with +8 non-zero (excluded; that
  field's meaning is a **guess**: skinned / deforming meshes); (3) **0x00418610** finds no animating object in it or
  its parents (0x004531e0); (4) its bounding sphere (vt+0x90) is not wholly behind any of the six planes.
- **0x00416bf0** DecalGeometry_AddEntity takes the planes to model space (M^T p, M = vt+0x7c, the world matrix),
  clips every submesh of the object's current-LOD **render** model (not its collision model) whose material lacks
  bit 0x10 of byte +0x34 (the blended materials, render_r4.cpp:534) with **0x00416930**, and moves the new
  vertices back to world space. Landscape and objects share one budget: **128 vertices / 384 indices** per decal
  (constructor 0x00417010); running out ends the build. Up to **4** receivers are recorded (+0x2c, count +0x24);
  later objects still add geometry but are not recorded.
- No normal test anywhere: a box at a wall's foot also covers the ground and both faces of thin parts.
- A receiver's removal (0x004547b0 -> 0x00416620) fades its recorded decals over 0.1 s (0x00418580).
- Which bullets make decals is data: in lev_1_m1's fx bank only 30 fx carry op 0x6a. Roman's rifle and machine gun
  (fx_end_material fx_imp_flemming) leave a decal only on snow (fx_imp_flemming_snow, 0.5); on stone / wood houses
  they leave none. Sticky bombs (fx_imp_sticky_<material>, 10.0), stig_mgun (fx_imp_stig_<material>, 1.5) and
  explosions (fx_exp_generic, fx_explosion_powder_keg, ...) do.

Remaster (engine/src/decals.rs): receivers are the placed, visible, non-background objects with decal_receive != 0,
no skeleton and no rigid anim (the windows and husks too); their render triangles are moved to game space once at
load and bucketed by 32-unit x/y cells (lev_1_m1: 237 objects, 125,781 triangles). A decal clips the landscape cells
first, then the object triangles in the cells its box covers (the original interleaves cell by cell: order differs
when the budget runs out). A destroyed object stops receiving, its husk becomes a new receiver, and its decals fade
in 0.1 s. Test: work/remaster/play/decals_obj.json (from states/after_pub: a sticky bomb at house 85's door gives a
decal with 1 receiver; `destroy 85` fades it). Not compared with an original capture yet.

The **pressure fade** (0x00416440) is not ported: it never runs in the original (see "Measured" below).

### Removal (all paths)

1. **Pool pressure, proactive** - **0x00416440** (per frame, from 0x00403050, arg = frame time):
   `free+fading = mgr+0x18 - mgr+0x1c + mgr+0x20`; `0041644f cmp eax, 0x20 / jae`. If it is **< 32**, the oldest
   active non-fading decal starts fading (duration mgr+0x24), and mgr+0x24 moves toward **0.3 s** by 10% per frame
   (0x005bccd4 = 0.3, 0x005bcbfc = 0.1). Otherwise mgr+0x24 relaxes toward **2.0 s** by 5% per frame (0x005bcb80,
   0x005bcc34). Net effect: about **128** visible decals at steady state (160 - 32), oldest first. The easing is per
   frame, not per second, so it is frame-rate dependent.
2. **Pool empty** - 0x004165c0 kills the oldest at once (see above).
3. **Lifetime** - 0x00417180 starts a fade when `age >= +0xf60`, but +0xf60 is only ever written as 0 (the
   constructor, `00417072 mov [edx+0xf60], ebx`; no other `0xf60` displacement exists in `.text`), so this path is
   dead in retail: there is **no time-based expiry**.
4. **Receiver destroyed** - 0x00416620 (from 0x004547b0) calls slot +0x1c = **0x00418580** on decals attached to an
   object, which fades them in 0.1 s.
5. **Level change** - 0x004160d0 destroys all.
6. No distance cull was found in the decal code (**guess**: none exists).

Fade itself: 0x00417180 decrements +0xf64 by dt, writes alpha, and at <= 0 calls **0x00417110** (back to free list).

### Save games

The decal manager is a save object (0x0048cb30 case 0x80000008; 0x0048c620 registers `g_game->field_a7c`).
Case 0x80000009 calls **0x004165a0**, which moves one free decal to the active list for each saved decal and returns
0 when the free list is empty. **Guess**: a save holding more decals than the pool loads with decals missing, or
fails, under a build whose pool is smaller.

## 2. Corpses (AI characters)

Character classes share vtable slots +0xd4 = **0x004319e0** (die), +0x17c = **0x00431310** (go ragdoll),
+0x100 = 0x0042eb60 (set alpha); see vtables 0x005a1b60 and 0x005b2958.

- **Death** - 0x004319e0 counts the kill (g_game+0xb1c), then picks either a ragdoll (via +0x17c) or a death
  animation (states 0x195 / 0x191, and `fx_bodyfall` 0x0043a8d0 on landing).
- **Ragdoll limit** - in 0x00431310 the code counts characters in the list `*(g_game+0xa84)+0xe7c` that are
  ragdolling (0x00431000: state == 400 or byte +0x421) and allows a ragdoll only if `count <= 3`
  (`00431621 cmp dword [esp+0x20], 3`): **at most 4 Havok ragdolls at once**. Otherwise the death is animated.
  A ragdoll needs `+0x1bc != 0` (**guess**: has a physics skeleton). `hit_ragdoll` (default 50.0, +0xa0c,
  0x004247f0) is the damage that triggers it.
- **Corpse timer** - two floats: +0x9f0 time left, +0x9f4 fade length.
  - Ragdoll: `0043169d mov [ebp+0x9f0], 5.0` and `004316a7 mov [ebp+0x9f4], 0.5`, state 400.
  - Animated death and the other paths (0x004319e0, 0x0042e1c0, 0x00427100, 0x0043ee90): 0.5 / 0.5.
- **0x0042eba0** (125 bytes, only caller 0x0043ee90 in states 400 and 0x191): time -= g_frameDt; when
  `time <= fade` it sets the alpha to `1 - (fade - time)/fade` through +0x100; at 0 it calls **0x00455640**
  (remove: vtable +0xdc, then 0x00455660 plays the object's `fx_death` and spawns its `husk` object).
  The result: a ragdoll body lasts 5 s and then fades over 0.5 s. An animated body fades over 0.5 s once it has
  landed.
- `husk` (0x00451360, also `husk1`..`husk9`, chosen at random) is spawned at removal. **Guess**: a level could give
  characters a static corpse prop this way. Which instances set it is not checked.
- `death_timeout` (0x0044fd20, +0x4bc/+0x4c0, counted down in 0x004505a0) belongs to the generic damageable
  object and calls die (+0xd4) when it reaches 0. It is not a corpse timer.
- No global corpse count was found. The only global cap is the 4-ragdoll limit. Whether troop generators
  (`alarm_generator_max_troops`, 0x0044c170) count dead objects is **not checked**.

### Measured and ported (2026-10-17, agent-corpses)

- Oracle (work/oracle/courtyard and the `corpses` scenario, lev_1_m1): a ragdolled grunt (state 400 at death) leaves
  the AI list 5.00 s later; an animated death (state 0x191, entered directly or after the launch state 0x195) 0.5 s after
  its death anim's length (rests 0.47..0.53 s for grunt_die_1 0.9, grunt_bow_die_3 0.97 / grunt_blown_up_die 0.97,
  grunt_barricade_death 0.63). So the body lies 5 s in all (the fade is inside the 5 s, not after it), and the 0x191
  countdown waits for the anim (slot flag 2, GenericAI_Update's SP+0x10).
- Struct atlas (tools/struct_atlas.py): +0x9f0 is m_deathTimer, +0x421 m_hkSimulateEnable (Havok simulating); +0x1bc
  reads as a WeaponObject pointer (confidence 0.6), which puts the "has a physics skeleton" guess above in doubt (open).
- Ported: engine/src/corpses.rs (docs/remaster-engine.md "Corpses").

## 3. Hook points and risks

| Want | Change | Size / state |
| --- | --- | --- |
| Bigger decal pool | 0x00416150: count 0xa0 -> N (imm at 0x00416153) | 53 B, small; recover or byte-patch |
| No proactive decal removal | 0x00416440: threshold 0x20 (imm at 0x00416451) -> 0, or hook the whole function | 230 B |
| Pool sized by memory | Hook 0x00416150: N = budget / ~3,960 B | 53 B |
| Decals survive receiver death | 0x00418580 (vtable slot) -> no-op | 70 B |
| Corpses persist | Hook **0x0042eba0**: skip the countdown, or keep a FIFO of N corpses and remove the oldest through 0x00455640 | 125 B, best hook |
| Longer ragdoll only | imm at 0x0043169d (5.0) | byte-patch |
| More ragdolls | 0x00431621 `cmp ..., 3` | byte-patch inside a 1,034 B function |

Risks:
- **Pool exhaustion still recycles.** With a bigger pool and no proactive fade, 0x004165c0 still kills the oldest
  decal (without a fade) when the pool is full. "No removal" in practice means a pool sized by a memory budget.
- **Heap**: decals come from heap 0x0063a210 (`FUN_004e07f0`). Whether that is a fixed-size arena is unknown
  (**open question**), so a large N could fail allocation at level load.
- **Draw cost**: 0x00416330 issues one draw per active decal (vtable +0x20), sorted by texture. Thousands of decals
  could cost frame time; batching would need the renderer work.
- **Ragdoll cap**: persistent ragdolls stay in state 400, so after 4 every later death is animated. A persistent
  corpse should leave state 400 and disable physics (0x004577b0(obj, 0)) to free the Havok body and the slot.
- **AI update**: corpses keep running 0x0043ee90 every frame, and a 400-state body keeps its Havok rigid bodies.
  Cap them by count.
- **Saves**: decals and the extra corpse objects are saved. A save made with the enhancement on could hold more
  than the stock pool. Clamp on load, or document that the switch must stay on for such saves.
- **Frame rate**: the decal fade easing (0x00416440) is per frame. The frame-rate enhancement will change decal
  turnover unless that is fixed too.
- **Per-level budgets**: no per-level decal budget was found in the fx bank. The only per-level step is the
  re-allocation in 0x00416130.

## Open questions

- Is heap 0x0063a210 bounded? What is its capacity?
- Do troop generators count corpses? (0x0044c170 and its runtime.)
- What `+0x1bc` (ragdoll capable) and `+0x421` mean exactly.
- Which instances set `husk`.

## Measured (2026-10-04, after recovery of 0x00416150 / 0x00416440)

- The manager's +0x18 counter is twice the pool size, perhaps because the front end and the level each allocate a pool
  (guess). With `[persistence] decal_pool=40` it reads 80, while active decals cap at 40.
- So `allocated - active + fading` never drops below 32, and the pressure fade never runs. The visible limit is the
  pool size (160 in the original). When the pool is full, the oldest decal is recycled at creation.
- Corpse removal: Corpse_CountDown with `[persistence] bodies_keep=1` keeps the body (work/shots/bodies.png).
