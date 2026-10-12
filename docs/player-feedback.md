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
| 7 | Mountain skybox texture not high-res enough: tiled Qwen | B_1_1_sky, B_level1_sky_part_A/B: SeedVR2 7b 4x canvas, then tools/retex/qwen_tiles.py with 1024 tiles, overlap 256, the agreed recipe (0.9 / cfg 5). work/retex/sky4/run.py. RUNNING; install when done |
| 8 | Footstep / ice kick-up flakes are BLACK when running | The colour pass was not alpha-weighted (particles darkened: fx_snow_flakes 53 -> 7). Fixed in batch.py/recipe.py (alpha-weighted); re-running finish() for all alpha textures. RUNNING |
| 9 | Building debris floats in the air after destruction; ALSO weighted things like dead BODIES float: "geometry passed to physics and geometry rendered isn't the same always" | Not the weightless-fx theory (that filter is in, but is not the cause). Suspects: lod_max (finest LOD drawn vs collision or destroyed-state meshes), extended culling showing hidden intact meshes, held bodies or debris outliving the geometry they rested on, or a recovered-code bug. Subagent investigating; A/B in game next. IN PROGRESS |
| 10 | Menu mouse: bounding box / hover still inaccurate, does not always highlight | Cause: the hit-box scale was computed once at start (1.25) while text is drawn 4.5x at 4K. Now per frame (logged: hit scale 4.50). The clicks that missed in the player's log now fall inside the boxes. WAITING FOR CONFIRMATION |
| 11 | "Don't revert, fix" | Standing instruction: when a fix misbehaves, fix it forward rather than reverting it (unless asked) |

## Standing preferences from this period

- Retexture recipe: docs/retexture.md "Recipe as agreed". 7b for direct SeedVR2 upscales, 3b for the canvas under Qwen.
  Originals under 32 px stay as they are.
- Motion blur is per object, never from camera pans. The setting is shutter speed (1/N s), not shutter angle.
- Tests run after human validation (docs/decision.md).
- The Afterlight GI is off by default and exposed in the remaster menu.
