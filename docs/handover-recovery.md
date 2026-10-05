# Handover: full decompilation by parallel static recovery

Read this with AGENTS.md, docs/decision.md, docs/recovery-guide.md and docs/patterns.md. It records how the
2026-10-05 recovery round works, every technique and pitfall learned in it, where things stand, and how to resume.
Live progress is never kept by hand: run `python tools/recovery_status.py` (per batch) and see symbols/functions.csv.

## 1. Goal and scope

- Human's direction (decision.md 2026-10-05): decompile all of the game's own code; then rebuild the game in a modern
  way. "When in doubt, decompile."
- Not recovered (fenced off by subsystem in symbols/functions.csv): `havok`, `qhull` (Havok 2.x, proprietary;
  boundary in docs/subsystems/physics.md), `d3dx`, `crt`, `zlib`, `thunk`. Game code inside the Havok ranges
  (`havok-ext`, `physics-glue`) IS recovered.
- Stages: S0 not recovered; S1 recovered statically and compile-checked (behaviour not yet verified, often because
  no recorded scenario executes it); S2 the comparison (tools/compare.py, all scenarios) passes with it hooked and
  the scenarios execute it; S3+ readability work.

## 2. The pipeline

1. Classify every function (done 2026-10-05: tools/merge_classify.py merged six classifier CSVs and the Havok map).
2. Batches: `tools/make_batches.py` splits the remaining S0 game code into ~20 KB batches per source directory into
   symbols/recovery_batches.json (key -> file src/<dir>/<key>.cpp, addresses). It refuses to overwrite (in-flight
   work depends on the assignment); `--force` only for a new round after everything is integrated.
3. Recovery agents: one per batch, prompt docs/recovery-agent-prompt.md with BATCH_KEY / BATCH_FILE filled in.
   Static only (no game runs, no full builds), one new file per batch, compile-check with tools/check_compile.ps1.
   Up to 8 in parallel worked well; they write early and append, so a stopped agent leaves a compiling partial file
   and can be resumed (the API rate limit stopped all 8 once; each was resumed from its transcript).
4. Integrate a finished batch: `python tools/mark_recovered.py --hooks src/<dir>/<key>.cpp keep` (tracker: name from
   the HOOK line, stage S1, subsystem kept), append the agent's original bugs to docs/original-bugs.md (mark
   **Deviation** where the recovery cannot reproduce the original, e.g. uninitialised stack).
5. Verify: build without the files still being written, `pwsh tools/build.ps1 -Exclude "game_07,ai_05"` (one
   comma-separated string), **check the DLL is fresh and contains the new names** (a failed build leaves the old DLL
   and a stale PASS means nothing), then `python tools/compare.py --scenario all`.
6. On a failure: `python tools/bisect_hooks.py --files <files> --scenario level` toggles hooks within those files
   (by and.ini, no rebuild) to the culprit; a crash: `python tools/symbolize.py --log work/instances/main/and.log`
   maps "hook dll +X" offsets to function and source line via the PDB.
7. Promote: `python tools/promote_verified.py --label <label>` moves S1 functions the scenarios execute to S2 (the
   rest stay S1: "not executed: compare cannot verify").
8. Commit: main only with a green tools/check.ps1 (needs git on PATH: C:\Program Files\Git\cmd). Work in progress
   goes to the branch `wip/full-decomp` (pushed) so nothing is lost; verified batches merge into main.

## 3. Tools added in this round

| Tool | Purpose |
| --- | --- |
| tools/merge_classify.py | merge classifier CSVs into the tracker (fills empty subsystems; evidence-backed corrections) |
| tools/make_batches.py | split S0 game code into recovery batches (symbols/recovery_batches.json) |
| tools/recovery_status.py | per-batch progress from files + tracker (`--markdown` for a table) |
| tools/mark_recovered.py | record a recovered file's HOOKs in the tracker (S1); `--hooks <file> keep` |
| tools/promote_verified.py | S1 -> S2 for functions the comparison executes, after a PASS |
| tools/symbolize.py | crash offsets in and.log -> function + source line (dbghelp + PDB) |
| tools/hook_room.py | hooked functions with fewer than 5 bytes before the next function (none so far) |
| tools/build.ps1 -Exclude | leave in-progress sources out of the build (hook/CMakeLists.txt AND_EXCLUDE) |
| tools/compare.py --only @file | hook-name lists too long for the command line (bisect_hooks.py uses it) |
| tools/tracker_sync.py | now keeps S1 and reads file-local `*_HOOK` wrappers |

## 4. Techniques and pitfalls (each one cost time once)

Binary / ABI:
- **Pushes that outlive a call** (docs/patterns.md): a callee ending in a plain `ret` while the caller never pops what
  it pushed means those pushes are the next call's arguments (renderer +0x68/+0x64 getters -> +0x5c). Misreading it
  crashed the first comparison. Check every callee's `ret` before mapping pushes to arguments.
- **symbols/abi.csv is often wrong in these ways**: stack-argument counts overstated (e.g. 0x004881c0 lists 8, reads
  0; 0x00457870 lists 3, reads 0; 0x004e2d90, 0x004e2e30); "ecx inputs" that are only `push ecx` local reservations
  (0x0046e2c0, 0x0042d0f0, 0x00493b70, `_stricmp` 0x005448ee); results in **st0** listed as eax/none (0x004e7880
  vec3 length, 0x004e78d0, 0x004e8040..0x004e8080, 0x004e7760, 0x004e7800, 0x00471970, 0x00475990, 0x004759e0,
  0x0046a6a0, 0x004f48d0, 0x0048a030). Generated orig:: wrappers would leave st0 on the x87 stack: call through a
  small asm shim that pops it (examples in src/ai/ai_06.cpp, src/game/game_01.cpp).
- **Tail jumps** ("no ret found"): recover as call-then-return when the jump leaves the caller's frame intact; the
  target's `ret N` decides who pops (hand-written entry stubs: src/renderer/render_r6.cpp, src/ai/ai_03.cpp
  0x0042d650 pops 4, src/ui/ui_03.cpp 0x00491b60).
- **Register inputs the caller never set on purpose** (upper bytes after `xor al,al`, leftovers): pass 0 or the
  known low byte; the callees checked so far read only the low byte.
- **EBP inputs** are unsupported by the generated stubs; 0x0043e710 is skipped (EBP + two x87 stack arguments).
- **functions.csv sizes are often short**: recover.py's disassembly then stops early; disassemble to the next
  function start. Some functions are missing entirely (0x004695d0, 0x00454530, 0x0042c080, callbacks only reached by
  pointer such as 0x00448db0 / 0x00489050); 0x004f00bb is padding before Renderer_Release 0x004f00c0.
- **Identical-code folding**: tiny game functions are shared with Havok and across vtables (0x00418600 ReturnZero in
  390 slots; 0x00412fa0, 0x00414220 called from Havok). Hooking them is fine (same code).
- **3-4 byte functions**: a hook writes 5 bytes; tools/hook_room.py confirms padding follows each (all OK so far).
- **Floats**: x87 at single precision (D3D9 and the Vulkan device set it): compute in float in the disassembly's
  order. fsin/fcos/fpatan: use asm helpers where exact bits matter (std:: double math may differ in the last bit).
- **Uninitialised stack in the original**: recovered frames are zeroed; where the original reads garbage the
  recovery differs (logged as **Deviation**; 0x00414110 query bytes, 0x00492d80 FindClose, 0x0042b480 ...).
- **Debug_Printf 0x00407ff0 is a bare `ret`** (compiled-out printf) called variadically with any argument layout;
  EAX passes through it unchanged and at least one caller relies on that.
- **Class names**: an object vtable's slot 1 returns a descriptor whose +0x14 is "CLASS(Name)". Vtables sit back to
  back in .rdata, so find a vtable's start from constructor `mov [reg], imm32` stores or the slot-1 getter, not by
  scanning backwards.
- ObjectSystem_CreateObject 0x0045f020: eax = optional instance name (often 0), ecx = type name, 9 stack arguments.
- Game+0x2c is the audio object (Ghidra calls it renderer_2c); Game+0xa8c the physics manager (+8 hkWorld); object
  +0x1c4 its Havok entity; entity +0x38 friction, +0x3c restitution.

Environment:
- **Display**: `[System.Windows.Forms.Screen]::AllScreens` = `WinDisc` -> every game run crashes (no display); TV
  attached but off -> ~4 fps (presentation throttled). Check before long runs.
- **Never run game code while the human is playing.** Static work only then.
- **Stale DLL**: after every build check the DLL timestamp and grep it for a new function name before comparing.
- Python scripts run from work/ pick up work/bisect.py instead of the stdlib module: keep tools in tools/.
- PowerShell mangles quotes in inline Python and array arguments to child pwsh processes: write scripts to files;
  pass lists as one comma-separated string.
- The API rate limit can stop all agents at once: their files are compiling partial states; resume each with
  SendMessage (it keeps its context) or start a fresh agent on the same batch (it must not redo existing HOOKs).

## 5. Where things stand (2026-10-05, evening)

- Rendering: all game-side rendering recovered (src/renderer/scene_pass.cpp, render_r0..r7.cpp), compare green,
  committed and pushed (f4014f6).
- Round 2 (all remaining game code): see `tools/recovery_status.py`. Integrated batches are at S1; audio_01, game_01,
  game_02, ui_01 passed the comparison (their executed functions are S2).
- **Open problem**: with physics_01 + anim_01 hooked, level/reset/play diverge at frame 39 (first frame after the
  level loads; frame hash differs, no crash). A bisection over those two files was running (fixed for long hook lists).
  Suspects: the zero-initialised query bytes in 0x00414110 (a documented deviation) or a recovery error.
- Not yet compared: game_03..06, ai_01..04, ai_06, loader_01, ui_02, ui_03, physics_02, audio_02 (and the batches
  still being written).
- Remaining batches (not started at the time of writing): ai_05, ai_07, ai_08, ai_09, anim_02, audio_03, engine_01,
  engine_02, game_09, input_01, loader_02, movie_01, physics_04, save_01 (some had agents just started).

## 6. How to resume

1. `python tools/recovery_status.py`: batches "not started" need an agent; "partial" ones either have deliberate
   skips (game_02 0x0043e710, ui_02 0x00466077 = not a function) or were stopped mid-way (start an agent with the same
   BATCH_KEY: it must read the existing file and continue, not redo HOOKs).
2. For each finished batch: steps 4-7 of section 2.
3. Fix the frame-39 divergence first (bisect, then read the culprit against the disassembly).
4. When every batch is recovered and compared: merge `wip/full-decomp` into main with a green tools/check.ps1, then
   write scenarios that execute the S1 functions (device lost/restore, env maps, other levels, menus, save/load) so
   they can reach S2.
