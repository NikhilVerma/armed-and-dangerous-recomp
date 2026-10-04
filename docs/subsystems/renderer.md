# Renderer subsystem

The game's Direct3D 9 layer, recovered into `src/renderer/` and reached through `src/rhi/` (docs/renderer-interface.md).

## Extent

- **Range:** `0x004fb000-0x0050d000`, 295 functions. 293 are at S2 and 2 at S3 (`symbols/functions.csv`,
  2026-10-04). All pass `tools/compare.py`.
- **Unrelated code in the range:** zlib (`Zlib_*`), wave-file reading, ambient sounds and heap helpers are linked
  into the same address range.
- **Single entry from game code:** the vtable at `0x00589990`, 129 slots (`tools/vtable.py`). The only other direct
  call is the factory `Renderer_Create`.
- **Device access:** nothing outside the range touches the device (`Renderer` +0x14cc) or IDirect3D9 (+0x14c8).

## Key pieces

| Area | Functions (examples) | Notes |
| --- | --- | --- |
| Device and modes | `Renderer_ParseOptions`, `Renderer_AddDisplayModes`, `Renderer_FindDisplayMode`, `Renderer_SetDisplayMode`, `Renderer_ResetDevice`, `Renderer_ReleaseDevice` | Device loss: release, then restore default-pool resources (`Renderer_RestoreTextures`, `VertexBufferSet_Restore`) |
| State | `Renderer_InitStateCache`, `DrawState_Set`, `Renderer_SetStageAddressing`, `Renderer_SetViewportRect` | Render states go through a cache; draw states are objects the renderer hands out |
| Materials | `Material_InitCombiners`, `Renderer_BuildColorMixPass`, `Renderer_GetNamedPixelShader` | Fixed-function colour-mix combiners plus named ps 1.x shaders |
| Textures | `Renderer_RegisterTexture` (src/renderer/textures.cpp), `Renderer_CreateTextureFromImages`, `Texture_LoadFromSurface`, `Renderer_UpdateTextureLod`, `Renderer_SetMaxTextureLod`, `TextureBuckets_Init` | Managed textures use SetLOD for distance streaming |
| Text | `Renderer_DrawTextA`, `Renderer_TextIsTagStart`, `Renderer_TextSkipTaggedBlock` | Tagged text markup |
| Meshes | `SkinMesh_CheckTexCoordChannels`, the strip draws near `0x004fe01f` | Skin meshes are triangle strips (docs/formats/big-skin.md 3.4) |

## Vertex shader constant layout

These conventions are shared by all 456 vertex shaders the game creates. Evidence: the position dependency
chains of the shaders dumped with `[test] dump_shaders=1` into `work/shaders` fall into 8 shapes, all with this
layout.

| Registers | Content |
| --- | --- |
| c0 | constants: the source of r0.w before the camera transform, and a scale on the blend weights v1 (guess: c0.x = 1; write masks not decoded yet) |
| c16, c17 | position dequantisation: `pos = v0 * c16 + c17` |
| c22..c25 | camera view-projection: `oPos = (dp4 r0 c22, ..., dp4 r0 c25)` with r0 the world position |
| c42..c44 | rigid draws: the 3x4 world matrix |
| c[a0.x + 42..] | skinned draws: the bone palette, 3 registers per bone, blend weights in v1 (the number of bones per vertex and the index source are not decoded yet) |

The Vulkan backend's scene description inverts c22..c25 to recover world positions (`src/rhi/vulkan/vk_scene.cpp`).

## Camera

- **Projection:** right-handed, set with SetTransform even though the shaders use c22..c25. At level 1 it is
  `_11 = _22 = 1`, `_33 = _43 = -1.0001`, `_34 = -1`, `_44 = 0`.
- **Scene viewport:** letterboxed to 640x372 at y = 54 in the 640x480 test window. The HUD uses the full
  viewport.

## Unknowns

- The meaning of the remaining vertex-shader constants (lighting, fog and texture-coordinate generation) has not
  been mapped.
- Paths no scenario reaches (display-mode change, debug overlay) are recovered but have not run. See
  `tools/coverage.py`.
