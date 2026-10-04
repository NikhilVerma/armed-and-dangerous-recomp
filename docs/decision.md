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
