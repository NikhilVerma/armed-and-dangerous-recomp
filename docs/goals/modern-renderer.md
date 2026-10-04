# Goal: modern rendering backend

Repo `C:\Users\nikhi\and-decomp`. Target `orig/pc/GameData/game.unpacked.exe` (SHA-256 `f6a690a4…`). Follows
`docs/goals/renderer-and-assets.md` (done 2026-10-03). Roadmap step 2 (`docs/roadmap.md`).

## Outcome

The game draws through a renderer interface we own, and a new backend implements that interface next to the
original Direct3D 9 path. The new backend runs the whole game at parity with D3D9. It also exposes what DLAA,
HDR, PBR and ray tracing need: depth, per-pixel motion vectors, linear HDR colour, and a scene description
(meshes, materials, lights, camera) that does not depend on D3D9 calls. No DLAA or ray tracing yet. This goal
builds what they plug into.

## Decision for the human (before step 3)

- **Backend API: Vulkan (recommended).** It is open, runs on Windows and Linux, has ray tracing, and DLSS/DLAA
  and FSR both support it.
- **Alternative: D3D12.** Better tooling on Windows (PIX), with the same DLAA and ray-tracing support.
- **Default:** Vulkan, unless the human says otherwise.
- **Either way:** the D3D9 path stays as the reference oracle.

## Scope

- **Renderer core:** the D3D9 layer `0x004fb000-0x0050d000`, 180 functions, 61 recovered at S2 as of
  2026-10-03. Recover all of it, including the draw path (`0x004fe01f` DrawIndexedPrimitive and the strip
  draws), virtual 84 `0x0050c360` (bank texture creation), lights, the shader constant layout and the
  `shaders.dat` vertex shaders.
- **Callers that drive it:** the render system (`RenderSystem_*`, render lists `0x0058b0d8`), the skin/mesh draw
  (stream blocks, bone palettes: `docs/formats/big-skin.md` 3.3-3.7), landscape, particles/fx, UI and font.
  Recover only as far as needed to find every call into the renderer.

## Steps, in this order

1. **Finish the renderer core.**
   - Every function in the range is at S2 and passes `tools/compare.py`.
   - Use the workflow pattern from the pilot: agents write and compile, `tools/bisect_hooks.py` verifies.
   - Retry the 16 functions skipped in the pilot after fixing the ABI-scanner gaps listed in the journal
     (tail-jump callees, false ST0 returns, stack-argument inference).
2. **Draw the interface boundary.**
   - Write `src/renderer/` down as one table: every renderer virtual (vtable `0x00589990`) and every direct call
     from game code into the renderer, with what it does.
   - Specify the interface in `docs/renderer-interface.md` and `src/rhi/`:
     - resource creation (textures, vertex/index buffers, shaders)
     - state (blend, depth, fog, samplers, transforms)
     - draws
     - frame begin/end and present
     - a per-frame scene description (camera, lights, mesh instances with material and bone matrices)
   - Recovered code calls the interface. A D3D9 implementation of it, `src/rhi/d3d9/`, must keep
     `tools/compare.py` identical. That is the proof that the boundary is complete.
3. **New backend** (`hook/enhance/rhi_vulkan/` or `rhi_d3d12/`, `[render] backend=vulkan`, off by default):
   - Translate the fixed-function and shader-model-1/2 state the game uses: texture stage combiners
     (`Material_InitCombiners`, colour-mix passes), fog modes, alpha test, the named pixel shaders and the
     `shaders.dat` vertex shaders.
   - Texture formats: DXT1-5, 16- and 32-bit formats, cube maps.
   - Device loss and reset; window, resolution and vsync from `[display]`.
4. **Parity test.**
   - `tools/compare_backends.py` runs the menu and `lev_1_m1` scenarios on both backends with the virtual clock.
   - It compares screenshots at fixed frames with a perceptual metric (exact pixels are not expected across
     APIs), records the thresholds, and fails on missing or extra geometry: compare draw counts per frame from
     the interface's own trace.
   - Add it to `tools/check.ps1`.
5. **Remaster hooks, exposed but unused:**
   - depth buffer and per-pixel motion vectors (previous-frame transforms per instance, including skinned
     meshes), checked by a debug view `[render] debug_view=depth|motion|normals`
   - render into a linear 16-bit float target with a final tonemap pass
   - the scene description available to a later ray-tracing pass

## Rules

- Same as the previous goal: HOOK and `and.ini` switch per function, recovered only when it passes the comparison
  test, `src/` faithful, additions in `hook/enhance/` and off by default.
- The interface layer in `src/rhi/` is the one structural addition allowed in `src/`, because the D3D9 path
  through it must stay bit-identical. Record this in `docs/decision.md` when step 2 starts.
- Ask the human before downloading SDKs: Vulkan SDK, DLSS SDK, Agility SDK.

## Done when, in the running game

1. Every function in `0x004fb000-0x0050d000` is at S2 or later in `symbols/functions.csv` and passes compare.
2. The game draws only through `src/rhi/`. The D3D9 implementation still gives 900 identical frames in both
   scenarios.
3. `[render] backend=vulkan` (or d3d12) plays start-up -> menu -> `lev_1_m1` with no missing or incorrect
   geometry, textures, fog, transparency or UI, passing `compare_backends.py`. Texture and model overrides
   still work on it.
4. `debug_view=depth`, `motion` and `normals` show correct depth, motion vectors (also on animated characters)
   and normals, and the frame renders through a linear HDR target plus tonemap with no visible change at
   default settings.
5. Tracker, docs (`docs/renderer-interface.md`, `docs/subsystems/renderer.md`) and journal are updated, and
   every commit passes `tools/check.ps1`.

## Status 2026-10-04

1. Done. 295 functions in the range: 293 at S2 and 2 at S3, all passing compare.
2. Done. With `[render] rhi=1` every GPU call goes through `rhi::`. The D3D9 backend is identical in all 5
   comparison scenarios (`check.ps1`, `compare.py --rhi`).
3. Done. `backend=vulkan` passes `compare_backends.py` in menu, level, play and quit. Texture and model overrides
   work on it.
4. Done. Debug views are verified (docs/renderer-interface.md, "Remaster outputs"). Motion vectors match pixel
   displacement within a median of 1.4 px. The HDR target and the default tonemap pass parity. The scene
   description reproduces the depth buffer.
5. Done with this session's commit: docs/renderer-interface.md, docs/subsystems/renderer.md, the journal, and
   `check.ps1` green.

## After this goal (not in scope)

- DLAA/DLSS and FSR using the motion vectors and depth from step 5.
- HDR10 output.
- PBR materials: map the material records (`docs/formats/big-skin.md` 3.5) to albedo, normal and roughness, and
  allow extra maps from overrides.
- Ray-traced shadows and GI from the scene description.
- A skin-chunk writer (OBJ/glTF -> `.skn`) for editing geometry.
