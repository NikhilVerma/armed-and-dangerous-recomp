# Player feedback backlog

One line per item, with its status. Newest session first. Each fix needs evidence: a capture, a log or a test.
Agents re-read this file after every context compaction (AGENTS.md).

## 2026-10-12 (play session after the overnight verification)

| # | Feedback (the player's words, short) | Status / plan |
|---|---|---|
| 1 | Triangle sliver / see-through cracks in the geometry | Two causes. (a) The shadow apply skipped faces turned slightly away from the sun (vk_shadows.cpp). Fixed, narrowed to ndl > -0.2 after it darkened whole cliff facets. (b) World tessellation (`tess_props=1`) opens cracks at split vertices. Now off in the player's and.ini. Real fix: crack-free Phong-AEN tessellation (item 2) |
| 2 | "Hole-aware" and UV-aware tessellation; are we doing SOTA? | Today's version is hand-rolled Phong. Plan: Phong-AEN (PN-AEN adjacency, McDonald & Kilgard 2010): CPU-welded 9-point patches, normals averaged across seams, crease and level rules decided per welded edge, boundary edges flat, `precise`, guard against bad normals, equal spacing option. Full design in docs/journal/2026-10-12.md. DEFERRED (the player: "if tessellation is hard, just turn it off for everything and defer it"): tessellation off by default and in the player's and.ini |
| 3 | Characters: tessellation breaks their UV mapping | Part of item 2 (split UV seams crack; bad rebuilt normals; fractional_odd swimming) |
| 4 | Snow particles have NO motion blur / shutter too weak | Snow streak exposure = shutter x `snow_shutter_scale` (4). Captured: visible short streaks. Waiting for the player to confirm |
| 5 | Character still blurs at its edges when the camera moves | Velocity = the shorter of screen and world motion. Captured: sharp during a running turn. Waiting for confirmation |
| 6 | Snow particle opacity: "like 0.7" | Flake opacity 0.7. Done |
| 7 | Mountain skybox texture not high-res enough: tiled Qwen | Tried a tiled Qwen 4x and a SeedVR2 4x of B_1_1_sky (256 px tall): both invent streaky painted ridges and halos (OneDrive sky-4x/). Not installed; the 2x SeedVR2 stays. Proposed: SeedVR2 2x + gentle Qwen (0.6 / cfg 3) per tile, or repaint the mountain crop alone. WAITING FOR THE PLAYER'S CHOICE |
| 8 | Footstep / ice kick-up flakes are BLACK when running | Fixed: alpha-weighted colour pass plus a whole-texture brightness match on the visible part (189 alpha textures redone); 12 still more than 25 % off (fx_snow_flakes, fx_impact_snow_small, breath, dark smoke, ...) now use a plain Lanczos enlargement of the original. WAITING FOR CONFIRMATION |
| 9 | Building debris floats after destruction; ALSO weighted things like dead BODIES float: "geometry passed to physics and geometry rendered isn't the same always" | Cause (subagent, code evidence): held bodies stay ragdolls (state 400) forever and count against the game's 4-ragdoll limit (Character_GoRagdoll downCount <= 3), so after 4 held bodies every later death is animated without physics and stays where it ends, e.g. in the air. FIXED: held bodies no longer count (g_isHeldBody). Part 2, TODO: kept bodies and debris sleep and are not woken when their support is removed; wake kept bodies near a removed object (activateIsland 0x4ad130). The weightless-debris filter is in but was not the cause. WAITING FOR CONFIRMATION |
| 10 | Menu mouse: bounding box / hover still inaccurate, does not always highlight | Cause: the hit-box scale was computed once at start (1.25) while text is drawn 4.5x at 4K. Now per frame (logged: hit scale 4.50). The clicks that missed in the player's log now fall inside the boxes. WAITING FOR CONFIRMATION |
| 12 | Foot smoke when running then stopping abruptly is pitch black | Reproduced (run, stop). A/B: with overrides off the puff is white, so it is a texture issue. Cause: upscalers darkened colour in semi-transparent edges (dark fringes; fx_smoke_snow). Fixed: soft edges keep the original's colour (finish()); all alpha textures re-run. WAITING FOR CONFIRMATION |
| 13 | Debris: the render does not match the hitbox | Recovered collision code verified against the disassembly (faithful). The original uses one bounding sphere per debris piece; kept planks rest on it and hover. Fix: kept debris uses the model's compound shape (g_meshCollision, debris_mesh_collision). BUILT, NOT DEPLOYED (the player is playing); needs an in-game explosion test |
| 11 | "Don't revert, fix" | Standing instruction: when a fix misbehaves, fix it forward rather than reverting it (unless asked) |

## Standing preferences from this period

- Retexture recipe: docs/retexture.md "Recipe as agreed". 7b for direct SeedVR2 upscales, 3b for the canvas under Qwen.
  Originals under 32 px stay as they are.
- Motion blur is per object, never from camera pans. The setting is shutter speed (1/N s), not shutter angle.
- Tests run after human validation (docs/decision.md).
- The Afterlight GI is off by default and exposed in the remaster menu.
