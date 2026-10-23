# Original vs remaster: same camera, same moment

The original Armed and Dangerous (2003, PC Steam build) next to the Rust / Bevy remaster (`engine/`). Each pair is
the same play frame of the same scripted run, rendered from the same camera:

- **Before**: the shipped `game.exe` itself (D3D9), run off-screen and muted by the test harness, every recovered
  function off except `Camera_SetupProjection` 0x0040f6e0 (recovered, verified identical by `tools/compare.py`),
  through which the harness records the camera it dumps. 1280x960 (1600x1200 for the close-ups), one of the game's
  own 4:3 modes. The original draws its 3D view into a letterbox strip; that strip is what is shown, except in the
  widescreen pair.
- **After**: the remaster, headless, with the settings below, held at the original's dumped view matrix
  (`--camera`, `engine/src/camdump.rs`) and given the same scripted input from the briefing on (the playtest
  "oracle" section, `engine/src/oracle.rs`), screenshot at the same play frame.

Both games show their own HUD. Characters and effects can differ in pose or timing: the AI and effects draw from
their own random streams, so the remaster cannot replay the original's rolls (docs/remaster-engine.md, "Oracle
runs"). None of the "before" images use the engine's Original preset; all are the original executable.

## Gallery

### Snow canyon, level 1 start

![lev_1_m1 start](images/compare/l1_start.jpg)

The first view of lev_1_m1, 60 frames after the briefing. The remaster replaces the original's snow (textured
cylinders around the camera whose UV scrolling reads as falling snow) with GPU snowfall: 1.5 million flakes
simulated in a compute pass, falling through gusting wind and settling on visible surfaces (`graphics/snow.rs`).
Also visible: Roman's sun shadow on the snow (4 cascades of 8192 px, PCSS soft edges), exponential height fog with
sun glow in place of the level's linear fog (`graphics/height_fog.rs`), XeGTAO ambient occlusion and the retextured
landscape.

### Village

![lev_1_m1 village](images/compare/l1_village.jpg)

The village houses. Cascaded shadows (Roman's on the snow), ambient occlusion under the eaves and where the walls
meet the ground, retextured walls, roofs and barrels, TAA with sharpening and ACES tone mapping.

### Village street

![lev_1_m1 village street](images/compare/l1_village_arch.jpg)

The street towards the ruined arch: the houses and Roman cast shadows across the street; the distant arch sits in
height fog instead of linear fog.

### Courtyard guards

![lev_1_m1 courtyard](images/compare/l1_courtyard.jpg)

The courtyard's guards. Characters are Phong-tessellated once at load (level 6, crack-free across UV seams,
`graphics/geometry/tessellation.rs`), always drawn at their finest LOD, and lit with ambient occlusion and soft
shadows. Poses and positions differ: the guards run their own AI rolls.

### Retextures, close up

![house close-up](images/compare/l1_house_closeup.jpg)

A crop of a 1600x1200 capture. The remaster's textures are AI retextures of the originals (a SeedVR2 canvas
re-rendered by Qwen Image, docs/retexture.md), then colour-locked to the original by `tools/remaster_textures.py`:
shrunk back to the original's size each one is the original texture; only detail finer than one original texel is
new. Normal maps and data textures (blend masks, lightmaps) stay original. Not a rendering difference: the archer
stands in the upper window in the original's frame only (AI timing).

### Roman, close up

![Roman close-up](images/compare/l1_roman_closeup.jpg)

Roman's tessellated mesh and his soft sun shadow on the snow. His pose differs at this frame (the original has him
raising the rifle).

### Widescreen

![4:3 letterbox vs 16:9](images/compare/l1_widescreen.jpg)

The original renders a 4:3 picture with a 1.72:1 letterboxed 3D view inside it. The remaster fills a 16:9 screen
with Hor+ (the original's vertical field of view, wider horizontally) and no bars. Each side here is its own follow
camera at the same play frame (not the dumped camera), 1280x960 against 1920x1080.

### Water mill, a later level

![lev_4_m3 water mill](images/compare/l4m3_mill.jpg)

lev_4_m3, the water-mill village, 5 s after the briefing: a different palette from the snow levels. Height fog
thickens towards the far mill house, ambient occlusion grounds the rock and the path's edges, ACES tone mapping
compresses the bright water and sky, and the leaves and walls are retextured.

## Hero image

![preview](images/preview.jpg)

`docs/images/preview.jpg`: the village pair as a diagonal split (original left, remaster right), 1600 px wide.

## Settings used

"After" is the Remaster preset with every remaster feature on at its highest setting, ray tracing off (the engine
has no ray-tracing option) and motion blur off for stills. The exact changes, as passed to the engine
(`tools/compare_gallery.json`, `remaster.graphics`):

```
and_remaster.exe --background --size 1280x744 --play <shot>/remaster.json --camera <shot>/original.camera.json
  --preset remaster --graphics remaster_textures=true,anti_aliasing=taa,alpha_coverage=true,ambient_occlusion=true,
  ao_quality=3,ao_contact=0.6,shadow_map_size=8192,shadow_cascades=4,shadow_distance=1.0,soft_shadows=true,
  fog_modern=true,fog_sun=0.6,tessellation=true,tessellation_level=6,terrain_detail=16,lod_max=true,lod_scale=8,
  aa_mip_bias=-1,tonemap_aces=true,dither=true,snow=true,snow_amount=1500000,shutter_speed=0
```

(`--size 1920x1080` and no `--camera` for the widescreen pair; `--size 1600x930` for the 1600x1200 close-ups.)

| Feature | Setting |
| --- | --- |
| AI retextures | `remaster_textures` on |
| Anti-aliasing | TAA + contrast-adaptive sharpening 0.35 (preset). Smooth foliage (alpha-to-coverage) needs MSAA, and MSAA turns ambient occlusion off in this engine (Bevy's SSAO), so TAA was used; `alpha_coverage` is set but has no effect under TAA |
| Ambient occlusion | XeGTAO, quality 3 (ultra), radius 24, contact shadows 0.6 |
| Shadows | 4 cascades of 8192 px over the whole drawn distance, PCSS soft shadows (softness 6), shadow strength 0.55 (preset) |
| Fog | exponential height fog with sun glow 0.6 |
| Geometry | tessellation level 6 (the maximum), terrain detail 16x (the maximum), finest LOD everywhere, LOD distances x8 |
| Texture sharpness | mip bias -1 under TAA, 16x anisotropic filtering on world, decal and effect textures (preset) |
| Post | ACES tone mapping, dither, bloom 0.08 (preset); motion blur off (`shutter_speed=0`) |
| Snow | GPU snowfall, 1,500,000 flakes |
| Ray tracing | none (the engine has no ray-tracing path) |

## Regenerating

```bash
# build the engine into your own target dir (never engine/target, the player's copy)
cd engine && CARGO_TARGET_DIR=target-wt-shots cargo build --release && cd ..
python tools/compare_gallery.py --list                 # the shot list
python tools/compare_gallery.py                        # every shot: original, remaster, compose (~45 s a shot)
python tools/compare_gallery.py --only l1_village      # one shot
python tools/compare_gallery.py --skip-original        # re-render the remaster only (after an engine change)
python tools/compare_gallery.py --compose-only         # re-compose from the captures in work/gallery/
```

Shots are defined in `tools/compare_gallery.json`: level, scripted keys (the hook's `frame:KEY*held` form, counted
from the briefing as `tools/oracle_diff.py` does), the play frame, an optional warp for Roman, a crop, a capture
size, and the remaster's settings. The original runs one instance at a time in its own game instance
(`work/instances/gallery`), off-screen and muted, and waits while the player's own game is running. Raw captures
(PNG, camera dumps, logs) stay in `work/gallery/<shot>/` and are never committed; the composed JPEGs are kept under
400 KB.

Not in the set yet: an explosion pair. A sticky bomb thrown at the level start lands out of view or as the red
marker puff only at the frames tried, and the keg blasts need aim that the scripted input does not reproduce; a
blast pair needs a scripted moment that frames the fireball in both games.
