# Shadows and lighting

Static reconnaissance (2026-10-04) for replacing the game's shadows with shadow maps rendered by the Vulkan
backend. Sources: `tools/recover.py <addr> --asm`, `work/decomp/`, `work/exports/functions.json`, constants read
from the image (`tools/reginputs.py`), recovered code in `src/renderer/`. Nothing here was checked at runtime.
**Guess** marks anything not proven.

## 1. What the game draws

There is **one** dynamic shadow technique: per-object **render-to-texture silhouettes, projected as decals** onto
the world. No blob shadows, stencil volumes or lightmaps were found:

- No stencil: `src/renderer` and `src/rhi` never set a stencil state (grep for `STENCIL` finds only Clear flags and
  depth-surface setup), and the Vulkan backend, which has no stencil test, matches D3D9 in the level/play scenarios.
  **Guess (strong):** no stencil shadows.
- No lightmap: no string, texture name or format field mentions one (docs/formats has none). Whether world
  geometry is pre-lit by vertex colour is **open**.
- Decals (scorch marks, fx opcode 0x6c) are a separate pool in the same manager (docs/subsystems/decals-bodies.md).
- The post-process targets ("blur texture" mip chain 512x256 down, "afterburn texture"; PostProcess_CreateTargets
  0x0047c920, owner game+0xa98) are unrelated to shadows.

### Owner: the decal/shadow manager `g_game+0xa7c` (ctor 0x00416050)

| Off | Meaning | Evidence |
| --- | --- | --- |
| +0x04 | u8 shadows enabled (copy of m_enableShadows) | 0x00401ce0 `00401cfd..00401d03` |
| +0x10 / +0x14 | free / active shadow lists (link +0x50/+0x54) | 0x00417b90 moves a shadow between them |

Pool, allocated at level setup by **0x00416190** (from 0x00416130): table 0x005b96f8 = (count, log2 size) pairs
`(10, 6) (10, 7) (4, 8)`: 10 shadows of 64x64, 10 of 128x128, 4 of 256x256. Each object is 0x59b0 bytes, ctor
**0x00417990**, vtable 0x005b96bc (base class = decal base 0x005b964c, ctor 0x004166b0).

### Shadow object (0x59b0 bytes)

| Off | Meaning | Evidence |
| --- | --- | --- |
| +0x58 | caster object (its +0x3e0 points back) | 0x00417b90 |
| +0x5c | texture record "Shadow texture", log2 size at +0x6c/+0x70, used as render target | 0x00417990, 0x00417f20 `00417fbe` |
| +0x80 | light view-projection x texture bias (0x005fe4e0, 0.5/-0.5 scale and offset): texgen for the projection | 0x00417c00 end, 0x00416190 |
| +0xc4 | light view matrix (game 0x44 format) | 0x00417c00 `00417cee..00417cfa` |
| +0x108 | light eye position | 0x00417c00 |
| +0x120 / +0x11c | light frustum for gathering receivers / result of 0x004889a0 | 0x00417c00 |
| +0x198 / +0x19c | near / far of the light projection | 0x00417c00 |
| +0x1a0 | FOV = 0x3db2b8c2 = 5 deg (nearly orthographic) | ctor |
| +0x1a4 | 1/tan(2.5 deg) = 22.9, eye distance factor (double 0x005bcd68 = 0.0436 rad) | ctor `00417999 fptan` |
| +0x118 / +0x119 | receiver mesh dirty / texture valid | 0x00417b90, 0x00418190, 0x004181e0 |

### Per frame, inside the main scene pass

The scene pass is **0x00489070** (flags; main scene 0x00489610 passes 0xffffffff, from 0x00403050). Order:
flag 0x10 sky (far 1e6), 1|8 world gather, 0x00488e90(0) **guess** opaque, flag 2 decals 0x00416330,
0x00488e90(1) **guess** translucent, **flag 4 shadows**, 0x20 fx/particles, 0x40 post-process, 0x80 HUD markers.

With flag 4:
1. **0x004163c0** (update, gated by manager+4): for each object in `[g_game+0xa84]+0xe78` whose vt+0x130 returns a
   shadow, **0x00418190** re-renders when the caster moved or animates (0x004531e0, 0x00416e90) or the texture is
   not valid:
   - **0x00417c00** builds the light camera: direction = **renderer light 0** (`Renderer_GetLight(0, buf)`
     `00417c1c`, then `buf+0x40` = D3DLIGHT9.Direction) if enabled, else the constant (0, -1, -1). The caster's
     bounding sphere (vt+0x90) gives the centre and radius; eye = centre - dir * radius * 22.9; look-at 0x004e2eb0;
     view = inverse (0x004e2b40) into +0xc4; projection 0x004f0100 (fov 5 deg, aspect 1).
   - **0x00417f20** renders the silhouette: `Renderer_SetRenderTargetTexture(+0x5c, 0, 0)` (vt+0xcc), turns option 4
     (projection flip) off, full viewport, view +0xc4 (vt+0x50), projection (vt+0x4c), Clear (vt+0xf4), material
     override 0x005fe498 (`[g_game+0x40]` vt+0x14), draws the caster with **0x00417e90** (and attached objects when
     caster+0x3ec bit 4 "shadow_drawlocked"), then restores target (null = back buffer), world, view, projection,
     viewport.
2. **0x004162b0** (draw, gated by manager+4): world = 0x005fe528, renderer vt+0xa4(1,3,4,3,0) (**guess**: blend
   mode), calls each active shadow's vt+0x20 = **0x00418380**: colour from **g_game+0x2704** (level shadow colour),
   **0x004181e0** rebuilds the receiver mesh when the light frustum moved (gathers world geometry from g_game+0xaa0
   via 0x0048eab0/0x0048eb60, objects too when caster bit 0x20; per-vertex fade `1 - d^2/(far-near)^2`), then the
   decal draw **0x00416740**.

Shadows go to casters by **0x00416530** (from object method 0x00455c10): the free shadow whose log2 size is closest
to the caster's `shadow_exponent` (+0x3e4).

## 2. m_enableShadows (game+0x4be)

0x00401ce0 copies it to **`[g_game+0xa7c]+4`** (`00401cf7 mov eax,[esi+0xa7c]; 00401cfd mov cl,[esi+0x4be];
00401d03 mov [eax+4],cl`). Read by 0x004162b0 (draw) and 0x004163c0 (texture renders) only. Off = no shadow
textures rendered and no shadow decals drawn; shadows are still assigned (0x00416530 does not test it). It does not
touch lighting.

Other controls (no other global flag found):

| Control | Where | Evidence |
| --- | --- | --- |
| Per object `shadow_enable` (bits 1-2), `shadow_drawlocked` (4), `shadow_receive` (0x10), `shadow_castontoobjects` (0x20) | object +0x3ec | 0x00451360 |
| `shadow_exponent` (log2 texture size, default 7), `shadow_blur` (+0x3e8), `shadow_radius_min/max` (+0x3f8/+0x3fc) | object | 0x00451360 |
| `shadow_color_red/green/blue/blend` (defaults 0, 0.125, 0.25, 0.3) -> ARGB | g_game+0x2704 | 0x0044c170 (level settings object) |
| `shadow_lod_index` | object | 0x0045bf70 (not read) |

## 3. Lights and the sun

- **Source: level objects of class "light"** (class id 0x5d, 0x0045df60; object 0x4cc bytes, vtable 0x005a8a50).
  Its property loader **0x0044add0** (vt slot +0x18) builds a D3DLIGHT9 at object+0x460: colour =
  (RedValue, GreenValue, BlueValue) x Intensity / 255 into Diffuse/Specular/Ambient per `AffectDiffuse`,
  `AffectSpecular`, `AmbientOnly`; **type 3 (directional) when Attenuation1 = 0** (HalfRange 0), else point (1);
  position from vt+0x54; **direction = -(column 2 of the object's world matrix)** (0x00453180 reads `[2],[6],[10]`: vt+0x5c is thiscall and pops its argument, so the copy starts 4 bytes lower than an earlier reading assumed;
  then three `fchs`); range 1e6. It then takes the first free renderer slot with **0x0048a4b0** -> Renderer_SetLight
  and keeps the index at object+0x4c8. 0x0044b100 releases it (0x0048a520).
- Other light users: fx lights (0x0041da90 and neighbours, "Fx : Failed to set light ..."; 0x0041a350 reads slots).
  **Guess**: point lights.
- **Renderer storage:** 8 D3DLIGHT9 at renderer +0x169c + i*0x68 (world space), enable bytes +0x1a44 + i
  (Renderer_SetLight 0x004ffbc0, Renderer_GetLight 0x004ffd10, vt+0xb0/+0xb4; both recovered, S2).
- **Shader constants** (Renderer_UpdateLightConstants 0x004ff2e0, called from Renderer_PrepareDraw for lit draws):
  up to 2 directional lights + 1 other; c7/c8 = view-space directions (world direction times renderer +0xb0,
  normalised), c9 = third light, c10.. diffuse x material, c13.. specular, c6 power, c3 summed ambient.
- **The sun = renderer light slot 0** as far as shadows are concerned (0x00417c00). **Guess**: the level's first
  "light" object is its directional sun; not verified per level. World up is probably +Y (fallback (0,-1,-1),
  **guess**).
- **Per frame:** lights are static after load (no per-frame SetLight in the scene path found). Read
  `renderer+0x169c+0x40` (Direction, world space, D3D convention: the direction light travels) and the enable byte
  `+0x1a44` for slot 0, or walk all 8 for the first type 3.

## 4. View matrix

- **g_game+0x704 is the main-scene view (world -> camera).** The camera update 0x0040e350 writes it as
  `inverse(camera world matrix at camera+0x8c)` (0x004e2b40 at three sites); 0x0040f6e0 passes it to renderer
  virtual 20 (fn_004fe9b0) right before 0x00489610. fn_004fe9b0 copies it to **renderer +0xb0** and stores its
  inverse (camera -> world) at **renderer +0xf4**.
- **Layout (0x44 bytes):** 16 floats `m[row*4+col]` then a flags dword at +0x40. Rows are output coordinates and
  column 3 is translation: `p' = M p`. Evidence: 0x004e2b40 computes `out[3] = -(out[0]*in[3] + out[1]*in[7] +
  out[2]*in[11])`; 0x0040f9e0 reads the camera position as elements 3, 7, 11 of `inverse(+0x704)`. Row 3 is (0,0,0,1).
  Flags (**guess**): 0x10 has translation, bits 1-2 non-orthonormal, 0x20 general (full inverse 0x004e2280).
  0x004e2210 transposes to D3D layout before SetTransform.
- Camera space is right-handed, looking down -Z (projection `_34 = -1`, docs/subsystems/camera.md).
- **Other views set in the same frame:** the env-map cube pass 0x00418880 -> 0x00489320 (before 0x0040f6e0), the
  render-to-texture scene pass 0x00489540 (from 0x0044f2d0), and each shadow texture render (0x00417f20, inside the
  main pass, restored afterwards). Take the view that is current when the back buffer is the target, or the value
  passed to fn_004fe9b0 from 0x0040f6e0 (block == g_game+0x704).

## 5. Hook points

| Goal | Hook | Notes |
| --- | --- | --- |
| (a) game shadows off | **0x004162b0** (115 B) and **0x004163c0** (119 B), both S0, single caller 0x00489070: return early when `[enhance] shadow_maps` is on | Same effect as m_enableShadows = 0 without touching the saved option. Cheaper alternative: write `[g_game+0xa7c]+4 = 0` after every 0x00401ce0 call (371 B, 9 callers), but that hides the user's setting. |
| (b) view per frame | **fn_004fe9b0** (renderer virtual 20, S2, recovered in src/renderer/auto_004fe3f0.cpp): publish the block when it is g_game+0x704, or read renderer +0xb0/+0xf4 at the main pass | Alternative: recover **0x0040f6e0** (110 B, S0, one caller) and read g_game+0x704 and the projection there. |
| (b) sun | **Renderer_SetLight 0x004ffbc0** (S2): record type-3 lights as they are set; or read renderer +0x169c slot 0 each frame | Already recovered, so no new recovery is needed. |
| frame boundary | 0x00489610 (31 B, S0) wraps the main 0x00489070 call | Optional marker for "main scene starts". |

Recover first: 0x004162b0 and 0x004163c0 (small, ABI: manager in eax; 0x004163c0 also takes one stack arg passed to
0x00418190). Nothing else is needed for (a). For (b) the hooks are already recovered.

Risks: shadow-texture renders switch render targets **inside** the main scene pass (0x00417f20), so with game
shadows on the Vulkan backend sees the back-buffer pass split. Turning them off removes those passes. Our shadow-map
casters must come from the scene description (positions are camera space; world = renderer +0xf4 x camera).

## Open questions

- Whether every level has a directional light in slot 0, and how levels without one look (fallback (0,-1,-1)).
- What the `alphashadow` named pixel shader (0x0050c5c0) is used for.
- Whether world geometry is pre-lit (vertex colour) or lit by the D3D lights.
- The flags 0x00418c80 passes to 0x00489070 for the env map, and the exact draw of 0x00416740 (blend, texture
  stage setup for the projected texture).
- `shadow_blur` and `shadow_lod_index` consumers.
