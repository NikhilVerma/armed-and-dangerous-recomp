# 2026-10-26 (agent-slope): Roman on steep side slopes, the drive decoded

Follow-up to docs/journal/2026-10-26-player-ledge.md (the lev_2_m2 kegs: a path divergence on a side slope).

## The decode

- The drive 0x00471810 is called (game_06.cpp:2663) with eax = wanted velocity, ecx = body velocity +0x4c0,
  edx = 0x005fb9a4, and stack radius = max speed, strength = mass x power, a2 = mass, a3 = +0x574, power = 5.
  0x005fb9a4 in game.exe .data is (0, 0, 1): **the direction is up**. +0x574 is a3, which the function does not read.
  The ledge journal's "the drive takes the ground normal as its direction" was a misread.
- It returns a2 / strength' = 1 / power', and 0x00455a30 passes that to ForceActionPool_Add 0x00403fb0 as the
  element's duration (+0x24; delay +0x20 = 0) with flags 0x12. ForceActionPool_Update 0x00404140 runs it per substep:
  above 0.2 s remaining (0x00589178 = 0.2) a force, at 0.2 s or less (flag 0x10) one impulse vector x remaining, then
  release. On foot power' is 5 or more, so the duration is at most 0.2 and the action is one impulse
  d x mass x power' x s^5 x (1 / power') = **d x mass x s^5**: each frame the velocity moves by d x s^5 (d = wanted -
  velocity, s^5 the uphill weakening). The pool replaces the action every frame (the previous handle is released).
- So the uphill weakening multiplies the step, it does not sit inside an exponent as player.rs had it. Walking across
  a side slope, gravity (160.9 units/s^2, jump_anti_gravity 2.5) pulls him downhill; the correction of that slide
  points uphill, c = sin(slope), and only s^5 of it comes back each frame. On normal z 0.72 that settles at ~8
  units/s down the slope (~6 sideways): the original's slide (the ledge agent measured about 8).
- 0x00477c90: contacts with normal z <= 0.7 get friction 0, others +0x5a0 (the 0x00478200 blend; friction_min once
  the grounded grace has run out).

## Change (engine/src/player.rs)

- The drive: wanted velocity in the ground's plane on any ground (was: only steep ground); velocity += d x s^5 x
  `drive_response` (the measured share the body keeps on level ground: the old DRIVE_GAIN fit, now only the flat
  response, s^5 outside it); d in 3D (the vertical part is no longer zeroed on walkable ground).
- The body: gravity always, then `ground_contact`: the velocity into the ground is removed and Coulomb friction mu x
  that normal impulse comes off the velocity along the ground (`ground_friction`: 0 on steep contacts). On level ground
  this is the old friction (mu x g x dt); standing on a walkable slope friction_max 2 holds him (no creep).
- On the ground the body keeps the velocity its step achieved (was: steep ground only).
- Replaced: the walkable-ground rule "vertical velocity = along the plane, no gravity", and the fitted steep-slope
  comment ("about 60 degrees is the most he climbs").

## Result

Trace of lev_2_m2 with the oracle's keys (W 120..240; work/slope/v1.trace.jsonl, compared with
work/oracle/lev_2_m2_start/original.jsonl):

| play frame | original | remaster before | remaster now |
| --- | --- | --- | --- |
| 200 | (-68.19, 64.90, -53.77) | (-70.65, 61.49, -50.18) | (-68.26, 62.94, -52.32) |
| 220 | (-63.14, 53.21, -57.28) | (-67.90, 48.73, -50.56) | (-62.72, 50.74, -56.52) |
| 240 | (-59.74, 40.81, -58.10) | (-65.11, 36.38, -52.21) | (-59.30, 38.94, -57.65) |
| 300+ | (-59.53, 40.19, -57.92) | (-64.38, 36.34, -52.95) | (-59.30, 38.94, -57.65) |

Same-frame distance: worst 9.9 before, 2.9 now (1.3 from frame 250 on). Most of what is left is along the path: the
remaster starts ~2 frames earlier (1.5 units ahead from frame 130, before any slope). Distance to the nearest point
of the original's path: 0.2-0.8, except 2.1 / 2.6 at frames 200 / 210, where the remaster's Roman leaves the ground
over the crest about 2 units higher.

The walk video's input (W 10..630, work/slope/v1video.trace.jsonl): he now ends held between powderkegs 157 and
156 at (-9.0, -181.6) from play frame ~490, as the original's Roman is held at the kegs (was: past keg 157 to the pipe).
Sheet work/review/slope_l2m2.jpg (original | remaster, video frames 200/240/260/280/299, capture
work/slope/cap_v1 made with compare_videos.capture_remaster): both are held at the kegs before snow_pipe 192. The
remaster's camera is closer and its Roman stands a little more between the two kegs than the original's, which
stands at the right keg's right side.

## Tested

Playtests on target-wt-slope, all pass: player_slope_l1m3 8/8, player_fall 9/9, foot_ik 8/8, foot_ik_drift 3/3,
camera_follow 15/15, gunner1 10/10, grunt_aim_turn 4/4, stop_fx 2/2, launch_newgame 42/42. The full regress suite
was not run.

## Next / open

- The crest at play frames 200-210 (2 units higher) and the exact held spot at the kegs.
- The drive's force branch (power' under 5: a knockback's slow-down, the jump power 4) is not ported as the decode
  gives it (a force over the frame, then no impulse); the jump keeps its fitted air control.
- The 2-frame earlier start of the walk in the remaster (already there before this change).
