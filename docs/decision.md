# Decisions

Every target or approach decision, with the evidence and numbers that triggered it. Newest first.

## 2026-10-03: remote repository

- The human asked to push to GitHub: private repository `NikhilVerma/armed-and-dangerous-recomp`
  (remote `origin`). Agents push after each verified (compare: PASS) batch so work is not lost.
- Visibility stays private; making it public is the human's decision (legal risk, see handover s15).
- Never push anything from `orig/`, `work/` or `ref/` (the pre-commit guard enforces this).

## 2026-10-03: human's goals and order of work (overrides handover s1 "recover first, improve later")

The human set the project goals, in order:

1. **Explode and recompile.** Every function exported, tracked and in a state where it can be rebuilt
   with a modern compiler (S2 under the staged-understanding scheme, handover s9).
2. **Migrate piece by piece.** Replace original functions one at a time in the running game. This is
   Approach B (hook and replace) as the backbone.
3. **Basic enhancements early:** any resolution, HDR, unlocked frame rate, field of view, and similar.
4. Everything else after that, including (long term) replacing Havok with an open-source physics engine,
   step by step.

How this coexists with faithful recovery:

- Enhancements live in `hook/enhance/`, never in `src/` (recovered code). Each has its own switch in
  `and.ini`, default **off**.
- All oracle tests (boot, replay, differential) run with every enhancement off. A function in `src/` stays a
  faithful reproduction of the original until M5.
- Unlocking the frame rate is expected to expose the known speed defect (frame-rate-dependent timing). The
  fix belongs in `hook/enhance/` behind its switch, and the original behaviour is logged in
  `docs/original-bugs.md`.
- Havok: identify and fence off its code range now (tag `subsystem=havok` in the tracker). Replacing it is a
  later, separate workstream, with an interface layer drawn first.

## 2026-10-03: Xbox pre-release builds

- The human approved using the Hidden Palace pre-release builds (16 Jul, 3 Sep and 13 Oct 2003).
- The human downloads them; agents do not fetch third-party game copies. Place each one under
  `orig/xbox/<yyyy-mm-dd>/`.

## 2026-10-03: DRM gate on the Steam build

- **Evidence:** `orig/pc/GameData/game.exe` (SHA-256 `960df24c...5b45`) has a `.bind` section, its entry
  point is inside `.bind`, and `.text` has entropy 8.00. This is the SteamStub wrapper. See `docs/recon.md`.
- **Asked the human.** Options offered: a DRM-free executable from the human, or the Xbox build as target.
- **Decision (human):** keep the PC Steam build as primary target. The human unwraps their own copy with
  Steamless and places the output in `orig/pc/GameData/`. Agents never unpack or patch protected executables.
  The human also declined using a third-party download of the game.
- **Approach:** none chosen yet. Default hypothesis from the handover is Approach B on the PC build; M1
  decides from recon evidence and the 20-function spike.
- **Budget for M0/M1:** no fixed limit; report at each milestone.

## 2026-10-04: rendering backend API

- The human chose **Vulkan** for the new backend (docs/goals/modern-renderer.md).
- Build dependencies come from source: open-source Vulkan-Headers, volk and glslang from KhronosGroup on
  GitHub, fetched by CMake at pinned tags (like MinHook). No SDK installer; the GPU driver provides
  vulkan-1.dll.
- Validation layers need the LunarG SDK. Ask the human before downloading it.

## 2026-10-04: own Vulkan backend, not DXVK

- The human asked for whichever option keeps ray tracing, path tracing and PBR open. We write our own Vulkan
  backend for the subset of D3D9 the game uses.
- Ray tracing needs the scene's geometry, transforms and materials, so we must own the layer that sees every
  buffer and draw. DXVK would hide it.
- Layering:
  - a D3D9 COM facade, used by recovered code and by the statically linked D3DX
  - `src/rhi` (our interface)
  - the backends: a D3D9 pass-through, which must stay bit-identical, and Vulkan

## 2026-10-04: enhancements serve the decompilation

- The human: everything is for the north star, decompiling the game and then enhancing it.
- Rule:
  - An enhancement changes behaviour only through recovered code in `src/`.
  - That code passes the comparison test with the enhancement off.
  - Each such function exposes a named enhancement point (a function pointer, limit struct or scale) that is null,
    or equal to the original constant, by default.
  - The switch, the settings and the menu entry live in `hook/enhance/`.
  - When an enhancement needs a function that is not recovered yet, recover it first: Camera_SetupProjection,
    Decals_*, Corpse_CountDown, UiPass_Draw, GfxMenu_InitResolutionList, Renderer_SetGamma,
    Renderer_GetPixelWidth/Height.
- Exceptions:
  - Patching the original image is the last resort, guarded by PatchBytes with the expected bytes. Today: the two
    letterbox constants (0x005bcf24 / 0x005bcf28).
  - OS and API hooks (DirectInput, DirectSound, window placement) are not game code.

## 2026-10-04: letterbox off by default

- The human: disabling the letterbox should be the default. `[camera] letterbox` now defaults to 0, so the 3D view
  fills the screen.
- The comparison harness (tools/compare.py write_ini) pins `letterbox=1`, so the tests keep comparing against the
  original framing.

## 2026-10-04: remaster features on by default; every feature on the Remaster page

- The human: add all of these and future features as remaster settings (paginated if needed), on by default,
  tessellation included.
- Each setting has two defaults, the original game's and the remaster's.
  - A setting that and.ini does not name takes the remaster default, through RemasterRegister(setting,
    original, remaster) and ConfigIntDefault / ConfigFloatDefault (hook/src/config.h).
  - `[remaster] original_defaults=1` gives every setting its original value.
- Tests: the comparison harness (tools/compare.py write_ini) always writes `original_defaults=1`, so the
  decompilation tests keep running the faithful game. This replaces the letterbox pin. Screenshots of remaster
  features must pass `--set remaster.original_defaults=0`.
- Remaster defaults on:
  - Hor+, letterbox off, HUD and menus at 4:3 proportions, text following the resolution
  - tessellation, 16x anisotropy, shadow maps with soft (PCSS) penumbrae
  - a 1024-decal pool, bodies kept
  - brightness in a window, all resolutions, F5 / F9 quick keys
- Kept at the original value, because they are preferences rather than upgrades: mouse invert and speeds, render
  scale, display mode, tone mapping, FOV, frame limit.

## 2026-10-04: AO, modern fog, testing alongside the player

- Ambient occlusion: adopt XeGTAO (Intel, MIT) unmodified in third_party/XeGTAO rather than reimplement (human:
  "try not to reimplement it, try to adopt it"). Remaster default on. Modern exponential height fog replaces the
  game's linear fog in the remaster (default on); AO must never show through fog.
- Tests never touch the player's game folder (and.ini, options.opt): all test tools run in work/instances copies,
  which take the DLL from build/hook-release. Test runs keep running unfocused ([test] parallel=1).
- Full check only before a push; single scenarios between changes (human: checks too slow).

## 2026-10-05: ray-traced shadows through a 64-bit helper

- The game is 32-bit; NVIDIA's 32-bit Vulkan driver has no ray tracing. Asked: helper process vs compute-shader
  RT vs fixing shadow maps. Human: "64-bit RT helper". rthelper/ (rt_helper.exe) shares GPU memory and timeline
  semaphores with the game (rt_ipc.h); shadow maps remain the fallback.

## 2026-10-05: decompile everything except proprietary code; then a modern rebuild

- Human: "the goal is to do full decomp except for proprietary tech like havok or blink", "once done, then we can
  rebuild the game in a modern way", and "when in doubt decompile": recovered code should steadily replace the
  game's functions; enhancements are built on recovered functions (enhancement point in src/, switch in
  hook/enhance/).
- Excluded from recovery (library code, fenced by subsystem in symbols/functions.csv): `havok` and `qhull` (Havok
  2.x, proprietary; boundary in docs/subsystems/physics.md), `d3dx` (statically linked D3DX), `crt` (MSVC 7.1 runtime),
  `zlib`. "blink" read as Bink video: this build has none (movies play through DivX from a DLL, glue
  0x0050dcf0-0x0050e770, which is game code and is recovered). Game-written code inside the Havok ranges
  (`havok-ext`, `physics-glue`) IS recovered: it holds game behaviour (speed traps, custom collision agents).
- Order: rendering first (done statically 2026-10-05), then the remaining game subsystems; the modern rebuild starts
  once the game code is recovered. Replacing Havok (engine choice is the human's) comes with that rebuild.
- Static recovery without game runs is allowed (human was playing); such functions are stage S1 until
  tools/compare.py passes, then tools/promote_verified.py moves executed ones to S2.

## 2026-10-06: Havok replacement: Jolt Physics, staged

- Human: "can we try swapping the physics engine with an open source one? I know one which was used in horizon zero
  dawn but feel free to pick most compatible with havok". Chosen: **Jolt Physics** (Jorrit Rouwe, Guerrilla; MIT;
  pinned v5.6.0, fetched by hook/CMakeLists.txt). Closest to Havok's model: bodies with static/kinematic/dynamic motion
  (Havok fixed/keyframed/dynamic), broadphase layers + collision filters, contact listener, constraints and ragdolls,
  MeshShape (BVH) for MOPP, ConvexHullShape for the qhull path; supports 32-bit x86 Windows.
- Staged behind `[physics] engine` in and.ini (default `havok` = the original; tests keep it): `jolt_shadow` mirrors
  every Havok body into Jolt and measures per-step agreement without changing the game; `jolt` lets Jolt simulate and
  writes the results back into Havok's entity/motion fields (the ~40 game functions that read them keep working);
  later stages move contacts, filters, constraints and ragdolls, then drop Havok. Plan: docs/subsystems/physics-jolt.md.
- Jolt needs SSE2 and its math types depend on the instruction set, while the hook DLL is built /arch:IA32 (x87, for
  faithful float behaviour): Jolt and its bridge are a separate static library (hook/physics/) compiled with SSE2 and
  reached only through a plain C interface.

## 2026-10-08: DLSS and XeSS SDK licences accepted

- Asked: "may I download NVIDIA's DLSS SDK and Intel's XeSS SDK? Using them means accepting their licence terms on
  your behalf." Human: "Yes, both".
- Pinned: NVIDIA DLSS SDK v310.9.1 (NVIDIA RTX SDKs licence) and Intel XeSS SDK v3.0.2 (Intel Simplified Software
  Licence). They are cloned into `work/sdk` by `tools/fetch_aa_sdks.ps1` and never committed. Their runtime DLLs
  (`nvngx_dlss.dll`, `libxess.dll`) are copied next to the game by `tools/deploy.ps1`.
- FSR 3.1 stays the remaster default (`[render] aa=1`). DLSS / DLAA is `aa=2`, XeSS `aa=3`.

## 2026-10-11: tests only on request

- Human: "your tests take ages, pls don't run them unless i say so".
- Agents no longer run `tools/check.ps1` or `tools/compare.py` by default. They build, check the change with a
  targeted capture where useful, and say in the commit message that the oracle was not run. The full checks run
  when the human asks (and before any merge to main).
- Human: "we will run tests after human validation". Order: change, build, the human plays and validates it, then
  the tests run.

## 2026-10-13: a new engine, reusable across games

- Human: "my big goal is to remaster many of my fav old games so its something that needs to scale and ideally
  reusable engine with reusable functions and approaches", "keep the recovered C++ and link it in ... to get the
  initial MVP built", "get an MVP level working of the game in the new engine", with "modern toolchains, Jolt physics
  and more, feel free to reuse existing community plugins for menus and such".
- Human: "don't add fake or your own explosions pls, we need to use what the game does", "it should play as the game
  and enhance it rather than make it worse".
- Chosen: **Bevy 0.20** (Rust) as the shared engine in `engine/`, **Jolt Physics 5.6** through `oxijolt` (built from
  its vendored source), **bevy_egui** for menus. Game data is exported from the retail `.big` files by
  `tools/remaster_export.py` into a game-neutral scene format; behaviour comes from the game's own property sets,
  templates and fx scripts. Game logic not yet ported runs as labelled stand-ins until the recovered code replaces
  it (grunt AI, the player).
- The D3D9 hook path stays: it is the oracle for the recovered code and the shipping remaster until the new engine
  plays the whole game.

## 2026-10-13: the new engine stays a manual port, one verified piece at a time

- Human, after seeing look-and-feel gaps (single aim pose, flat materials, no terrain snow, dead grunts floating,
  vertical tracers, pickups): "i am ok with the most efficient approach, we have gotten surprisingly close with
  manual rewrites", "but i just need to ensure we do it piece by piece and each piece is properly ported".
- Rule for engine/ from now on: a piece (one system or behaviour) is ported from the recovered code with the
  function addresses cited in the source, then verified against the original before the next piece starts:
  a capture of the original (tools/shot.py) side by side with the engine's (tools/ui_compare.py), or a logged
  value from a playtest run compared with the recovered rule. Guesses stay labelled. A piece is not called done
  while its comparison shows a difference.
- Future levels' mechanics (jetpacks, capture the flag, escort missions, ...) are ported the same way, each as its
  own piece.

- Human, the same day, on what counts: "the subjective part means the new game should look and play the same OR
  better than old. the objective part means: no unexpected behaviour, bugs, crashes, glitches"; "if the score is
  poor but the game looks subjectively better then the score is irrelevant"; "it should look a faithful
  remaster/remake". Porting order: rendering, then player movement, the sticky bomb (homing), guns, zoom, the rest;
  verification by playing the same inputs in both (e.g. dismiss the briefing, walk forward 5 s) and comparing.
- Human, 2026-10-13: "this is such an old game, we can probably live with high LOD for all cases": the engine draws
  every object's finest LOD at all distances (the original switches by Object_SelectLod 0x00452f50; not ported).

## 2026-10-14: port by subsystem, each working in isolation; levels after

- Human: "Instead of vertical slices, let's do subsystems and ensure they work in isolation. And eventually, we
  will have enough of them built to start testing levels." Context: the game logic is about a third of the exe
  (symbols/functions.csv: AI 12 %, game 11 %, UI / audio / anim / camera / input / save the rest; libraries
  Havok / qhull / D3DX / CRT / zlib 42 %), all of it already recovered as C++ in src/. Porting one level
  mechanic at a time re-read the same code from cold for each feature and cost 200k-400k tokens per mechanic.
- Rule for engine/ from now on: one subsystem at a time (e.g. grunt AI, player, mission / triggers, weapons and
  projectiles, pickups, HUD, audio cues), ported in one pass from its recovered source files with addresses
  cited, scoped to the functions the levels reach (hook coverage of the original). Each subsystem gets its own
  isolation tests (headless playtests from saved states, or unit tests, with expect checks), added to
  tools/regress.py. Level playthroughs and look comparisons come once enough subsystems stand.
- This replaces the per-level mechanics inventory as the work order (docs/level1-mechanics.md stays as the
  checklist of what level 1 needs); the 2026-10-13 rule (each piece verified against the original, guesses
  labelled) still applies to each subsystem.

## 2026-10-14: the remaster's code is a base for the human's own games

- Human: "in future I would like to build my own games by borrowing mechanics from the decompiled games. So I want
  to make sure that whatever you do has a good enough code quality that it can be built upon ... hack on, build
  upon, extend upon, add mod support". A long-term instruction.
- Rule for engine/ (and shared tooling; src/ stays the faithful decomp, where "no restructuring for taste" still
  holds): behaviour stays faithful, structure is clean. Each mechanic is a self-contained module / Bevy plugin
  with a clear public API and no hidden coupling to Armed and Dangerous; game specifics live in data (properties,
  templates, tuning, mission scripts) so mods can change content without recompiling; names say what things do,
  with the original's addresses as provenance in doc comments; tests per mechanic double as usage examples;
  unavoidable hacks are labelled and logged as debt. This adds to the 2026-10-13 "reusable remaster engine" goal.

## 2026-10-15: two presets — Remaster for the human, Original for comparisons

- Human: "For your own comparisons, feel free to use the original game settings. For ME to play: you must have
  implemented proper remaster with the new textures, better shadows, better physics and all the goodies we agreed
  on." The Bevy engine had the original's look only; the remaster goodies (AI retextures in
  GameData/overrides/textures, AO with snow exempt, temporal AA via libraries, 16x anisotropic, no shadow pop-in,
  max draw distance, soft particles; docs/decision.md 2026-10-08, memory priorities-2026-10-08) existed only on the
  D3D9 / Vulkan hook route.
- Rule: the engine has a Remaster preset (default for a normal launch: what the human plays) and an Original
  preset (the original's textures and look) that every tool run uses automatically (--play, --snap, --camera,
  --bench comparisons, render_compare). Builds handed to the human must have the Remaster preset working; the
  4K 100 fps floor applies to it.
