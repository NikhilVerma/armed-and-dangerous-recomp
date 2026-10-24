# Goal: renderer and assets recovered

Repo `<repo>`. Target `orig/pc/GameData/game.unpacked.exe` (SHA-256 `f6a690a4…`).

## Outcome

Decompile the renderer and the data-loading code into compilable C in `archive/src/`, running in place of the original
through the hook DLL, so that wireframe and texture/model replacement work. This is the foundation for the
remaster: a modern backend, then DLAA, then ray tracing.

## Scope

- **Renderer:** the Direct3D 9 layer around the `Renderer` object: device setup, render states, textures,
  vertex and index buffers, shaders from `shaders.dat`, frame and draw calls.
- **Loading:** the `filesys.cpp` file system, all 13 `.big` level archive kinds, textures, models and skins,
  animation.

## Build first, in this order

1. **Generated calling-convention layer.** Generate C-callable wrappers and entry stubs for every function from
   `archive/tools/reginputs.py` plus runtime `abi.csv`. These replace hand-written asm shims.
2. **Automated comparison test.** A scripted, repeatable start-up → menu → `lev_1_m1` run
   (`[startup] load`, `[test] fixed_dt_ms`, a fixed random seed, scripted input). Its call trace and per-frame
   state hash must match the original game's. Wire it into `archive/tools/check.ps1`.
3. **Ghidra types.** Seed them from `symbols/types/data_members.csv`, then re-export the decompiled code.
4. **100-function pilot.** Report pass rate and tokens per function, then ask the human before fanning out to
   parallel agents.

## Rules

- Each recovered function replaces the original through `HOOK()` and has an `and.ini` switch.
- A function counts as recovered only when it passes the comparison test.
- `archive/src/` stays faithful to the original. Every addition lives in `archive/hook/enhance/`, switchable in `and.ini` and
  off by default.

## Done when, in the running game

1. `[render] wireframe=1` draws wireframe using the recovered renderer code.
2. A loose PNG/DDS dropped into an override folder replaces the matching `.big` texture through the recovered
   loader, and a replacement model loads the same way.
3. Command-line tools dump and rebuild every `.big` kind, rebuilding each one byte for byte.
4. Tracker, docs and journal are updated, with all commits passing `archive/tools/check.ps1`.

## Status (2026-10-03)

1. Done: `[render] wireframe=1` (archive/hook/enhance/render.cpp) sets the game's own debug bit, renderer +0x1628 bit 1.
   The recovered Renderer_BeginScene then sets D3DFILL_WIREFRAME. Seen in lev_1_m1.
2. Done:
   - `[overrides] textures=1`: `overrides\textures\<name>.dds|.png` replaces a texture after the recovered
     Renderer_RegisterTexture (0x0050bb30) creates it. Seen with a PNG and with a DDS exported by bigtool.
   - `[overrides] models=1`: `overrides\models\<name>.skn` (a skin exported by bigtool, from any level) is opened
     by the recovered ObjectSystem_LoadSkins in place of the bank's skin, through the `g_skinChunkOverride` knob,
     which is null in the original. Seen with the pine trees replaced by the rocket tower.
   - `log_names=1` lists texture and model names.
3. Done: `tools/formats/bigtool.py` dump/rebuild for all 13 kinds, byte for byte on all 325 files
   (docs/formats/big-*.md).
4. Done: `archive/tools/tracker_sync.py` updates symbols/functions.csv (135 rows at S2+), and check.ps1 enforces it.
   Journal updated. check.ps1 is green.

Remaining gaps, recorded so they are not hidden:
- Model meshes cannot yet be edited as geometry. Writing a skin chunk from OBJ needs a chunk writer
  (docs/formats/big-skin.md section 6).
- 16 functions were skipped in the pilot and the renderer is far from fully recovered. Both are follow-up work
  under the main roadmap.
