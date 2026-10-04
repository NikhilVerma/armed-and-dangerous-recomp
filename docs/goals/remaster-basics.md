# Goal: remaster basics

Requested by the human on 2026-10-04 ("for the start"). It runs after, or interleaved with,
`docs/goals/modern-renderer.md`; the dependencies are noted per item. The rules are the same as before:
features live in `hook/enhance/` behind `and.ini` switches that are off by default, the code they build on is
recovered first and passes the comparison test, and `src/` stays faithful.

## Features

1. **Proper aspect ratio, with UI that handles it.**
   - Hor+ field of view for any aspect: 16:9, 16:10, 21:9, 32:9.
   - Known so far: Renderer_InitDeviceState (0x00503b30) stores `width / height * 0.75` at renderer +0x1648,
     and the projection is built by Renderer_SetProjection.
   - The UI uses screen batches (Renderer_BeginScreenBatch, DrawGlyph, DrawText*) and probably 640x480 virtual
     coordinates.
   - Needs:
     - recovery of the UI layout code (the `pui_*` / `ui_*` objects in the ui bank, docs/formats/big-ui.md) and
       of the HUD
     - an anchoring scheme: HUD elements pinned to the screen edges, menus centred at 4:3 with a scaled
       background, a UI-scale option
2. **Menus that can be overridden or extended**, so the remaster options get their own page.
   - Needs recovery of the front-end/menu system: the pause menu seen in the play scenario (Continue, Save Game,
     Quickload, Restart, Options, Abandon) and the main and options menus.
   - Then a way to register extra pages and items from `hook/enhance/` (labels, toggles, sliders, lists) that
     read and write `and.ini`.
3. **Persistent decals and bodies.**
   - Find the limits and timers that remove decals (`fx_decal_*`, ParticlePool_Create pool sizes) and corpses
     (character removal or fade timers).
   - Add options for no removal, bigger pools and a cap by memory.
   - Needs the fx and character-lifetime code recovered. Watch the `.big` per-level budgets
     (docs/formats/big-fx.md).
4. **FOV.**
   - Slider plus Hor+ scaling. Needs the camera code; the Xbox prototypes name `src\camera.cpp`.
   - Separate first-person/sniper zoom handling.
5. **Tessellation.**
   - D3D9 has no hull or domain shaders, so this lands in the Vulkan backend.
   - PN-triangles or Phong tessellation on character and prop meshes (normals are in the vertex stream:
     docs/formats/big-skin.md 3.3), with distance-based factors.
   - Later: displacement from PBR height maps.
   - Depends on the Vulkan backend (modern-renderer step 3).
6. **Other basic QoL a remaster does:**
   - Frame rate decoupled from simulation: fix the frame-rate-dependent behaviour recorded in
     docs/original-bugs.md, behind a switch.
   - Borderless window, any resolution, a render-scale slider.
   - Anisotropic filtering, a texture LOD bias, and draw-distance and LOD-distance multipliers.
   - Raw mouse input, sensitivity separate per axis, invert, remappable keys, XInput controller support with
     analogue movement.
   - Subtitle size and background.
   - Skip intros and legal screens (already in `[startup]`).
   - Brightness and gamma that works in a window.
   - Audio device selection and a master volume.
   - Quick save/load hotkeys if the game's own save system allows it.
   - Alt-tab and device-loss safety (the reset scenario covers the device side).

## Done when, in the running game

- Each item has an `and.ini` switch and an entry on a remaster options page inside the game's own menus.
- The recovered code under each feature passes the comparison test.
- Screenshots or recordings show:
  - 21:9 with correct HUD and menus
  - FOV changes
  - decals and bodies persisting past their original limits
  - tessellated characters (Vulkan backend)
  - the options page
- Tracker, docs and journal are updated, and `tools/check.ps1` is green.
