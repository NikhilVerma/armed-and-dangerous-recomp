# 2026-10-26 (agent-ledge): Roman walks off ledges; the lev_2_m2 kegs are a path divergence upstream

Follow-up to docs/journal/2026-10-26-camera-collision.md (branch fix/camera-collision): the two walk-video glitches
blamed on the camera came from where Roman is.

## lev_4_m3 ledge: fixed

Cause: Jolt CharacterVirtual's stick-to-floor (`ExtendedUpdateSettings::default()`, step down 0.5 m = 2.5 units a
frame at METERS_PER_UNIT 0.2). It snapped him down the shelf's 5-unit drop in 3 frames, and the shelf's edge hid him.

The original (game_06.cpp:2620-2680, PlayerObject_Update): outside the jump state, the drive's wanted velocity is the
stick's turned by the rotation from up to the ground normal +0x574 (0x004798c5, Quat_FromTwoVectors 0x004f1830).
The drive 0x00471810 gets that normal as its direction, so its s^5 weakening only applies to a pull into the ground,
and a body leaving the ground's plane is pulled back at full strength. Gravity always acts on the Havok body.
0x00478200 keeps the last normal while he touches nothing. So he follows a slope and walks off a ledge with the
shelf's flat velocity, then falls under gravity.

Change (engine/src/physics.rs, engine/src/player.rs):
- `Walker::stick_to_floor` (on by default; the AI walkers keep it). Roman's walker turns it off.
- On walkable ground, his vertical velocity is the part that keeps the horizontal velocity in the floor's plane.
  Horizontally the drive is unchanged: the measured run speeds are horizontal.
- No gravity term on walkable ground. A first version subtracted this step's gravity, and Jolt turned that into a
  downhill creep of about 0.8 units/s while he stood still. tools/regress.py caught it: gunner1 and grunt_aim_turn
  failed, then passed again on the base and with the fix. In the original, friction_max 2 at rest holds him on any
  walkable slope.

Result (trace, play frames, work/ledge/l4m3.trace.jsonl):
- Flat at -36.5 until play frame 119.
- Then a gravity fall: -36.7, -37.0, -37.3 ... -42.7 over 13 frames.
- He lands on the slope below (nz 0.98) and stays grounded down it.
- Sheet work/review/ledge_l4m3.jpg (original | remaster at 54..64): his legs stay visible through f56-60 and there
  is no snap. The camera pulls in at f62, one step before the original's f62-64.

## lev_2_m2 kegs: path divergence, not fixed

The kegs are powderkeg 155 / 156 / 157 (scene ids), static colliders from prop_powderkeg_coll (an octagonal barrel,
belly radius 2.76 at 2.75 up, height 5.5, force-grounded to -66.75).

Remaster trace (W held from play frame 10, work/ledge/l2m2.trace.jsonl):
- At play frame 464 Roman meets keg 157 (game -13.99, -180.35) on its belly vertex, 0.8 units to his right (-x) of
  its centre. The contact normal rotates round that vertex.
- He slides right round the keg into a walkable gap rising toward the rock wall (floor nz 0.78-0.94, feet -66.8 ->
  -61.7). He then runs on between snow_pipe 192 and the wall.

The original's Roman stops behind kegs 157 / 156. Their surfaces are about 3.6 apart, less than his 4-unit diameter.

Where the paths split, from the original oracle trace work/oracle/lev_2_m2_start/original.jsonl (W from play frame
120) against the remaster's (work/ledge/l2m2s.trace.jsonl):
- The two agree to within 1 unit until play frame 170.
- On the side slope after that (the remaster's floor nz 0.64-0.76, at the 0.7 walkable limit), the original's Roman
  veers downhill (+x) and drops: frame 220 is (-63.1, 53.2, -57.3) against the remaster's (-65.6, 49.8, -53.3).
  That is 2.5 units further +x and 4 lower, with a path angle of up to 23 degrees against his 11.5-degree heading.
- Extrapolated to the kegs, the original meets keg 157 nearer its centre or toward the gap to 156, where he is held.

So the cause is Roman's controller on steep side slopes: the original's body slides downhill there and the
remaster's barely does. It is not fixed here because the mechanism is not decoded. With the fitted drive stiffness
(DRIVE_GAIN), gravity along that slope gives under 1 unit/s of sideways slide, and the original shows about 8.

Tried and reverted (no effect, or worse):
- Wall contacts taking the velocity into them before the ground friction. The keg case did not change.
- The drive's c taken along the ground normal (the original's direction argument, 0x00471810 with +0x574) instead of
  up. He climbed the side slope more (z -54.0 at frame 240 against -55.4), further from the original's -58.1.

Sheet work/review/keg_l2m2.jpg (original | remaster at 200, 240, 260, 280, 299) shows the unchanged divergence.

## Tested

- Playtests on target-wt-ledge, all pass: player_slope_l1m3 8/8, player_fall 9/9, foot_ik 8/8, foot_ik_drift 3/3,
  camera_follow 15/15, gfx_effects_remaster 16/16, geom_remaster 6/6, launch_newgame 42/42, jump_markers_bm1 5/5,
  bodyfall_fx 5/5, ai_fall 9/9, barracks_jumpers_l3m1 2/2.
- tools/regress.py --exe target-wt-ledge, run once, on the first version (gravity term). It failed the known set
  (campaign_chain, companion_carried, controls_rebind, goliath_legs, hud, hud_meters, q112_extras, taunt1, ticker,
  volumes, weapon_blackhole, weapon_mortar, weapon_shark; save_read / save_write passed) plus gunner1 and
  grunt_aim_turn. Those two pass on the base (2 runs each) and on the final version. The full suite was not rerun on
  the final version.

## Next / open

- Decode the force action pool (0x00455a30 -> ForceActionPool_Add 0x00403fb0) to learn the drive's real stiffness.
  Then port the steep-slope behaviour: no friction on contacts with nz <= 0.7 (0x00477c90), gravity along the slope,
  and the drive's c along +0x574. Check it against both player_slope_l1m3 (he climbs the nz 0.53 hill) and the
  lev_2_m2 side slope (he slides down it). Then recheck the kegs.
- The remaster drive's c is taken against up (player.rs, "the drive"), but the original passes the ground normal
  (game_06.cpp:2663). The steep-slope comment in player.rs rests on that and is a fit, not the decoded rule.
