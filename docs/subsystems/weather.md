# Weather (snow, rain): camera-attached fx domes

Status: 2026-10-04, static reconnaissance only (nothing run). Sources: `work/decomp/`, `archive/tools/recover.py --asm`,
`archive/tools/vtable.py`, and `bigtool.py dump` of lev_1_m1 / lev_5_m* banks (fx, obj, prop, inst, skin). Goal: replace
the game's snow with a GPU snow simulation in the Vulkan backend. **Guess** marks anything unconfirmed.

## Summary

There is **no weather system, no weather strings in code, and no particles** in the level-1 snow. `.text` has no
string matching snow/weather/rain/precip/flake/storm (functions.json string scan). Snow and rain are pure **data**:

1. A `camera_fx` object instance in the level's inst bank names an fx script (`fx_name`).
2. The script creates a few **fx objects (`fxo_*`) that are static meshes**: open cylinders/domes around the camera
   textured with `fx_snow_flakes.tga` (rain: `fxo_rain_N`, `fxo_wind_spray`).
3. The falling motion is **UV scrolling** of up to 4 texture stages (fx opcode 0x40), not particle motion.
4. Each frame the camera update copies the camera position (or full transform) onto the `camera_fx` object; the fx
   objects are attached to it, so the domes travel with the camera.

## 1. Who creates it (lev_1_m1)

| Step | Where | Evidence |
| --- | --- | --- |
| Behaviour `camera_fx` = class id 0x97 | 0x0045df60 (behaviour name table) | `FUN_0044ad70("camera_fx")`, `FUN_00461ff0(..,0x97,..)` |
| Object ctor: size 0x488, vtable **0x005ad058** | 0x0045f020 case 0x97 | `FUN_004e07f0(..,0x488,..)`, `*obj = 0x005ad058` |
| Init from props (vtable +0x18) | **0x004101a0** (248 B) | reads `position_only`, `fx_name` (-> +0x460), `trigger_start` (+0x480), `receive_trigger_kill` (+0x484). position_only=1 -> list `g_game+0xa84 -> +0xed0`, else `+0xecc` |
| Start fx | **0x004102e0** (82 B) -> 0x0041cc60(fxsys `g_game+0xa90`, name obj+0x460, params) | params[0] = 4 (the "locked objects allowed" flag tested by 0x00423680); owner = the camera_fx object. Called at init when trigger_start == 0, else by trigger in 0x004102b0 (+0x484 trigger kills it via vtable +0xd4) |
| Follow camera each frame | **0x0040f750** (231 B, only caller 0x0040e350 camera update) | list +0xecc: full camera matrix (camera +0x8c) via 0x00457990; list +0xed0: position only (camera +0x98/+0xa8/+0xb8) via 0x00457870 |

Instances in `lev_1_m1_big10_inst.big` (two `camera_fx`, at ~(69, 1141, 250)):

- `fx_camera_l1_m1`, position_only = 1 (prop default from `p_camera_fx`). **The persistent snow.**
- `fx_camera_l1_m1_locked`, position_only = 0 (full camera transform): intro overlay (`fxo_white`,
  `fxo_true_story`, two `fxo_snow_spray`, an `fxo_glow_red` particle emitter). Lifespans 3 to 10 s.

`fx_camera_l1_m1` script (dump `fx/fx_camera_l1_m1.json`):

| Object | Scale (0x0b) | Fade | UV layers (0x40: layer, angle deg, speed, duration, ramp in, ramp out) | Life |
| --- | --- | --- | --- | --- |
| `fxo_snow` | 1 (none) | in 0..0.2 s (0x09), out 2.7 s (0x0a) | (1, 210, 0.3, 1e5, 0, 0) | 1e5 s |
| `fxo_snow` | 6 | none | (1, 220, 0.2, ...), (2, 240, 0.15, ...) | 1e5 s |
| `fxo_snow_2` | 8 | none | (1, 250, 0.1, ...), (2, 245, 0.105, ...) | 1e5 s |

The first push also holds opcode **0x73 `camera_path_l1m1`** -> 0x0040ff50(name, scene camera, 1, 0): it starts
the scene camera's path animation. It is not guarded by the created object, so it runs even if the object is not
created (0x00420770 case 0x69 has no `piVar14 != 0` test). **Do not skip the whole fx**, or the camera path is lost.

Opcodes confirmed from their helpers (adds to docs/formats/big-fx.md):

| Opcode | Meaning | Helper / evidence |
| --- | --- | --- |
| 0x09 | fade in (start, end) -> obj +0x474, +0x478 = 1/(end-start), flags +0x460 \|= 0xc | 0x0041dba0 |
| 0x0a | fade out duration -> +0x47c, flag 0x10 | 0x0041dc30 "set fade out" |
| 0x0b / 0x0c | uniform scale / scale linear (from, to) -> +0x480/+0x484, flag 0x20 | 0x0041d170 "set scale linear" |
| 0x13 | create object **attached to the fx owner** at the attach point from the 0x104 list ("null" = origin) | 0x00423680 "Unable to create locked object" |
| 0x16 | set invisible (object vtable +0xf4) | 0x0041e280 |
| 0x40 | UV scroll for layer 1..4 | 0x00420670 "set uv scroll", alloc 0x00420520 ("fx_uvscroll", 0x174 B at obj +0x514) |
| 0x73 | start camera path on scene camera | 0x0040ff50 |
| 0x79 | set inert (+0x424 \|= 0x20000) | 0x0041e230 |

## 2. How it is drawn and animated

- Fx object class: behaviour `fx` = id 0x38, size 0x63c, vtable **0x005a52d8** (0x0045f020 case 0x38). Init
  0x00418f60 (puts it on list `+0xa84 -> +0xedc`), update vtable +0xbc **0x0041a350** (6,616 B), texture-stage
  setup vtable +0x28 **0x004196f0** (450 B), destroy +0xdc 0x00418db0.
- Mesh (skin bank, `fxo_snow`, `fxo_snow_2` identical geometry): 102 vertices, 160 triangles (one strip, 287
  indices), 16 segments, radius ~1.02, z -1.21..2.40 (open ends). Material texture `fx_snow_flakes.tga`
  (txtr `mfx_snow_flakes`), float 0.8 at material +0x48 (**guess**: opacity), uv0 u 0..12 (12 tiles around), v
  -5..1 (6 tiles in height). `fxo_snow_spray` uses `fx_snow_spray.tga`, radius 1.10. `p_fx` has `fog` = 1.
- World space, drawn as normal model objects: centred on the camera position, world-aligned axes for the
  position_only instance (camera rotation not applied).
- UV scroll per frame, 0x00419d30 (785 B, from 0x0041a350): layer record (0x20 B at +0x514 + 0x20*layer):
  +4/+8 direction = cos/sin of the angle (angle*16384/360 indexes the 0x614b00/0x614b08 table; 0x5bcba4 = 45.511),
  +0xc speed, +0x10 ramp in, +0x14 ramp out, +0x18 duration; `offset += speed * dir * ramp * g_frameDt`.
  UV units per second, frame-rate independent.
- 0x004196f0 builds a texture matrix per enabled layer (flags 0x100000 << layer-1) with 0x004195e0 (rotation +0x134,
  scale +0xa4/+0xa8, offset) and sends it through renderer virtual 22 (+0x58) and virtual 41 (+0xa4) for stages 0..3.
  So the Vulkan backend sees ordinary textured draws with a texture transform.
- Counts: 3 meshes, 480 triangles in total. No ParticlePool (0x0046d780) use: the fx contains no emit opcodes
  (0x1f, 0x27, 0x28, 0x29, 0x2d -> 0x0041e760).
- **"Snow bubbles"** (pop in, shrink, fade): not identified with certainty. Candidates, in order of likelihood
  (**guess**, needs a runtime A/B):
  1. The scale-1 `fxo_snow` (radius ~1) around a camera whose near plane is ~1.0 (camera.md): flakes appear where
     the cylinder crosses the near plane and shrink as the scrolled texture moves away along the curved wall.
  2. `fxo_snow_spray` in `fx_camera_l1_m1_locked` (full camera transform, scale 4, scroll speed 10 and 14 / 0.75
     and 0.5, fade out 3 s and 5 s, life 7 s and 10 s): a camera-locked burst at level start.
- World-unit speeds (**guess**, from UV density at scale 1: ~0.6 units per v tile): scale 6 at 0.2 UV/s is about
  0.7 units/s; scale 8 at 0.1 UV/s about 0.5 units/s. The angle (210..250 deg) mixes sideways drift (u) and fall (v).

## 3. Per level

From raw scans of every `*_inst.big` and `*_fx.big`:

| Level | camera_fx fx_name | Weather |
| --- | --- | --- |
| lev_1_m1 | `fx_camera_l1_m1`, `fx_camera_l1_m1_locked` | snow (only level with snow domes instanced) |
| lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5, lev_5_m5 | `fx_camera_locked_fade*` | none (fades); lev_1_m4 and lev_bonus_m1 banks carry the snow fx but no instance uses it |
| lev_5_m1 | default `fx_camera_l5_m1` (prop) | rain: `fxo_rain_1..3` via `fx_5_1_rain_N` (opcode 0x30 every 15 s, 0x05 create, scale 7, scroll 220 deg 1.5), `fxo_wind_spray` (scale 7, 120/140 deg), thunder/lightning fx |
| lev_5_m3 | `fx_camera_l5_m3` (trigger_start 1) | rain, same scheme |
| lev_5_m2 | `fx_l5_m2` (trigger 1) | **not checked** |

Rain uses the same mechanism, so a generic solution covers both. Snow tracks (footsteps `fx_fs_*_snow`, impacts
`fx_imp_*_snow`, `fx_cloud_snow_1`) are world-space puffs, not weather; leave them alone.

## 4. Turning the game's snow off cleanly

Do not edit data and do not skip the fx (camera path, intro overlay). Make only the weather meshes invisible:

- **Recommended**: hook **0x00423680** (294 B, fx opcode 0x13, only caller 0x00420770) and **0x004234d0** (opcode
  0x05, for rain's sub-fx): call the original, then if the returned object's name (0x00453270) is in a configurable
  list (`fxo_snow`, `fxo_snow_2`, optionally `fxo_snow_spray`, `fxo_rain_1..3`, `fxo_wind_spray`) call its vtable
  +0xf4 (what opcode 0x16 "invisible" does, 0x0041e280). Script flow, lifetimes and the camera path stay intact.
  Optionally also require owner (0x00423680: `*(ebx+0xb4)`) to have vtable 0x005ad058 (camera_fx).
- Alternative: in 0x004196f0 there is no draw to skip (it only sets texture stages); skipping draws needs the
  generic model draw path. Not recommended.

Parameters to read for the replacement (all available after the script ran, from the fx objects):

- Centre: camera position (camera +0x98/+0xa8/+0xb8), as 0x0040f750 uses. Area: the dome scales (1, 6, 8; rain 7)
  times radius ~1 and height -1.2..2.4.
- Direction and speed: per layer angle and speed (object +0x514 records), or parse `fx_camera_*` from the fx bank
  at level load (format known, tools/formats/big_kinds/fx.py).
- Intensity: number of domes, material opacity (0.8 snow, 0.99 spray), fade in/out (+0x474..+0x47c).
- Wind: no wind parameter exists; the only lateral motion is the UV angle. `fxo_wind_spray` is a separate dome.
- On/off timing: lifetime of the camera_fx fx and its kill trigger (+0x484); level unload destroys the objects.

## 5. Hook points, smallest first

| Address | Size | Role | State |
| --- | --- | --- | --- |
| 0x004102e0 | 82 | camera_fx: start its fx (a place to note "this level has camera weather") | S0 |
| 0x0040f750 | 231 | per-frame camera follow; gives the camera position each frame | S0 |
| 0x004101a0 | 248 | camera_fx init: fx name, position_only, triggers | S0 |
| 0x00420670 | 256 | opcode 0x40 uv scroll setup: capture angle/speed per layer | S0 |
| 0x00423680 | 294 | opcode 0x13 create attached object: **hide snow meshes here** | S0 |
| 0x004234d0 | ~ | opcode 0x05 create object (rain sub-fx) | S0 |
| 0x00419d30 | 785 | per-frame UV advance (reference for replacement timing) | S0 |

Recover first: 0x00423680 and its callees' ABI (0x0045f020 factory call, 0x0041cba0, 0x004539f0, 0x00453750),
then 0x004102e0 / 0x0040f750. 0x0041a350 (6,616 B) and 0x00420770 (10,512 B) do not need recovery for this.

## Open questions

- Which of the candidates in section 2 are the on-screen "snow bubbles" (runtime A/B by hiding each object).
- Runtime near-plane value in lev_1_m1 (decides whether the scale-1 dome is clipped).
- What opcode 0x30 / 0x19 (both 0x0041e4d0) schedule exactly (**guess**: repeat a sub-fx at an interval).
- lev_5_m2 `fx_l5_m2` content; lev_5_m1 prop default confirmed only for lev_1_m1's `p_camera_fx`.
- World unit scale (metres?) for converting UV speeds to fall speeds.

## Findings 2026-10-04 (runtime)

- **World axes:** Z is up. Measured: the camera's up axis in world space is (0, 0, 1) (archive/hook/enhance/weather.cpp
  fx_log output). The first GPU snow fell along -y, i.e. sideways.
- **The "snow bubbles"** are particles, not the cylinders: the camera-locked intro fx `fx_camera_l1_m1_locked`
  attaches an invisible `fxo_glow_red` emitter to the camera_fx object (parent pointer at object +0x34, written by
  the attach function 0x004539f0 at 0x00453a02). Its script (opcodes 0x27, 0x47 -45 deg, 0x24 speed 5..15, 0x25
  life 12) sprays particles along the view, towards the camera; it sets no lifetime, so it lives for the whole level
  and saved games restore it. A/B with its update skipped: the bubble is gone (work/shots/mute_ab.png).
- **Saved games** recreate fx objects through the object factory (0x0045f020, 12 callers), not fx opcodes 0x13 /
  0x05, so creation hooks miss them. Every fx object (vtable 0x005a52d8) updates through slot +0xbc (0x0041a350,
  thiscall, no stack arguments): the weather layer patches that slot with a trampoline that hides `fxo_snow*` and
  skips the camera emitter's update, whatever the creation path.
- **Recovered:** 0x004234d0 Fx_CreateObject (opcode 0x05), with g_fxObjectCreated; Fx_AttachMesh gained
  g_fxAttachAllowed (unused by the remaster now).
- **GPU snow** is a simulation (archive/src/rhi/vulkan/vk_snow.cpp): per-flake position and velocity in a storage buffer,
  born in the sky, terminal fall plus gusting, swirling wind with drag, respawned on landing (depth buffer) or below
  the volume; not drawn while the game is paused (the pause menu shows a frozen copy of the scene).

## Remaster engine (Bevy) port, 2026-10-19

engine/src/graphics/snow.rs (`SnowPlugin`, settings block `SnowSettings`: `snow`, `snow_amount`, `snow_tuning`) and
snow.wesl port the Vulkan snow above (vk_snow.cpp shaders to WGSL, same maths, constants and defaults; world Y up in
Bevy, so the ground plane is x, z). What Bevy forced: the pass is a render-world system in `Core3d`'s
EarlyPostProcess set after TAA (no motion vectors for the flakes, so they are drawn on the resolved image, as the
hook drew them after its AA at SceneEnd), reading the depth prepass (reverse Z: view z = -P32 / (d + P22)). The flake
volume is filled when the pipelines are first ready (not on the first prepared frame: the pipelines compile in the
background, and a fill consumed before then left every flake at the origin). Active while a live fx object named in
`snow_tuning.replaces` (fxo_snow, fxo_snow_2) exists; those are made invisible (marker `HiddenBySnow`, restored when
the setting goes off). Unlike the hook, the camera's flake emitter fxo_glow_red is not muted (player feedback FX1 wants
its flakes). Playtest state "snow": active, flakes, cylinders_hidden / cylinders_shown, frames_drawn.
