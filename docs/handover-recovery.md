# Handover: full decompilation by parallel static recovery

Read this with AGENTS.md, docs/decision.md, docs/recovery-guide.md and docs/patterns.md. It records how the
2026-10-05 recovery round works, every technique and pitfall learned in it, where things stand, and how to resume.
Live progress is never kept by hand: run `python archive/tools/recovery_status.py` (per batch) and see symbols/functions.csv.

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

1. Classify every function (done 2026-10-05: archive/tools/merge_classify.py merged six classifier CSVs and the Havok map).
2. Batches: `archive/tools/make_batches.py` splits the remaining S0 game code into ~20 KB batches per source directory into
   symbols/recovery_batches.json (key -> file archive/src/<dir>/<key>.cpp, addresses). It refuses to overwrite (in-flight
   work depends on the assignment); `--force` only for a new round after everything is integrated.
3. Recovery agents: one per batch, prompt docs/recovery-agent-prompt.md with BATCH_KEY / BATCH_FILE filled in.
   Static only (no game runs, no full builds), one new file per batch, compile-check with archive/tools/check_compile.ps1.
   Up to 8 in parallel worked well; they write early and append, so a stopped agent leaves a compiling partial file
   and can be resumed (the API rate limit stopped all 8 once; each was resumed from its transcript).
4. Integrate a finished batch: `python archive/tools/mark_recovered.py --hooks archive/src/<dir>/<key>.cpp keep` (tracker: name from
   the HOOK line, stage S1, subsystem kept), append the agent's original bugs to docs/original-bugs.md (mark
   **Deviation** where the recovery cannot reproduce the original, e.g. uninitialised stack).
5. Verify: build without the files still being written, `pwsh archive/tools/build.ps1 -Exclude "game_07,ai_05"` (one
   comma-separated string), **check the DLL is fresh and contains the new names** (a failed build leaves the old DLL
   and a stale PASS means nothing), then `python tools/compare.py --scenario all`.
6. On a failure: `python archive/tools/bisect_hooks.py --files <files> --scenario level` toggles hooks within those files
   (by and.ini, no rebuild) to the culprit; a crash: `python archive/tools/symbolize.py --log work/instances/main/and.log`
   maps "hook dll +X" offsets to function and source line via the PDB.
7. Promote: `python archive/tools/promote_verified.py --label <label>` moves S1 functions the scenarios execute to S2 (the
   rest stay S1: "not executed: compare cannot verify").
8. Commit: main only with a green archive/tools/check.ps1 (needs git on PATH: C:\Program Files\Git\cmd). Work in progress
   goes to the branch `wip/full-decomp` (pushed) so nothing is lost; verified batches merge into main.

## 3. Tools added in this round

| Tool | Purpose |
| --- | --- |
| archive/tools/merge_classify.py | merge classifier CSVs into the tracker (fills empty subsystems; evidence-backed corrections) |
| archive/tools/make_batches.py | split S0 game code into recovery batches (symbols/recovery_batches.json) |
| archive/tools/recovery_status.py | per-batch progress from files + tracker (`--markdown` for a table) |
| archive/tools/mark_recovered.py | record a recovered file's HOOKs in the tracker (S1); `--hooks <file> keep` |
| archive/tools/promote_verified.py | S1 -> S2 for functions the comparison executes, after a PASS |
| archive/tools/symbolize.py | crash offsets in and.log -> function + source line (dbghelp + PDB) |
| archive/tools/hook_room.py | hooked functions with fewer than 5 bytes before the next function (none so far) |
| archive/tools/build.ps1 -Exclude | leave in-progress sources out of the build (archive/hook/CMakeLists.txt AND_EXCLUDE) |
| tools/compare.py --only @file | hook-name lists too long for the command line (bisect_hooks.py uses it) |
| archive/tools/tracker_sync.py | now keeps S1 and reads file-local `*_HOOK` wrappers |

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
  small asm shim that pops it (examples in archive/src/ai/ai_06.cpp, archive/src/game/game_01.cpp).
- **Tail jumps** ("no ret found"): recover as call-then-return when the jump leaves the caller's frame intact; the
  target's `ret N` decides who pops (hand-written entry stubs: archive/src/renderer/render_r6.cpp, archive/src/ai/ai_03.cpp
  0x0042d650 pops 4, archive/src/ui/ui_03.cpp 0x00491b60).
- **Register inputs the caller never set on purpose** (upper bytes after `xor al,al`, leftovers): pass 0 or the
  known low byte; the callees checked so far read only the low byte.
- **EBP inputs** are unsupported by the generated stubs; 0x0043e710 is skipped (EBP + two x87 stack arguments).
- **functions.csv sizes are often short**: recover.py's disassembly then stops early; disassemble to the next
  function start. Some functions are missing entirely (0x004695d0, 0x00454530, 0x0042c080, callbacks only reached by
  pointer such as 0x00448db0 / 0x00489050); 0x004f00bb is padding before Renderer_Release 0x004f00c0.
- **Identical-code folding**: tiny game functions are shared with Havok and across vtables (0x00418600 ReturnZero in
  390 slots; 0x00412fa0, 0x00414220 called from Havok). Hooking them is fine (same code).
- **3-4 byte functions**: a hook writes 5 bytes; archive/tools/hook_room.py confirms padding follows each (all OK so far).
- **Floats**: x87 at single precision (D3D9 and the Vulkan device set it): compute in float in the disassembly's
  order. fsin/fcos/fpatan: use asm helpers where exact bits matter (std:: double math may differ in the last bit).
- **Uninitialised stack in the original**: recovered frames are zeroed; where the original reads garbage the
  recovery differs (logged as **Deviation**; 0x00414110 query bytes, 0x00492d80 FindClose, 0x0042b480 ...).
- **Debug_Printf 0x00407ff0 is a bare `ret`** (compiled-out printf) called variadically with any argument layout;
  EAX passes through it unchanged and at least one caller relies on that.
- **Class names**: an object vtable's slot 1 returns a descriptor whose +0x14 is "CLASS(Name)". Vtables sit back to
  back in .rdata, so find a vtable's start from constructor `mov [reg], imm32` stores or the slot-1 getter, not by
  scanning backwards: require a code reference to the candidate start (a plain backward scan called 0x0047fc90
  "PickupObject slot 0x11d"; it is SharkGunProjectile slot 0x2f).
- ObjectSystem_CreateObject 0x0045f020: eax = optional instance name (often 0), ecx = type name, 9 stack arguments.
- Game+0x2c is the audio object (Ghidra calls it renderer_2c); Game+0xa8c the physics manager (+8 hkWorld); object
  +0x1c4 its Havok entity; entity +0x38 friction, +0x3c restitution.

- **Trace coverage gap (likely)**: the recorded traces (work/traces, used by recover.py and promote_verified.py for
  "executed in recorded runs") seem to miss virtual calls: per-frame input virtuals (0x004f8b20, 0x004f8f00,
  0x004e4d40) show as never executed. So "not executed" overstates; many S1 virtual methods are probably exercised
  by the scenarios and could be promoted once the tracer records indirect calls (check archive/tools/record.py).

Harness (2026-10-06, verification phase):
- **Hook install is fast now**: HooksInstallAll reads the [hooks] section once and enables all hooks with one
  MH_ApplyQueued (it was GetPrivateProfileInt + MH_EnableHook per hook: ~2.5 minutes for 3,500 hooks, now ~1 s).
  A full menu-scenario bisection over all 117 files takes ~15 minutes.
- **Crash in a trampoline** is named in and.log ("in the trampoline ... of 0x... Name"): the original's first
  instructions faulted, so a recovered caller passed it bad inputs.
- **Enhancements must wrap recovered functions with HookWrap, never HookAt on the same address** (MinHook refuses
  the second hook, MH_ERROR_ALREADY_CREATED, and the recovered function silently stays unhooked). The remaster menu
  (Text_Lookup, UI_Init, Hud_CreateLevelUi) and [test] trace_fatal (LoadingScreen_ErrorLoop) were converted.
- **Promotion uses hook coverage**: `python tools/coverage.py` (DLL counting stubs: counts virtual and indirect calls,
  and recovered functions, which archive/tools/record.py skips) then `archive/tools/promote_verified.py --coverage work/coverage.json`.

- **Hook names come from two forms**: `HOOK(0x..., "Name", ...)` and file-local wrapper macros such as
  `R2A_HOOK(0041bd30, Name, ...)` / `R7_PIXEL_HOOK(...)` (address without 0x, unquoted name). compare.py and
  bisect_hooks.py missed the macro form until 2026-10-06: those 119 hooks stayed on in the baseline as well, so they
  were never compared. A new wrapper macro must match `^\s*[A-Z0-9_]+_HOOK\(([0-9a-f]{8}),\s*(\w+)`.
- **Coverage mode and self-branching originals**: a counting stub patches the original's first 5 bytes, so originals
  with a branch back into those bytes (0x004643d0, 0x00413d40) crash when switched off: coverage.py passes them as
  `[test] coverage_skip` (list cached in work/coverage_unsafe.json; recomputed with the .venv python).
- **Recurring recovery mistakes found by the comparison** (2026-10-06, 12 functions): stack arguments swapped in the
  _Abi wrapper (0x00462060, 0x004e2910, 0x00403fb0 fields), eax vs stack swapped (0x004f1140), a wrong buffer passed
  (0x0046ed30), the full value used where the original masks (kind & 1, 0x00472cc0), x87 results rounded to float
  too early (fcos 0x00408ee0 / 0x00409ec0, acos/asin 0x00472cc0 / 0x004735f0), and `(int)float` where the original
  calls its own __ftol2 0x0056a7c8 (64-bit fistp: NaN / huge values differ; 0x0048ed90). Check these first.
- **"Bad Image" 0xc0e90002 on a fresh build**: once Windows refused to load a just-built dinput8.dll (a hidden
  system dialog, the game hangs and writes no and.log). Rebuilding (touch a source) fixed it. Symptom in compare:
  "0 frames in 0s". Close the dialog (UI Automation, "game.unpacked.exe - Bad Image") before rerunning.

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

## 5. Where things stand (2026-10-06)

- **Static recovery is complete.** All 37 batches of round 2 are recovered, plus the functions Ghidra never identified
  (archive/src/engine/missing_01.cpp, archive/src/renderer/missing_02.cpp) and the cleanup items. Game code (libraries excluded):
  3,496 functions, S0 0-1 (0x004193a0 last), S1 2,723, S2 768, S3 4 (`python archive/tools/recovery_status.py`).
- Rendering was compared green before round 2 (f4014f6 on main). In round 2 only audio_01, game_01, game_02 and ui_01
  have been compared (green). **Everything else is S1: compile-checked, never run.**
- **Open problem**: with physics_01 + anim_01 hooked, level/reset/play diverge at frame 39 (first frame after the
  level loads; frame hash differs, no crash). The bisection over those two files was interrupted (no game runs while the
  human plays); rerun: `python archive/tools/bisect_hooks.py --files archive/src/physics/physics_01.cpp archive/src/anim/anim_01.cpp --scenario
  level` on a freshly built DLL (tools/compare.py now ignores names the DLL lacks).
  Suspects: the zero-initialised query bytes in 0x00414110 (a documented deviation) or a recovery error.
- physics_03 / physics_04 showed 31 "S0" functions in archive/tools/recovery_status.py: they are Havok library code fenced
  after the batches were made (hkInertiaTensorComputer, display builders, three Havok singletons, the hard key-frame
  utility; evidence in docs/subsystems/physics.md). recovery_status.py now counts fenced rows separately (2026-10-16).
- 0x004eb5d0 ECX question: resolved statically (every original call site passes edx = 0, so the ECX slot is never
  read); see docs/original-bugs.md.
- The trace coverage gap (section 4) means "not executed" is probably overstated for virtual methods.

### Cleanup list (after the round)

- Done: 0x0043e710 recovered as Character_DropPickup (archive/src/game/cleanup_01.cpp; EBP input spurious; `_CIpow` shim);
  game_02 now calls it directly. 0x00447040 (GoliathObject_Update) was already recovered and tracked in ai_07.
- Functions missing from the tracker: archive/tools/find_missing_functions.py + agent reports (work/classify/missing_*.csv);
  recovered into archive/src/engine/missing_01.cpp (tracker rows to add from that agent's report).

## 6. How to resume (verification phase)

1. Build everything (`pwsh archive/tools/build.ps1`), check the DLL is fresh, run `python tools/compare.py --scenario all`.
   Expect failures: round 2 was never run. Bisect per subsystem directory (`archive/tools/bisect_hooks.py --files archive/src/<dir>/*.cpp`),
   fix each culprit against its disassembly, repeat until green; then `archive/tools/promote_verified.py`.
2. Start with the frame-39 divergence (physics_01 + anim_01).
3. Fix the tracer so it records virtual / indirect calls (trace coverage gap), re-record, re-promote.
4. Write scenarios that execute the remaining S1 functions (device lost/restore, env maps, other levels, menus,
   save/load, movies, input remapping) so they can reach S2.
5. Merge `wip/full-decomp` into main with a green archive/tools/check.ps1. Then the modern rebuild (docs/decision.md).
