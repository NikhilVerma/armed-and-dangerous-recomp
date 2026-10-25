# 2026-10-25: jump pack wings (fix/jetpack-wings)

## Done
- Findings A2 (lev_3_m2: Roman's jump pack showed short jagged orange fragments where the original has dark blades).
  The fragments are the pack's "black pipes" submesh (turbo_jump_pack_0/1, n_t_iron, bones wing_l1..wing_r3), at
  the right size: turbo_idle scales the wing bones 0.18..0.36, as in the original, and the blades are thin open
  strips. The orange was not their shading but the level's clear (fog) colour:
  - removing the submesh removed the orange; recolouring its material (emissive magenta, no texture) left it orange;
  - no depth prepass (MSAA, no AO, snow off, soft_particles 0) drew dark blades like the original;
  - Bevy's prepass pipeline leaves `cull_mode` None, the colour pass Some(Back): the prepass wrote the strips' back
    faces, the colour pass culled them and failed the grass behind against their depth.
- Fix (gamematerial.rs specialize): the colour pass and the camera's prepass both cull back faces, as the original
  draws one-sided (draw state cull index 1, Renderer_CreateDrawState 0x004fd000 -> Renderer_ApplyDrawState
  0x00504940 -> table 0x005ca798 {1, 2, 3} = D3DCULL_CW). The sun's shadow views (prepass pipeline with a projection
  in the key) keep both faces. Crop: work/review/jet_180_fix.png (original / before / after).

## Learned
- The earlier hypotheses (case-sensitive texture name, uv sets, alpha) were wrong: the material records and texture
  are exported correctly; the per-bone scale is applied like the original (Transform_ToMatrix 0x004e3370).
- The lev_bonus_m3 walk clip has no pack on Roman, so it shows nothing of this.

## Open
- Any other open surface whose back the camera sees had the same fog-coloured holes in the Remaster preset
  (perhaps part of the earlier lev_bonus_m1 / lev_4_m3 slivers put down to depth rounding); the walk videos should
  be recaptured.
