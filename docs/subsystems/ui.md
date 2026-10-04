# UI and HUD (2D drawing and layout)

Static reconnaissance, 2026-10-04: Ghidra exports (`work/decomp/`, `work/exports/functions.json`), the recovered
renderer (`src/renderer/`) and a dump of `lev_1_m1_big12_ui.big` (`tools/formats/bigtool.py dump`). Nothing here
was run. Statements marked **guess** are not verified. Goal: `docs/goals/remaster-basics.md` (UI at any aspect
ratio).

## 1. Coordinate system

All 2D drawing uses one **normalised screen space**: x and y in [0, 1] over the current viewport, origin at the
bottom left, y up. Layout data is authored in **640x480 virtual pixels** and divided by 640 and 480 by game code.

| Fact | Evidence |
| --- | --- |
| Screen transform 0x004f01c0 (renderer in arg0). It builds three static matrices once (flag byte 0x005fb898) and sets them: projection 0x0064aab0 (virtual 19, +0x4c): perspective, fov pi/2, aspect 1, near 1, far 10000 (0x004e3490). View 0x0064aa68 (virtual 20, +0x50): scale (2, 2, 1) times translate (-0.5, -0.5, -1) (0x004e2d90, 0x004e2910). World 0x0064aa20 (virtual 21, +0x54): identity. A vertex (x, y, 0) lands at NDC (2x-1, 2y-1). | decomp 0x004f01c0, 0x004e3490 |
| Because the projection is perspective, a vertex with z != 0 is scaled towards the screen centre (0.5, 0.5). UI meshes pass a z (see 0x00464e20). | 0x004e3490: m[14] = -1 (w = -z) |
| Glyph size: Renderer_DrawGlyph (0x005078c0) uses pixelW = Renderer_GetPixelWidth (0x00506f90) = 1 / (2 * viewport half width +0x40), pixelH likewise (0x00506fc0, +0x44). Text callers pass 0 for both (0x00495e80), so **glyphs are sized in physical pixels**: text does not grow with resolution. | src/renderer/auto_00503900.cpp, auto_00504b90.cpp, decomp 0x00495e80 |
| Viewport block renderer +0x40 = {half w, half h, centre x, centre y} in pixels; virtual 31 (+0x7c, 0x004b0300) returns its address. Renderer_SetViewport(null) = whole back buffer (+0x1634, +0x1638). | src/renderer/auto_004fde30.cpp, decomp 0x004b0300 |
| Renderer_GetAspect (virtual 34, 0x00508860) = +0x1648 = W / H * 0.75 (1.0 at 4:3), set by 0x00503b30. | src/loader/auto_00502cb0.cpp |
| Constants: 0x005bcbf8 = 1/640, 0x005bcbf4 = 1/480, 0x005bce1c = 480.0, 0x005bcd90 = 4/3, 0x005bcb4c = 0.5. Readers of 1/640: 0x004190a0, 0x00464b10, 0x00464e20, 0x00465720, 0x00467f50, 0x0046b0a0, 0x0046b0d0, 0x004948d0, 0x004a3260. | image read via tools/reginputs.py; grep of work/decomp |

### Draw paths (all end in the same screen transform)

| Path | Entry | Vertices | Transform set by |
| --- | --- | --- | --- |
| Text | Renderer_DrawTextW (virtual 114, 0x00507c10) -> Renderer_DrawGlyph -> VertexBatch_AddQuad (0x005077b0), batch renderer +0x588 | 6 per glyph, {x, y, z, colour, u, v} | Renderer_BeginScreenBatch (virtual 100): with argument 0 it calls 0x004f01c0; with 1 (most UI text) it keeps the caller's transform |
| Textured quads (loading screen, backgrounds, fades, progress bars, credits) | 0x0047cdd0 (corners in [0,1]; null = full screen 0x00588d68/0x00588d88), batch from g_game+0x44 virtual 5 | 4 per quad, written directly, **not** through VertexBatch_AddQuad | 0x00415dc0 "begin 2D": saves the three matrices and the viewport into g_game->postProcess, SetViewport(null) (virtual 29), then 0x004f01c0. Callers restore them afterwards (virtuals 21, 20, 19, 29). |
| Full-screen fade | 0x0047cff0 (colour, alpha) -> 0x0047cf10 -> 0x0047cdd0 | as above | as above |
| UI objects (HUD and menu pages, meshes and ui_text) | UI pass 0x00464460 (called from 0x00403050) | meshes: skin draw groups via 0x0048a290 into g_game+0x44 virtual 6; text: as above | 0x00464460 calls 0x004f01c0 once, then each object's virtual +0x13c, and resets the three matrices after each object |

**Best choke point: the screen transform 0x004f01c0 and the view matrix it sets (virtual 20).** Every 2D
path above goes through it, and no UI object sets the view matrix itself (meshes set only the world matrix,
0x00464e20). VertexBatch_AddQuad sees only text; 0x0047cdd0 sees only quads; neither sees UI meshes.

## 2. Where positions come from

- The UI bank carries **no positions**. It lists pages, objects, refs and texts (docs/formats/big-ui.md).
- **field_46c is the page index** (strong evidence: grouping the dump by it gives page 0 `pui_main` = every HUD
  object and text, page 1 `pui_pub`, page 3 `pui_mainmenu` = `ui_and_title` + the main-menu texts, and so on).
- **field_468 is an index into the 77 `ref_*` names** (0x00464ae0 reads ui +0x88 [+0x468]). Example: texts
  `ui_ammo_count` -> 36 `ref_ammo_count`, `ui_grenade_count` -> 37 `ref_grenade_count`, `ui_captured_count` -> 7
  `ref_captured`.
- 0x00464b10 places a text: finds an object by name (0x00463c90, list g_game+0xa84 +0xec0), reads the transform
  of the ref node (0x00453c50) and stores x * 1/640, y * 1/480 with alignment (0.5, 0.5) through 0x004659e0 into
  **+0x4e8/+0x4ec (position, normalised)** and +0x4f0/+0x4f4 (alignment); sets +0x474 bit 0x2000. So the layout
  is in **ref nodes of meshes in the object/skin banks, in 640x480 units** (guess for which object owns the refs).
  With +0x474 bit 0x20000, x is corrected: (x/640 - 0.5) * (aspect * 4/3) / (vp w / vp h) + 0.5.
- ui_text draw 0x00464750 (from 0x00465360 and others): colour +0x4e4 (or white/grey/red by flags +0x474 0x80,
  0x40, 0x800), scale = ((ui +0x94 - 1) * +0x4e0 + 1) * +0x4c8 * arg, position arg + (+0x4e8, +0x4ec), shadow
  flag (+0x474 0x200). Text auto-fit 0x00465720 converts a 640-unit width to back-buffer pixels with QueryInfo(1)
  and (2) (width, height) and binary-searches the scale.
- UI mesh draw 0x00464e20 (virtual, no direct callers): world matrix = object rotation, an X scale of
  (GetAspect * 4/3) / (vp w / vp h) (skipped with +0x474 bit 0x10), and a scale/translation built from object
  +0x37c/+0x380/+0x384 times 1/640, 1/480, 1/480 (**guess** which; order swapped by +0x474 bit 8). The game was
  already partly aspect-aware, but at full-screen viewports that factor is 1.
- Visibility in 0x00464460: +0x474 bit 2, or (+0x470 bit 1 and +0x474 bit 4); +0x474 bit 0x20 hides the object
  while g_game+0x2684 is set (**guess**: paused). Draw order = list order (g_game+0xa84 +0xeb8), then the list at
  ui +0x9c, then ui +0x54 (**guess**: the mouse cursor or a modal page).

### Telling HUD from menu at the choke point

Use the object being drawn, not the vertices: in the 0x00464460 loop each object is known.

| Signal | Use |
| --- | --- |
| field_46c (+0x46c) == 0 (`pui_main`) | HUD: anchor to edges |
| field_46c != 0 | Menu or shell page: centre at 4:3 |
| Object name (`ui_mouse_cursor`, `ui_crosshair`, `ui_*_crosshair`, `ui_hit_*`, `ui_background`, `ui_ad_image`, `ui_pub_interior`) | Exceptions (below) |
| Paths outside 0x00464460 (0x00415dc0 callers: loading, credits, backgrounds, fades, version string) | Not UI objects: full-screen or centred by their own code |

## 3. Anchoring design (proposal, `hook/enhance/`, off by default)

Let k = (4/3) / (W/H) (k < 1 on wide screens). Express each element in 4:3 space, then map x -> a + k * x:

| Anchor | a | Applied to |
| --- | --- | --- |
| left | 0 | HUD element with ref x < 213 (of 640) |
| centre | (1 - k) / 2 | HUD element with 213 <= x <= 427, all menu pages (pillarbox) |
| right | 1 - k | HUD element with x > 427 |
| stretch (k = 1, a = 0) | | full-screen backgrounds, fades, movies, `ui_hit_*` edge flashes (**guess**) |

- **Implementation:** hook 0x00464460 (or wrap each object's virtual +0x13c). Before drawing an object, set the
  view matrix (virtual 20) to `[a + k*x]` composed with the 0x0064aa68 matrix; restore after. Text inside the
  object uses BeginScreenBatch(1) and inherits it. One affine x-only matrix per element; no vertex edits.
- **Anchor source:** prefer a small table keyed by ref name (`ref_compass` right, `ref_health` left,
  `ref_crosshair` centre, `ref_weapon` / `ref_grenade` / `ref_ammo_count` right, chat lines left) with the x
  threshold as fallback. Read x from +0x4e8 (text) or the mesh world translation.
- **Paths outside the UI pass** (0x00415dc0 callers): apply the centre pillarbox by patching the view matrix in
  0x004f01c0 when called from 0x00415dc0, except full-screen quads (corners exactly 0..1, the default arrays
  0x00588d68/0x00588d88) which stay stretched or get aspect-fit (0x004937a0 already fits images using GetAspect).
- **UI scale:** text is in physical pixels (section 1). Under the enhancement, pass pixelW = s / 640 and pixelH = s / 480
  (s = user scale) for UI text, in pre-view coordinates (the anchor matrix then applies k), so glyphs scale with
  height like the meshes. At 640x480 and s = 1 this equals the original.

### Risks

- **Full-screen quads and fades** (0x0047cff0, loading 0x00449ac0, backgrounds 0x004935e0): must not be squeezed.
- **Movies** (DivX): drawn through another path (not traced); leave alone.
- **Mouse cursor and hit testing:** `ui_mouse_cursor` is a page-0 object. Menu hit tests compare mouse
  coordinates with 640x480 layout (**guess**, not traced); pillarboxing menus needs the same inverse transform on
  the mouse position, or clicks miss.
- **Perspective z:** UI meshes with z != 0 converge on (0.5, 0.5) of the projection; prepend the anchor to the
  view (after projection would be wrong). Keep the projection untouched.
- **World-tracking markers** (list +0xebc via 0x004190a0, 0x00464b10 with bit 0x20000): positions come from the
  3D camera; they must stay in true screen space (no anchor).
- **Text wrapping/fitting** (0x00465720) measures in physical pixels against a 640-unit width converted with the
  real width; with the scale override the result changes. Verify menus with long strings.
- **Glyph snapping** in DrawGlyph rounds to pixelW; with virtual pixels text may blur slightly.
- **The comparison scenarios do not execute GetAspect or QueryInfo** (src comments), so 0x00464e20 and
  0x00465720 may not run in menu+level. Trace which UI paths run before relying on them.

## Open questions

- Which object owns the `ref_*` nodes (0x00463c90 name argument), and what are object +0x37c..+0x384 in 0x00464e20?
- Where are mouse coordinates mapped into UI space?
- How are movies drawn (full-screen quad or a separate path)?
