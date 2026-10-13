# Open questions

| Question | Evidence so far |
| --- | --- |
| Does the Steam build's `game.exe` differ from the retail 1.0 disc executable beyond the wrapper? | PE timestamp 2004-01-22, after the NA release (2003-12-02); may be a patched build. |
| What is `FileParser.dll` and does the game call it for archive access? | Linker 6.0 (VC++ 6), built 2003-04-16; different toolchain from `game.exe`. |
| Is the `.big` format a descendant of Giants `.gzp`? | Magic `DE C0 AD DE`; GiantZiP not checked yet. |
| What are `.jit` files? | One per level folder, 0.3 to 1.6 MB, header starts with a small integer. |

## Falling snow in the remaster engine (2026-10-13)

The original shows many bright small flakes (synced walk frame 520); the engine shows almost none. Known: the snow is
three camera-attached domes (docs/subsystems/weather.md), material log (`[test] log_materials`): t0 = t1 =
fx_snow_flakes, t2 = fx_wind_spray_M, stage 0 MODULATE tex*diffuse -> TEMP, stage 1 MODULATE tex*diffuse, stage 2 op
26 (LERP) by t2 between CURRENT and TEMP, alpha MODULATE(t2.a, TFACTOR), blend SRCALPHA / ONE. The engine implements
exactly that (engine/src/fxlayer.wesl), and the domes are created, follow the camera and cover the view (checked by
drawing them red). But the exported fx_snow_flakes (DXT3, 256x256, 1 mip) has nearly black colour (mean 31 where
alpha > 0, max 206) and sparse 4-bit alpha, so the additive result is near zero. Open: is the export of this DXT3
texture right (decode the raw blocks), or is my reading of the op / argument bytes wrong (e.g. the alpha argument
of stage 2, or TFACTOR / a COMPLEMENT / ALPHAREPLICATE modifier on the flake argument)? Next step: dump the D3D
texture stage states and the texture the original binds while the snow draws ([test] d3d_dump_frame) and
reproduce one frame of the dome offline.

## The big red arrow's shape (remaster engine, 2026-10-13)

The arrow over the pub (bigredarrow 258) now draws and is removed by trigger 900, but at 1175 units (synced walk
frame 520) the original shows the full arrow (shaft and head, facing the camera, bobbing), the engine only a small
head. Its model is a rigid mesh in the node m_bigarrow (rotation 90 degrees about x in anim bigredarrow_0, which
also bobs Center with 30 keys): check whether the exported positions need that node transform, and play the bob.
