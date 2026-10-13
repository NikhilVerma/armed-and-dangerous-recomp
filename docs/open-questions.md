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
Still a guess: the flake colour. The flake texture is DXT2 (premultiplied, exported as DXT3 unchanged) with dark
colour; the stage maths of the material log leave the flakes nearly black, so the engine uses the material's
white times flake alpha times mask alpha, added in gamma space. The engine's snow is a little denser and streakier
than the original's sparse round flakes.

## The big red arrow's shape (remaster engine, 2026-10-13)

The arrow over the pub (bigredarrow 258) now draws and is removed by trigger 900, but at 1175 units (synced walk
frame 520) the original shows the full arrow (shaft and head, facing the camera, bobbing), the engine only a small
head. Its model is a rigid mesh in the node m_bigarrow (rotation 90 degrees about x in anim bigredarrow_0, which
also bobs Center with 30 keys): check whether the exported positions need that node transform, and play the bob.
