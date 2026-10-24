# 2026-10-23: visual fidelity pass (branch visual-fidelity)

The human's gallery review (docs/comparisons.md, first version): snow "insane", no icy sheen, fog masking the image,
roofs black; then more levels in the gallery, a like-for-like hero, and walk videos of every level.

## Done

- **Measuring stick.** tools/compare_gallery.py `--stats` (luma mean / spread, mean saturation, snow specks of the
  original's 3D strip and the remaster at the same size) and `--variant NAME=key=value,...` (one more remaster render
  with changes over the JSON's; `original_preset` renders the engine's Original preset). Every number below is from
  these, at the original's dumped camera and play frame.
- **--graphics warnings.** graphics/mod.rs `resolve_checked`: an unknown key or a value of the wrong type is named on
  stderr ("--graphics: ...") instead of vanishing; compare_gallery echoes it.
- **Snow (G1).** Default 200,000 flakes of radius 0.03 (was 1,500,000 of 0.02). Over the village roof: 274 bright
  specks beyond the scene's own with the old default, 87 with the Original preset's snow cylinders (the original's
  look), ~90 now. The big soft blobs are the original's camera-locked particle emitter (weather.md "snow bubbles"),
  proven with `--debug-hide fx_pool`.
- **Tone curve and height fog (G3).** The washed-out look was mostly the ACES fit (Narkowicz on the game's display
  values: shadows lifted, white at 0.8): lev_1_m1 start luma spread / saturation 31.1 / 0.088 with it, 45.6 / 0.142
  without, the original 47.7 / 0.157. `tonemap_aces` is now a highlight roll-off (identity to 0.9 of white, a
  Reinhard shoulder above, hue kept). Height fog 0.6 / sun 0.3: the village's haze equals the linear fog's.
- **Environment cube maps (G2, the icy sheen).** The ice (n_ice_test, 7,400 landscape triangles on lev_1_m1), water and
  bronze are environment materials: stage words 0x03020184 / 0x00020199 decoded with Renderer_ApplyMaterial's
  SetTextureStageState order = `cube * TFACTOR + base * diffuse`, TFACTOR = record float +0x40 (0.3) as a grey
  (loader_01.cpp, the 0x7000 colour mix). The cube: the level's EnvMapMarker renders the scene once at load
  (EnvMapFreq 0 everywhere). The engine bakes it once per level and preset (`--bake-env-map`,
  tools/bake_env_maps.py; 11 levels have a marker) and samples it per pixel. First tried as a capture at level start
  with six extra Camera3d: every "the 3D camera" `single()` lookup in the game logic skipped those frames and the
  scripted run diverged (Roman and Jonesy elsewhere at the gallery frame); baking keeps play untouched.
- **Level fog defaults.** LevelProperties_Load reads fog_near / fog_far / colour with the property set p_globalsettings
  and the reader's defaults behind the level's own values; nine levels have no fog_near of their own and the engine
  drew them without fog. lev_3_m3's town through the gate is in its orange haze again.
- **Roofs (G4), 2026-10-24.** The lighting matched the original (Renderer_UpdateLightConstants 0x004ff2e0: c3 =
  material ambient x the lights' summed ambient + emissive); the conversion did not: gamematerial.wesl turned the
  GPU's sRGB-decoded samples to gamma with pow(1/2.2) and back with pow(2.2), and the sRGB curve's linear toe made
  every value under ~0.05 near black (the eaves at 2 / 255, the original 9-10). The sRGB curve both ways is
  byte-exact: 8.9 / 8.8. It also brought the non-snow levels' saturation onto the original's.
- **Snow puffs.** The original's camera-locked emitter, ported faithfully (count, size, speed, direction: the camera
  object's frame is view space, fxo_white sits at z -1.26); brighter over dark walls because the original blends in
  gamma space. Listed under Known differences.
- **Gallery** regenerated with three non-snow levels (lev_2_m3, lev_3_m3, lev_4_m1) and a side-by-side hero of the
  same view; **videos** (tools/compare_videos.py): see docs/comparisons.md and below.

## Learned

- A `Camera3d` added during play is not free in this engine: about 40 systems take "the" 3D camera with `single()`
  or `iter().next()`. Anything that needs an extra view during play needs those filtered, or must happen outside play
  (the env-map bake). The save thumbnail gets away with it because it lives two frames during a pause.
- The game's texture-stage op words: byte 0 op (bit 7: uses a texture), byte 1 COLORARG0, 2 COLORARG1, 3 COLORARG2,
  raw D3DTA values (0 diffuse, 1 current, 2 texture, 3 tfactor, 5 temp). fxlayer.wesl's LERP comment reads stage 2 as
  LERP by the mask; by this order the interpolant is CURRENT (arg0): worth a check against a capture.

## Open

- **G5, lev_5_m3's rain sheet.** Bisected with --debug-hide: the camera's fxo_wind_spray dome (fxlayer.wesl) and the
  rain domes fxo_rain_1..3 (fxadd.wesl) add far more light than their textures hold (fx_wind_spray: opaque alpha,
  colour max 41/255; fx_rain ~1/255, one mip). Matching the additions over the level's brightness (fog-based) did
  not change it: the cause is elsewhere in the fx material path. Not in the gallery.
- Environment materials in levels without a marker: the original would draw them untextured (docs/open-questions.md).
- The lev_1_m1 pub windows were not in any gallery view: not looked at.
