# FX script VM: semantics reference

Status: 2026-10-13, static reading of the recovered C++ (no new oracle runs); the engine port's state and the
opcodes added on 2026-10-15 are in section 7. Purpose: port the fx interpreter
0x00420770 to the Rust engine so explosions behave like the original. Scope: the opcodes used by
`fx_explosion_powder_keg` and the fx it starts (`fx_keg_trail`, `fx_powder_keg_fire`, `fx_sticky_blast_ring_snowk`,
`fx_sticky_flashk`), the fx object, the particle system and the powder keg's death (area damage, chain reaction).
Script format and operand grammar: `docs/formats/big-fx.md`. The full opcode table (every opcode, its callee) is the
header comment of `archive/src/renderer/render_r2b.cpp`.

Conventions. `r(a,b)` = Rand_FloatRange 0x0046e2c0 = `(b - a) * rand() * (1/32768) + a` (render_r2b.cpp:173,
constant [0x005bcbc0] = 3.0517578e-05), so it lies in [a, b); `rand()` is 0x005391cd (guess: MSVC CRT rand, 15 bits).
Times are seconds of the frame time [0x00604a90] (guess for the unit: every timer subtracts it directly). Distances
are world units. "obj" = the fx object (class `fx`, vtable 0x005a52d8, docs/subsystems/weather.md); field offsets are
obj offsets unless marked `e+` (particle emitter, 0x194 bytes), `p+` (particle, 0x6c bytes) or `fx+` (fx instance,
0x128 bytes). Constants were read from `game.unpacked.exe` .rdata (values given inline).

## 1. Execution model

| Topic | Behaviour | Evidence |
| --- | --- | --- |
| Who runs a script | Fx_Step 0x00423360 per fx instance: state fx+0x10: 0 run; 1 paused (fx+8 -= dt; at <= 0 state 0 and run in the same call); 3/4 idle. Run = 0x00420770. Result 0 (paused/waiting): fx+8 -= fx+0xc, fx+0xc = 0. Result 1 (ended): state 3 if fx+0x14 has 0x10 without 0x100, else Fx_Release 0x0041d030 | render_r3.cpp:125 |
| Instruction pointer | fx+4, saved back after each run; the run executes instructions until 0x04, 0x12 or 0x15 | render_r2b.cpp:1203 |
| Locals reset per run | current object `cur`, the push stack, list1 and list2 are locals of 0x00420770: they are 0 at every resume after a pause. Objects still pushed at the end are popped (nodes freed, objects untouched) | render_r2b.cpp:211-219, 1204 |
| `cur` vs `top` | 0x05 sets `cur`. Every setter tests `cur != 0` but writes to `top` = the top node of the push stack (node = {next, object}; object = node+4). So an object must be created (0x05) **and pushed** (0x06) before setters reach it. A setter with `cur` set but empty stack gets null and prints "no push"/"no fxpush" (log is compiled out, 0x00407ff0 is `ret`) | render_r2b.cpp:251-254; each helper's `if (!push)` in render_r2a.cpp |
| Push / pop | 0x06: if cur, push cur (FxStack_Push 0x00423430; logs "Pushing object w/o a previous Pop" when the stack was not empty but pushes anyway). 0x07: if cur, 0x004540a0(top object) (+0x2d4 = -1.0), pop the tail node, `cur = 0` | render_r2b.cpp:285-302; render_r3.cpp:176 |
| Operands always consumed | Operands are read and `r()` is called even when `cur == 0`; only the helper call is skipped. The RNG sequence therefore does not depend on `cur` | every case in render_r2b.cpp |
| Unknown opcode | logged, execution continues at the next word (no operands skipped) | render_r2b.cpp:1190 |

### Name lists (0x104) and random choice (0x39)

| Item | Behaviour | Evidence |
| --- | --- | --- |
| 0x104 `L` | Stores a pointer to the list (u32 count + 32-byte-padded names) in list1 if list1 is 0, else in list2 (a third overwrites list2) | render_r2b.cpp:1177 |
| Consumers | 0x0f / 0x2f use list1 and list2, then clear both; 0x39 uses list1 and clears it (even with no cur) | render_r2b.cpp:368-379, 765-777 |
| FxNameList_PickRandom 0x0041cba0 | null list: 0, no rand. Else index `(rand() * count) >> 15`; returns that name, or null when it is `"null"` (_stricmp with 0x005bba94) | render_r2a.cpp:354 |
| 0x39 `L w` | Picks a name from its own inline list with `(rand() * count) >> 15` (always, one rand), then reads `w`; if cur: point = PickRandom(list1), Fx_PlaySound(top, name, mgr, point, positional = 1, 0, looping = w) | render_r2b.cpp:765; audio_01.cpp:247 |

### Pause and wait

| Opcode | Behaviour | Evidence |
| --- | --- | --- |
| 0x12 `min max` | t = r(min,max) always; Fx_Pause 0x0041dea0: only when the push stack is empty: fx+8 = t, state fx+0x10 = 1. The run stops in every case. With a non-empty stack the pause is refused (no state change), so the script resumes next frame. Resume happens on the first frame where fx+8 - dt <= 0; the overshoot is not carried (the next pause overwrites fx+8). A pause of 0.01 s is "one frame" at any frame rate up to 100 fps | render_r2b.cpp:406; render_r2a.cpp:963; render_r3.cpp:133-140 |
| 0x15 | Fx_Wait 0x0041dee0: with an empty stack, state = 4 (idle until something else changes it); stops the run | render_r2a.cpp:975 |
| 0x04 | stop + finished: Fx_Step then releases the instance (or state 3) | render_r2b.cpp:259; render_r3.cpp:155 |

## 2. Opcode table

`w` = 32-bit word as the dump shows it (float or int). "Reader" = where the stored field takes effect.

| Op | Operands (type, unit) | What it does | Field written | Reader / effect | Evidence |
| --- | --- | --- | --- | --- | --- |
| 0x04 | none | end of script | locals | Fx_Step frees the instance | r2b.cpp:259 |
| 0x05 | s: object template | Fx_CreateObject 0x004234d0: factory 0x0045f020 class `"fx"` (0x005bbb04), template = name; placement by fx flags (see section 3); obj+0x468 = fx+0xc - dt. Result becomes `cur` (not pushed) | new object | section 3 | r2b.cpp:264; game/fx.cpp:111 |
| 0x06 | none | push cur | push stack | see section 1 | r2b.cpp:285 |
| 0x07 | none | pop; cur = 0 | push stack | | r2b.cpp:295 |
| 0x08 | min, max (f32 s) | lifespan r(min,max) (max < min: max = min, logged). Fx_SetLifespan 0x0041d110 | +0x460 \|= 2; +0x470 age = 0; +0x46c lifespan | obj update: age += dt_obj; at age >= lifespan the object ends (0x00419290: optional on-death fx, then vt+0xdc destroy) **before** its emitters run that frame | r2b.cpp:304; r2a.cpp:604; render_r1.cpp:1071 |
| 0x09 | s0, s1, e0, e1 (f32 s) | fade in: start = r(s0,s1), end = r(e0,e1). Fx_SetFadeIn 0x0041dba0, then vt+0x100(0) (alpha 0, guess) | +0x474 = start; +0x478 = 1/(end - start); +0x460 \|= 4\|8 | update: while bit 8: +0x474 -= dt_obj (delay); when <= 0, bit 8 cleared and +0x474 = dt_obj - +0x474. Then +0x474 += dt_obj, alpha = +0x474 * +0x478 via vt+0x100; at alpha >= 1 bit 4 cleared, alpha = 0.999. While bit 4 is set, 0x0a's fade-out is **not** evaluated (else-if) | r2b.cpp:323; r2a.cpp:842; render_r1.cpp:1080-1102 |
| 0x0a | min, max (f32 s) | fade-out **start age** r(min,max) (not a duration). Fx_SetFadeOut 0x0041dc30 | +0x47c; +0x460 \|= 0x10 | update: age > +0x47c: alpha = 1 - (age - start)/(lifespan - start); else 0.999. vt+0x100 (object alpha, guess) | r2b.cpp:333; r2a.cpp:860; render_r1.cpp:1103-1113 |
| 0x0c | f0, f1, t0, t1 (f32 scale) | scale linear from r(f0,f1) to r(t0,t1) over the lifespan. Fx_SetScaleLinear 0x0041d170: uniform scale = from now (Object_SetScale 0x004535d0) | +0x480 from, +0x484 to; flag 0x20 when from != to | update: s = (to-from)/lifespan*age + from (Fx_Lerp 0x0041a2d0), uniform. No lifespan: age stays 0, scale stays `from` | r2b.cpp:349; r2a.cpp:621; render_r1.cpp:950, 1116 |
| 0x0e | f0, f1, t0, t1 | scale "log" (eased-out): same fields, flag 0x80. 0x0d (exp) is the ease-in variant (flag 0x40, rate +0x488 = sqrt(to-from)) | +0x480/+0x484, flag 0x80 | update: u = age/lifespan, s = from + (to-from)*u*(2-u) (0x0041a310: `(1-u)*p + p + from`, p = (to-from)*u). Exp: s = from + (rate*u)^2 | r2a.cpp:621; render_r1.cpp:957-971 |
| 0x0f | s: fx name | if cur: Fx_Attach 0x0041dd50(name, top, list1, list2, detached = 0): child fx instance on `top` object, run **immediately** (nested Fx_Step). Child objects are placed at the parent's attach point then detached (they do not follow). Clears list1/list2 | new fx instance: fx+0x14 \|= 2, +0x64/+0x68 attach points picked from list1/list2 (rand each, "null" = origin), +0x6c = parent object (refcount +0x28) | section 3 | r2b.cpp:368; r2a.cpp:878-932 |
| 0x2f | s: fx name | as 0x0f with detached = 1: fx+0x14 \|= 1, **not** run now (next Fx_Step pass, guess). Its objects stay attached to the parent (Object_Attach, obj+0x424 \|= 0x20000) and follow it | as above, flag 1 | game/fx.cpp:132-140 | r2b.cpp:368 |
| 0x104 | L | name list (section 1) | list1/list2 | | r2b.cpp:1177 |
| 0x10 | min, max (f32 impulse) | r(min,max) always; if cur: reference = FxNameList_PickRandom(list1) (one rand when list1 is set; "null" = none), Fx_SetImpulse 0x0041e0c0 (shared body Fx_ApplyForce with 0x1e "drift" 0x0041dfb0): no push or strength 0: nothing; wakes the object (0x004577b0); no reference: force = Object_GetNegColumn1 0x00453150 (-column 1 of the object matrix) x strength, Body_ApplyImpulse 0x00458440 at the centre (point null); with a reference: relative matrix 0x00453c50(obj, reference), force = -its column 1 x strength at its translation. Clears list1 (even without cur) | (Havok body) | does **not** make the body dynamic itself; the scripts give 0x11 right after it (fx_debri_bolt: 0x10 50..100 then 0x11 1.0). Engine: impulse kept on the object until it has a body (0x11 / 0x64, or at its next update with gravity 1.0, guess), scaled as 0x64; attach-point references use the object axis and centre | r2b.cpp:380; r2a.cpp:1010-1070; engine_01.cpp:981 |
| 0x11 | min, max (f32 gravity scale) | r(min,max); if cur: Fx_SetGravity 0x0041de10: nonzero -> +0x48c; exactly 1.0 -> unlinked from the gravity-scale list Game+0xa84 +0xf1c (ObjectList_Unlink 0x00454750; the recovered comment says "registers", the callee is the unlink). Then Object_setPhysicsEnabled(obj, 0) = body made dynamic (not keyframed) | +0x48c | Physics_ApplyGravityScale 0x00403c70 (per substep): objects in +0xf1c get force = g_world * (scale - 1) * mass, read via vt+0x8c (0x00419390 returns +0x48c). So 1.0 = normal gravity; FxObject_Load clears +0x48c to 0 (weightless if listed) | r2b.cpp:398; r2a.cpp:947; physics_01.cpp:107; render_r6.cpp:809, 923 |
| 0x12 | min, max (f32 s) | pause (section 1) | fx+8, fx+0x10 | Fx_Step | r2b.cpp:406 |
| 0x14 | none | if cur: Fx_SetLogObject 0x0041df20: fx+0x14 \|= 0x20, object list fx+0x124 (made on first use), the pushed object added (0x004546d0) | fx+0x124 | Fx_Release 0x0041d030 (instance ended by Fx_Step, or released by its owner) with flag 0x20: FxInstance_DestroyEmitters 0x0041cfb0 ends every listed object (0x00419290, al = 1). Used by looping muzzle flashes / slides that end in 0x15 (fx_muzzle_flash_flemming, fx_roman_slide). Engine: destroyed when the instance ends or, while waiting, when its parent entity is gone | r2b.cpp:414; r2a.cpp:988, 543, 578 |
| 0x16 | none | if cur: Fx_SetInvisible 0x0041e280 = obj vt+0xf4(0): the mesh is not drawn; emitters, physics and lifespan still run | (object visibility) | weather.md; r2a.cpp:1102 |
| 0x1c | min, max (f32 units) | r(); if cur: Fx_SetHeightOffset 0x0041f970: position (vt+0x6c) z += offset, set once (0x00457870) | position | one-off move | r2b.cpp:672; r2a.cpp:1955 |
| 0x20 | a, b (f32) | particle "blur" = stretch factor range | e+0x1c, e+0x20 (default 1, 1) | p+0x34 = r(a,b); in the draw > 0 stretches the quad along the per-frame motion (section 4) | r2a.cpp:1454; missing_02.cpp:93 |
| 0x21 | a, b (f32 units) | particle size range | e+0x14, e+0x18 (default 0.2, 0.3) | p+0x40 = r(a,b) = quad **half**-extent (corners centre +-right*size +-up*size) | r2a.cpp:1459; render_r3.cpp:804 |
| 0x22 | a, b (f32 units/s) | particle growth range ("scale") | e+0x34, e+0x38 (default 0, 0) | p+0x44 = r(a,b); if a > b: p+0x44 = -(size / life) (shrinks to 0 at death). Draw: size += t * p+0x44, unclamped: a negative growth that overshoots (fx_smoking_grunt -2..-1 on size 1..2) draws the quad turned half a turn and growing again until the ramp fades it | r2a.cpp:1464; missing_02.cpp:131-134 |
| 0x23 | a, b (f32 1/s) | particle damping (drag) range; a clamped below to 1e-6 ([0x005bcc68]) | e+0x3c, e+0x40 (default 1e-6) | p+0x3c = r(a,b); each step v -= v * drag * min(t, 1/drag) | r2a.cpp:1683; render_r3.cpp:853-861 |
| 0x24 | a, b (f32 units/s) | particle speed range | e+0x44, e+0x48 (default 8, 30) | initial speed r(a,b) along a random cone direction | r2a.cpp:1474; render_r3.cpp:967 |
| 0x25 | a, b (f32 s) | particle lifespan range | e+0x2c, e+0x30 (default 4, 7) | p+8 = r(a,b); also the time base of 0x49 | r2a.cpp:1469 |
| 0x26 | a, b (f32 degrees) | spread cone angle range | e+0x4c, e+0x50 (default 36, 36) | cone = r(a,b) * pi/180; direction = axis tilted by r(0,cone) about a perpendicular, then spun by r(0, 2pi) about the axis (not uniform over the solid angle) | r2a.cpp:1484; render_r3.cpp:967 |
| 0x27 | pool (int 0/1/2), i0, i1 (f32 s) | if cur: Fx_EmitParticle 0x0041e760(flags 0x20): new particle emitter slot (max 10 per object) on `top`, following the object. interval = r(i0,i1) (rand drawn even without cur) | e+0 \|= 0x20\|8; e+4 = pool; e+8 = interval; direction e+0x80 = Object_GetNegColumn1 (-column 1 of the object matrix); previous position e+0x74 = object position; e+0xec = fx+0x6c object (ref). Defaults listed in this table's "default" values; e+0x10 = -1 (no emitter expiry); colour e+0x64..+0x6c = 255, alpha e+0x70 = 0 | section 4 | r2b.cpp:496; r2a.cpp:1338 |
| 0x2b | u0, u1, v0, v1 (f32 0..1) | particle texture rectangle in the bank texture (helper comment calls them u0 v0 u1 v1; the draw proves u0 u1 v0 v1) | e+0x54..+0x60 -> p+0x5c..+0x68 | corner (-r,-u) = (u0, v0); (-r,+u) = (u0, v1); (+r,+u) = (u1, v1); (+r,-u) = (u1, v0) | r2a.cpp:1704; render_r3.cpp:917-940 |
| 0x2e | none | if cur: Fx_SetKite(0x400): billboard | +0x460 \|= 0x400 | update 0x00419a60: obj matrix = camera matrix (0x004e2210 of 0x0040fa50) with the object's own position, each frame (full camera facing). fxo_flash / fxo_flame_anim are single quads, so they become camera cards | r2b.cpp:737; r2a.cpp:2106; render_r1.cpp:661 |
| 0x31 | a0,a1, b0,b1, c0,c1 (f32 degrees) | a1 = r(a0,a1), a2 = r(b0,b1), a3 = r(c0,c1) always; if cur: Fx_SetInitialRotation 0x0041f850: R = Matrix_FromEulerDeg 0x004e3db0(A = a1, B = a3, C = a2), rows (cC cB, -sA cC sB - sC cA, sC sA - cC sB cA), (cB sC, cC cA - sC sB sA, -sC sB cA - cC sA), (sB, cB sA, cB cA) = Rz(C) Ry(-B) Rx(A) (column vectors); X = Matrix_Multiply 0x004e2910(A = object matrix vt+0x70, B = R) = W R (turns the object in its own frame, position kept); Object_SetTransform 0x00457990(X) | object transform | one-off. fx_cyc_shell / fx_hawk_shell (0,0,0,0,0,180): 180 deg about the object y axis; fx_breath_f: 180 about z | r2b.cpp:617; r2a.cpp:1917; engine_01.cpp:3341, 4150 |
| 0x32 | none | if cur: 0x0041faf0 -> Fx_SetHitGround 0x0041f9d0: +0x460 \|= 0x10000; collision mask vt+0xf8(0x20000, 0), or (0x70001, 0) when 0x3c (0x0041fa60, flag 0x20000) was also given; wakes the object (0x004577b0) | +0x460, collision mask | guess: the object collides with the ground (the mask bits are not decoded). Engine: stored only (its fx bodies collide with everything) | r2b.cpp:688; r2a.cpp:1967-2008 |
| 0x39 | L, w (int looping) | random sound at the object (section 1) | obj+0x558 = attach point | Fx_PlaySound 0x00420080: positional, audio +0xc4 (name, object, ...) | r2b.cpp:765; audio_01.cpp:247 |
| 0x3b | w0 time (s), w1 radius (units), w2 amplitude | if cur: FxScreenShake_Set 0x004201f0 | +0x4d4 = w0, +0x4dc = w1, +0x4d8 = w2; +0x460 \|= 0x8000 | next obj update: if camera position (0x0040f9e0) within radius (squared compare), camera shake 0x0040fac0(amplitude = w2, time = w0); bit cleared (one-shot). Camera 0x0040e300 per frame: target z += amp, amp *= -0.75, timer -= dt (per-frame decay: frame-rate dependent) | r2b.cpp:779; game_02.cpp:864; render_r1.cpp:1451; game_01.cpp:2392, 2607 |
| 0x3f | s name, w0 looping, w1 player only, w2 min, w3 max (f32 units) | always (top tested by the callee): Fx_PlayForce 0x0041ff60(ebx = name, esi = top, edi = w0; a1 = w2, a2 = w3, a3 = w1): w2 and w3 <= 0 and w0 = 0: one-shot rumble Rumble_Start 0x004e5020 (Game+0x30, the active device), no object needed. Else without top: nothing (compiled-out print). With w1: only when the object or an ancestor is the player (0x0045a480). Then +0x460 \|= 0x80000, +0x4e4 = min, +0x4e8 = max, +0x4e0 = Rumble_Start(looping = w0), gain 0 | +0x4e0..+0x4e8 | object update: gain 1 within min (squared distance compared with min), 1 when max = 0, else (d - min)/(max - min) (as the original: rises with distance), 0 beyond max; the rumble stops with the object (FxObject_Remove 0x004e5130). Engine: queued and logged with FXLOG (rumble samples not exported; input.rs drives its own rumble) | r2b.cpp:750; input_01.cpp:639; render_r1.cpp:1467; render_r6.cpp:796 |
| 0x40 | layer (int 1..4), angle (deg), speed, duration (s), ramp in (s), ramp out (s) | if cur: Fx_SetUvScroll 0x00420670: texture-stage uv scroll | uv block +0x514: layer record (0x20 B) +4/+8 direction from the sine table (index = int(angle*[0x005bcba4] + [0x005bce68]) & 0x3fff), +0xc speed, +0x10 ramp in, +0x18 duration, +0x14 ramp out (only when duration > 0); +0x460 \|= 0x100000 << (layer-1) | TexAnim_Step 0x00419d30: offset += speed * dir * fade * dt; fade = 0 after duration, t/in during ramp in, 1 - (t-start)/start in the ramp out (start = duration - out; divides by start, not out: possible original bug, see render_r1.cpp:749) | r2b.cpp:790; r2a.cpp:2273; render_r1.cpp:776 |
| 0x46 | x, y, z (f32 degrees) | if cur: particle emitter axis 0x0041eef0: M = Rx*Ry*Rz (x87 fsin/fcos, angle * (1/180) * pi), emitter direction e+0x80 rotated by M **now** (world axes), flag 0x200 = direction fixed from now on (does not follow the object's rotation). 0x47 = relative variant (flag 0x400, re-applied each step to the object direction) | e+0x8c = M; e+0x80; e+0 \|= 0x200 | spawn step uses e+0x80 as the cone axis. With an upright object (-col1 = (0,-1,0)), (-90,0,0) gives +z: particles go up. M applies to column vectors (Mat_TransformPoints 0x004e36a0), so Rz turns first: (-90,0,25) (footstep snow, explosions) leans the axis 25 deg off vertical (the engine applied Rx first before 2026-10-22, which kept it vertical). The flags OR: 0x46 then 0x47 stays fixed | r2a.cpp:1611; render_r1.cpp:870; engine_01.cpp:3341, 3943 |
| 0x48 | a, b (f32) | particle gravity range | e+0x24, e+0x28 (default 0) | p+0x38 = r(a,b) * -64.348099 ([0x005bcd64], = 2 * 32.174: guess, feet/s^2 units); step vz += t * p+0x38. Positive = falls, negative = rises | r2a.cpp:1479; missing_02.cpp:137 |
| 0x49 | in start, in end, out start (f32 s, particle age) | if cur: Fx_SetParticleFadeInOut 0x0041eb70: builds a shared 12-entry colour ramp (rgb always 1) on the current emitter | e+0x190 ramp | particles get p+0x4c = 10, p+0x50 = 10/life, ramp ref; section 4 gives the alpha table and the lookup | r2a.cpp:1510; render_r3.cpp:804 |
| 0x4a | r, g, b (f32 0..255) | if cur: Fx_SetLight 0x0041da90: no push: nothing; D3DLIGHT9 at +0x560 cleared, Type 1 (point), Diffuse = rgb x 1/255 ([0x005bcc18]), alpha 1, Range 1e6, Falloff 0, Attenuation0 1, Attenuation1 0.1; +0x5c8 = FxNameList_PickRandom(list1) (one rand when list1 is set), +0x5cc = -1; +0x460 \|= 0x1000000. Clears list1 (even without cur) | +0x560..+0x5cc | object update: each frame the light is moved to the attach point (0x00453c50) and re-added (0x0048a520 / 0x0048a4b0, light manager Game+0xa78); removed with the object. 0x58 / 0x5a change it. Engine: stored only (the game materials take no dynamic lights) | r2b.cpp:953; r2a.cpp:812; render_r1.cpp:1526 |
| 0x55 | s texture | if cur: Fx_SetStreamerTexture 0x0041f5a0: current streamer (0x0041f560, the last of the object's leading slots); texture from the bank Game+0xa78 (0x004881c0), Streamer_SetTexture 0x004952b0 (+0x14, render flags \|= 0xc). A missing texture passes the print's EAX | streamer +0x14 | Streamer_Draw 0x00495380 draws the strip with it (texture x vertex colour; engine: one mesh per texture and blend, repeat addressing) | r2b.cpp:905; r2a.cpp:1804; render_r4.cpp:1526 |
| 0x56 | u0, u1, v0, v1 (f32) | if cur: Fx_SetStreamerUvs 0x0041f640: Streamer_SetU 0x00495270(u0, u1) -> +0x48 / +0x50, Streamer_SetV 0x00495290(v0, v1) -> +0x4c / +0x54 (defaults 0x00494d40: 0, 1, 0, 1) | streamer +0x48..+0x54 | Streamer_Draw: per point u = (u1 - u0) t + u0 (t = age / life), v0 on the +side vertex, v1 on the -side one (fx_trail_cap_mgun: u 0..5) | r2b.cpp:912; r2a.cpp:1823; render_r4.cpp:1512, 1584 |
| 0x63 | layer (int), frames (int), rate (f32 frames/s), loop (int 0/1); the string is **not read** (0x0041e310 takes it as an unused stack argument) | if cur: Fx_SetTextureAnimation: texanim block +0x518 | +0x464 bit (layer-1); +0x518: rate [layer-1], frame count [4+layer-1], loop bit +0x40 | update: acc += dt_obj; frame += int(acc*rate), acc keeps the fraction; loop: frame %= count, else clamp count-1. Draw: stage texture replaced by "<stage texture name>_<frame>" (frame 0 = the base texture) | r2b.cpp:431; r2a.cpp:1129; render_r1.cpp:1497, 611 |
| 0x64 | x0,x1, y0,y1, z0,z1 (f32 impulse) | r() x3 always; if cur: Fx_SetAbsoluteImpulse 0x0041e1e0: Object_setPhysicsEnabled(obj, 0) (dynamic), Body_ApplyImpulse 0x00458440(point = null, impulse) = Havok linear impulse at the centre of mass, world frame | (Havok body) | velocity change = impulse / mass (mass from the template's density and collision volume; not computed here) | r2b.cpp:619; r2a.cpp:1077; physics_02.cpp:2569 |
| 0x6a | half size (f32 units), s: texture | if cur and top: Decals_Spawn 0x004165c0(Game+0xa7c, obj vt+0x60 (placement matrix, guess), size, name, entity 0, single 0): first free decal, else the oldest is recycled; Decal_Build projects onto the world inside the box +-size along the three matrix axes around the object | decal manager | Decal_Update fades by the manager's fade length once a lifetime is reached (lifetime source not traced) | r2b.cpp:1010; render_r0.cpp:1004, 1652 |

## 3. Fx objects and placement

| Topic | Behaviour | Evidence |
| --- | --- | --- |
| Creation | Factory 0x0045f020 with class name `"fx"` (id 0x38, size 0x63c, vtable 0x005a52d8): the template (`big07_obj`) gives the meshes (`lods`), its property set the physics values. The property `behavior` is `fx` for every fxo_* used here (`p_fx`: density 1, friction 0.2, restitution 0.2, fog 1, lod cutoffs 30..70; `p_fx_zero_mass`: mass 0.01, friction 0, restitution 0). So an fxo is a plain mesh object with a physics body; particles, lights and decals come only from script opcodes, never from the template | game/fx.cpp:111; weather.md; dumps |
| Init | FxObject_Load 0x00418f60 clears +0x460/+0x464 flags, +0x468, +0x48c, emitter and anim pointers; +0x5d0 = -1 (no light) | render_r6.cpp:809 |
| Placement (fx+0x14 flags) | bit 0 (0x2f child): Object_Attach to fx+0x6c at point fx+0x64 and child point fx+0x68, +0x424 \|= 0x20000 (follows the parent). Bit 1 without 0x40 (0x0f child): attach, place (0x00453750), then Object_Detach 0x00453b20 (keeps the parent's attach-point transform, then independent). Bit 1 with 0x40 (fx started with a matrix, e.g. fx_death): transform = fx+0x70 matrix (times the inverse of point fx+0x68 when set). Neither: position fx+0x18 only | game/fx.cpp:132-169; game_03.cpp:1660, 1709 |
| First update time | obj+0x468 = fx+0xc - dt; the update uses dt_obj = dt + +0x468 once. For fx+0xc = 0 the object's first update in the creation frame advances by 0 | game/fx.cpp:171; render_r1.cpp:1060 |
| Draw | FxObject_Draw 0x00419060: hidden when +0x464 bit 0x80; else the base model draw 0x00454530. Material blend (guess, from fn_004897d0: material +0x34 bit 0x10 selects mode byte +0x37: 0 -> 0x120 (additive class), 1 -> 0x420, 2 -> 0x320; otherwise 0x320 only when alpha < 1) | render_r6.cpp:844; render_r4.cpp:548 |

Meshes (lev_1_m1 skin bank, dumped to a scratch folder with `bigtool.py dump`; bounds in object space):

| Template | Property | Mesh | Role in the keg fx |
| --- | --- | --- | --- |
| fxo_ref_circle_90 | p_fx_zero_mass | 0 vertices (empty) | invisible carrier: anchor for 0x0f children, sound, shake, decal; particle emitter |
| fxo_ref_sphere | p_fx_zero_mass | 24 vertices, no texture, +-0.25 | particle emitter carrier (first two are not hidden by 0x16) |
| fxo_ref_plume | p_fx_zero_mass | 16 vertices, N_T_BB.tga, cone 5 units tall | emitter carrier, hidden (0x16), raised 16 units (0x1c) |
| fxo_flash | p_fx_zero_mass | 1 quad x +-0.5, y 0..1, fx_flash.tga, material flags 0x111 | camera card (0x2e), scaled 5 -> 20 |
| fxo_flame_anim | p_fx | 1 quad x +-0.37, y 0..2, fire_test.tga, flags 0x301 | camera card, 30-frame texture animation |
| fxo_blast_ring_snow | p_fx_zero_mass | 190 vertices, ring r 1, h 0.72, fx_blast_ring_snow.tga, flags 0x23B3D | expanding ring 4 -> 50, two scrolling stages |
| fxo_powderkeg | p_fx | 446 vertices, keg (rust, barrel top, danger textures) | flying keg: impulse 200000 up, gravity 1 |
| fxo_powderkeg_trail | p_fx | 38 vertices, fx_fire_trail.tga, streak 53 units down -z | trail mesh attached to the flying keg |

## 4. Particles

| Topic | Behaviour | Evidence |
| --- | --- | --- |
| Pools | Fx_LoadChunk 0x0041c7e0: pool 0 = 2000 particles, texture `n_fx_bank_1`; pool 1 = 4000, `n_fx_bank_2`; pool 2 = 2000, also `n_fx_bank_2` (the second lookup is reused). 0x27's first operand selects the pool | loader/auto_00410ec0.cpp:206-235 |
| Blend per pool | Fx_DrawParticles 0x00423820 draws pool 0 with state flags 0x100, pools 1 and 2 with 0x310 (state block flags 0x530a0 \| these \| 4 or 8). Guess: 0x100 = additive (Streamer_SetBlend 0x004952d0 maps its "additive" flag to 0x100 and otherwise 0x300), 0x300 = alpha blend; bit 0x10 unknown | render_r3.cpp:246; render_r4.cpp:1540 |
| Emitter step | Per object update (bit 0x200), before nothing else ends it: emitters with flag 0x20 spawn along the segment previous position e+0x74 -> current object position, axis lerped e+0x80 -> current direction (0x0046e650); flag 0x40 bone emitters likewise; otherwise in the box e+0xd0..e+0xdc (0x0046e4c0). Timer e+0xc: timer -= dt; while timer < 0: spawn, timer += interval. So per frame about dt/interval particles, sub-frame birth time kept in p+0xc (negative), first step uses t = dt + p+0xc. Pool full: the particle is dropped | render_r1.cpp:870, 1438; render_r3.cpp:1013, 1051 |
| Particle init 0x004193a0 (rand order) | life p+8 = r(e+0x2c,e+0x30); cone r(e+0x4c,e+0x50)*pi/180; RandomConeVelocity: tilt r(0,cone), spin r(0,2pi), speed r(e+0x44,e+0x48) -> velocity p+0x28; colour; size p+0x40 = r(e+0x14,e+0x18); growth p+0x44 (r(e+0x34,e+0x38) unless shrink mode, which draws no rand); drag p+0x3c = r(e+0x3c,e+0x40); stretch p+0x34 = r(e+0x1c,e+0x20); gravity p+0x38 = r(e+0x24,e+0x28) * -64.348099; uv p+0x5c..+0x68; then 0x0046e2f0: life = min(life, p+0x4c/p+0x50) | missing_02.cpp:93-143; render_r3.cpp:967; engine/missing_01.cpp:798 |
| Colour without 0x49 | p+0x48 = ARGB(e+0x70, e+0x64, e+0x68, e+0x6c) bytes, p+0x4c = 1.0, p+0x50 = 0. The draw uses alpha = 255 * p+0x4c and only the rgb of p+0x48, so the emitter alpha e+0x70 is unused and the particle stays fully opaque | missing_02.cpp:116-127; render_r3.cpp:897 |
| Simulation (in the draw pass) | Particles_Draw 0x0046d9f0, t = dt + p+0xc (p+0xc cleared): life -= t (<= 0: released); size += t*growth; p+0x4c -= t*p+0x50; k = min(t, 1/drag); v.xyz -= v*drag*k; vz += t*gravity; previous pos = pos; pos += (v + v) * t/2 (= v_new * t). dt = 0 while Game+0x2684 is set (pause, guess) | render_r3.cpp:836-866, 246 |
| Quad | Camera-facing: centre = midpoint(previous, current position); corners centre +-right*size +-up*size (camera matrix rows 0 and 1). Stretch > 0: d = per-frame motion projected on the view plane; each corner moves by c*d*stretch with c = dot(corner direction, d/\|d\|) in [-sqrt2, sqrt2]; trailing corners (c < 0) get alpha * (1 + 0.7071*c). Streak length is per-frame motion, so frame-rate dependent | render_r3.cpp:868-940 |
| 0x49 ramp lookup | j = floor(p+0x4c) (10 at birth, 0 at death), f = frac; colour = lerp(entry j, entry j+1, f); entry j at ramp + 8 + 16j {r, g, b, a}. Entries: j = 11 alpha 0; j = 10 alpha 1 if in end == 0 else 0; j = 9 - i (i = 0..9) alpha(t_i), t_i = i * 0.1 * T, T = (life min + life max)/2 | r2a.cpp:1510-1572; render_r3.cpp:874-896 |
| 0x49 alpha(t_i) | t < in start: 0; t < in end: min(1, n / ((in end - in start)/step)) with n counting 1, 2, ... ; t < out start: 1; t < life min: max(0, 1 - n / ((life min - out start)/step)), n counting; else 0. step = 0.1*T. Note entry i is displayed at age (i + 1) * 0.1 * life (one-entry lag, original behaviour) | r2a.cpp:1540-1570 |

## 5. Powder keg death and chain reaction

Property set `p_powderkeg` (behavior `fx_explosion`, FxExplosion vtable 0x005a4300): start_delay 0.5, acceleration_rate
0.85, min_delay 0.2, max_health 10, area_damage 150, damage_radius_inner 20, damage_radius_outer 50, impact_force
30000, fx_death `fx_explosion_powder_keg`.

| Step | Behaviour | Evidence |
| --- | --- | --- |
| Load | FxExplosion_Load 0x0041c180: start_delay +0x478 (default 1.0), acceleration_rate +0x47c (0.75), min_delay +0x480 (0.1), impact_force +0x474 (2010), area_damage +0x464, inner +0x468, outer +0x46c; +0x470 = 1/(outer - inner) when outer > 0.01 and outer > inner, else 1.0; fuse +0x460 = 0 | game_02.cpp:652 |
| Damage in | FxExplosion_OnDamage 0x0041c4e0 filters by source (player_damage / teammate_damage / badguy_damage bytes), then Object_TakeDamage 0x004555e0: health -= record+0x14; <= 0 -> vt+0xd4(record) = FxExplosion_Die. A keg already at health <= 0 ignores further damage | game_02.cpp:724; game_03.cpp:2098 |
| Die, chain record (flag 0x40) | Only lights the fuse, and only if the fuse is not running: fuse +0x460 = +0x478 = max(*(record+0x30), min_delay) | game_02.cpp:775-792 |
| Fuse | FxExplosion_Update 0x0041c6a0: fuse -= dt; at <= 0 vt+0xd4(0) | game_02.cpp:827 |
| Die, real | trigger, Object_Kill 0x00455640 (vt+0xdc, then Object_SpawnHusk 0x00455660: Fx_Create(fx_death, params flags 2, keg matrix at +0x30) -> fx at the keg's full transform). Then if area_damage != 0: delay = +0x478 when killed by a damage record, else acceleration_rate * +0x478 (fuse expiry passes no record); AreaDamage_Apply(&area_damage block, excluded = keg, centre = keg position (vt+0x54), source = keg, impact_force, owner = keg, flags 0x40, &delay); area_damage = 0 | game_02.cpp:793-819; game_03.cpp:2118, 2134 |
| AreaDamage_Apply 0x0047f020 | Skipped when outer < 0.01, outer < inner or damage == 0. For every object of Game+0xa84 +0xe94 except excluded and source that answers vt+0xd8: bounding sphere (vt+0x94: centre C, radius R); n = normalise(C - centre); d = \|C - centre\| - R (surface distance, can be negative); skip d > outer. Record (0x34 B): flags \|= 0x40\|1, +4 centre, +0x10 part (0x00458720), +0x14 damage, +0x18 direction = normalise(n.x, n.y, 0.7), +0x24 impact force, +0x2c owner, +0x30 &delay; sent to vt+0xd0 | game_06.cpp:1597 |
| Falloff | damage = area_damage if d <= inner, else area_damage * (1 - (d - inner) / (outer - inner)). Keg values: 150 within 20 units of the surface, linear to 0 at 50. Impact force is passed **without** falloff (30000) | game_06.cpp:1637-1640 |
| Chain timing | A neighbour taking >= 10 damage (its max_health) lights with fuse max(delay, 0.2). First keg (shot): delay = 0.5; each later keg: 0.85 x its own fuse, floor 0.2: 0.5, 0.425, 0.361, ... | above |
| Impact force use | Receiver specific. Characters (Character_GoRagdoll 0x00431310): impulse = direction * impact * [0x005bcedc] (x r(2,5) in one branch) when the hit exceeds 10050; other classes not traced | physics_01.cpp:3528, 3594-3602 |

## 6. Walkthrough: fx_explosion_powder_keg (from the dump)

Frame 0 (one run until the 0x12):
1. `fxo_ref_circle_90` at the keg matrix, pushed. One random sound of `exp_shoot_up_1/2`, positional. Shake: time 1.0 s,
   radius 120, amplitude 0.5. Starts three child fx at its origin, each run now: `fx_sticky_blast_ring_snowk` (ring,
   eased scale 4 -> 50 over 0.3 s, fade-out from 0.1 s, two uv scroll layers speed 4 and 2), `fx_sticky_flashk` (camera
   card, scale 5 -> 20 over 0.2 s, fade from 0.01 s), `fx_powder_keg_fire` (camera card, scale r(8,16) -> r(2,4) over
   r(2,4) s, fade in 0 -> 0.2 s, 30 fps looping flame frames). Lifespan 0.01 s; decal `fx_decal_blast` half size 10.
2. `fxo_powderkeg`: lifespan 1.5 s, fade-out from 1.25 s, impulse (0, 0, 200000), child `fx_keg_trail` attached
   (streamer mesh, plus a pool-1 emitter every 0.005 s: life 1.5..2, size 1.5..3, growth 1..2/s, speed 10..20, cone
   15..25 deg, axis (-90, 0, 15), gravity -0.01, uv (0.76..0.99, 0.626..0.874), fade 0 / 0.1 / 0.2), gravity scale 1.
3. Two `fxo_ref_sphere` (visible), 0.2 s each, pool 0 (additive) emitters every 0.01 s straight up (axis -90 deg):
   life 3, stretch 0.5, size 0.1..0.3 / 0.1..0.4, cone 45 / 10, speed 25..40 / 35..50, uv (0..0.125, 0.875..0.99),
   drag 0.25, gravity 0.4..0.75 / 0.5..1.0.
4. `fxo_ref_sphere` hidden, pool 1 every 0.001 s (about one per 1 ms: 200 in 0.2 s): life 2, size 0.1..0.4, stretch
   0.3, cone 50, speed 20..30, uv (0.01..0.124, 0.51..0.624), drag 0.35, gravity 0.5..1.0.
5. `fxo_ref_circle_90` hidden, pool 1 every 0.01 s for 0.1 s: stretch 3, size 5..10, life 5, cone 5, speed 15..35,
   uv (0.51..0.99, 0.01..0.49), gravity 1.0.
Frame 1 (after `0x12 0.01`): `fxo_ref_plume` hidden, raised 16 units, pool 1 every 0.05 s for 0.2 s: stretch 3, size
8..16, life 4, cone 3, speed 10..20, uv (0.25..0.5, 0.625..0.875), gravity -0.05..0.01, fade 0 / 0 / 1.0.

## 7. Engine port (2026-10-15, engine/src/fxvm/)

Supersedes the "Engine:" notes in the table above (0x10 now uses the reference point's axis, 0x32 drives ground hits,
0x4a lights are published). Static reading of render_r0 .. r7, r2a, missing_02 (no new oracle runs).

| Item | Original | Engine | Evidence |
| --- | --- | --- | --- |
| Attach points (0x104 lists) | 0x00453c50 (object, key, ..., a3 = 1, a6 = 1, a8 = 1): the node's world matrix, translation scaled by the object's scale | `attach.rs`: the template's default anim (`anims/<name>.json`), nodes at their first key, parents chained; "null" / unknown = the object's matrix | anim_01.cpp:2241; fxo_ref_explosion_medium has `center` (scale 2.1) and `ref_debri_00..10` |
| Random picks per opcode | 0x0f / 0x2f / 0x30: one rand for list1, one for list2 (Fx_StartOnObject 0x0041dc80); 0x39 / 0x3a with cur: one for the point; 0x13: one when there is an owner | as the original (the engine drew none for 0x0f and 0x39 before) | r2a.cpp:872-932; audio_01.cpp:235 |
| Mesh emitters 0x17 / 0x18 / 0x19 / 0x30 / 0x36 | Fx_Emit 0x0041e4d0: interval +0x28, timer +0x2c = -interval; spawn while timer <= -interval (timer += interval, the pass repeats with dt 0); 0x30 (flag 1): a child fx following the object when the timer goes negative, timer = interval; 0x19 (flag 2) a point of list1, the matrix interpolated by interval / -timer (guess for [0x005bcb5c] = -1), list2 the child's point; 0x36 adds a random Euler rotation from three rand() (object flag 0x4000); 0x2a stops one (timeout +0x38) | `object.rs` MeshEmitter | render_r1.cpp:1233-1385 |
| Ground hits | the Havok contact callback (FxObject slot 0x2a, 0x00419330: +0x464 \|= 0x2000, contact point +0x544, other body +0x554); the next update: on hit create (0x35: `<fx>_<material>`, at most w0 times, w1 s apart), area damage (0x76: impact w0, damage w1, inner w2, outer w3, once), delete (0x33) / death (0x34) | a ray from the body's centre along its velocity (down when at rest) excluding itself; a contact counts when it starts. Area damage = `crate::fx::Explode` | render_r1.cpp:1026-1058; r2a.cpp:2011-2083; render_r6.cpp:900 |
| On death (0x1d) | +0x51c fx, flag 1: started at the object's position (params 0x38) when it ends with al = 1 (lifespan, on hit death, logged release) | `object::end_object` | render_r1.cpp:514 |
| Spin (0x1a) | Fx_RotateObject 0x0041f8f0 -> ForceActionPool_Add 0x00403fb0 flags 0x30: an angular impulse of the vector x remaining (1/120) | an angular velocity change of the vector in degrees / s, capped at 20 rad/s (guess) | physics_01.cpp:251, 372 |
| Gravity list | every fx body is registered on Game+0xa84 +0xf1c by its body set-up fn_00419020 0x00419020; +0x48c = 0 from FxObject_Load, so a body without 0x11 would be weightless | answers the open question; the engine still gives such bodies (only fx_trash) gravity 1 (guess kept until compared) | render_r1.cpp:443; render_r6.cpp:805 |
| Particle birth time | SpawnAlong 0x0046e650 carries -timer - dt, so the first step advances the particle by its age within the frame | fixed (the engine advanced it by the other part of the frame) | render_r3.cpp:1048 |
| Particle quad centre | position + half the last move (dx * 0.5 + pos), not the midpoint | as the original | render_r3.cpp:862 |
| Pool full | Particle_Alloc 0x0046d910 fails before the set-up: no random numbers drawn | as the original | render_r3.cpp:1060 |
| Uv transforms 0x40 / 0x41..0x44 / 0x45 | the stage matrix = rotation (0x45, deg) x scale (0x41..0x44 path block) + scroll offset (mod 64); the scroll's duration 0 means no end (its ramps are set only with a duration) | `object.rs` UvLayer (layer 1 on the mesh material's uv transform, layer 2's offset on the layered domes); the engine ended a scroll at once for duration 0 before | render_r1.cpp:547, 770-855; r2a.cpp:2186-2268 |
| Lights 0x4a / 0x58 / 0x59 / 0x5a | D3D point light at its attach point, re-added each update | `FxVm::lights` (engine space), not drawn (engine-limits.md R4) | r2a.cpp:757-812 |
| Post effects 0x5b..0x61 / 0x70 / 0x71 / 0x77 | PostEffects (Game+0xa9c), id = the object; phases in / hold / out (defaults 1, -1, 1), weights, distance / edge fades, accumulators (PostEffect_Apply 0x0047c200) | `screen.rs`; the fade drawn as a UI overlay; glow / blur / overlay computed, not drawn | render_r3.cpp:1085-2135 |
| Camera paths 0x73 / 0x74 | Camera_StartPath 0x0040ff50 mode 1 / 2 (2 takes the path's Camera01 matrix once) | both send StartCameraPath (campath.rs plays mode 1) | game_02.cpp:244 |
| Fx ears (bb_ear) | FxOwner_Toggle / OnMessage / Update 0x0041bf60 / 0x0041c050 / 0x0041c110: random initial delay, cool-down, looping handle toggled off by a second message, lock_fx = on the owner | fxvm/ear.rs (mission.rs sends TriggerFxEar): one_shot, the random initial delay and the delay_repeat cool-down; looping_fx is set by no shipped ear and not ported; lock_fx is not distinguished (see the module doc) | r2a.cpp:96-207 |

Opcodes not ported (logged once as "fx vm: not ported" when met): 0x1f, 0x28, 0x29, 0x2d (other particle emitter
shapes: in none of the 25 shipped fx banks, lev_shell included, counted 2026-10-22; 0x1f would pass a null direction
to 0x0041e760's default branch; 0x2c, the emitter colour, is unused too), 0x3e, 0x66, 0x67..0x69, 0x6b..0x6f, 0x72:
no shipped fx bank uses them (all levels' fx.json, 2026-10-22); 0x62 (Fx_SetBackground 0x0041fe00: the lev_5 sea,
cloud and shoreline objects drawn as background, flags on their materials): deferred, the object-group draw 0x00488130
and the material flags 0x10 / 0x20 are not decoded. 0x4b / 0x4c (cards turning about x / z, object.rs Billboard) and
0x75 (face the flight, object.rs) are ported.

## 8. Fx bodies, the keg's launch and debris (2026-10-18, player feedback)

| Item | Original | Engine (fxvm/body.rs) | Evidence |
| --- | --- | --- | --- |
| Mass | the fx body set-up fn_00419020 0x00419020 -> Object_BuildRadiusCollision 0x004527d0 (cinfo +0xc): a sphere of the bounding radius, mass r^3 x density x 4/3 pi (min 0.05); `collide_poly` templates: the collision model's compound (0x00456c40 -> Body_BuildShape 0x00413450, hull flag) with the object's density. Havok runs in game units (gravity 64.348) | `BodyDef`: the game mass (hull volume x density for collide_poly, `hull_volume`), Jolt mass = game mass, impulse x metres per unit only. The engine had mass 30 and impulse / 66 for every body | physics_02.cpp:645; physics_01.cpp:2448 |
| Resulting speeds | impulse / mass: keg 200000 / 1118 = 179 units/s up; house debris 91..145 (village_1) / 39..63 (village_2); barracks roof 8000000 / 39504 = 203 units/s, back down within its 12 s and bursts on landing (0x34 -> fx_top_death) | as the original (guess for the hull: the roof's enclosed volume would give 862 units/s and no landing, which the script's 0x34 contradicts) | data: fx.json, models |
| Shape | built at creation (factory 0x0045f020), before the script's 0x0b scale, never rescaled | the model's bounding box round its centre, at the object's scale (deliberate: a full-size shape held small pieces in the air; the flying keg's box was centred on its base, half underground, and was pushed out sideways) | object.rs scale |
| Gravity | g x the 0x11 scale, g = 64.348 | the 0x11 scale as the gravity factor (the Jolt world has the game's 64.348 units/s^2 = 12.87 m/s^2 at 0.2 m per unit, PhysicsManager_Init 0x00411960) | Physics::game_gravity_factor |
| Surface | the template's friction / restitution (p_fx 0.2 / 0.2, p_fx_collide 0.85 / 0, p_fx_collide_friction 0.001 / 0.001) | the template's | properties.json |
| Blast fling | Character_GoRagdoll 0x00431310: a killing record with an owner and impact > 10050 throws the body along normalise(n.x, n.y, 0.7) x impact x 0.15 (keg: 56 units/s) | fx.rs `fling`, ai.rs die (before the ragdoll count). Not modelled: the x 2..5 / 25% launch when a launch happened the same frame | physics_01.cpp:3582-3600 |
| Smoke blending | D3D alpha blend in gamma space | particle.wesl: 1 - (1 - a)^2.2 for a black source (exact), a^2.2 for white over black, mixed by the source's brightness; alpha^2.2 alone drew the keg's dark trail smoke at a fraction of its opacity | the intro fade measurement (bright end) |

## Open questions

- The per-frame caller of Fx_Step (fx system update over list +4) and the order of fx update vs object update vs the
  particle draw in a frame (affects the first-frame dt of new objects and emitters).
- Answered (section 7): fx bodies are registered on the gravity-scale list by fn_00419020, so a body without 0x11
  is weightless in the original. Open: whether fx_trash (0x10 without 0x11) floats in a capture.
- The Havok units of 0x10 / 0x64 impulses and 0x1a spins (the engine's scales are guesses), and the [0x005bcb5c]
  constant of the mesh emitters' interpolation.
- What vt+0x100 does exactly (alpha assumed), and vt+0x60 (assumed: pointer to the world matrix, used by 0x6a).
- Meaning of state bit 0x10 in the pool 1/2 draw flags and of the base flags 0x530a0; the blend mapping above is
  inferred, not read from the state-block applier (Game+0x40 vt+0x14).
- Direction vector of the sine table for 0x40 angle 0 (table at 0x00614b00/0x00614b08 is filled at run time).
- Decal lifetime for 0x6a (Decal_Build clears the fade; where +0xf60 is set was not traced).
- How non-character receivers (physics props, other kegs) turn record+0x24 impact force into an impulse.
- `rand()` 0x005391cd assumed to be the MSVC CRT LCG; needed for bit-exact replay of the RNG order above.
