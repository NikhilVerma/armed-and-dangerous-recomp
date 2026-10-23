# Archive: the decompilation and the DLL-injector era

**Not maintained.** Kept, with its full history, as the reference specification the Bevy engine in `engine/` is
written and checked against. New work goes into `engine/`.

## What this was

Before the engine, the project rebuilt Armed and Dangerous inside the original executable:

1. Every function of the PC `game.exe` (Steam build 251748) was catalogued in `symbols/functions.csv` (5,544
   functions), and every function of the game's own code was rewritten as C++ by reading its machine code: the
   recovered code in `src/` (3,496 functions; library code such as Havok, D3DX, the C runtime and zlib was left
   alone).
2. A hook DLL (`hook/`, loaded by the game as `dinput8.dll`) replaced the original functions with the recovered ones
   at run time, one function at a time, each behind its own switch.
3. Each replacement was verified against the running game: `tools/compare.py` (still in the top-level tools/) runs
   the game twice under a virtual clock with scripted input, once with the recovered functions switched on and once
   with them off, and compares every frame's Direct3D call stream. A function the scenarios execute and that keeps
   every frame identical moves to stage S2 ("verified"): 2,254 functions are; the rest are S1 (recovered and
   compile-checked, not executed by a scenario).
4. On top of that it grew a remaster of the original executable: `hook/enhance/` (resolution, aspect ratio, frame
   rate, FOV, persistent decals, physics limits, each behind an `and.ini` switch, off by default), a Vulkan renderer
   behind the recovered Direct3D 9 layer (`src/rhi/`), a 64-bit ray-tracing / upscaling helper process
   (`rthelper/`), FSR 3.1 (`hook/fsr/`) and a Jolt Physics mirror of the Havok world (`hook/physics/`).

The engine replaced that route (docs/decision.md 2026-10-13 and 2026-10-15): a clean Rust / Bevy rewrite with the
running game as the oracle and this recovered code as the explanation of what the game does and why.

## How the engine uses it

- Engine code cites the original's addresses; `archive/src` holds the recovered function for each, marked
  `HOOK(0xADDRESS, "Name")`. To read the specification of a behaviour: `grep -rn "HOOK(0x0047afa0" archive/src`.
- `python tools/struct_atlas.py` answers what an object field at an offset means, from this code.
- `python tools/port_queue.py` lists which recovered functions the engine ports, replaces, defers or leaves.
- The oracle tools in the top-level tools/ (`shot.py`, `oracle_diff.py`, `coverage_level.py`, `render_compare.py`)
  still drive the original game through this hook DLL to capture the reference behaviour. They need the DLL built
  (`pwsh archive/tools/build.ps1`, MSVC + Ninja, 32-bit) and an executable whose code the hooks can patch. The Steam
  executable's code is encrypted by its DRM wrapper; this project does not remove DRM and gives no instructions for
  it (docs/decision.md 2026-10-03). Playing the remaster needs none of this.

## Layout

| Folder | What |
| --- | --- |
| `src/` | the recovered game code, C++, one file per batch of functions (`src/<subsystem>/`), `addresses.h`; `src/rhi/` the renderer interface and its Direct3D 9 and Vulkan backends |
| `hook/` | the hook DLL: loader, hook table, test harness (virtual clock, scripted input, frame traces), `enhance/` (the remaster switches), `fsr/`, `physics/` (Jolt mirror), `and.ini` (every switch with its default) |
| `rthelper/` | the 64-bit helper process (ray-traced shadows, DLSS / XeSS / FSR) the 32-bit game talks to over shared memory |
| `third_party/` | XeGTAO, the FidelityFX Shadow Denoiser and ffx-api headers, unmodified, with their licences |
| `tools/` | tools that only served this route: building and deploying the DLL (`build.ps1`, `deploy.ps1`, `verify.ps1`, `check.ps1`), bisecting failing hooks, recording call traces (Frida), the Ghidra queries and exports, the recovery-round bookkeeping (`recover.py`, `recovery_status.py`, `mark_recovered.py`, `tracker_sync.py`), and the Vulkan backend's checks |

Paths inside the archived files were written before the move: there, `src/`, `hook/`, `rthelper/`, `third_party/`
and the tools listed above mean the same folders under `archive/` (the archived tools themselves were updated and
resolve paths from the repository root). The process is documented in docs/handover-recovery.md,
docs/recovery-guide.md, docs/patterns.md and the journal entries of 2026-10-03 to 2026-10-12.
