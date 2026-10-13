# Open questions

| Question | Evidence so far |
| --- | --- |
| Does the Steam build's `game.exe` differ from the retail 1.0 disc executable beyond the wrapper? | PE timestamp 2004-01-22, after the NA release (2003-12-02); may be a patched build. |
| What is `FileParser.dll` and does the game call it for archive access? | Linker 6.0 (VC++ 6), built 2003-04-16; different toolchain from `game.exe`. |
| Is the `.big` format a descendant of Giants `.gzp`? | Magic `DE C0 AD DE`; GiantZiP not checked yet. |
| What are `.jit` files? | One per level folder, 0.3 to 1.6 MB, header starts with a small integer. |

## Parked for the human (2026-10-13)

Things that need you, collected instead of asked mid-work:
- Audio by ear: the sound log matches the original's on the same inputs, and the mixes rebuilt from both logs
  (tools/sound_mix_compare.py: bank volumes, positional loops left out) have the same peaks and balance; the
  remaining differences are random picks (the opening ambient voice line) and positional sounds whose distance the
  logs do not carry. Listening is still unchecked.
- Window grunts: verified (2026-10-13): the hook's camera dump now lists the original's AI positions; all four
  window / roof grunts stand within 0.1 units of where the engine places them.
- Feel: mouse look verified (the original's measured 0.0100 / 0.0057 rad per count, 8be95d6); zoom matches its
  camera dumps; grunt difficulty and Roman's damage are the data's values, not judged by play.

## Companions kill grunts the original's only wound (2026-10-14, mostly resolved)

Measured with the hook's AI dump (target +0x588, health +0x454, anim +0x498; run fightT): the companions take the
archer at (-173, 744) at frame 840 and leave it at 74 health; Jonesy fires 3 bursts of 4, reloads, throws TNT
(anim 39 molmer_throw_tnt). Causes found and fixed (287ee9d): grunts' shots hurt grunts in the engine (the original's
0x00430340 ignores same-team damage), Jonesy's clip (fire_alt wastes a round), targets never dropped, the whole
capsule counted as a hit, companions kept running toward the target while firing; Jonesy's TNT throw ported
(3d6c810). The archer now ends at 72..88 health in three runs (original 74). The guessed part radii were
replaced by the original's collision models (fd3ffb5; hitparts.rs); the bone of each box is inferred.

## Grunts hit Roman more often than the original's (2026-10-14, open)

Second encounter (hook [test] warp=720:0,-350,60, Roman standing before the courtyard's grunts; engine fight2E):
the same number of shots (original 42 in 720-1250, engine 41..48), but by frame 1200 the original's Roman has lost
225 (15 hits of ~38 shots, ~40%) and the engine's ~390 (~26 hits, ~65%). Checked equal: the grunts' aim inputs read
at runtime from the original (+0x5d8.. : scan 350, hear 40, height 0.5, angle 15, accuracy 70 / 500, infinite 210,
no 0x400000 flag), no shooter limit (Game+0xc50 0) and no fake-bullet flag; the aim code 0x004284e0 and the
collision parts (trotter_coll_0, bones from the skin data). Not checked: where the original's bullet ray meets
Roman (its hit part and point), whether the detail-level-2 ray uses every part, Roman's ref_hit_00 in the original
pose.
Measured since ([test] log_hits, the camera dump's player_hit): the original's Roman takes 22 of 39 shots (56%):
L_wrist 13, Center 5, R_shoulder 2, R_wrist 1, L_arm 1, never the chest, all at 3.94..4.26 above his position; the
engine's 27 of ~38 before his death (~71%), 12 on the chest, at 4.0..4.46. His ref_hit_00 (the aim point): original
(-0.2, -0.2, 4.2), engine (-0.23, -0.01, 4.31). So the aim point sits ~0.1 higher and 0.2 further back in the
engine: Roman's pose differs a little (which anims the original plays after the warp is not known; the engine
plays trotter_aim with the pitch blend). The anim slots (+0x248) show the original in trotter_hit_small most of
the fight; the engine cancelled it on the knockback's hop (fixed): chest hits 12 -> 5..6, death 1173..1289 vs
~1320. Still more hits than the original's (about 25 vs 22 before frame 1250).

## Falling snow in the remaster engine (2026-10-13, partly resolved)

Snow now shows (synced walk frame 520). Causes found: the fx textures loaded with the default clamp sampler (the
domes' uvs run 0..12: repeat needed), the mask loaded as data, and an invalid WESL line (`let _ = x;`) that made the
layer shader fail to compile (watch for `failed to process shader` in the log: a failing shader draws nothing).
Flake density resolved (2026-10-13): the too-dense fine flakes were the radius-1 fxo_snow dome, clipped by the
original's near plane of 1 (the engine camera used Bevy's 0.1). Still a guess: the flake colour (white x flake alpha x
mask alpha, added); the stage maths taken literally leave the premultiplied flakes nearly invisible (tried:
work/remaster/render/snow_variants.png). The mask's uv set is not known (both sets tile it; the texcoord index lives
in the renderer slot at +0x640, not in the logged stage words); left on uv set 0, which matches the captures best.

## Penguins (2026-10-13, open)

PenguinObject (vtable 0x005a1768, anims 0x0048fc90: pngn_stand, walk, run, salute, scan_chat (note `fx fx_penguin`
at 0: the calls), scan_chat1, blown_up, land_dead; screams 0x0048fd80) is not ported: the engine's 10 penguins
stand in their bind pose. The generic fidget (0x00427330) needs anim 0xf playing, which the penguin table maps to
penguin_error_anim, so the calls must come from penguin-specific update code (not read). The original's sound log
shows every penguin calling at once at load and again near frame 238.

## The HUD health figure's tint (2026-10-13, open)

The original's green Roman figure (ui_player_health, b_health band 0) reads (106, 176, 56) on the synced frame
against the texture's (99, 191, 0) and ours (91, 182, 11): slightly muted. The per-channel alpha does not fit one
blend factor, so the skin material's colour or blend (hud.md open question 4) is probably involved. Position, size
and the other HUD parts match (work/remaster/render/hud_zoom.png).

## The big red arrow (resolved 2026-10-13)

It bobs: its own anim moves Center z from 20 to 46 above the placement (loop 0.97 s). Without it the arrow sat
behind the arch's top and only the head showed. RigidAnim (level.rs) plays a rigid model's Center track; it now
matches the original's synced captures.
