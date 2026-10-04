# Camera, projection, FOV and aspect

Static reconnaissance (2026-10-04), for Hor+ widescreen, an FOV slider and letterbox removal. Sources:
`tools/recover.py <addr> --asm` (disassembly is the evidence), `work/decomp/*.c`, `src/renderer/`. Guesses are
labelled **guess**. Nothing here was checked at runtime.

## Data path in one paragraph

The scene camera object is `g_game->+0xa68` (g_game pointer at 0x005fe490). Each frame 0x0040e350 (camera update,
4995 bytes) evaluates the camera node (path/weapon camera) with 0x0040a4d0, which yields a vertical FOV, near and
far; 0x0040fbb0 blends position and FOV towards a target camera (zoom transitions). The scene render 0x00403050 sets
a fixed letterbox viewport, then 0x0040f6e0 builds the projection with 0x004f0100 -> 0x004e3490 and hands it to
Renderer_SetProjection (virtual 19), and the view matrix `g_game+0x704` to virtual 20. Renderer_FlushTransforms
(0x004fcc30) uploads the **projection alone** (renderer +0x138) to c22..c25 (`SetVertexShaderConstantF(0x16, +0x138,
4)`) and world*view (0x004e2910 of +0x6c and +0xb0 into +0x1c0) to c42..c44. There is no combined view-projection;
the view is folded into the world 3x4.

## Camera object (g_game+0xa68), constructor 0x0040e050

| Offset | Meaning | Evidence |
| --- | --- | --- |
| +0xf0 | base / previous FOV, radians (vertical) | ctor 0x3f860a92 = 1.047198 (60 deg); 0x0040e350 copies +0xf4 <-> +0xf0 |
| +0xf4 | current FOV, radians, **vertical full angle** | ctor 60 deg; read by 0x0040f840 and 0x0040f6e0; blended in 0x0040fbb0; set by 0x0040a4d0 out-param |
| +0xf8 | near plane (also written to +0xd8 by 0x0040f890) | 0x0040f850 getter, ctor 1.0, 0x0040a4d0 out-param = node+0xa0 |
| +0xd4, +0xd0 | far = +0xd4 * +0xd0 (0x0040f860 with cl=0) | ctor 100000.0 and -1.0 (sign unexplained, **open**) |
| +0x154 | aspect used to convert a horizontal authored FOV to vertical | ctor 0x3fd9df52 = 1.702128; 0x0040a4d0 `2*atan(tan(h/2)/[+0x154])` |
| +0x68 / +0x6c | zoom transition timer / state (1,3 = in, 2 = out) | 0x0040fbb0; weight = +0x68 / g_game[1]+0x4c (transition length) |

Accessors: 0x0040f840 get FOV, 0x0040f850 get near, 0x0040f860 get far, 0x0040f880 set FOV, 0x0040f890 set near.

## FOV source and zoom

- 0x0040a4d0 (559 bytes) evaluates a named camera node (via 0x00453c50) and, when out-params are given, the node's
  animated FOV (0x004eb710): node +0x9a == 0 means a horizontal FOV, converted to vertical with camera +0x154; == 1
  means vertical; anything else pi/2 (0x005bce6c). Near = node+0xa0, far = node+0xa4.
- Weapon zoom: the weapon table built in 0x00402110 gives each weapon a camera node name ("Camera_machinegun",
  "Camera_sniper", "Camera_rifle", "camera_vindaloo", "camera_gurner"). **Guess (strong):** the sniper/rifle zoom is
  that node's authored FOV, blended in by 0x0040fbb0 (`fov = target*w + fov*(1-w)`). No numeric zoom factor exists
  in code.
- FOV unit: radians, full vertical angle. 0x004e3490 computes `_22 = cos(f/2)/sin(f/2)`, `_11 = _22 / aspect`,
  `_33 = -far/(far-near)`, `-1` at +0x38, `-near*far/(far-near)` at +0x2c (internal layout is transposed; 0x004e2210
  transposes before SetTransform).

## Projection builders (callers of 0x004f0100)

0x004f0100 (esi = renderer; out, fov, near, far, aspect): when aspect == 0.0 it uses
`(viewport halfW / halfH) / Renderer_GetAspect()`, viewport from virtual 31 (0x004b0300, returns renderer+0x40).

| Caller | Pass | FOV | Aspect |
| --- | --- | --- | --- |
| 0x0040f6e0 (only caller 0x00403050) | **main scene** | camera +0xf4 | 0 (viewport / +0x1648) |
| 0x00489070 (flag 0x10) | secondary scene pass, far 1e6 (**guess**: sky/reflection) | camera +0xf4 | 0 |
| 0x00489320 | 6-face cube map, restores state after | pi/2 | 0 |
| 0x00417c00 | **guess**: shadow/light projector | obj +0x1a0 | 1.0 |
| 0x00417f20 | **guess**: same object, render-to-texture | obj +0x1a0 | 0 |
| 0x004fffe0 Renderer_InitDefaults | default | 60 deg | (1.0, 1000.0) |

The runtime capture `_11 = _22 = 1` (90 deg, aspect 1) does not match the main scene path (aspect 1.72 at
640x480). **Guess:** it was the cube-map pass 0x00489320 (pi/2, square target), the last projection set.

## Aspect (+0x1648) and letterbox

- Renderer_InitDeviceState 0x00503b30 (asm 0x00503b56..0x00503b70): `+0x1648 = BackBufferWidth(+0x1490) /
  BackBufferHeight(+0x1494) * 0.75` (0x005bc15c). Read only through Renderer_GetAspect 0x00508860 (virtual 34);
  decomp grep finds no other `+0x1648` access.
- Meaning: a pixel-aspect correction that assumes the **display is always physically 4:3**. At 4:3 it is 1.0; at
  16:9 it is 1.333, so the projection aspect stays 640/372 = 1.72 at every resolution and a 16:9 image is stretched
  horizontally (same horizontal FOV as 4:3).
- Other GetAspect users: 0x00464b10, 0x00464e20 (screen-space scale `aspect * 1.3333 / viewportAspect`, **guess**:
  billboards/flares), 0x004937a0, 0x004948d0. 0x004707a0 calls a +0x88 slot on another object (unverified).
- Letterbox: 0x00403050 (243 bytes) saves the viewport (virtual 31), calls Renderer_SetViewportRect (virtual 28)
  with `(0, H*0.1125, W, H*0.775)` (0x005bcf24 = 0.1125, 0x005bcf28 = 0.775; W/H = g_game +0x4c/+0x50), renders,
  then restores the viewport (virtual 29). It is unconditional: a **fixed** gameplay letterbox, not a cinematic
  effect. At 640x480 this gives 640x372 at y=54 (SetViewport flips y). Design aspect = (4/3)/0.775 = 1.72; the
  camera's +0x154 = 1.702 is close (**guess**: the authoring aspect).
- 0x00406840 (1806 bytes) maps normalised screen y into the band with `y*0.775 + 0.1125` (HUD/marker placement,
  **guess**). Decomp grep finds 0x005bcf24/0x005bcf28 used only by 0x00403050 and 0x00406840.

## Recommended hook points

| Goal | Hook | Notes |
| --- | --- | --- |
| (a) Hor+ | Renderer_GetAspect 0x00508860 (recovered, S2): return 1.0 (square pixels) | Projection aspect becomes the true viewport aspect; vertical FOV kept. Also corrects the screen-space users above consistently. Alternative: the store in 0x00503b30. |
| (b) FOV multiplier | 0x0040f6e0 (110 bytes, S0, single caller): scale the FOV passed to 0x004f0100, e.g. `2*atan(k*tan(fov/2))` | Do not write camera +0xf4 (0x0040fbb0 blends from it). Apply the same to 0x00489070's camera pass. Zoom keeps its ratio. |
| (c) letterbox | 0x00403050 (243 bytes): viewport `(0, 0, W, H)`, or a configurable band; adjust 0x00406840 the same way | Simplest: patch floats 0x005bcf24 -> 0.0 and 0x005bcf28 -> 1.0 (read-only data, needs VirtualProtect). |

With the letterbox removed the vertical FOV spans the full height, so the picture zooms out vertically. To keep the
authored framing, use the design aspect A0 = 1.72: for viewport aspect a < A0 widen the vertical FOV to
`2*atan(tan(v/2) * A0 / a)` (keeps horizontal), for a >= A0 keep v (Hor+). Do this in the 0x0040f6e0 hook.

## Open questions

- Far plane sign (+0xd0 = -1.0 in the ctor) and the runtime `_33 = -1.0001` (far/near = 10001): which pass.
- Which pass 0x00489070 flag 0x10 and 0x00417c00/0x00417f20 are.
- Whether anything outside the decompiled set reads 0x005bcf24/0x005bcf28 or renderer +0x1648.
