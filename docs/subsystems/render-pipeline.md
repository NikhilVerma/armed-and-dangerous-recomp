# Game-side render pipeline

Status 2026-10-05: every function is classified (symbols/functions.csv subsystems `render-scene`, `render-fx`,
`render-post`, `render-ui`; the D3D9 layer 0x004fb000-0x0050d000 is `renderer`, docs/subsystems/renderer.md).
Game-side rendering is recovered statically in archive/src/renderer/ (scene_pass.cpp, render_r0..r7.cpp): stage S1 =
compile-checked, the behavioural comparison (tools/compare.py) still to run. Statically linked D3DX
(0x00545c10-0x00565160, `d3dx`) is library code and is not recovered. Original bugs found: docs/original-bugs.md.

## Frame

| Step | Function | Notes |
| --- | --- | --- |
| Frame | `Game_RenderFrame` 0x00403050 | HUD viewport, camera (0x0040f6e0), decals update, env maps `EnvMaps_Update` 0x00418880, main scene, UI pass, present |
| Main scene | `ScenePass_RenderMain` 0x00489610 | `ScenePass_Render(pass, 0xffffffff, ...)`; its tail target 0x00433e70 is a bare `ret 4` |
| Env map | `EnvMap_Render` 0x00418c80 -> `CubeMap_RenderScene` 0x00489320 | six scene passes into a cube map; nearest map `EnvMap_FindNearest` 0x00418920 |
| Render to texture | `ScenePass_RenderToTexture` 0x00489540 | used by `SceneThumbnail_Render` 0x0044f2d0 (save thumbnail) |
| UI pass | 0x00464460 (archive/src/game/ui_pass.cpp) | `UiText_Draw` 0x00464750, `UiMesh_Draw` 0x00464e20, `Text_Draw` 0x00495e80, 2D primitives 0x0046b260-0x0046baf0 |
| Loading screen | `LoadingScreen_Draw` 0x00449ac0 | show/fade 0x004499c0 / 0x00449860, error loop 0x004495b0 |

## Scene pass `ScenePass_Render` 0x00489070 (archive/src/renderer/scene_pass.cpp)

1. View (renderer virtual +0x64) and projection (+0x60); view-projection 0x004e2910; frustum
   `Frustum_FromViewProjection` 0x004889a0 into pass +0x110 (count +0x170; `ScenePass_GetFrustum` 0x00488990).
2. 0x10 sky: far-1e6 projection (0x004f0100), objects of the lists pass +0x1bc / +0x1c0 (virtual +0xc0; added by
   0x00488120 / 0x00488130).
3. 1 | 8 world: `World_GatherFrustum` 0x0048eab0 (terrain grid clip 0x00449230 + world tree query 0x004e7970), then
   `World_WalkCells` 0x0048eb60 (per-cell 0x0048eb20) calls 0x00448db0 (objects, flag 1) and 0x00489050 (flag 8).
   Objects: `Object_InFrustum` 0x00454360, `Object_SelectLod` 0x00452f50, then draw items via `ItemList_Add`
   0x0048a0d0 (view depth 0x0048a030, st0).
4. Opaque: `ScenePass_DrawLayers(pass, 0)` 0x00488e90 -> `MeshGroup_Draw` 0x0048a290 (the central draw submission:
   material `Material_BindTextures` 0x0048a210 / flags 0x004897d0 / `Material_IsTranslucent` 0x004897a0, env map
   0x00487e30, CPU skinning `Skin_DeformAll` 0x004edb80 / palette 0x004edbe0, dynamic vertices
   `Mesh_FillDynamicVertices` 0x00489f30); per object `Object_DrawGroups` 0x0048a540. Layer order: 0x00489910
   (key sort), 0x00489a20 (layer depth records). `ScenePass_ResetStages` 0x0048a1a0.
5. 2 decals `Decals_Draw` 0x00416330 (decal build/clip 0x00417260, `Polygon_ClipPlanes` 0x00416010, render
   0x00416740); translucent `ScenePass_DrawLayers(pass, 1)` via `SortedTree_Draw` 0x00488df0 (back to front).
6. 4 the game's own shadows (silhouette render-to-texture `Shadow_RenderTexture` 0x00417f20, light camera
   0x00417c00, receivers `GameShadow_RebuildReceivers` 0x004181e0, draw 0x00418380; archive/src/game/shadows.cpp switches
   them off when renderer shadow maps / RT are on). Lights: `LightObject_Load` 0x0044add0 builds a D3DLIGHT9,
   slots `LightSlot_Acquire` / `LightSlot_Release` 0x0048a4b0 / 0x0048a520.
7. 0x20 fx: `Fx_DrawParticles` 0x00423820 (`Particles_Draw` 0x0046d9f0, pools 0x0046d810-0x0046d9a0), streamers
   `Streamer_Draw` 0x00495380. The fx system: `Fx_Create` 0x0041cc60, `Fx_RunScript` 0x00420770 (the fx script
   interpreter, ~110 opcodes documented in archive/src/renderer/render_r2b.cpp), fx object update 0x0041a350, texture
   animation 0x004195e0-0x00419d30, opcode helpers 0x0041d110-0x00420670 (archive/src/renderer/render_r2a.cpp).
8. 0x40 post-process `PostProcess_Pass` 0x0047c600 (effects 0x0047bda0-0x0047c1d0 started by fx opcodes,
   `PostEffect_Apply` 0x0047c200, screen quad 0x0047cdd0, fade 0x0047cff0, blur 0x0047d0f0, rings 0x0047d230).
9. 0x80 HUD: `Hud_ProjectMarkers` 0x00406840 (frustum 0x00408390), chat lines 0x0046a820.

## Enhancement points

- `g_sceneCullRadius` (archive/src/renderer/scene_pass.h, `[render] cull_radius`, remaster default 100): the main pass's
  side and near planes move out by R, so casters near the camera are never culled (shadow pop-in / pop-out); the
  renderer's caster cache is then off (rhi::SceneHints::shadowCasterCache).
