# Open questions

| Question | Evidence so far |
| --- | --- |
| Does the Steam build's `game.exe` differ from the retail 1.0 disc executable beyond the wrapper? | PE timestamp 2004-01-22, after the NA release (2003-12-02); may be a patched build. |
| What is `FileParser.dll` and does the game call it for archive access? | Linker 6.0 (VC++ 6), built 2003-04-16; different toolchain from `game.exe`. |
| Is the `.big` format a descendant of Giants `.gzp`? | Magic `DE C0 AD DE`; GiantZiP not checked yet. |
| What are `.jit` files? | One per level folder, 0.3 to 1.6 MB, header starts with a small integer. |

## Falling snow in the remaster engine (2026-10-13, partly resolved)

Snow now shows (synced walk frame 520). Causes found: the fx textures loaded with the default clamp sampler (the
domes' uvs run 0..12: repeat needed), the mask loaded as data, and an invalid WESL line (`let _ = x;`) that made the
layer shader fail to compile (watch for `failed to process shader` in the log: a failing shader draws nothing).
Flake colour resolved (2026-10-13): the stage maths as the original (LERP of the two flake layers by the mask
colour) times the stage alpha (flakes x mask), added. Because the texture is premultiplied this squares the flake
profile, giving the original's sparse round flakes; the earlier "white x alpha" guess drew too many faint specks.
Open: the mask's uv set (both uv sets tile it; the logged stage words carry no texcoord index, it lives in the
renderer slot at +0x640), left on uv set 0.

## The big red arrow (resolved 2026-10-13)

It bobs: its own anim moves Center z from 20 to 46 above the placement (loop 0.97 s). Without it the arrow sat
behind the arch's top and only the head showed. RigidAnim (level.rs) plays a rigid model's Center track; it now
matches the original's synced captures.
