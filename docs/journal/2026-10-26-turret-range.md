# 2026-10-26: lev_bonus_m1's turrets fire late (branch fix/turret-range)

## Bug

Walk video of lev_bonus_m1 (work/review/v7_lev_bonus_m1.jpg). In the original, the stig gun tracers converge on Roman
from about 5 s, rocket captains' rockets hit him, and he is dead by ~6 s. The death menu shows at the end of the clip.
The remaster's Roman walked unharmed until ~11 s, then the fire started.

## Cause

Two rules were missing. Acquisition range, line of sight and accuracy were not the cause: every grunt has Roman as
target from the load frame (ai_target_tag), and the aim error is the same in both.

1. **Start turret.** A `mode turret` gunner is put on his gun by AI_ResolveStartObjects 0x00425e20
   (ai_02.cpp:204-232), straight into state 0x7d. There is no 0x7b mount, so no counter roll and no stig_fire_delay.
   He keeps the counters rolled at init (0x00427600 in Ai_InitGeneric, ai_01.cpp:2441, before any gun). Those are the
   infantry fire_num_* values: grunt_regular has 3..5 clips of 1 round. +0x494 is 0, so he fires on his first frame
   (original: stig fire at play frame 8). The 1-round clips are spent through the gun table's 1 s pauses
   (0x004292f0). Then stig_fire_delay runs, all counters are 0, so +0x48c flag 1 is set: the aim snaps and the shots
   are led (0x004284e0). Led fire starts at ~2.5-4.5 s.
   The remaster mounted these gunners through the 0x7b arrival: the stig roll, a 0.5-0.75 s delay, a second roll
   with a 1 s clip pause, then 3..5 unled 10-round clips. Its first led burst came at ~7 s. Bullets fly at 360 u/s
   and the gunners are 1000-1300 units away, so the shots landed at ~10 s. Unled shots miss a walking Roman: they
   land 100+ units behind him.
2. **Rocket captains on the tower roofs.** In state 0x3c (ai_06.cpp:1495-1531), when AIRoute_Build 0x0046f6d0 finds
   no route on the AI grid to a target more than 10 away, the captain runs the hiding-spot search
   (vt+0x240(0, 0, 1)). If that finds nothing he becomes a guard where he stands (0x0042d0f0). This does not apply on
   base-defence levels or while charging (+0x958). The original's captains go 0x3c -> 0x79 / 0x7a at play frames
   2..12 and fire rockets from the roofs. The remaster's walked off the roof edge ("Falling" at frames 42..110), then
   chased on foot without firing.

Also, roll_count now gives RandRange's result when the max is below the min.

## Evidence

- Oracle lev_bonus_m1_start, remaster side against the existing original trace. Scratch runs:
  ORACLE_OUT=work/turret/oracle, and a walking copy of the scenario, work/turret/bm1_walk.json.
  - Stig fire loops in the first 0.5 s: 0 before the fix, 12 after (original 12).
  - Forge cannon fire at frame 6 (original 6).
  - Roman -200 at frame 64 (original 66).
  - Rocket captains: Guard by frame 48 (original 0x79 by frame 60). Before the fix they were Chase, then Falling.
- Walking capture (made the way compare_videos.py does it, into work/turret/cap1). Sheet: work/review/turret_bm1.jpg.
  Tracers and rockets converge on Roman at ~7 s and he is down at ~8 s (original ~6 s). The death menu falls just
  after the clip.
- Temporary per-gunner trace (removed before the commit): a start gunner's counters were c 3..5 r 1 b 1 at frame 1.
  It fired at 0.08 s, paused 1 s per clip, then snapped and fired led bursts from ~2.8 s.

## Tests

- Playtests pass: gunner1, gunner2, gunner_lead, turret_kickout, hud_turret_start, vehicles_forge_cannon,
  gun_save_write / gun_save_read, gfx_effects_remaster, launch_newgame.
- regress.py: see the commit.

## Open / next

- Roman still dies ~1 s later than in the original on the walk. Two causes are left:
  - Stig fire: the original has a few loops in its 0.5..2.5 s window that the remaster does not have. Their source
    is not identified; maybe gunners whose target enters the 65-degree window later.
  - Rocket timing: on the standing scenario the remaster's rocket captains fire on the load frame (3 rockets at
    frame 2), while the original's fire at frames 8..16 after their 0x3c -> 0x79 switch. With that, the standing
    Roman now dies earlier than the original (frame 144 vs 390).
- The engine's chase still routes with nav.rs. The original's AI grid (navgrid.rs) is used only for the "no route
  -> guard" decision.
- Rendering seen on the sheet, not in scope: the explosion fireball is cut by a hard horizontal edge at the ground
  (frame 195), and the stig impact smoke is grey blotchy clumps.
