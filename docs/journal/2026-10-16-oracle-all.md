# 2026-10-16: the oracle on every level (agent-oracle-all)

Branch agent-oracle-all (from wip/full-decomp 6fa77c0). Remaster built in engine/target-wt-oracle-all, hook in this
worktree's build/hook-release. Every original run off-screen ([test] offscreen=1), every remaster run --background.

## Done

- Coverage on all 24 levels: tools/coverage_level.py `--level all`, generic scenarios from each level's export (start,
  fall, two enemy clusters, up to five objects by behaviour), play-frame keys, `--jobs`, `--resume`. Hook
  (testoracle.cpp): a keys-only run ends at play_frames and writes the coverage first. 200 runs, none failed.
  Registered functions executed: 2306 -> 2605 of 3491. Reached per game subsystem (before -> after): ai 270 -> 378,
  game 452 -> 583, ui 228 -> 230, audio 109 -> 129, anim 72 -> 83, camera 42 -> 43, render-fx 191 -> 198; input 54,
  save 22, movie 6 unchanged. 299 newly reached: 121 already ported, 156 already explained in docs/port/*.csv, 22 left
  (9 game: ui 2, anim 6, camera 1; 13 plumbing: math, physics glue, havok-ext, render-scene, engine_01):
  docs/port/queue-after-all-levels.md. Backup of the earlier coverage: work/coverage_before_all_levels.json.
- Oracle start scenarios for all 24 levels (tools/oracle_scenarios/<level>_start.json), run in both games; the
  divergences are classified in docs/remaster-engine.md "Oracle findings on every level" (B9-B20, C1, R1, E1, and
  more cases of B2 / B3 / B4); each scenario's `known` names them, all 24 pass with them, nine are in the suite.
- Harness: no movies in the remaster (`--movies` to a missing folder, as the original's no_movies), vehicles out of
  the grunt families, more AI states mapped, time from the first common line, pause ends compared, `--instance`,
  `--jobs` for oracle_suite, playtest.py `--arg`, instances.wait_while_player_plays.

## Learned

- The original's start message appears at load frame 0 on most levels, 20 frames later in the remaster; on the four
  base-defence levels the original's comes 279-399 frames after the load, after its start sequence (B10).
- With [test] no_movies the original still pauses for the start movie and needs a second ENTER (lev_2_m1, lev_5_m1).
- The remaster's oracle trace has no vehicles (oracle.rs lists ai::Grunt only); the original's lists colossus,
  mini_zeppelin and saucer as grunts.
- engine/src/ai/squad.rs leaves the squad goliath to another module that never spawns it (B11).

## Next

- Assign B9 (start weapon), B10 (start message timing), B11 (squad goliath), B12 (building IC grunts), B13 (bully
  state), B14 (lev_bonus_m1 opening), B15 (generators), B17 (sounds), B18 (early targeting) to their subsystem owners.
- Add vehicles to engine/src/oracle.rs's trace so they can be compared.
- Mid-level oracle scenarios (one per level's first encounter / objective), now that the start ones pass.
- lev_5_m5's object coverage runs all executed 1503 functions (Roman starts in the turret: the warp likely does not
  move him): give it hand-made scenarios.

## Open questions

- B19 (lev_1_m1_b sphere 376 at load, an id-0 object's death, Roman -15) and B20 (lev_5_m5 turret 166 trigger 90,
  lev_5_m1 id-0 death): what sets them off in the original.
- B17: does the pub's AudioLoop (level.rs:858) not start, or start without reaching SoundBank::started_keys?
- B10 base levels: what holds the original's start message 279-399 frames (the turret mount, the start sequence's
  timers, or a delay in the mission object)?
