# 2026-11-03: ground detail and tessellated scenery

## What was done

Floors looked the worst in the remaster. They are painted textures stretched over large flat ground, so close to the
camera each texel covers two to three screen pixels at 1080p (measured with a debug shader: 0.33 to 0.66 texels per
pixel near Roman). Upscaled textures add only fine detail from the same painting, so they can't fix this alone.

Modern games solve it with a second layer: small tiling detail materials (bumps and grain) on top of the painted
colour. Two new settings do the same here. Both are on in the Remaster preset and off in the Original, and both have a
checkbox on the Detail page.

- **Ground Detail** (`ground_detail`). graphics/ground_detail.rs generates 10 tiling layers at startup in about 100 ms:
  snow, ice, rock, dirt, grass, moss, sand, stone, wood and metal. Each layer stores a bump map and a brightness
  change. graphics/ground_detail.json maps every texture the 24 exported landscapes use to one surface, sorted by
  hand from the descriptions in tools/retex/desc. gamematerial.wesl projects the detail from the three world axes at
  a 4-unit tile (about 0.75 m), mixed with a larger turned copy so the repeats are harder to see. It fades out with
  distance.
- **Tessellated Scenery** (`tessellation_scenery`). The Phong tessellation that characters used now also runs on the
  landscape and static objects. Rocks and hills round off.

## Evidence

- Before and after crops are in work/retex/snap/levels/detail/all.png, for snow, rock, moss and grass levels. The
  tessellation comparison is work/retex/snap/levels/tess/lev_4_m1/cmp.png.
- Frame time, 1080p, M5 Pro, median: lev_1_m1 8.9 to 9.0 ms, lev_2_m2 8.25 to 8.65, lev_4_m1 8.75 to 8.9.
- The first shader version cost 4 ms on lev_1_m1. It sampled all three projections at every pixel. Skipping planes
  that contribute under 2 %, using explicit-gradient samples, brought it to 0.1 ms with the same picture (99th
  percentile difference 0.7/255).
- The 4 unit tests in `cargo test ground_detail` pass. One of them is an ignored test that dumps the layers. The
  playtests pass: geom_remaster 6/6, geom_original 7/7 and gpu_snow 17/17.

## What was learned

- The first rock and ice layers used a plain cell pattern and looked like crazy paving. Bending the cells with noise
  and leaving out most of their edges reads as natural cracks.
- Brightness changes multiply the colour, and bumps change only the sun's light. On dark floors lit mostly by ambient
  light (lev_3_m3, lev_5_m2), the detail changes pixels by only 1 to 3 out of 255.
- Static meshes now keep their vertices on the CPU (`RenderAssetUsages::all()` in scene.rs), because scenery
  tessellation reads them.

## Turret intro (player feedback M16)

In the turret levels the intro zooms in on an empty turret shell. The turret has two models. The finest is an empty
shell for the view from inside, and the original draws it only when the camera is within 4 units of it. The next
model has Roman seated with the gun, and the original shows it from everywhere else. The Remaster always drew the
finest model, and LOD switching skipped models whose parts ride single bones, so the Original preset never switched
either.

graphics/geometry/lod.rs now switches bone-riding parts too. A first LOD whose cutoff is set below 8 counts as an
inside view (`LodSource::inside_view`), which the Remaster still leaves once the camera is outside it. The turret is
the only such object in the shipped levels. Before and after: work/retex/snap/turret/fly4/cmp.png. While the camera
passes through the glass for one frame, Roman's legs and seat show from inside. That follows the original's rule,
but no capture of the original confirms it.

The `--no-hud` flag also skips loading the mission (main.rs), so a level started with it has no turret, no mission
and no triggers. Shots of gameplay need to leave it out.

## Next

- The player's verdict in play, in motion.
- Detail for dark, ambient-lit floors: an occlusion term from the layer's height would darken its hollows under any
  light.
- Grass and sand could be stronger; snow and moss are the most convincing.

## Open questions

- Whether a 4-unit tile is right for every surface, or each layer needs its own scale.
