# 2026-10-26: the AI grid router (companions)

Branch fix/grid-router (from fix/l5m3-quad 11ed0429). Target dir engine/target-wt-router (baseline build of
11ed0429: engine/target-wt-router-base).

## What was done

- **engine/src/navgrid.rs** (new): the original's AI grid Game+0xaa4, kept in the physics world (physics.rs
  `nav_grid`). It was a Bevy resource first. In Bevy 0.20 a resource is an entity, so one inserted at level load
  shifted every later entity id by one. That changed the order the grunts iterate in, and weapon_guyfawkes failed
  (its loyal grunt took another target). Kept in Physics, no entity is added and the test passes again.
  - Built at level load from the landscape (PathGrid_Build 0x0046ed30): 10-unit cells from the landscape bounds'
    minimum snapped down to a multiple of 10 (SnapTen); a triangle whose surface name has `nonpath_` / `_nonpath`
    blocks its cells, `path_` / `_path` keeps them open, any other steeper than 50 degrees (normal z < 0.64278,
    Plane_FromPoints 0x004e80b0 order) blocks them. Cells by clipping the triangle to each cell square
    (PathGrid_RasterizeTriangle 0x0046ea00, Poly_ClipToPlane 0x0046e830). The constants and the direction tables
    (0x005ce42c) were read from the executable's data section; the surface names are the export's `surface`
    (the material dictionary +0x70 the loader reads).
  - Objects' footprints (Object_MarkGrid 0x004552f0, Grid_RasterTriangle 0x00455000, DefaultGameObject_MarkGrid
    0x004509a0): level.rs `grid_mark` lists the classes that mark at load (buildings unless invisible, default
    objects with block_path, kegs, the pub, anim players, turrets, trees (guess)); unmarked when StageSwap unlinks
    the object, a husk marks again by its own class. A mode-1 default object's mark stays (the original refuses the
    unmark).
  - The search: Grid_FindPath 0x0046fd20 with Grid_LineClear 0x00470310 (Bresenham, the goal moved to the cell before
    a blocked goal cell, result 2), the A* exactly as the original's (4 directions never back, cost 1, euclidean
    heuristic, the 1-based heap's tie rules, AStar_Visit's quirks: an open node keeps its parent and heap place, a
    closed one goes back unchanged; a blocked goal neighbour ends at the current node's parent; AStar_Closest when
    the heap runs dry), Path_Smooth 0x00470100 (skipping the shifted point), at most 400 points, the start dropped.
  - Plans (AIRoute_Build 0x0046f6d0 / AIRoute_Plan 0x0046fa80): one point found becomes the target; several (at most
    5) go before the target, which is planned again from where the walker is when it is next.
- **ai.rs companions**: `formation_route` uses the grid with Roman's and the partner's cells kept out (0x00483d60);
  the route is built again every 0.25 s on the move only when the slot has moved more than 1 unit from the one routed
  to (+0xb70); reached points pop within 2.5 (AIRoute_CheckReached 0x0046f900); the pending target is planned when
  next (AIRoute_Update 0x00470aa0). The old 0.25 s re-plan timer (`replan`) is gone.
- docs/port/ai.csv: the grid rows ported / replaced / deferred; new feature `grid-routes-grunts` (the grunts' and
  marker runs' routes, the steering curve 0x004707a0 / 0x00470910 / 0x00493d00, the start node's turn, the
  characters' own cell marks).
- Debug: `NAVGRID_DUMP=x,y,radius` prints the grid round a game point at load; `AIDBG=1` logs each formation plan.

## Results

oracle_diff lev_5_m3_start --only remaster (original dump work/oracle-aiparity2, copied to work/oracle-router):

| | baseline 11ed0429 | grid router | original |
| --- | --- | --- | --- |
| Q off the original, frames 150-260 (mean / max) | 13.5 / 18.0 | 3.7 / 5.0 | |
| Q's x on the bridge, frames 210-260 | -3055.7..-3054.1 | -3045.5..-3045.4 | -3045.9..-3045.4 |
| Q from Roman at frame 240 | 29.5 | 38.3 | 39.5 |
| Q from Roman at frame 300 | 17.3 | 18.3 | 27.2 |
| first companion.q.pos divergence | frame 178 | frame 322 | |

The bridge course change is the grid's: columns 66 and 69 of the bridge are blocked (its rails / sides), so Q, who
starts in column 66, routes into column 67 (cell centre x -3045.41) and follows it, as the original does.

The stop: with Roman at y 3229.6 (where the original's Roman stops, 3229.7), the plan snaps Q's slot to
(-3045.41, 3254.45), exactly the cell the original's Q stops at (within 2.5: 3256.9). The remaster's Roman walks on
2.1 units (3227.6), across the cell line 3229.45, so the next plan snaps one cell further and Q stops 19.6 behind him.
The remaining difference is Roman's walk length, not the router.

Walk video (tools/compare_videos.py --only lev_5_m3, sheet work/review/router_l5m3.jpg, frames 0/120/240/270/285/299):
on the bridge Q trails in the original's column. At the end of the 10 s walk, Roman stops at a wall (y 2974.3). Q's
slot (ref_q_00, right-behind) is in a blocked cell (column 66), so Q stops in column 67, 22.3 behind Roman, where the
follow camera is (y 2997.0). He is not drawn there: the camera is inside him. In the original's frames 280-299 Q comes
up on Roman's left, a few units in front of the camera. **Not resolved.** Likely cause, unverified: the leader pick
0x00483810 on the move. In the remaster both companions trail at 49-50 units at frames 500-520 (delay_distance 50),
and Jonesy, the nearer one, is under 50, so no pick runs. If Q is the nearer one and both are beyond 50, Q gives the
lead to Jonesy and takes ref_molmer_00 on the left. The original's long walk has no companion dump to check against.

## Tests

- cargo test navgrid (7 unit tests: SnapTen, straight line, blocked goal snap, route round a wall by clear legs,
  closed-off goal -> nearest, steep / path / nonpath surfaces, avoided cells released).
- Playtests (--exe target-wt-router): companion_attack_order, companion_death_fail, companion_engage_exit,
  companion_fidget, companion_idle_cue, companion_leader_swap, companion_orders_regroup, squad_sees_companion,
  tower_l4m2, tower_l4m2_fail, camera_follow, gfx_effects_remaster, geom_remaster, launch_newgame, ai_fall,
  ai_sight_pursuit pass; companion_carried fails 1 (topsy_left), as on the baseline.
- companion_close_kill and close_kill_knocked failed with the router: Q's grid slot beside the pub (the pub's
  footprint, mode 2) is snapped to the cell next to it, which puts the barracks grunt chasing him within reach of his
  own close kill. That grab came before the scripted one. Both scripts now hold the companions' own targets
  (ai_force companions_hold) and pass.
- regress.py --exe target-wt-router (final build): the same failing scripts as 11ed0429 (target-wt-router-base):
  save_write, save_read, hud, hud_meters, taunt1, volumes, ticker, campaign_chain, companion_carried, q112_extras,
  weapon_mortar, weapon_shark, weapon_blackhole, controls_rebind. None new. goliath_legs failed in one run of each
  build and passed in another (flaky).

## Next

- The long walk's end on lev_5_m3 (above): a companion dump of the original's 10 s walk (tools/oracle_diff with the
  video keys) would settle the leader-pick question.
- Roman's walk length on lev_5_m3 (2.1 units long after a 2 s walk), which moves the stop cell.
- The walker's creep after stopping on the bridge planks (about 1 unit/s; the original stands still; also in the
  2026-10-16 traces, so not from this change). The cause is not traced; a guess is Companion::walk pressing down with
  vy -5 on the sloped planks.
- Feature grid-routes-grunts: move the grunts', marker runs' and close kill approaches' routes to navgrid.rs, the
  steering curve (0x00493d00), the start node's turn and the characters' cell marks.
