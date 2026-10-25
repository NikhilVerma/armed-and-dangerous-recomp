# 2026-10-25: texel density (blurry walls, blobby snow ground) and the snow amount

Branch worktree-agent-a2cd06b8f6b00a709 (from main + fix/visual-bugs-1024), build engine/target-wt-tex. Evidence in
work/texdensity/ (same-camera captures per gallery shot in work/texdensity/<shot>/, sheets and crops at the top).

## Cause 1: every map read the first uv set (the main one)

Each material record says which uv set each map is sampled with: byte +0x32, two bits per map (base, normal map,
second texture, mask). RenderModes_Load (0x00489b40) passes them to the stages with their textures. The stage
addressing (0x00504ef0) then packs set + 1 into the vertex shader's key, and the shaders.dat programs read
TEXCOORD<set>. Key "2100212" is the bump shader with the base on TEXCOORD0 and the normal map on TEXCOORD1; key
"6521112" is the terrain with its mask on TEXCOORD1. Each name was matched to its dumped bytecode, and the oT
outputs of 46 vertex shaders were tabulated. The exporter read two uv sets and the engine sampled every map with
set 0 (the mask with set 1).

Survey of all 24 levels:
- 0x04, the plaster normal map on set 1: v_archway_a, v_house_1f_a, boulder_*. Here uv1 runs -5.8..9.8 where uv0
  runs 0..1.8, so the map was stretched ~5x into blotches. This is the courtyard and village walls.
- 0x40, the mask on set 1: the terrain, as the engine already did.
- 0x90, the second texture on set 1 and the mask on set 2:
  - the snow domes fxo_snow / fxo_snow_2. Their second flake layer runs on uv1 at 1.5x / 2x the density, and the
    wind-spray mask runs once over the dome on uv2. Both were read on set 0, so the mask tiled 72x.
  - the ground of lev_3_m3 and lev_bonus_m1. Their whole-level mask (uv2 0..1) tiled ~7x as dark rectangles and
    lines (found by the r4 agent).

Fix:
- skin.py decodes all four uv sets.
- remaster_export.py writes material "uv_sets" and submesh "uvs2".
- tools/remaster_patch_materials.py added both to all 24 shared exports in place. Nothing else was touched (no
  textures_remaster). The bytes are identical to a fresh export.
- The game material samples each map on its set; the third set is bound at vertex location 10, from the mesh's own
  buffer.
- The fx layer material has a vertex stage that passes three sets.

Measured (l1_courtyard, same camera, wall crop, fine detail = mean |image - blur 2px|): original 6.17, before 2.93,
after 6.26 (Original preset), 6.39 (Remaster without retextures), 7.01 (Remaster). Sheets:
sheet_l1_courtyard.jpg, sheet_l1_house_closeup.jpg, sheet_l1_village.jpg, sheet_l3m3_town.jpg (the town's ground now
as the original's), sheet_s3_lev_bonus_m1.jpg; crops_l1_courtyard_{wall,floor,snow}.jpg.

Ruled out: camera FOV/distance (the same-camera captures put the snow ripples at the same period as the original's);
the exporter's uv scale/offset (the stream block's per-set scale/offset are the vertex shaders' c18..c21); mips.
Original preset with every Remaster setting added one at a time: none blurs except terrain_detail and the retextures
(g_floor_ovar.jpg).

## Cause 2: tangents (r4 item 5)

Bevy's MikkTSpace tangents average across shared vertices where the uvs turn per triangle (lev_1_m3's off-path
snow). The original lights in its stored frame, (L.v15, L.v14, L.normal). Checked: the stored vectors are dP/du and
dP/dv of the normal map's set (cos 0.98 / 0.99 on the archway's set 1). They are now exported ("tangents": unit
tangent + binormal side) and used. lev_1_m3 frame 120 in the Original preset matches the original's facets
(i_l1m3_tangents_orig.jpg).

## Cause 3: the snow ground near the camera (lead's t1 floor crop)

The Original preset matches the original. In the Remaster preset the terrain detail layer (0.6 strength; the gallery
used 16x) outweighed the snow's own ripples and gave soft lumps under an embossed grain. Its strength is now 0.25,
and the gallery uses 8x like the preset (g_floor_rvar.jpg). The remaining softness comes from the retextured
b_level1_ground (see below).

## Snow amount

The GPU snowfall alone added a tenth of the domes' brightening over the near walls (difference against an FXSKIP
no-snow render: mean add 0.74 vs 6.88), as small hard specks. The domes, drawn right now, carry the original's soft
flakes. The Remaster keeps them under the snowfall (SnowTuning::keep_cylinders, default true). fxo_glow_red stays
muted (player G6). crops_l1_courtyard_snow.jpg and the house sheet show the after state.

## Retextures worse than the original (not regenerated; texture sheets k_retex_l1_a.jpg, k_retex_l1_b.jpg)

- b_level1_ground (the snow ground): washed out; the grey patches and grain are gone. This is the Remaster floor's
  remaining softness.
- b_dirty_wall: feathery vertical brush strokes over the rust stains (j_retex_wall.jpg; the lead saw it too).
- k_t_plaster_wall_red: flattened into a cartoon fill, with no plaster texture.
- n_t_metal_rust and n_t_metal_rust_low: invented bright scratch lines that are not in the original (corr 0.86).
- n_t_wood_planks_dark: invented V-shaped cracks.
- n_t_bronze: hairy red strokes.
- k_t_corrigrust: glyph-like marks on the corrugation.
- b_level1_ground2: worm-like melted shapes up close.

Fine or better: n_t_metal_plates, b_t_tree_bark_1, n_t_roof_tile_clay_curved, k_t_wood_small_dark, n_ice_test,
k_t_panel_dark.

## Tests

- Playtests (--exe target-wt-tex):

  | Playtest | Passed / failed |
  | --- | --- |
  | launch_newgame | 42/0 |
  | gfx_effects_remaster | 16/0 |
  | gpu_snow | 17/0 (now expects the domes shown) |
  | gpu_snow_original | 9/0 |
  | fx_snow | 12/0 |
  | camera_follow | 15/0 |
  | geom_remaster | 6/0 |
  | geom_original | 7/0 |

- Unit tests: geometry / snow / gamematerial / scene 7/0.
- 4K Remaster bench (--bench 20, background): 116.1 / 115.5 fps average.

## Open

- The courtyard's left wall: the original shows a lit rust face where the engine shows a dark blue diagonal face
  (e_snow_left.jpg). Lighting or geometry; not investigated.
- A red band across the top of the Remaster courtyard frame (in the gallery's before shot too): not investigated.
- No record uses a fourth uv set; the engine would read set 3 as the third.
