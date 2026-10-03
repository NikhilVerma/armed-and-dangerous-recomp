# Goal: renderer and assets recovered

Repo `C:\Users\nikhi\and-decomp`. Target `orig/pc/GameData/game.unpacked.exe` (SHA-256 `f6a690a4…`).

## Outcome

Decompile the renderer and the data-loading code into compilable C in `src/`, running in place of the original
through the hook DLL, so that wireframe and texture/model replacement work. This is the foundation for the
remaster: a modern backend, then DLAA, then ray tracing.

## Scope

- **Renderer:** the Direct3D 9 layer around the `Renderer` object: device setup, render states, textures,
  vertex and index buffers, shaders from `shaders.dat`, frame and draw calls.
- **Loading:** the `filesys.cpp` file system, all 13 `.big` level archive kinds, textures, models and skins,
  animation.

## Build first, in this order

1. **Generated calling-convention layer.** Generate C-callable wrappers and entry stubs for every function from
   `tools/reginputs.py` plus runtime `abi.csv`. These replace hand-written asm shims.
2. **Automated comparison test.** A scripted, repeatable start-up → menu → `lev_1_m1` run
   (`[startup] load`, `[test] fixed_dt_ms`, a fixed random seed, scripted input). Its call trace and per-frame
   state hash must match the original game's. Wire it into `tools/check.ps1`.
3. **Ghidra types.** Seed them from `symbols/types/data_members.csv`, then re-export the decompiled code.
4. **100-function pilot.** Report pass rate and tokens per function, then ask the human before fanning out to
   parallel agents.

## Rules

- Each recovered function replaces the original through `HOOK()` and has an `and.ini` switch.
- A function counts as recovered only when it passes the comparison test.
- `src/` stays faithful to the original. Every addition lives in `hook/enhance/`, switchable in `and.ini` and
  off by default.

## Done when, in the running game

1. `[render] wireframe=1` draws wireframe using the recovered renderer code.
2. A loose PNG/DDS dropped into an override folder replaces the matching `.big` texture through the recovered
   loader, and a replacement model loads the same way.
3. Command-line tools dump and rebuild every `.big` kind, rebuilding each one byte for byte.
4. Tracker, docs and journal are updated, with all commits passing `tools/check.ps1`.
