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
The original is deterministic on these inputs (three runs: 22 hits before 1250, none on the chest, the first at 822).
The engine's chest hits mostly come after Roman's death; while he lives its hits spread over more parts (the right
arm and wrist: the original's barely), the 27th (fatal) hit comes ~20% sooner. The grunts' distances to Roman
match the original's within a few units through the fight (camera dumps, 840-1200). Not compared: the muzzle
point (engine: 7 above the feet; original: the weapon's emitter bone) and Roman's facing during the hit anim.
Checked and ruled out: the bullets' target (0x004a2010 gives each bullet its target and ref_hit_00 through
Projectile_SetTarget 0x0047e3b0) only measures the distance (0x0047e4d0) and does not steer or end a bullet: the
range removal in Projectile_Update needs vt+0x168, which is 0x00435250 (false) for proj_bullet (vtable 0x005a4438).
The facing is the same (the cameras, which follow his yaw, agree). Left: how the original poses the collision parts
in trotter_hit_small (the engine poses each part by its bone's skinning matrix; the original never has the chest hit
while the engine's is hit a few times during the anim).
Measured after: the hit parts' bones sit where the engine's do in the hit anim (hook log_hits with the bone's world
position: L_wrist, R_shoulder within ~0.25), and the hit points cover the same region (x -1.3..1.1, y -2.4..1.0,
z 3.9..4.3 around his position). What remains is a count: the engine lands ~3..5 more of ~40 shots before frame
1250 (25..27 vs 22). The original's hits never name the chest, neck, legs, R_arm or L_shoulder parts (three runs),
the engine's do. Ruled out: missing parts (the original's player has all 13, the camera dump's player_parts, in the
engine's order). Not measured: the original's part shapes' world placement (their Havok bodies via the +0x1bc
list), which would show whether the chest part sits elsewhere than the chest bone suggests.
How the original's ray reaches the parts (RayCollector_AddBroadphaseHit 0x00413a90, physics_01.cpp:2785): only when
the ray's broadphase meets the owner's main collidable (Roman: the radius shape plus two spheres, PlayerObject_
BuildCollision 0x00475220, collision_radius 2); with detail >= 2 the owner is posed as a ragdoll (0x00458970) and
every ragdoll body (+0x1bc records, built by fn_00456a90 from Game+0xa8c) is cast. So the hit shapes are the ragdoll
bodies' shapes; that they are trotter_coll_0's parts is inferred from their 13 bone names, not read. Lateral reach of
the hits: original |x| <= 1.57, engine <= 1.72 (not a gate effect at radius >= 2). The ragdoll bodies' shapes
(camera dump player_part_shapes): Havok boxes (type 11 in a 0x19 wrapper), convex radius 0.1, with the half extents
and centres of trotter_coll_0's parts: the engine's hit geometry is the original's, posed alike. The remaining
difference (a few hits in ~40 shots) is not explained by shapes, pose, aim, positions or fire rate.
Found since (fixed-step playtests, frame-exact): the engine's guards fired on their first cycle; the original's
first cycle is dry (fire anim, reload, then shots from ~800). Now: shots 44 vs 42, hits 27 vs 22 by 1250, first hit
853 vs 822. The engine's grunts fire in tighter groups (5 within 6 frames; the original's spread over ~37): their
acquisition and turning are more uniform than the original's (turn anims 0x0042e050, physics yaw 16 sin(error)).
Turn anims ported (d2aa9ef): shots 39 vs 42, first real volley 864 vs 800, hits 29 vs 22 (~74% vs ~52% of
shots). Checked: the scatter constant 0x005bcbfc is 0.1 (read from the exe); a lower muzzle (5 instead of 7 above
the feet) changes nothing. The original's misses are not logged yet (log_hits sees only object hits): logging where
its bullets end (Projectile_Update's ray hits on the landscape) would show whether they strike the ground first.
Logged since (log_hits' and-rayhits.txt): of the original's ~40 grunt bullets in 720-1250, 22 hit Roman, 11 the
ground, 4-5 a grunt standing in front of a shooter. The engine's shots came from the body's centre line and passed
beside that grunt; from the weapon hand (c8088ae) they hit it 4 times. Now: Roman 25 of 39 shots (original 22 of
42). Left: the muzzle's exact place (the weapon's emitter node is not exported; ~3 along the aim from ref_rhand).
Since: the muzzle measured in the original (and-shots.txt: ~6.9 ahead, 1.3..1.5 right, 8.4 up) and the dry
cycle tied to a turn: shots 40 vs 42, Roman's hits 24 vs 22 (frames 720-1250); the first fight's first arrow 683 vs
696. Open: the source of the dry cycle after a turn (guessed: engage -> state 0xa -> guard cycle, note #7).

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

## Companions dying in engine bot runs (2026-10-14)

Since companion death was ported (ccd9aec), Q and Jonesy die in most full-level bot runs (level1h: Q; level1j: both,
with the alarm troops). Their health is 500 (x Game+0x554, normal 1) and grunt shots hurt them in full, as traced.
Not compared: the hook's AI dump (World+0xe7c) lists grunts only (vtables 0x5a9cf0 / 0x5abb88), not Q / Jonesy.
Next: a hook dump field for the companions' health (+0xad0 / +0xad4 of the player), then the same scripted fight in
both games.
Measured (hook dump field "companions", testshot.cpp): first fight (compT, keys 500:MOUSE1, W held 510..1610), frames
900..2700: original Jonesy 500 -> 500, Q 500 -> 475, Roman 400 -> 340; engine (fightLc, same inputs) Jonesy 500 -> 490,
Q 500 -> 500, Roman 400 -> 250 at 46 s. Companion damage is of the same size: the deaths in bot runs come from the
bot's long fights and the alarm's troops. Roman takes more in the engine (the open question above).
In the warp scenario (warp=720) the companions stay at the start in the original (not comparable there).
