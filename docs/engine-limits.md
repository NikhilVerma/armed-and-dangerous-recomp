# Engine limits

The 2003 engine's hard limits, and whether the remaster lifts them. A remaster removes these limits rather than
accepting them (the player, 2026-10-12). When a new limit turns up, add it here with evidence and a plan.
Sources: two read-only code audits (2026-10-12) and the playground captures. Guesses are labelled.

## Simulation, gameplay, audio, input

### Lifted

| Limit | Evidence | Original | Remaster |
|---|---|---|---|
| Positional voices | 0x004fa210 (a 3D start fails when +0x120 > 0x3f) | 64 | no limit (Bevy mixes every voice; engine/src/audio.rs) |
| Simultaneous ragdolls | Character_GoRagdoll, src/physics/physics_01.cpp (`downCount <= g_ragdollLimit`) | 4 | `[persistence] ragdoll_limit` 64; held bodies don't count |
| Corpse fade | Corpse_CountDown 0x0042eba0 (src/game/corpses.cpp) | fade after 1-5.5 s | `bodies_keep` 1, `bodies_max` 64 |
| Explosion debris lifespan | FxObject update 0x0041a350 | 1-12 s | `debris_keep` 1, `debris_max` 1000 |
| Debris collision = one bounding sphere | Object_BuildRadiusCollision 0x004527d0 | sphere | `debris_mesh_collision` 1 |
| Decal pool | 0x00416150 (`mov ebx,0xa0`) | 160 | `decal_pool` 1024 (cap 4096) |
| Mouse: one sensitivity, no invert | 0x004f83c0 / 0x004f9560 | | `qol mouse_x/mouse_y/mouse_invert_y` |
| Controller: DirectInput joystick only | docs/subsystems/qol.md | | `input gamepad` (XInput, DualShock, DualSense) |
| No quicksave keys | | | `qol quick_keys` |

### Not lifted

| # | Limit | Evidence | Original | Plan | Effort |
|---|---|---|---|---|---|
| S1 | Simulation tied to the frame rate | PhysicsManager_Step 0x00411d40 raises steps under 1/60 s to 1/60 s | measured x2.41 at 144 Hz | LIFTED for physics: `[limits] physics_fixed_step` (accumulator, 1/60 s steps); x1.00 at 30/60/144/240 Hz (playground physics_rate). Still to do: interpolation (objects move at 60 Hz above 60 fps) | done / L |
| S2 | Havok world settings | WorldCinfo_Init 0x00414370, overridden in 0x00411960 | Broadphase ±5000 x ±5000, -1100..2000; +0x30 = 512 and +0x3c = 4 (guess: capacity and solver iterations) | Raise them after confirming the field meanings, or let Jolt own them later | M |
| S3 | Force-action pool | ForceActionPool_Init 0x00403ef0 | 512 | LIFTED: `[limits] force_pool` 8192 (not reached in the 4-barracks stress scene even at 512) | done |
| S4 | AI pathfinding | AStar_Add 0x00470600, Grid_FindPath 0x0046fd20 | 2000 nodes | LIFTED: `[limits] path_nodes` 16000 (arrays enlarged in play, AI pathing scene ran clean) | done |
| S5 | Audio output | 0x004fa8f0 primary buffer | 22050 Hz | LIFTED: `[audio] mix_rate` 48000 (the device reports 48000 Hz); audible effect not checked | done |
| S6 | Animation blend slots | anim_01.cpp | 2, or 4 when a weight is set | Later | M |
| S7 | Saves under the install folder | save_01.cpp, 0x0048b370 | Fails under Program Files without VirtualStore | Redirect to Saved Games | S |
| S8 | Fixed string buffers | 0x004198c0 (32-byte names); options registry 32 entries | | As needed | S |

### Unknown, needs a runtime check

- Address space: 32-bit process, LARGEADDRESSAWARE not confirmed; heaps grow (no fixed arena found). Check peak
  commit with kept bodies, debris and a 4096-decal pool.
- AI activation by distance: none found in recovered code yet.
- Maximum characters and objects: world lists are linked lists. Guess: no hard cap.
- Havok internal pools (hkPoolMemory, islands, constraints).
- Behaviour of AI, decal fade and particles at 144 fps and above (`[test] fixed_dt_ms`).

## Rendering

### Lifted

| Limit | Evidence | Original | Remaster |
|---|---|---|---|
| Shadows: per-caster silhouette decals, 24 textures (64-256 px) | table 0x005b96f8, 0x00416190, 0x00417c00 | blob-like | `render shadow_maps` 1 (one 4096 map, range 200, PCSS) |
| Draw/fog distance | fn_0040f8c0 clamps the far scale to 0.25..1.0 | <= 1.0 | `render draw_distance` up to 4 (default still 1.0) |
| Vertex fog (linear, per vertex) | Renderer_SetFog 0x00504010 | | `render fog_modern` 1 (per-pixel height fog) |
| LOD by distance, no hysteresis | Object_SelectLod 0x00452f50 | | `render lod_max` 1 |
| Decal pool | 0x00416150 | 160, oldest killed | `persistence decal_pool` 1024 |
| Anisotropic filtering | | 1 (Vulkan backend default) | `render anisotropy` 16 |
| 640x480 4:3 UI | 0x004f01c0 | | `ui anchor`, `text_scale`, `camera hor_plus` |
| Texture resolution, terrain detail, hard particle edges, no AO | | | override packs, `terrain_detail` 8, `soft_particles` 1, `ao` 1 |

Remaster engine (engine/, Bevy; 2026-10-15, engine/src/graphics/, Remaster preset; the Original preset keeps the engine's faithful look): textures -> the AI retextures (tools/remaster_textures.py); no AO -> Bevy SSAO, snow exempt; aliasing -> TAA + sharpening (SMAA / MSAA selectable); shadows -> 4096 PCSS cascades fitted to the far clip; hard particle edges -> soft particles (depth prepass); far clip -> dissolve; anisotropy 16x on effects and decals too; bloom above white. Draw distance stays the original's maximum (fog unchanged), LOD always the finest.

### Not lifted

| # | Limit | Evidence | Original | Plan | Effort |
|---|---|---|---|---|---|
| R1 | Lights per draw | Renderer_UpdateLightConstants 0x004ff2e0 | 2 directional + 1 other, by slot order (no nearest-light choice; a held slot drops effect lights) | Vulkan light list (tiled/clustered), fed from all slots and effect lights | L |
| R2 | Lights per scene | LightSlot_Acquire 0x0048a4b0 | 8 slots; a 9th fails ("Fx : Failed to set light") | Larger table outside the D3D9 slots (with R1) | M |
| R3 | Per-vertex lighting | vertex shaders (c3, c7-c15); only 3 pixel shaders light per pixel (bump+specular, bump diffuse, terrain: sun only), vk_shader_translate.cpp | characters faceted and flat | Re-light per pixel in the translated shaders (normal and position as varyings) | L |
| R4 | Point light not visible on the grunt | playground grunt_light | c9 uploaded, no visible effect | Part of R1/R3 | |
| R5 | Particle pools | Fx_LoadChunk (2000 / 4000 / 2000); Particle_Alloc 0x0046d910 | 65,779 particles refused in the 4-barracks scene; smoke and dust missing | LIFTED: `[limits] particle_pool_scale` 16 (none refused; lowest fps 65 -> 41 in that scene). GPU particles later | done / L |
| R6 | Texture registry | TextureBuckets_Init 0x0050a200 | 1024 buckets | NOT A PRACTICAL LIMIT: peak use 318 / 306 / 321 in lev_1_m1 / lev_3_m3 / lev_4_m1 | - |
| R7 | Post-process blur targets | 0x0047c920 | 512x256 and down, whatever the resolution | Scale with the back buffer | S-M |
| R8 | Environment map | 0x00418880 / 0x00418c80 | one map, faces updated over several frames, size from level data | SSR or probes | M |
| R9 | Frame rate | see S1 | physics too fast above 60 fps; opening stalls above about 85 fps | S1; meanwhile a 60 fps cap as remaster default | S/L |
| R10 | Bone palette | Skin_BuildPalette 0x004edbe0 (17 bones, vs_1_1 constants) | meshes split by the data | Only matters for new content | S/M |
| R11 | Decal geometry | decal constructor 0x00417010 | one draw per decal, no distance cull (about 128 vertices, guess) | Batch | M |
| R12 | Geometry density | | low poly | Tessellation deferred (docs/journal/2026-10-12.md) | L |

### Limits in our own Vulkan code

| Constant | Value | Where | Note |
|---|---|---|---|
| Shadow map | one cascade, 4096 (512-8192), range 200 | rhi.h, shadows.cpp | about 0.1 unit per texel; cascades if the range grows (M) |
| kTessBuffer | 64 MB | vk_device.cpp | overflows silently with many tessellated characters (tessellation is off) |
| kUniformRing | 32 MB | vk_device.cpp | |
| kVsConstantCount / kPsConstantCount | 256 / 32 | vk_abi.h | |
| kMaxSamplers | 8 | vk_abi.h | |
| kMotionBlurTile | 48 px (= the longest blur) | vk_motionblur.h | |
| Snow | 1.5M flakes (0.1M-3M) | weather.cpp | |
| GTAO | 3 slices x 3 steps | lighting.md | |

### Unknown, needs a runtime check

- Which draws use fixed-function lighting (all 8 lights, per vertex) versus the shader path; which objects use the bump
  pixel shaders.
- Whether world geometry is pre-lit through vertex colour.
- Texture bucket overflow behaviour; per-level EnvMapSize values.
- Particle peaks against the pools in heavy combat (guess: the 4000 pool saturates in explosions).

## Frame budget at 4K (2026-10-12, RTX 4090, playground walk, [limits] log + [render] vulkan_verbose / gpu_timing)

| | remaster defaults | remaster off |
|---|---|---|
| fps (driver cap 119) | 70-112, ~90 average | at the cap |
| GPU per frame | ~7 ms (AO 0.6 half-res, shadows 2.3, FSR 2.0, snow 0.9, opaque 0.8) | ~0.7 ms |
| game logic + physics | ~1.3 ms | ~1.3 ms |
| render submission (game scene traversal + Vulkan layer + Present) | 7.7-9.7 ms | ~7 ms (includes the cap's wait) |
| Vulkan layer preparing ~1,000 draws | 3.4-4.9 ms | 2.7 ms |

The limit is single-threaded draw submission: about 3-5 us per draw in the Vulkan layer. Next step: record draws
on worker threads (secondary command buffers) or batch the shadow-caster replay. Not started (structural change;
waiting for the player's go-ahead).

### Where the remaining dips come from (2026-10-12, after the draw-state cache: ~99 fps average, 82-116)

In the slow windows (77-85 fps) the shadow casters double (about 70 -> 130 per frame): their CPU replay and the shadow
map's GPU time (2.3 -> 3.0 ms) grow together, and one window shows a 4.4 ms Present spike (GPU at the edge of the
119 fps cap). Game logic stays ~1.3 ms; no pipeline compiles there. Next: cache the static casters' part of the
shadow map and redraw only moving ones each frame, with "moving" decided per draw from its world-space transform AND
its skinning constants (an animated character standing still must not freeze its shadow).

### Static shadow cache result (2026-10-12)

Built ([render] shadow_static_cache): 829 of ~894 casters per frame come from a cached map rebuilt 20 times per 300
frames; images match the uncached pass. It did not remove the dips: the shadow pass's CPU is ~1.9 ms (about 1 ms of
per-caster bookkeeping for ~900 casters) and its GPU time is mostly the soft-shadow apply at 4K. Remaining frame
time in the slow stretches is the game's scene traversal plus the Vulkan layer's per-draw work (~3 ms for ~1,100
draws), on one thread. Next real gain: record draws on worker threads.
