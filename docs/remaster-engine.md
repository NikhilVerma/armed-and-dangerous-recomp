# Remaster engine: how to continue the port

The new engine (`engine/`, Rust + Bevy + Jolt) re-implements Armed and Dangerous by hand from the recovered code in
`src/`. The human's rule (docs/decision.md 2026-10-13): **one piece at a time, each piece properly ported and
verified against the original before the next.** This file is the working handover: the order, the verification
loop, the commands, and the traps found so far. Keep it current at the end of every session.

## Porting order (the human's, 2026-10-13)

1. **Rendering** (in progress): terrain layers and lightmaps (snow), materials (environment / specular maps: Q's
   metal), fog, lighting, sky, snow particles, characters' look. Verified by same-camera captures (below).
2. **Player movement**: Roman's walk / run / jump, camera follow. Verified by the walk test (below).
3. **Sticky bomb**: throw, ballistic arc, homing toward the nearest target, sticking (game_07.cpp 892-1270).
4. **Gun mechanics**: fire gate, recycle, clips, reload, spread, projectiles, damage, head shots.
5. **Zoom** and the other weapon modes; then the rest (ragdolls, decals, tokens, save / load, cheats ...).
6. Later levels' mechanics (jetpacks, capture the flag, escorts ...) the same way, each its own piece.

Known open differences, each a piece: aim blend (the original blends four aim poses tl / tr / bl / br by the aim
direction, ids 0x21..0x24 in fn_00428980; the engine plays one, so guns point up), treasure tokens (pickup_treasure1: "You have found a token! ... 1 out of 21"), the grunts' shot cadence (fire anim 0.47 s, engine 0.6 s).

## What "done" means (the human, 2026-10-13)

- **Subjective:** the new game looks and plays the same as the original **or better**, as a faithful remaster /
  remake. If a score is poor but the game looks subjectively better and faithful, the score is irrelevant.
- **Objective:** no unexpected behaviour, bugs, crashes or glitches.

## The verification loop

A piece is done only when its comparison with the original shows no difference that matters. Guesses stay labelled
in the source (`guess:`), with the evidence that would settle them.

### Synced walk comparison (use this first)

Both games get the same inputs from the same point: the original with `tools/shot.py --scenario level --keys
"500:MOUSE1,510:17*300"` (dismiss the briefing at frame 500, W from 510), the engine with the playtest script
work/remaster/play/walkA.json (the same timeline at 60 fps; it writes `walkA.<frame>.png` and
`walkA.<frame>.camera.json`: the engine's camera and Roman in game space). Run the engine at `--size 640x372`
(the original's letterbox strip) so the framing matches, then `tools/render_compare.py work/shots/walkA-520.png
work/remaster/play/walkA.520.png`. Compare the cameras as numbers first (the original's dumps vs the engine's
`.camera.json`): on 2026-10-13 they agreed within 7 units and 1 degree, and Roman's walking speed matched.

Results (frame 520), see work/remaster/render/sync_*.png: first baseline mae 60.6 / psnr 10.7 dB; with the landscape's original materials, the
original camera FOV and synced timing: mae 29.8 / psnr 16.0 dB. After the materials on every object, per-vertex fog,
no far clip and blended alpha (2026-10-13): the numbers barely move (mae ~31) but the look got closer (snowy
ground, distant hills, bare sparse trees); the human's rule: the look decides, not the score.

Latest (after the camera nodes, the aim blend and blue-first colours): synced frame 520 mae ~26, psnr ~16.8 dB;
by eye the frames now match closely (work/remaster/render/final_520_pair.png, token_815b.png).
After tonemapping off, the sky's horizon disk and the near plane (2026-10-13): mean colour within 2 levels of the
original (133/149/142 vs 135/151/144), horizon gradient matching row for row, snow flakes of the original's size
(work/remaster/render/tonemap_none_520.png, snowF.png). Still different: a little more snow near the top of the view.

Earlier list of open differences at 580 (several fixed since: arch scale, token, aim pose, camera): the ground's shading (engine brighter cyan, stronger normal-map relief;
original softer blue-grey), the ruined arch
looks smaller in the engine (unexplained: check its instance transform / scale), the token pickup's model, Roman
aim pose and his screen position (camera 4-7 units off: check Camera_Update 0x0040e350).

### Oracle runs: the original as ground truth (lockstep traces, 2026-10-15)

The human (2026-10-15): "the Oracle is always in front of us, as we have a known source of ground truth". Every
remaster bug can be found by running the original on the same input; `tools/oracle_diff.py SCENARIO` does that: it
runs the original (off-screen) and the remaster (--background) on one scenario and prints the first divergence per
field family, earliest first, each with where to look (the remaster's file, the original's function).

```bash
.venv/Scripts/python.exe tools/oracle_diff.py walk --exe target-NAME        # both games, ~1 min
.venv/Scripts/python.exe tools/oracle_diff.py walk --only remaster          # after an engine change: reuse the original's trace
.venv/Scripts/python.exe tools/oracle_diff.py walk --no-run                 # re-diff the traces in work/oracle/walk/
.venv/Scripts/python.exe tools/oracle_show.py walk roman.pos --every 6      # one field side by side (grunt:67.mind, events, ...)
.venv/Scripts/python.exe tools/oracle_suite.py --exe target-NAME            # every "suite" scenario, one line each
```

- **Off-screen original** (`[test] offscreen=1`, hook/src/testoffscreen.cpp; tools/compare.py writes it for every
  harness run, so shot.py, coverage.py, coverage_level.py and the rest never show a window): the game's window is
  created at (-20000, -20000) as a tool window (no taskbar button, no Alt+Tab), never activated; the game's own
  SetWindowPos calls (Renderer_ResizeWindow 0x005080d0 passes SWP_NOSENDCHANGING with insert-after HWND_TOP; 0x00508210
  asks for HWND_TOPMOST) are rewritten to stay there at the bottom, and WM_WINDOWPOSCHANGING holds everyone else's;
  SetForegroundWindow / ClipCursor / SetCapture are no-ops, GetCursorPos reads a point outside the window (the menus'
  mouse, 0x00491380), MessageBoxA is logged instead of shown, DirectInput gets only scripted input, and the process's
  audio sessions are muted. Pause on focus loss (App_WndProc 0x004e0110 -> 0x005fb918 -> AppBase_UpdateActive) never
  fires: deactivations reach the game as activations and both flags start at 1 in the image. Frame pacing: the
  virtual clock steps 16.6667 ms (166667 ticks of 100 ns, not exactly 1/60 s) per presented frame, so speed does not
  matter; the remaster's scripted runs step the same (main.rs ORACLE_FRAME_STEP, 2026-10-16: a 1 s countdown runs
  out on frame 60 in both, where f32 steps of 1/60 took 61). compare.launch refuses a hook DLL without
  this support, sets an error mode with no crash / "Bad Image" boxes, and kills a game whose visible window reaches
  the desktop (a guard thread, 10 ms polls). Measured: the window stayed at -20000 for the whole run, no foreground
  change (first build, without the SetWindowPos rewrite, showed the window at 0,0 for one run: fixed).
- **One input format**: the hook's keys_frames text (`frame:KEY*held`, scan codes, MOUSE1..3, MOUSEX<n>), read by the
  hook ([test] keys_from_play) and by the remaster (engine/src/oracle.rs, a playtest script's `"oracle"` section).
  Frame 0 is `play_delay` (30) frames after the briefing (the first paused message after the load) appears; the
  scenario's `0:ENTER*3` dismisses it. `warp` puts Roman at a point at play frame -10 in both, keeping his heading.
- **Randomness**: the original's CRT srand runs once in App_Init from GetTickCount (virtual: deterministic) and again
  with 12345 at the end of the level load ([test] reseed_on_load): two runs of the original give identical traces
  (checked: sticky, 211 lines). The remaster's streams are its own (fx::FxStats seeded 0x5eed per level, fxvm CrtRand,
  level.rs CrtRand(0)); reruns agree except a sound's start frame by one or two. Same-seed parity with the original is
  not possible, so roll-dependent outcomes (AI choices, spread, sound variants) get tolerances, not bit equality.
- **One trace schema** (`and-trace.jsonl` / `<script>.trace.jsonl`; hook/src/testoracle.cpp documents it): every
  `trace_every` frames from `trace_from` (default -play_delay: the briefing on; every frame before frame 0): game
  time, paused, Roman (position, heading, health, weapon, clip, ammo, stickies; the remaster adds look_yaw), the
  companions, each grunt (instance id +0x396 / InstanceId, position, health, state +0x460 mapped to ai.rs Mind,
  target), mission state and objective tag, and the events since the last line: triggers sent with their senders
  (Object_SendTrigger 0x00461870 / mission.rs dispatch), placed objects that died (0x00450ee0, 0x0041c570 / a
  destructible gone), sounds started (definition names; the original's come from the recovered play functions
  fn_004f32f0 / fn_004f3740 / fn_004f3c60, switched on for that while every other function runs the shipped code).
- **The diff**: tolerances per scenario (`tolerance`: positions in units, discrete fields and events within N frames,
  sound variant numbers ignored, pauses shorter than 4 frames ignored). A diverging family is EXPECTED (an accepted
  difference, `expected`), KNOWN (an open remaster bug below, `known`: it does not fail the scenario, so the scenario
  still guards everything else) or DIVERGE. Scenarios: tools/oracle_scenarios/; the three tree grunts (instance id 0,
  state 0x8c) are left out everywhere (grunt-ai.md section 14). `tools/oracle_suite.py` runs those marked `"suite"`
  (walk, walk_turn, jump, fire_rifle, sticky: all PASS with their known bugs on 2026-10-15); separate from regress.py
  because each scenario also loads the original (about 35-120 s wall). pub and courtyard are exploratory (AI rolls).
- **Finding the code behind a difference** (agent-aiparity2): a scenario's `"hook_test": {"trace_calls":
  "Name1,Name2"}` (up to 8 HOOK names, e.g. GenericAI_SetTarget, Object_SendTrigger, fn_004976a0, Object_LoadProperties,
  ObjectSystem_CreateObject) makes the hook log one `call` line per call in original.log: frames since the load and play
  frame, the return address (tools: map it with symbols/functions.csv), ecx / eax / edx / ebx and three stack
  arguments, each shown as `#id` when it is a placed object and `"text` when it points at text. Wrappers stack
  (hooks.cpp HookWrap), so the oracle's own event hooks keep working. Each grunt line also carries `kf` (1 while the
  body is keyframed, Body_isKeyframed 0x00457790). Run the original alone: `ORACLE_OUT=... oracle_diff.py PATH.json
  --only original --instance NAME` (a scenario file anywhere).

### Oracle findings on lev_1_m1 (2026-10-15, first runs)

Evidence: work/oracle/<scenario>/report.txt and the traces (`tools/oracle_show.py`).

Fixed on 2026-10-15 (agent-oraclefix; docs/journal/2026-10-15-oraclefix.md): B1, B2, B3, B4, B5, B7; open: B6, B8 (AI),
B9, B10 (new, below).

| Id | Remaster bug (first divergence) | Evidence |
| --- | --- | --- |
| B1 | Roman is still sliding when the briefing appears and keeps going ~4.8 units after it is dismissed with no input; the original's Roman is at rest | walk roman.pos: original y 1059.67 from frame -30 to 30; remaster 1057.8 at 0 -> 1053.0 at 30. Follows: the stop / slide foley at other frames, the treasure token reached 10 frames early (timing at 154) |
| B2 | Trigger volume 269 (bb_300x300, a 20-unit slab rotated ~13 deg) is tested as the world AABB of its corners (mission.rs Volume::Box from `world_aabb`), so trigger 711 and Jonesy's jon50001_l1_ambient fire 4.3 s into the intro | original: 711 sent by 269 at walk frame 48 (Roman at y ~1048.5 after 18 frames of W), running jump frame 110, never when Roman stands (fire_rifle); remaster: "volume 269 -> trigger 711" at 4.32 s, before the briefing. Use the oriented box (world_corners) |
| B3 | Grunt 181 (grunt_regular, placed at z 10.4) stands at z 108 (on a roof) | every scenario, frame -30: original (19.39, -548.29, 13.97), remaster (19.25, -548.34, 107.98) |
| B4 | Roman's model heading stops at exactly 0 deg (+y) while his look yaw keeps turning | walk_turn: 40 frames of 10 counts turn the original 229 deg (model heading -179.5 -> 49.6) and the remaster's look_yaw 229 deg (-180.5 -> -409.7), but its Transform heading holds 0.0 from frame 88 to 136 |
| B5 | Trigger volumes are evaluated while the briefing pauses the game | courtyard: 98 at frame -9 (the warp, still paused) vs 2 in the original; pub: 810 likewise |
| B6 | Far grunts react in the original, not in the remaster | sticky: grunt 201 (~450 units away) takes Roman as target in state 0x1f at frame 150 after the blasts; jump: grunt 208 targets Roman at 138 after the running jump  **2026-10-15 (agent-infantry):** sticky: Roman's sticky bomb now posts the grunts' noises (its start effect's, following it, and its end effect's at the blast: ai.rs Interests::follow / end_at from sticky.rs), so 201 takes Roman (remaster ~136 vs 150, then it chases where the original's takes a generic cover slot, state 0x1f: the slot search of World+0xed4 is not ported). jump: 208's sight ray to Roman's ref_hit_00 at the jump's peak is blocked by the landscape ridge near (-190, 865) by ~3 units; the original's query includes ownerless shapes (Query_Init reset: +0x3c = 1), so the landscape blocks there too: a geometry / ref_hit height difference, not the AI's logic (open) |
| B7 | After the pub the original holds the machine gun (weapon 0, clip 100) from frame 322; the remaster keeps the rifle. The pub screen: the remaster leaves on the fifth ENTER (900 at 382), the original only on ESC (900 at 472) | pub roman.weapon / clip, events.triggers |
| B8 | Guards: the original's stay in state 0x79 (Guard) while firing; the remaster's switch to Reload. First hit on Roman at frame 14 in the remaster (15) vs ~30 (45) in the original, then less damage (295 vs 235 health at 196) | courtyard grunt:175.mind, roman.health  **Fixed 2026-10-15 (agent-infantry):** a guard engages from Mind::Guard (state 0x79 running the fire cycle with may_move clear, ai_06.cpp:2664-2673); a clip's end plays the reload set in place and re-rolls the counters at its end. courtyard: grunt.mind now Guard throughout; the damage still differs (310 vs 235 at 198): the guards' acquisition order differs (original 180 at ~22, 175 at ~54; remaster 175 at 6), an AI roll / cone timing question, open |
| B9 | The companions do not idle at their formation slot while Roman walks off: the original's stop ~20 units behind him from frame ~38 to ~54 (walk), the remaster's run on (ai.rs companions arrive only within 4 units of the slot). Seen once B1 was fixed | walk companion.jonesy.pos at 54 (12.2 apart) |
| B10 | Footstep notes after a start / stop come at other frames: after the briefing stop the original plays the run cycle's fs_* notes at 2, 6, 18, 20, 36, the remaster at 12, 16, 32 (charanim.rs notes of the run / stop blend); the walk's step notes drift by up to 6 frames | fire_rifle / walk events.sounds |

How B1-B7 were fixed (each cause found with the traces; the scenario files no longer list them):
- B1: the ground drive was far too soft and nothing slowed Roman but the drive. Measured every frame on the original
  (a `stop` scenario: walk 1 s, release, strafe, back): full speed in a frame, a dead stop on release, a 39.2 units/s
  walk. player.rs now has the drive gain (DRIVE_GAIN, fitted) and the ground contacts' friction (+0x5a0: friction_max
  2 at rest to friction_min 0.3 at full speed, x gravity; it sets the 39.2); the slide / stop fx read the speed
  measured before the drive (+0x4bc). Also the briefing: the original pauses on the frame the intro path ends, Roman
  still walking; the remaster gave him one unpaused frame without the stick. The forced stick now lasts until the
  path's text comes up (campath.rs until_text; ordering the mission before the player instead shifted Bevy's order of
  unrelated systems). Intro trace (trace_from -900): roman.pos same.
- Found on the way: ai.rs think took the start gunners' guns before turret.rs setup had run (an order left to Bevy);
  think's chain is now after setup (infantry_l2m1 lost its 6 gunners when an unrelated system was added).
- B2: box volumes are oriented boxes (mission.rs Volume::Box: the model's local box under the placement's matrix).
- B3: Object_ForceGround's ray (0x00455860 -> 0x004137e0 -> Ray_CastQuery 0x00414110: no include list, "accept bodies
  without owner") hits only the landscape; grunt 181 is under the archway v_archway_a, whose top the remaster's ray
  hit. physics.rs raycast_landscape, used for force_ground, Roman's and the companions' grounding.
- B4: a trace bug: oracle.rs took the facing as rotation x game_to_engine x -Y (the up axis: atan2 of noise). The
  model's -Y through the Transform is right; the model does turn with the look yaw.
- B5: mission.rs volumes return while Time<Virtual> is paused (the contacts and the volume update stop with the game).
- B7: the keys' Select no longer leaves the pub with nothing left (a click still does: feedback 17); the pub page
  fades out over 0.5 s as in the original (gamemenu.rs instant), the game paused under it, and the exit trigger and
  the unpause come when it is gone (pubscreen.rs leave_pub: 900 at 470 vs 472); Roman is not moved to Ref_exit
  (0x004690c0 takes that bone's position and drops it: the original's Roman stays at the door); PlayerStats is
  published every frame (player.rs publish_stats), so the machine gun shows as taken in the pub.
- Harness: a voice line from a sound ear compares by its kind (the speaker is a roll, oracle_diff.py sound_key); an
  unlimited reserve near a weapon change compares as agreeing; the remaster's trace counts placed objects without
  health removed by receive_trigger_kill as deaths (the big red arrow 258).

Not bugs: the rifle's reserve (the original keeps 62 and never spends it, the remaster reports u32::MAX: unlimited,
not compared); the original unpauses a frame after the dismissing key (pauses under 4 frames are ignored).
Open: state 0x1f (sticky, grunt 201) is not in grunt-ai.md's table; restart scenarios (pause menu -> restart) are not
written yet (menu navigation of the original's pause page to script).

### Oracle findings on every level (2026-10-16, agent-oracle-all)

`tools/oracle_scenarios/<level>_start.json` for all 24 levels: the start message dismissed (`0:ENTER*3`; lev_2_m1 /
lev_5_m1 also `40:ENTER*3`, see E1), 10 s of play with a 2 s walk (W from 120), traced from play frame -60 so events
before a later start message in one game are seen. Run them all (about 25 min with two at once):

```bash
.venv/Scripts/python.exe tools/oracle_suite.py --exe target-NAME --jobs 2 --instance oracle-NAME lev_1_m1_b_start ...
```

Harness changes that came with them (tools/oracle_diff.py): the remaster runs with `--movies <a folder that does not
exist>` (the original's `[test] no_movies=1`; before, lev_2_m1 / lev_5_m1 / lev_5_m5 played their start movie and the
remaster's clock waited 600 frames); vehicles (colossus 0x5aa238, mini_zeppelin 0x5af500, saucer 0x5a5e98) are left out
of the grunt families with a note (the remaster's trace, engine/src/oracle.rs, lists `ai::Grunt` only: adding the
vehicles there would let them be compared); states 0x97, 0xdc / 0xde, 0xc4 / 0xc5 / 0xbd, 0xf0, 0xf1 map to ToMarker,
InSquad, Jump, Teleport, Cast (ai.rs `Mind`); game time is compared from the first common line; a pause that both games
start but end at different frames is a divergence (before, only the start was checked: this now flags pub's B7);
`--instance` gives parallel agents their own original instance. Each scenario's `known` names the bugs below, so it
passes and still guards every other family; the nine with deterministic divergences only are in the suite (lev_1_m4,
lev_2_m3, lev_2_m4, lev_3_m2, lev_3_m4_bd, lev_4_m3, lev_4_m5, lev_5_m2, lev_5_m4: rerun twice, same result).

| Id | Remaster bug or difference (first divergence) | Evidence (work/oracle/<level>_start/) |
| --- | --- | --- |
| B9 | Every level starts on the rifle; the original holds the machine gun (weapon 0, clip 100) when the mission gives it (`player_add_machinegun` 1: all levels but lev_1_m1). Look at the loadout at level start (player.rs `use_mg`, pickup.rs loadout) | roman.weapon / roman.clip at the first line on 23 levels: original 0 / 100, remaster "rifle" / 8 |
| B10 | The start message: the remaster shows it 20 frames after the load, the original at once (load frame 0), so the remaster's patrols walk 20 more frames (grunt.pos 12-26 units: lev_1_m1_b grunt 1, lev_2_m2 16, lev_3_m2 69, lev_bonus_m3 201). Base-defence levels (lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5): the original's comes 279-399 frames after the load, after its start sequence (lev_4_m5 traced from the load: trigger 1 at load, 2 and the `basedefend` sting at +59, 90..94 at +164..+184, the message at +399); the remaster's at +20, so on its play clock the timed triggers (3, 402, 400, 11) and the army's first grunts come ~350 frames later (only-original events) | original.log / remaster.out "briefing at"; work/oracle/lev_4_m5_long (trace_from -460) |
| B4 (more) | Roman's model heading ignores the start facing (the look yaw is right: look_yaw = -heading): 0 on identity starts where the original has 180, 20-110 deg off elsewhere | roman.heading on 15 levels, e.g. lev_2_m3 69.5 vs -123.7, lev_4_m1 49 vs -45 |
| B3 (more) | `force_ground` placement picks another surface: lev_3_m3 grunt 32 / lev_bonus_m2 grunt 23 (placed z 20) at z 114 (original 20); lev_5_m1 captain 66 (placed 102) at 227 (original 298); lev_5_m3 grunt 44 at 287 (316); lev_bonus_m3 Jonesy 114 units higher | grunt.pos, companion.jonesy.pos at -30 |
| B2 (more) | lev_1_m3: volume 219 (bb_300x300 rotated 141 deg, z 149) fires 801 when Roman walks below it (remaster only), waking grunts 161-164 / 170 | events.triggers 801 at 186 only in the remaster; grunt 161 Chase vs WaitTrigger at 206 |
| B11 | Squad goliaths are not created: `squad_goliath_*` squads carry a GoliathObject (0x5af298, 3000 health) in the original; ai/squad.rs leaves `Squad::goliath` to the goliath module, which never spawns one (lev_3_m1 3, lev_4_m1 3) **FIXED (agent-aispawn): ai/squad.rs reserves the goliath, goliath.rs makes it (SpawnGoliath: cage + peasant, walk with the squad, the squad's kill / death triggers); oracle lev_3_m1 / 4_m1 vehicles 9/9, 3/3 same.** | grunts.alive 59 vs 56 / 57 vs 54; grunt.health "3000 vs 150" (the goliath paired with a captain by position) |
| B12 | Building IC grunts present at load (id 0, state 0x64, beside v_house_* with `ic_door_*` / `ic_climb_*` / `ic_roof_jump_*`) are not created: lev_1_m1_b 6, lev_5_m1 2 **FIXED (agent-aispawn): ai/ic.rs hidden grunts (59 building IC sources on 10 levels, trees, powder kegs); oracle grunts.alive same on every level (the tree-grunt known gap is gone from oracle_diff).** | grunts.alive 36 vs 30 at -30 |
| B13 | Grunts in the bully state 0x118 (bully.rs: a peasant to bully) are in Roam in the remaster: lev_3_m3 grunt 9, lev_4_m2 grunt 40, lev_bonus_m2 grunt 8 **FIXED (agent-aispawn): Mind::Bully (state 0x118).** | grunt.mind at -30 |
| B14 | lev_bonus_m1's opening assault does not happen: in the original 17 `veh_jumper_blastoff` at load, jumper captains flying (grunt 8 state 0xc4), forge cannon and captain rocket fire from frame 6, Roman hit for 200 at 66; the remaster's grunt 8 waits for its trigger, Roman keeps 400 **OPEN (investigated): in the original every one of lev_bonus_m1's 50 grunts has Roman as target at the first line (up to 1569 units away); the mission's start trigger 1 is also alarm_trigger_activate (the controller's alarm forced, which only gates troops); what sets the targets is not found (no AI_FindTarget range reaches; Game+0xdcc has no other reader).** **FIXED (agent-aiparity2): the mission's ai_target_tag (Game+0xae8) makes every bad guy's AI_FindTarget 0x00426920 take only objects of that tag (Roman, instance 19) at any range, with no cone or sight test, never dropped for distance (vt+0x180 0x0043ecf0): ai.rs ArmyTarget; a trigger_charge activation pursues at once (0x0042ce70 -> vt+0x184: Grunt::pursue); a jumper has no ground contact before his first physics step (0x00454c20), so the load-frame pursuit flies to a jump marker. Oracle: 50/50 targets at -30 (was 0), 14 jumpers in the air at -30 (original 15). Left (known, open-questions.md): the stig gunners' first bursts ~110 frames late, Roman dies earlier.** | grunt.mind / target, roman.health, events.sounds |
| B15 | Grunt generators: lev_2_m2's `grunt_generator` 17 / 25 / 27 spawn troops in the first 10 s in the remaster only (no alarm raised); lev_bonus_m2's 171 spawns in the original only **FIXED except lev_bonus_m2's 171 (agent-aispawn): alarm.rs timer rolled at load, alarm forced, the seen test (0x00404d00) with a level ray that only drawn placed objects stop; hook traces the controller's gates ("alarm"). Open: lev_bonus_m2 generator 171 (1490 away, ahead of Roman) releases in the original only.** **lev_bonus_m2 FIXED (agent-aiparity2): the generators' weights fall back to the grunt_generator template (num_regular 1; 169..171 had weight 0 and were skipped); the seen test as 0x00404d00 reads: a ray at the eye's height, any collidable object hides (+0x424 bit 11 is the camera fade, not a draw flag). 169 / 171 send troops in both; the frames are rand() rolls (expected R2).** | grunts.alive 32 vs 33..36 (from 162), 38 vs 37 (from 218) |
| B16 | lev_4_m2: 9 grunts come out at the l4 trees (l4_tree_d 121, l4_tree_dense_d 128 / 131) in the first 10 s in the original; none in the remaster (the tree grunts gap, grunt-ai.md section 14) **lev_1_m1 / 1_m3 tree grunts FIXED (ai/ic.rs); lev_4_m2 FIXED (agent-vehicles2): not twiglets: invisible_barracks 284 (activated by trigger 1 at load) releases one troop a second at (-69, -121); its model "nothing" has no matrices, the spawn / exit are in its template's anim slot grunt_generator_base, which mission.rs now reads; the release period (60 frames, the remaster's 61) needed the harness's 16.6667 ms step (main.rs). Same release frames and order on all 10.** | grunts.alive 24 -> 33 vs 22 |
| B17 | Fixed (agent-sound, 2026-10-16; docs/subsystems/audio.md section 8): the sound events were switched on after the level's first frame, so its loops (pubs' `amb_pub_exterior_lp`, oil pumps, world ambience) were never listed, and only sounds the driver accepted were (far zeppelin creaks); fx_origin_start_lock was read for the bosses only (shrub bombers' `veh_bomber_lp`); sheep looped sheep_bleat (now SheepObject: graze / scan / bleat, walks on start_in_turret levels); the music played level 1's tracks everywhere (now by env_type); every surface was snow (now the skin materials' names, surface.rs: `fs_water_shallow` on lev_bonus_m3, dirt / grass / stone / wood elsewhere); the base turret start played `wpn_stig_mount` (0x0049bcc0 is silent). Left (each scenario's `known`): timing of load-time sounds (B10 / E1), random picks, grunts' gait and `_ic` lines, boulder rolls, vehicle loops | events.sounds |
| B18 | Grunts take Roman as target at the start in the remaster only: lev_1_m1_b captain 134 (trigger_marker with `marker_patrol`, activate 900) patrols in the original (0x96) but chases Roman at -30; lev_3_m3 grunt 51, lev_bonus_m2 grunt 40 likewise **FIXED (agent-aispawn): ai::activate (0x0042f5c0 by mode): only trigger_charge grunts take the player as target; trigger_marker runs its spline.** | grunt.mind / grunt.target at -30 |
| B19 | lev_1_m1_b, original only (open): sphere 376 (`on_enter` 700, `ai_can_trigger` 0, one shot, far from Roman) sends 700 at load; an object without an instance id dies at 36 (`imp_turret_glass_break`); Roman loses 15 health at 548 **PARTLY EXPLAINED: 700 / 703 wake house 163's window diver / house 380's roof jumper (IC grunts, now ported); the id-0 death at 36 is the window the diver breaks (now ported: ai/ic.rs); what makes spheres 376 / 387 fire at load in the original stays open (not the hidden grunts inside other spheres, not windows, not penguins: volload checks in the journal).** **FIXED (agent-aiparity2): 376 / 387 hold sphere_radius 100.0 as a float; the original reads a dword (0x00497460 / 0x004975d0) = 1.12e9 units: both cover the level and fire when Roman appears (hook trace_calls of 0x004976a0: other = Roman, 1200 units away). Reproduced (mission.rs sphere_radius; original-bugs.md). events.triggers / deaths same; the IC grunts by house 380 take Roman at load as in the original (search hit points from TransformHelper). Left: one loses him at 498 and the original's rifle shot hits Roman at 548 (known).** | events.triggers / deaths, roman.health |
| B20 | Original only (open): lev_5_m5 base turret 166 sends 90 at 180; lev_5_m1 an object without an instance id dies at 74 **lev_5_m1 EXPLAINED + FIXED: the id-0 death at 74 is a window broken by an IC dive (houses 33 / 38, trigger 1). lev_5_m5 turret 166's trigger 90 at 180: not investigated (defence turret).** **lev_5_m5 EXPLAINED (agent-aiparity2): the turret intro's notes (base_turret_l5_m5_enter: trigger 90..94 at 2.73..3.07 s) send 90..94 on both games' clocks (hook: from 0x0040972e, the anim note handler); the play-frame offset is E1 (scenario expected).** | events.triggers, events.deaths |
| C1 | A companion 12-13 units from the original's (tolerance 12) while Roman walks (lev_1_m1, lev_2_m2, lev_5_m3, lev_bonus_m3) or at the start (lev_4_m2 Jonesy): follow distance / start spot | companion.*.pos |
| R1 | Roman walks 3-6 units farther in 2 s of W on 20 levels (original 76-81, remaster 79-87; lev_1_m1 the other way, B1): speed or acceleration at the walk start | roman.pos from 110 to 300 (lev_2_m2 6.1 apart at 210) |
| B24 | lev_3_m1 captain_sniper 228 (force_ground 0) on v_rockettower2 272 falls through the tower's top in the remaster (original stays at z 899.7) **CAUSE FOUND, OPEN (agent-vehicles2): he is not over the deck but beside a beam (its ridge 1.6 units west at z 897.5, its top edge 3.6 west at 900.9, nothing under him). The original's body is a sphere of collision_radius (5) on the feet (Object_BuildRadiusCollision 0x004527d0) with friction 0.8 against statics at default_friction 1.0: it rests on the beam's edge. The remaster's frictionless capsule (radius 0.55 x 5) slides off. Tried: that sphere + frictions holds him at 899.56, but breaks infantry_l2m1 squad charges and defence_l2m4 (the wider sphere) and infantry_l3m1 jumper landings (the friction); backed out. Next: the original's character-character collision filter (0x00411710 / 0x00411850) and its walking / flight under friction, then the body.** | grunt.pos from 58 |
| V1 | The saucers / mini zeppelins on mover paths started at load run ahead along the same path in the remaster (the original covers 11 units in its first second, the remaster 57; ~60 frames ahead after 3 s): the original's mover starts slowly (lev_2_m1, 2_m3, 5_m2, 5_m3; vehicles now traced: hook "vehicles", oracle.rs VehicleKind) **FIXED (agent-vehicles2): state 0x64 is a chase: the gap to the path's pose is set as the body's velocity (0x00458510), so the object starts from rest and runs a second of path behind (vehicles/mover.rs; the model matches the original within 0.03 units over 9 s). vehicle.pos same on 2_m3, 5_m2, 5_m3; lev_2_m1 differs only by E1 (within 0.2 units at equal game time).** | vehicle.pos |
| T1 | Twiglets were not in the remaster (lev_4_m1's trees activated by trigger 200 give 30 in the original: twiglets_l4m1) **PORTED (agent-twiglets): twiglet.rs, the hook and the remaster trace "twiglets" / "swarm"; twiglets_l4m1 PASS (counts within 14 frames, the nearest within 7.5 units to frame 1240); the first bomb 1300 vs 1258 (each game's Roman elsewhere by B6's hits: past the traced window).** | twiglets.*, twiglet.nearest |
| G1 | Fixed 2026-10-17 (agent-world): the goliath (p_goliath no_collide 1) is goliath_coll's 20 convex hulls posed on their bones each frame (goliath.rs `hulls`), in a hit-only Jolt object layer (physics.rs HIT_ONLY: in no collision pair; walkers query static + moving only), so Roman stands between its legs (playtest goliath_legs: landed under goliath 170, not pushed in 8 s; before, shoved ~9 units) and its legs and hulls stop shots (Physics::main_body maps a hull to the goliath at the hit lookups). An enemy bullet stops at the first body it meets (player.rs projectiles, Projectile_Update 0x0047e580: Roman and the companions are tested only up to the step's world hit; before, the whole step was, so shots through the hulls still counted). Oracle ticker_l3m1 (2026-10-18): Roman stays within 0.1 units between the legs from landing to the blast, and takes the freed caged peasant (counted at 1428, the original's 1404) | goliath_legs |
| G2 | Squads 171 / 172 of lev_3_m1 head for their first marker on another line than the original's (goliath 172 ~12 units further south by frame 210, ~2 by 900): which marker of spline 600 a squad starts at (0x0042d1b0, state 0x96) is not traced; squad 170 matches within 8 units | ticker_l3m1 vehicle.pos 210 (known) |
| G3 | lev_3_m1 squad 172's goliath (squad_goliath_4, force_ground 0) drifts ~10 units in the original before its squad is activated; the remaster's stands | jetpack_l3m1 vehicle.pos 366 (known) |
| G4 | The ticker blast (area_damage 100, radius 40..60) takes ~14 off a neighbouring squad goliath in the remaster; the original's keeps 3000 at ~44 units (which filter or distance its blast uses against a GenericAI is not traced; the remaster measures to the bounds' surface) | ticker_l3m1 vehicle.health 1384 (known) |
| E1 | Expected: with movies off in both, the original's start movie still pauses and takes the first ENTER (its start message follows), the remaster's missing movie ends at once (lev_2_m1, lev_5_m1: paused -30..42 vs -31..0; lev_5_m5 timing) | paused, timing |

Per level (first divergence frame in brackets; families not listed are the same within tolerance):

| Level | Divergences | Suite |
| --- | --- | --- |
| lev_1_m1 | B3 grunt 181 (-60), B1 slide / sounds / token pause (2, 18, 236), B2 711 (138), C1 (264), B6 (304) | walk etc. |
| lev_1_m1_b | B9, B4, B17 pub loop, B19 700 / death / health, B18 captain 134, B10 grunt 1, B12 6 IC grunts | |
| lev_1_m3 | B9, B4, B17 pub loop, B2 volume 219 -> 801 (186) and grunt 161 (204-206) | |
| lev_1_m4 | B9, B4, B17 stig mount (-48), B10 base start (trigger 3 at 22) | yes |
| lev_2_m1 | B9, B4, B17 pub loop, E1 | |
| lev_2_m2 | B9, B17, B10 grunt 16 (2), B15 generators (162, 180), C1 (184, 274), R1 (210) | |
| lev_2_m3 | B9, B4, B17 pub loop | yes |
| lev_2_m4 | B9, B17 sheep (24), B10 base start (402 at 322, army at 418) | yes |
| lev_3_m1 | B9, B4, B17 pub loop, B11 3 squad goliaths | |
| lev_3_m2 | B9, B4, B17 pub loop, B10 grunt 69 | yes |
| lev_3_m3 | B9, B4, B17, B13 grunt 9, B3 grunt 32, B18 grunt 51, barracks release rolls (192: the generator at (17, 391), delays rolled in release_time_min..max) | |
| lev_3_m4_bd | B9, B17 sheep (-48), B10 base start (400 at 184, army at 450) | yes |
| lev_4_m1 | B9, B4, B17 pub loop, B11 3 squad goliaths | |
| lev_4_m2 | B9, B4, B17, C1 Jonesy (-30), B13 grunt 40 | |
| lev_4_m3 | B9, B4, B17 pub loop | yes |
| lev_4_m5 | B9, B17 zeppelin creak (174), B10 base start (11 at 502, army at 568) | yes |
| lev_5_m1 | B9, B4, B17, B3 captain 66, B12 2 IC grunts, E1, B20 death (74) | |
| lev_5_m2 | B9, B17 pub loop | yes |
| lev_5_m3 | B9, B17, B3 grunt 44, C1 Q (198) | |
| lev_5_m4 | B9, B17 pub loop | yes |
| lev_5_m5 | B9, B17 bomber loop, E1 timing, B20 trigger 90 (180) | |
| lev_bonus_m1 | B9, B4, B17, B14 opening assault (-30 .. 328) | |
| lev_bonus_m2 | B9, B4, B17, B13 grunt 8, B3 grunt 23, B18 grunt 40, B15 generator 171 (218) | |
| lev_bonus_m3 | B9, B4, B3 Jonesy, B10 grunt 201, B17 water footsteps (12), C1 Q (168) | |

Also from the remaster's own logs on these runs (`not ported` lines in remaster.out): mission `peasant_max_saved` /
`peasant_max_casualties` (lev_2_m3, lev_3_m1, lev_3_m3, lev_4_m1, lev_4_m2, lev_bonus_m2: ported 2026-10-16, peasant.rs),
ticker sites'
`lock_parent_tag` (0x0044e120: lev_3_m1, lev_4_m1), fx vm opcodes 0x4c (lev_3_m3, lev_5_m1, lev_5_m3, lev_bonus_m1,
lev_bonus_m2) and 0x62 (lev_5_m1..m3).

**2026-10-16 (agent-startfix; docs/journal/2026-10-16-startfix.md).** Note first: the oracle-all remaster traces were
taken with a build from before the oraclefix merge, so several "more cases" were already gone on wip/full-decomp:
- B9 fixed: the mission's loadout (campaign agent's mission.rs Loadout / player.rs apply_loadout, merged; this branch's
  equivalent dropped in favour of it): roman.weapon / clip same on all 24 levels.
- B10 fixed: mission.rs start shows the start text on the load frame unless a cinematic (Game byte +0xdce) plays: a
  camera path or the base turret's mount intro (defence.rs `BaseTurret::in_intro`); 2 frames of remaster pipeline
  latency (START_PATH_LATENCY). Start text: remaster frame 4 vs the original's 0; lev_4_m5 403 vs 399; timed triggers
  of the defence levels agree (lev_4_m5 11, lev_1_m4 3, lev_2_m4 402, lev_3_m4_bd 400). Left under the old B10 label,
  not a start-text cause: grunts on a marker walk (state 0x96) set off ~18 frames after the unpause in the original, at
  once in the remaster (lev_2_m2 grunt 16, lev_bonus_m3 grunt 0: AI, **B21**: agent-aiparity2 found grunt 69's ~55 frames no delay: spline legs walk at the previous marker's speed (AIMarker_PrevSpeed 0x0044b4f0, vt+0x80 0x004297a0: marker 71's 1.0), else patrol_speed with marker_patrol, else the walk; nodes reached within 2.5 (0x0046f900). The 0.3 s before the first step stays fitted, open-questions.md); the defence army's first release (an
  army grunt at 418 / 450 / 568 in the original only: army.rs, its timers list 0 platoons).
- B4 (more): gone with oraclefix's trace fix (c22074b): roman.heading same on all levels (also after a Q switch to the
  rifle, lev_2_m3).
- B3 (more): lev_3_m3 / lev_bonus_m2 / lev_bonus_m3 were the archway case (raycast_landscape). lev_5_m1 / lev_5_m3:
  the landscape ray hits an overhang's underside (Havok and Jolt rays hit back faces); the original then lifts the
  body out by bisection (Object_ForceGround's penetration loop, now physics.rs force_ground with the character's
  collision sphere, 0x004527d0): 230 -> 297.0 (original 297.9), 289.6 -> 314.6 (316.0). Left: the original's lifted
  grunts stay up, the remaster's fall (**B3b**, open: dynamic bodies in the original too, the hook's kf field; most
  likely the back faces Jolt's simulation ignores, docs/open-questions.md "AI parity leftovers").
  lev_3_m2 window grunt 72 (z 471 vs 492: the window bone placement, level.rs spawn: spawn owner).
- B2 (more): lev_1_m3 volume 219 agrees (oraclefix's oriented boxes).
- R1: the 3-6 units on 20 levels were the pre-B1 drive; left: a one-frame input lead (the remaster moves on the frame
  W is pressed and stops on the frame it is released, the original a frame later: 0.65 units, the hook's key timing or
  the game's, open) and lev_2_m2's steep slope (normal z 0.59 < 0.7: the sliding path veers 4 units, open).
- C1 / B9' (oraclefix): companions route to the formation point of their last pick (ai.rs Companion::goal; the pick
  every 0.25 s on the move), not the live bone: walk now matches (stop at 36-44, off at ~50, trail ~29 vs ~30). Left
  on other levels (lev_2_m3 Jonesy, lev_2_m2, lev_5_m3 Q, lev_bonus_m3 Q, lev_4_m2 Jonesy at the start): the regroup
  start (the original's Q sets off at ~32 units from Roman, Jonesy ~12 frames later at ~39; the remaster's both at the
  slot distance 15: ai.rs regroup rule; the code reads delay_distance 50 for Defend Me, which the traces do not show:
  open).
- B10' (footsteps after the briefing stop): open; the original keeps firing run notes for ~20 frames after the stop
  (2, 6, 18, 20) with the slide loop, the remaster plays the stop at 4 and the slide's end; Roman stands still in both,
  so the run anim's mode / phase at the pause differs (charanim.rs mode choice vs 0x00476a10's speed), not the notes.
- Port queue after all levels: UiText_ShowTime (hud.rs ui_mission_timer), the wall scales of 0x0043bcf0 (army.rs
  ic_scale_base_wall), the rest explained in docs/port/{ui,anim,camera}.csv.

### Same-camera render comparison (rendering pieces)

**Caution:** `--camera` (fixed camera from a dump) disagreed with the synced run on 2026-10-13 (rock walls where the
original shows the ruined arch, while the synced run matches): its view-matrix conversion is suspect. Prefer the
synced walk until it is fixed.

1. Capture the original at chosen frames with its camera (the hook DLL writes `and-frame-N.camera.json` next to every
   dumped frame; build it first with `pwsh tools/build.ps1`):

```bash
.venv/Scripts/python.exe tools/shot.py --scenario level --frames 520,540,560 --keys "500:MOUSE1,510:17*300" --frames-total 570 --name walkA
```

   Frames must be evenly spaced. Keys: `frame:KEY` or `frame:DIK*count` (17 = W held for count frames), `MOUSE1`,
   `ENTER`, `ESC`, `UP`, `DOWN` (not `RETURN`). The camera dumps are left in the "main" game instance folder
   (`python -c "import sys;sys.path.insert(0,'tools');import instances;print(instances.instance_dir('main'))"`):
   copy them to `work/shots/<name>cam/`.

2. Render the engine from the same camera (the original draws its 3D view into the letterbox strip, 640x372 at
   640x480, with that strip's aspect):

```bash
cd engine && ./target/release/and_remaster.exe --size 640x372 --skip-title --no-hud --no-ai --camera ../work/shots/walkAcam/and-frame-00520.camera.json --snap ../work/shots/eng_walkA-520.png
```

3. Score and look:

```bash
.venv/Scripts/python.exe tools/render_compare.py work/shots/walkA-520.png work/shots/eng_walkA-520.png --name walk520
```

   Prints mae / psnr / ssim and the mean colour of the top and bottom thirds, writes
   `work/remaster/render/walk520.png` (original | engine | difference heat map). HUD areas and the player are
   masked. Record the numbers before and after each change in the journal. Baseline (2026-10-13, frame 520): mae
   60.6, psnr 10.7 dB, ssim 0.163; ground colour original (137, 157, 151) vs engine (75, 68, 55).

### Behaviour comparisons (movement, weapons)

The walk test (the human's): in the original, dismiss the briefing (Mouse 1), hold W for 5 s, capture; do the same in
the engine with a playtest script; the player and scene should mostly match. Movement pieces compare Roman's logged
positions and the camera dumps frame by frame. In the original, Roman crosses the treasure token about 2.5 s into the
walk (a paused message), so capture frames before ~640 or dismiss it.

### Playtest bot (whole-level runs)

`--play SCRIPT.json` drives the game: `steps` run in order (`wait`, `goto` [game x, y, z] with optional `near`,
`hold_use`, `tap` key, `down` / `up`, `click` / `hold_mouse` / `release_mouse`, `shot` name with optional
`shot_after` seconds, `fight` true / false, debug `hurt`, `cover` (the grunt nearest Roman runs the hiding-spot
search now; engine/playtests/cover1..3.json check the trenches), `spawn` and `warp` [game x, y, z, yaw degrees] to
put Roman at a spot without walking there (work/remaster/play/pump.json), `quit`); `"title": true` starts on the title
menu. It logs the game state every second to `<script>.log.jsonl` (position, health, mission events, prompts,
subtitles, sounds, skinning check) and saves `<script>.<shot>.png`. The full-level script is
work/remaster/play/level1b.json (pub, both gates, win). Run it in the background and stop on Won / Failed:

```bash
cd engine && ./target/release/and_remaster.exe --size 1600x900 --play ../work/remaster/play/run.json > ../work/remaster/play/run.stdout.txt 2>&1
```

Fast checks (2026-10-14): `python tools/playtest.py SCRIPT.json [--exe target-agent2] [--grep AIDBG]` runs one
script headless and prints only EVENT / EXPECT / PLAYTEST lines, the time and the exit code (full log in
`SCRIPT.out`). A `{"save": "NAME"}` step writes `states/NAME.json` next to the script (savegame.rs: Roman,
mission, dead grunts, destroyed objects); a script with top-level `"load": "states/NAME.json"` starts there with
the intro skipped. `{"expect": {"health": [200, 290], "stickies": 5}}` checks state-log fields (two numbers: an
inclusive range; else equality) and prints EXPECT PASS / FAIL; any failure exits 1. `{"expect_perf": {...}}` holds wall-clock
limits (frame_ms, main_ms): checked only by `tools/regress.py --perf` on an idle machine, skipped otherwise. `{"blast": D}` sets off a
badguy explosion of area damage D at Roman. States: `after_pub` (state_after_pub.json, 23 s to make). Example:
topsyhit2.json (24 s; the same check replayed from the start took about 4 minutes).

Completion tooling (2026-10-16, agent-complete-b; engine/playtests/complete_<level>.json play a level to mission
complete through its real objective chain, then Stats and the next level):
- `{"land": [x, y, yaw]}`: Roman onto the ground under game x, y (the landscape's ray, else the first hit), logged as
  "EVENT playtest: landed at x y z" or "no ground under": skip travel without knowing heights.
- `{"land_by": [id, distance]}`: beside a placed object, the best of eight points round it (the landscape nearest its
  height), facing it; a pub's id stands Roman at its doorway (Use enters).
- `{"probe": [x0, y0, x1, y1, step]}`: a ground-height map in the log ('-' no landscape, 'a/b' something else hit first
  at b): find the walkable ground near a volume, holes, off-map areas (lev_4_m3 has both: a land there falls forever).
- `{"damage": [id, amount]}` also hits a placed AI (grunt, Q1-12, monk) by instance id; `{"kill_dropped": amount}`
  hits every live troop a vehicle dropped (lev_5_m2's paratroopers, as a player shoots them on landing).
- `{"until": "message", "wait": S}`: a paused message with S seconds as the limit (a timed mission's win);
  `{"defence": {"mortar": "auto"}}` fires the base turret's mortar at the nearest platoon every second until
  `{"mortar": false}` (the defence levels' 500 / 600 platoon kills).
- Static: `tools/mission_chain.py LEVEL` (agent-complete-a) prints the win / fail chains and what each sender waits on.
- Paused messages during a `goto` are dismissed by the harness (a walk would never end): check them afterwards with
  `mission_obj.objectives_id`, not `until: message`.

The bot is debug tooling, not game logic: it lives in engine/src/bot.rs (`BotPlugin`; `think` runs before
player::control and leaves its walk / facing / aim in the `Bot` resource, which control and weapons read as they read
the keys) and must never change how the game plays for a human. Tactics (2026-10-15, after the AI port made the
grunts dangerous): the grunts that can see Roman are counted with a bullet ray (muzzle 8.4 up to his chest,
`raycast_bullet`); a duel at range (at most two in sight) is fought side-stepping (their bullets fly at where he
was: at 450 units/s a moving Roman is missed at range, a standing one is hit by nearly every shot); under fire from
three or more it takes the nearby open-ground spot that sees one of them (else none) and fights from there; badly hurt
it backs off; sticky bombs at groups only with the crosshair on the target and a clear line (a throw without a target
flies along the camera ray and can land at Roman's feet); machine-gun bursts at range (the scatter grows over the
first second held); kegs: no fire when one Roman would catch is on the line of fire; detours to health pickups; 12 s
pinned by a tactic -> push on 6 s. The state log's `bot` field shows its mode (route, duel, cover, to_cover,
back_off, keg, pickup, push) and the grunts in sight. AIDBG adds `romandmg` lines: every hit on Roman with the
shooter's kind, weapon, distance (who killed him).

Full-level run (not in the suite: about 8 minutes of game, 20 to 40 of wall time on a loaded machine):
`python tools/playtest.py engine/playtests/level1u.json --exe target-NAME --timeout 3600` (expects Won at the end).
The script gives the bot the player's route between the gates (waypoints through the blown gate 1 at x -88, the
village west of the trenches, past the guard camp at (0, -515)), as a player picks a way; no debug steps. It saves
states at each leg's start (engine/playtests/states/, ignored): l1_after_pub, l1_at_gate1, l1_before_camp,
l1_before_gate2. The leg scripts start there and save the next one, to iterate on one leg in minutes: l1_leg_pub
(cold start, about 2 min wall), l1_leg_gate1, l1_leg_siren (gate 1 blown, the siren shot, the trenches; about 7 min
wall), l1_leg_camp, l1_leg_gate2 (expects Won). Run level1u or the legs in order once to make the states. A loaded
state has the live grunts at their placements and no raised siren (not saved), so the legs are for iteration; the
cold-start run is the test. The playtest warps a bot that has not arrived after 240 s ("bot stuck ... warped"): a run
with that line is not clean.

Aim and targets (2026-10-15, agent-bot): the bot aims as a player does, the crosshair on the target
(camera.rs `CameraRig::look_at`: the yaw / pitch whose follow-camera ray passes through the point) and fires only while
the crosshair's point is on it, so its shots take the player's path (gun to crosshair point). Before, it looked
straight at the target from the eye while the shot left the hand parallel to that line: about 5 % of shots hit (1339
shots, 69 hits); the pub leg now takes about 40 shots for 15 kills. A target the crosshair cannot reach for 1.5 s is
skipped for 4 s. Targets: a raised siren (alarm.rs `Markers::sirens`, within 400 and in sight) before anything but a
grunt within 60; kegs with grunts beside them; else the nearest grunt. Explosions: no sticky throw, keg shot or fire
past a keg whose blast, or chain of blasts (`blast_reaches`), would reach Roman (three kegs by the siren killed a
full-health Roman 30 units away); cover spots keep out of keg blasts and the bot steps out of one under fire. Stickies
go at groups with the crosshair closed on a grunt. Holding Use (ticker sites) the bot stands still. Stuck: escape
turns of 45, 90, 135, 180 degrees by the seconds stuck, a jump at 2 s and every 4 s after (jumping every stuck frame
put Roman on a wall top and into a pocket between walls he never left). nav.rs: steps without room for Roman's width
cost more; waypoints are kept where the straight walk between them would brush something. `NAVMAP=<cells>` (env)
prints the planner's map round Roman at each new goal ('.' open, '|' / '-' blocked from the west / north, 'x' no
ground, '^' / 'v' above / below).

### Level completion (objective chains, 2026-10-16)

`python tools/mission_chain.py LEVEL [--all] [--json]` prints what wins and what fails a level, traced backwards
from the mission's `receive_trigger_success` / `_failure*` / `_pre_success` / peasant trigger: each trigger's
senders (instance, event, game position) and what each sender waits on (a counter's count and activate trigger, a
ticker site's enable trigger, a volume's enable, a timer's delay, an object's kill trigger), and where the player gets
ticker bombs. `--all` adds the flat trigger graph. Use it to write a level's completion playtest
(engine/playtests/complete_<level>.json): warp along the route, do each objective's real action, and expect each
trigger with the state log's `triggers.<id>` (counts of the triggers sent so far). Completion scripts take
`{"cheat": "SHIELDME"}` so they test the objectives, not survival. Results: docs/journal/2026-10-16-complete-a.md.

Steps and state for these runs (engine/src/playtest.rs, bot.rs; agent-complete-a / -b):

- Travel: `land` [x, y, yaw] (onto the ground), `land_by` [id, distance] (beside a placed object, a pub at its door),
  `land_sight` [id, distance] (a spot from which a bullet reaches the object, from Roman's eye and his camera),
  `land_peasant` true (beside the nearest peasant free to rope), `probe` [x0, y0, x1, y1, step] (a ground-height
  map in the log).
- Actions: bot orders `shoot` / `bomb` INSTANCE_ID (a placed object, a placed AI, a rocket tower's turret; bombs for
  buildings, which take no bullets; 0 cancels), `damage` [id, amount] (debug), the defence aim help
  `{"defence": {"aim": "army" | "platoon", "fire": true}}`.
- Waiting: `until` "trigger:N" / "dead:ID" / "message" with `wait` as the limit, so a run does not sit out fixed
  waits after the thing happened.
- State: `triggers.<id>`, `grunt_ids.<id>` (pos, mind, health, rigged, body), `turrets.<id>`, `objects.<id>`,
  `mission_obj.pubs.<id>`, `peasants`; an EVENT where the player's shell lands.

### Widening the original's coverage to a whole level

The port queues (`python tools/port_queue.py`) list only functions the original was seen to execute
(work/coverage.json). tools/coverage.py runs the short comparison scenarios; `python tools/coverage_level.py`
(the .venv python: PIL, capstone) adds one level's mechanics: a list of scenarios in the script, each one run of
the original (hooks off, `[test] hook_coverage=1`) with one `[test] warp=FRAME:x,y,z` next to a mechanic and
scripted keys (pub and awards, topsy, ticker bomb at gate 1 and the alarm chain, the courtyard fight to death and
MissionFailed, mounted gun, kegs, pipes, penguins, pickups, a fall and the extras token, the win and Stats). Counts per
run go to work/coverage_runs/ and are merged into coverage.json without double counting (`--merge` after rerunning
tools/coverage.py). To steer a new scenario, `--only NAME --shots first:every:count` saves frames as
work/shots/cov-NAME-F.png and prints Roman's position and health per frame (it switches on the recovered
Camera_SetupProjection for the camera dump; rerun without `--shots` for the counts). Roman keeps his heading after the
warp (facing -y on lev_1_m1: W -y, S +y, D -x). For a later level add a LEVELS entry with its own positions.

Every level (2026-10-16): `python tools/coverage_level.py --level all --jobs 3 --resume` (or `--level lev_2_m1,lev_3_m1`;
`--list` shows the runs). A level without a LEVELS entry gets generic scenarios built from its export
(work/remaster/<level>/mission.json): `start` (walk, look, fire, sticky, jump, next weapon, reload, special, zoom, the
teammate orders), `fall` (1500 units above the start), `group1` / `group2` (the two largest enemy clusters: fire while
turning, stickies, until Roman dies or time is up) and `obj-<behaviour>` (next to the first object of up to five
behaviours from `OBJECTS`, mounted guns and pubs first: hold Use, fire, sticky). They count play frames from the start
message ([test] keys_from_play, play_warp, play_frames; the hook now writes the coverage before a play_frames exit), so
no load time or start movie shifts them; a level with a start movie gets a second ENTER at 40 (E1 under "Oracle
findings on every level"). Each run is 30-55 s; the 200 runs of all levels took about 2 h 10 min with three at once
in instances cov0..cov2. Result (2026-10-16): 2306 -> 2605 of 3491 registered functions executed; 299 newly reached,
22 of them left for the port: docs/port/queue-after-all-levels.md (`python tools/port_queue.py --new-since
work/coverage_before_all_levels.json --md docs/port/queue-after-all-levels.md`). Test runs wait while the player's game
runs (tools/instances.py `wait_while_player_plays`: engine\target\release\and_remaster.exe or an exe in
orig\pc\GameData).

### Capturing the original's menus and screens

`tools/shot.py --scenario menu --frames 800` (main menu), `--scenario play` (briefing at 480, pause at 720), keys to
navigate (the Options page opens on Back; Up moves). `tools/ui_compare.py` puts the UI pairs side by side in
work/remaster/play/compare/.

## Rendering: what the original does (found 2026-10-13)

- Three pixel shaders in the whole game (tools/ps_disasm.py work/shaders/ps_*.bin): bump diffuse (base t0, normal
  t1), terrain (normal t0, base t1, second t2, mask t3 on uv set 1), bump + specular (characters). Everything else
  is D3D9 fixed function.
- `[test] log_materials=1` (hook) writes and-materials.txt: every bound material with its four stage textures, pixel
  shader, flags and stage ops; that is how each material's set-up was read.
- Material records (exported per submesh as `material`): +0x16 base, +0x18 normal map, +0x1a second texture, +0x1c
  mask (texture ids into the mesh's dictionary), +0x34 low byte the shading kind (3 bump, 15 terrain, 5 environment
  cube map * factor + texture * lighting, 1 fixed function), +0x50.. emissive / ambient / diffuse / specular, +0x60
  power. The engine's game material (engine/src/gamematerial.rs + .wesl) implements these, in gamma space.
- Lighting: the sun per pixel (through the normal map), the ambient-only light and the fill light per vertex,
  multiplied by the material's colours (the bluish diffuse 0x85AFB9 is why the snow looks blue).
- The follow camera: horizontal 60 degrees (Camera_end), vertical 0.654 rad in the letterboxed 1.702 view.
- Fog is per vertex in the original (vertex shader: oFog = clamp((end - view depth) / (end - start)), c1 =
  {start, end, 1/(end-start), 0.5}), interpolated; the engine's game material does the same in its own vertex stage.
- The landscape is clipped at the original camera's far plane (1712 on lev_1_m1: fog_far * 1712.132 / 1500); without
  it a hard pale band of fully fogged land showed. The camera's own far plane is 50000 so the sky dome (radius
  20000) is never clipped. The original also shows a fog-coloured band at the horizon (measured: ~(205, 213, 208)
  at the left of frame 580); resolved: it is the sky model's untextured disk under the horizon (emissive (212, 218, 212)), now drawn,
  and the sky texture's pale bottom rows fading into it, which only matched once tonemapping was off.
- The flat pale band hiding the village's lower half (archway, houses, tree trunks) in the --camera comparisons
  (2026-10-14) was that same disk, lifted: camdump.rs `hold` moved the sky dome to the dumped camera's full position,
  y included, while level.rs follow_camera (play) moves it horizontally only. The disk (model z -26..-33) then sat at
  ~107 on walkA 520 (camera at 136.7, from its camera.json), above the village ground (~10; archway placed at 12.4),
  and occluded it; AND_DEBUG_NOFOG=1 still showed the band, so not fog or the far clip. Fixed in `hold` (x, z only).
  walkA ssim / mae before -> after: 520 0.516 / 18.8 -> 0.560 / 16.7; 580 0.510 -> 0.577 / 18.9; 640 0.369 ->
  0.412 / 54.0 (640's remaining gap: the original's token-found overlay). Play was not affected.
- sound_ears remove themselves after playing unless `reapeat` (sic) is set (0x00497900): a voice line played twice
  before.
- Alpha textures blend (blend table 0x0057d9e0 entry 3: SRCALPHA / INVSRCALPHA, alpha test on): the bare snowy
  trees look right only with blending, not a 0.5 alpha cut.
- LOD: always the finest (the human's decision, docs/decision.md).
- Environment cube maps (ice, water, metal): rendered by the game each frame (cubemap_texture); the engine uses the
  fog colour for now (a labelled approximation).

## Characters and colours (found 2026-10-13)

- Roman's `_b` / `_t` animation pairs are the **bottom and top of an aim-pitch blend** (like the grunts' fire_bl /
  fire_tl), not lower / upper body: the rifle points ~50 degrees down in `_b`, ~47 up in `_t`. The engine blends
  them with w = (aim pitch in degrees + 50) / 97 (derived from the skeleton; the original's mapping, 0x00475ff0 /
  +0x580, not traced). This fixed "the player is always pointing up".
- Material colours (the records' ambient / diffuse / specular and the untextured parts' colour) are stored
  blue-first, 0x00BBGGRR, like the UI bank's flat colours (Roman's belt is brown, not blue).
- Instance placement scale is ignored by the original (archways 0.5, pine trees 0.55..0.88 draw at full size).
- The follow camera: eye at Trotter_ref's Camera_end (22.703 behind, 11.128 up), looking at Camera_start's target
  (7.194 ahead, 6.877 up); rest tilt -0.141 rad, as in the original's camera dumps.
- Speech subtitles go to the top message feed as "Speaker: line" in #1fffff (the chat colour table's entry 1), as
  every feed line; there is no bottom subtitle line in game.
- Extras tokens: placed pickups spin at rotate_rate; the message and unlockables follow fn_004a0850 and the table
  0x0057e878; the count persists in options.json (delete it before a comparison run).

## HUD and menus (subsystem port, 2026-10-14)

What the remaster does: docs/subsystems/hud.md section 12 and menus.md section 4. `python tools/port_queue.py ui`
went from 168 to 121 entries; every remaining one is replaced by the engine or not needed, one row per function in
docs/port/ui.csv (which `port_queue.py` reads):

| Original | Remaster | Why no port |
| --- | --- | --- |
| UI object framework: 0x004626e0, 0x00462770, Hud_CreateLevelUi 0x004629b0, 0x004637d0, ObjectList_FindByName 0x00463ad0, 0x00463c50, 0x00463c90, page shown bits 0x00463d60 / 0x00463df0 / 0x00463e80 / 0x00463f30, 0x004643b0, 0x004643f0, kind bits 0x004648a0 / 0x00465350 / 0x00410bd0 / 0x0046ce40, base init 0x004648b0, hit rect 0x00464990, 0x00464a00, 0x00464ae0, placement 0x00464b10, 0x00464e10, 0x00465130, 0x00465160, text init 0x004651a0, the UiText setters 0x00465470 / 0x00465850 / 0x004658a0 / 0x00465970 / 0x004659b0 / 0x004659d0 / 0x004659e0, class inits 0x00466430 / 0x00466520 / 0x00466530 / 0x00466580 / 0x00467170 / 0x00467270 / 0x004676f0 / 0x004679f0 / 0x00467d90 / 0x00467f00, 0x00467fd0, 0x0046b0a0, 0x0046bbe0, CheckBox_Init 0x0046cbc0, Stepper_SetSteps 0x0046cf70, FitText_Init / SetScale 0x0046d400 / 0x0046d580, Spline_Coefficients 0x0046d630 | Bevy entities spawned from layout.json (hud.rs `spawn`, gamemenu.rs `Spawner`), `HudElement` / `HudText` / `MenuRow`, `place` | plumbing of the original's object system |
| Screen manager: UI_Init 0x0048ff90, 0x00490760, 0x004909f0, 0x00490a20, 0x00490a30, 0x00490a80, 0x00490b40, 0x00491020, 0x00491110, UiScreen_Construct 0x004916e0, 0x00491750, show 0x004918b0, selection 0x004919c0 / 0x00491b60, 0x00491ae0, enable 0x004927a0, focus 0x004928f0, list 0x004929a0, stats screen ctor / dtor 0x00494000 / 0x00494030, credits ctor 0x00415910 | `GameMenu` (page, stack, fades), `navigate` / `animate` | plumbing |
| Save / load lists: ListBox 0x0046bdd0 / 0x0046be90 / 0x0046bec0 / 0x0046bed0 / 0x0046c5f0 / 0x0046c980, save slots 0x004a2f70 / 0x004a2fd0 / 0x004a2fe0 | the single quick save (savegame.rs) | the remaster's save design |
| 0x00415d50 (poster render target), 0x00494820 (poster widget release), 0x00495e50 (text table), DisplayMode_Pack / Unpack 0x00470c90 / 0x00470cc0 | statscreen.rs draws the poster as a quad; `Texts`; the window size in options.json | renderer / storage plumbing |
| 0x00401010, Options_Apply 0x00401ce0 | options.rs applies each option (window, exposure, far plane, mixer, mouse) | owned by options.rs |
| Mode counters 0x00465be0 / 0x00465c90 / 0x00465ce0 / 0x00465d30, flag and handle meters 0x00465d50 / 0x00465de0 / 0x00465e30 / 0x00465f30 / 0x00465f70 / 0x00466010, 0x004671b0 | none | not in lev_1_m1 (Game+0x26ec, +0x26fc, +0xcbc unset); later levels' mechanics |
| Auto-aim pass 0x004067e0 / 0x00408050 / 0x00408190 / 0x004082a0 | none (the option shown disabled) | option off by default; open: port with an auto-aim option |
| Key rebinding entries 0x00447d20 / 0x00447e20 (labels "A or B" ported, 0x00447e70) | the controls page shows the device's bindings | open: rebinding not ported |
| 0x00410ca0 (cheat flags applied at level start) | cheats.rs applies effects on activation | the 0x2000 object pass (0x00418690) is not ported |
| UI_GetLoadingText 0x00447600, timed splash 0x00493520 / 0x00493590, exit screen 0x00493770 | none | no loading screen (the level loads before the title menu); LegalText's art is not exported (the movie screens are movie.rs since 2026-10-15) |
| 0x00467220, 0x00467740, 0x004677c0 | none | their UI elements are not identified (hud.md open question 7) |

Level-1 coverage follow-up (2026-10-15, branch agent-reach): `tools/coverage_level.py` reached 43 more functions.
- The pub screen's update 0x00493130 -> 0x004695d0 is now ported in full in pubscreen.rs (docs/subsystems/ui.md section 4).
  That covers the bar run-down, the award grow / shrink / glide, the notice, cursor moves along the bank's nav links,
  the pointer boxes, the 0.1 s glides and the weapon info of the selected slot. The pool bar's full scale is now
  health_pool, not health_bar_rate.
- Cited where the remaster already did the work: the Stats screen's line fillers (0x00494160 / 0x00494280 /
  0x00494310, Stepper / FitText, Ui_DrawPixelRect, 0x00494870), the prompt formatter 0x004652b0 and the sticky's
  range test 0x00408330.
- The loading screen and the save / load slots are replaced, listed in ui.csv.
- Camera zoom state 0x0040fae0 / 0x0040fb00 / 0x0040fb50 / 0x00475850 is cited in camera.rs.
- anim and input rows are in their CSVs.
- Test: pub_screen.
- Not ported: the carry / swap when Roman's three weapon slots are full (needs a third weapon).

## Mission, triggers, pickups and camera fx (subsystem port, 2026-10-14)

docs/subsystems/mission.md "Remaster port": every logic object (volumes, ticker sites, counters, timers, camera fx,
the pub, generators, the mission object) compares the broadcast trigger with its own fields, from the instances.
Replaced or not needed: docs/port/game.csv. Tests: mission_start, volumes, ticker, pickups, pubflow, winfail.
Trap: a playtest expect value that is a two-number array is a range, so list-like state fields are strings
(`mission_obj.compass_target` "234,252").

**Ticker sites on moving objects (2026-10-16, agent-blockers1).** mission.rs `lock_sites` ports
PlayerTickerBomb_AttachToParent 0x0044e120: a site with lock_parent_tag rides the lock_parent_bone (p_player_tickerbomb
"ref_ticker") of the run-time object of that tag (level.rs `ObjectTag`, on the squads' goliaths); its position is the
bomb's; Use arms only the nearest enabled site in range (0x004788a0); player_has_tickerbomb gives ticker bombs at the
start. lev_3_m1's three goliaths and lev_4_m1's are blown by their bombs as they walk (playtests ticker_goliath,
ticker_goliath_l4m1; oracle ticker_l3m1: the parent is the goliath, 2001 / 3000 at the same frame). Fixed on the way:
the squads' anchor followed any body under it and climbed its own goliath (z 24000), the strict-patrol squads walked at
move_speed (40) instead of patrol_speed (10), the squad scan's ray started inside its goliath and never saw Roman, the
goliath took its own side's bullets. Open: G1-G3 above; lev_3_m1's win also needs the peasants (agent-peasants: the
caged peasants walk to the doormats, peasant_success_trigger 4000).

## Save / load (subsystem port, 2026-10-15)

Pause menu Save Game opens the original's save screen (six slots, saves/game1..6.json next to the exe; saveslots.rs,
feedback 26), main menu Resume Game its load screen; checkpoints write saves/quicksave.json. Quick Load and Restart
from last save load the newest save; every load restarts the level in the running game and applies the save
(levelreset.rs, 2026-10-15; before, a fresh process with `--load FILE`). A level restart enters AppState::Loading
until the rebuilt level's frames are quick again, then play / the title (feedback 28: play right after the rebuild
took its seconds as one time step). Playtests: `clear_saves`, `note` + {"as": note} expects, until "loaded",
"real_time" scripts; state "saves", "frame_ms", "main_ms", "dt_max_ms", "entities". The original (src/save/save_01.cpp) serialises the whole
object graph (save manager 0x0048b620, class descriptor passes 0x0048bc80 / 0x0048bda0 / 0x0048c0f0, Havok body
states) after a 0x124-byte header with the level, the difficulty and a thumbnail, into one of six slots; the
remaster saves the state a player sees instead (docs/port/save.csv: replaced / not needed, one row each):
- Roman (position, facing, health, weapons, ammo, specials), the mission (triggers fired, volumes, counters, bomb
  sites, barracks, pub, compass, objectives, voice lines), dead placed grunts;
- each placed object no longer as placed: the template it is now (a multi-stage husk loads at its stage) or gone,
  and the windows shot out. level.rs `RestoreStage` puts it there without a death: no fx, no score, no
  send_trigger_on_death sent again (both were gaps until 2026-10-15). Saves from before keep loading (`destroyed`
  ids: first husk);
- the difficulty (Game+0x550; gamedata.rs `Difficulty`, the table 0x00589188 read by pickup.rs and statscreen.rs)
  and the money (Game+0x570; a new game starts with 5000, 0x00401e80).
Options: options.json is read key by key like options.opt (0x00462310: a wrong-typed value keeps that option's
default, the rest still loads). The option `checkpoint_saves` (off by default) gives the original's rule: the game
saves only where the original offers its QuickSave screen (0x00403560): when the mission start has been shown
(0x0044daf0) and when the pub screen is left (0x004690c0); the pause menu then refuses ("Saves are made at
checkpoints"). Not in the menus yet (options.json only).
Not restored (open): live grunts a barracks had released, companions' positions, the camera pitch, the alarm's
raised sirens (the original re-creates their fx_siren after a load, 0x0048c530).
Tests: save_write then save_read (regress SUITE, in that order: save_write writes engine/playtests/states/,
git-ignored); unit tests in savegame.rs (stages, file round trip, old saves, the save rule), options.rs (file round
trip, partial files, draw distance 0x0040f8c0), weapontable.rs (Weapon_BuildTable 0x00402110's values).

## Game object remainder (2026-10-15)

The Game object's own functions (src/game/game_01.cpp, start_level.cpp) are replaced by the App: construction and
teardown are main.rs and the process, the frame update is the Update schedule with physics in FixedUpdate, the level
requests (Restart, Return to title, Load) restart the level in-process (levelreset.rs: the level's entities
despawned, its resources re-initialised, its spawners run again). Ported as data: the weapon table (weapontable.rs: 28 slots of names, the slot numbers the
pickups and the AI's pickup_drop_specific use) and the difficulty table (gamedata.rs). Not needed on lev_1_m1: the
time-scale effect 0x00403aa0 / 0x00403ac0 (only fx op 0x72 sets it; no lev_1_m1 fx script uses it). The rest:
docs/port/game.csv.

## Object model (2026-10-14)

docs/subsystems/objects.md has the Object base class, default objects, the factory and triggers with addresses.
Replaced by the engine (not ported line by line): the property readers (gamedata.rs `Props`: placement over
template), the object lists of World +0xe6c.. (components, queries, `physics.no_bullets` for the bullets' list
+0xe90), attachment (the Bevy hierarchy), matrices and scale (Transform / GlobalTransform), visibility (models not
spawned), reference counts and removal (despawn), the force action pool (Jolt impulses), the per-object update
(Bevy's Update schedule). Ported: Object_TakeDamage's cap (fx.rs `take_damage`), husk1..husk9 picks and multi-stage
husks (a husk is a new object with its own health, fx and husk: the two-floor houses go _da -> _db -> _dc), windows
dying with their house, no_collide / visible / collide_barrier, DefaultGameObject trigger fields (level.rs
`object_triggers`), impact_force's default 2010. Test: engine/playtests/objects1.json and the unit tests (regress.py
`unit`).

## Player and weapons (subsystem port, 2026-10-14)

docs/subsystems/player.md section 10 has what was ported with addresses, the tests and the open questions. Replaced
by the engine (docs/port/game.csv): the weapon-state slot lists and ammo records (Roman's `rifle` / `mg` / `special`
fields), the PlayerWeapon objects (the hand models, `Weapon` from the property set), the Projectile object's create /
launch / remove (spawn_projectile and a per-frame ray, no physics body), Object_Release and the spatial grid (Bevy
despawn, Jolt's broad phase). Changed play: the reload refills at clip_reload_time (1.0 s, was 1.43), a standing jump
takes off after trotter_jump_into (0.133 s), the machine gun's scatter grows over the first second of fire, sticky
bombs set each other off, grunts' arrows fly an arc (spawn_projectile_to), Roman says an idle line after 30 s.

## Weapons (2026-10-17, agent-weapons)

`engine/src/weapons/` (docs/subsystems/weapons.md): Roman's `Arsenal` (three gun slots, the specials' counts) replaces
the rifle / machine gun / stickies / topsy fields; the sniper rifle, the Gurner, the vindaloo, the land shark, the black
hole, the Guy Fawkes bomb, auto-aim and the pub's swap are ported, each with a playtest (weapon_*). Tuning items for
the comparison with the original (guesses and differences seen, not checked frame by frame):

- the force actions (0x00455a30 / 0x00471750) are modelled as a pull of the velocity toward the wanted one at the
  factor per second (the vindaloo's (1 - d/d0) x 100 + 0.5, the shark's, the carried AIs' 5, the projectiles' 1 toward a
  black hole); the original's Havok response to the force is not decoded;
- the black hole rises 5 % of the way a frame in the original (frame-rate dependent): taken at 60 frames a second;
- Roman's pull toward a black hole is added to his velocity, which the drive (player.rs control) pulls back toward
  the stick's: how strongly it moves him is to compare; the carried AIs' "+0x948" speed is taken as their move speed;
  which AIs can be carried (vt+0x254) and bitten (vt+0x24c) is guessed (grunts and captains on their feet);
- the shark's arc is flown here (no physics body), its height is the ground under it + its depth (Object_ForceGround's
  arguments read as a ray start and reach), the fin and the missile face along the heading (`rockets::facing`); a hunted
  AI stands still (the original's guard state 0x79 with its aim set); the bite ends when sharkbite_* has played;
- the vindaloo's lock-on candidates and the sniper's pierced list are the grunt-class characters (World +0xea4 / +0xe8c
  not traced: vehicles and companions may belong); the fan's yaw / pitch signs (which rocket goes left / up);
- auto-aim aims at the target's point without the lead of 0x004580a0 (the target's speed); the narrowed view's
  horizontal half-angle uses the 1.7204 aspect factor sticky.rs measured;
- the Guy Fawkes bomb flies straight at 300 (no gravity); a turned guard holds his post without 0x0042ec20's guard run;
  the receivers within 200 by their bounding sphere (4.66);
- the launchers' hand models are drawn in their rest pose on the hand (as the Topsy Turvy bomb's): their grip is to
  compare; the pub's carried gun rides the cursor (remaster: the original's carry marker is not traced).
- the knockout bomb (2026-10-18, weapons/glove.rs, agent-leftovers): the glove model sits in the hand (the original's at
  r_wrist), its six targets get no HUD markers, a flung grunt flies without a body and lands on whatever its step meets,
  a punched one is a ragdoll from the punch on (the original flails through Character_Launch's arc, ragdoll on
  landing); the land shark's prey is now GruntObject_Slot147 0x00434e10 (targeting.rs `can_be_bitten_now`: a gunner on
  his gun can be bitten, a grunt on a roof or in a window cannot); the black hole's carry-off keeps the old guess.

## Cheats

Options > Cheats: the 12 codes of the table 0x00589090 (read from the exe: SHOOTMOR full ammo, AMBULNCE full
health, VERYHIIQ big heads, EATGRENZ big hands, SHOESIZE big boots, NDMRBLTS infinite ammo, SHIELDME invincible,
PRAYTOME god mode, MISSIONS / AMATINEE unlock levels / movies, WAAAAAAH upside-down screen, DAJIGSUP list), toggled
as Cheats_Check 0x004107c0 does, messages from the text bank (cheats.rs). The big-part scale (x2) is a guess.
Trap: systems that change joints must run after skin::AnimateSet (PostUpdate), or the animation overwrites them.

## Developer tools and session recording (2026-10-18, agent-devtools)

engine/src/devtools (remaster only): `DevToolsPlugin`. mod.rs: `DevSettings` (options.json "dev"), `DevToggle` (the
cheat toggles drive `Cheats.active` through cheats.rs's flag constants: set at every level load and on change, cleared
when switched off), `DevAction`, the `DevCommand` message (menu, hotkeys F5 / F9 / F8, command line `--level`,
`--dev`), `DevOverlay` (a developer screen is open: the game's menus take no input, `overlay_closed` on
gamemenu::navigate; opened in play, it pauses under the pause menu and resumes on close). menu.rs: the egui menu
(pages Main / Levels / Weapons; keyboard and pad through input::Controls, mouse through egui; the state log's "dev":
open, page, selected label, toggles on, cheat flags). overlay.rs: FPS and state overlays, egui zoom by window height.
session.rs: per-launch session folder (`%LOCALAPPDATA%/AndRemaster/sessions/<local time>`; a tracing layer in the
LogPlugin writes game.log and the EVENT lines to events.jsonl; a panic hook writes panic.txt with a backtrace;
state.jsonl 2 Hz with the bulky lists every 10 s; inputs.jsonl; frames.jsonl; F8 bookmarks with screenshot, save and
note; retention 20 sessions / 2 GiB). Tool runs record only with a script's "session": true or AND_SESSION_DIR.
tools/session.py lists / summarises sessions and turns a bookmark into a playtest. Tests: devmenu_toggles,
devmenu_level, devmenu_title, devtools_quicksave, devtools_bookmark, unit tests (retention, stamps, menu items, toggles)
and pyunit (tools/test_session.py). Cost: 4K Remaster bench 117.1-117.3 fps with and without recording (driver cap).
Note: `PlayState` is filled every frame in normal play too (it always existed), so the session's state log adds only
its 2 Hz serialisation; in normal play the session drains the state's "events" list, which grew without bound before.

## Ragdolls

`engine/src/ragdoll.rs`: a dying character with a skeleton becomes a Jolt ragdoll (oxijolt `RagdollSettings`), with
11 capsule parts (center, chest, head, shoulders, arms, thighs, shins) joined by swing-twist joints. Each part is
built from the bone's world pose at death and launched with the hit's push. The `drive` system (PostUpdate, after
`skin::AnimateSet`, before transform propagation) writes the bodies back into the bones. Characters without those
bones keep the single-box fallback in `ai.rs` die(). The original's GoRagdoll parameters (Havok) are not recovered,
so the sizes and limits are guesses judged by eye. Test: `work/remaster/play/death2.json` (`{"kill":1000}` gives a
heavy hit); the b1..b4 shots show the grunt down, splayed and settled, with no jitter.

## Corpses (2026-10-17, agent-corpses)

`engine/src/corpses.rs` (CorpsePlugin): the dead's bodies, for every AI character (grunts of every kind including the
army's, companions, critters; a grunt the Topsy bomb throws into the sky is removed at once). A death inserts a
`Corpse`: `Corpse::ragdoll()` (GoRagdoll 0x00431310: 5.0 s, the last 0.5 s fading), `Corpse::animated(slot)` (0x00427100,
state 0x191: 0.5 s, all fading, counted only once the death anim on that slot has ended), `Corpse::lying(..)`. The
countdown is Corpse_CountDown 0x0042eba0 (+0x9f0 m_deathTimer / +0x9f4), the fade `camfade::ObjectAlpha` (the meshes'
own material copies, marked `OwnAlphaMaterial` and freed with them), the removal Object_Kill 0x00455640 in `Last`: the
Jolt ragdoll or body leaves the world, fx_death plays and the husk appears (`Remains`, `level::SpawnHusk`; no shipped
character sets either), the entity tree goes, and a placed grunt's id is kept for saves (`Corpses::removed_instances`).
GoRagdoll's limit (at most 4 down, 0x00431000) makes a later death animated (ai.rs die).
Settings (graphics.rs, Remaster / Original): `corpse_keep` 48 / 0 (no cap), `corpse_time` 60 / 0 (the original's),
`corpse_fade` 1.5 / 0, `ragdoll_limit` 16 / 4. With bodies outliving 5 s a ragdoll at rest (oxijolt is_calm, at most
15 s more) settles: Jolt ragdoll removed, pose kept (`corpses::Settled`, charanim skips it).
Before: no body was ever removed (each death left its ~70 entities and an 11-body Jolt ragdoll). Measured after
(lev_1_m4, two army deaths a second, `corpses_l1m4`): Original 3-8 bodies lying, at most 4 ragdolls, entities 31.5k ->
24.7k over 65 s of killing, physics bodies 160-230; Remaster 50-53 lying (48 + fading / dying), ~30 settled, entities
flat. Tests: playtests corpses_l1m4 / corpses_l1m4_remaster (regress; `kill_army` steps, `at_most` expects,
`physics_bodies`), oracle scenario `corpses` (corpse.ragdoll 5.00 / 5.02 s, corpse.anim 0.50 / 0.50 s; tools/oracle_diff.py
`bodies`). Debug: `ENTDBG=1` logs the entity shapes every 30 s (playtest.rs).
Not corpses, found on the way: lev_2_m4's 123k entities are mostly the army's live marching members: its five army
timers' anims create 357 platoons (1,785 members) in 133 s and each member is a skinned low-detail grunt of ~70
entities (the original draws them as particles, pool 2 of Game+0xa90); with the releases waiting on the live-grunt
limit they pile up (1,745 members, 122k entities, 70-120 ms frames headless at 150 s). Done since (agent-armyperf,
below): each member is one crowd mesh.

## Army crowds (2026-10-17, agent-armyperf)

The original's marching platoon members are not characters: FakeGrunt_Update 0x00446460 gives each ref point of the
platoon model one particle of pool 2 (Particle_Alloc 0x0046d950: a 16-unit sprite, half-extent 8, uv rect u 0..0.25,
v 0.625..0.875 of n_fx_bank_2 for grunts / regulars / torches, u 0.25..0.375, v 0.51..0.624 for captains: the grunt and
captain pictures in the bank), placed on its point every update; only at a release (the "release_platoon" note, then
one member every 0.1..0.2 s while the live army grunts are under Game+0xbf0) does a member become a real
"grunt" / "grunt_regular" / "captain". So the rules are: every marching member is cheap, a full grunt exists only
after its release, and the grunt limit (army_max_grunts 16 / army_unpause_count 14, the timers' grunt_limits) caps
the full ones; the pool's 2000 particles (shared with the effects) capped the members (docs/engine-limits.md).
army.rs keeps those rules; the representation is engine/src/crowd.rs (CrowdPlugin, game-neutral): a (model, anim)
pair is baked once per level into a flipbook (the run cycle skinned on the CPU at 30 frames a second of anim time,
8..48 frames, parts sharing a material merged: the grunt is 2 meshes), and a `CrowdMember` is one entity showing the
frame of its own phase (spread by the golden ratio; the pace 0.85..1.15 drawn from the shared random stream as
before). A frame change swaps the member's meshes, which the renderer re-extracts, so the pose rate falls with
distance (`crowd::frame_step`: every baked frame nearer than 150 units, every second to 400, every third beyond:
30 / 15 / 10 a second) and a member out of view keeps its pose. Members share meshes and materials, so the renderer instances them; no shadows (as before). The look is
the running low-detail grunt the port already showed (shots compared at lev_1_m4 30 s: no visible difference), at a
sprite's cost: one entity with one child instead of ~70.
Measured (headless 640x372, turret on the army, 190 s; the machine loaded by other agents' runs both times):
lev_2_m4 entities 110k -> 14.9k at ~1,580 members (frames 74 -> 56 ms mean; the rest is the main world's AI and the
machine), lev_1_m4 30.1k -> 9.4k max (frame 18.3 -> 16.7 ms, main 12.3 -> 8.4 ms). At 4K Remaster (headless,
the playtest clock's 60 Hz cap = 16.7 ms; lev_2_m4 after 60 s, ~1,400 members, ~29 live grunts): frame 38.6 ->
22.5 ms with the crowd, 17.4 ms with the distance rates and the platoon hit test's sphere reject (fake_hits: shots
gathered once, one inverse per platoon), main world 25.2 -> 12.0 -> 9.2 ms (of which the AI 4 ms); same kills.
Test `army_l2m4_long` (regress LONG: members >= 1000 and entities <= 25k at 150 s); complete_lev_1_m4 /
complete_lev_2_m4 pass.

## Sticky bomb (piece 3, 2026-10-13)

`engine/src/sticky.rs`, from StickyBombProjectile (game_07.cpp 892-1300) and Hud_UpdateCrosshair's auto-aim
(0x00406f50); the spec with every address is summarised in the module comment. Fire Special plays trotter_throw /
trotter_run_throw and wpn_grenade_throw, the bomb leaves the L_hand after 0.4 s, flies 1 s on a solved arc turned 10
degrees left, homes (k 2..7) on a ref_hit_0N bone of the first grunt inside the narrowed view (factors 0.04 / 0.12,
range 250, line of sight; the cone test's exact frustum is a guess), sticks (ground: hit - direction x 5), scares,
lingers 4 s, explodes (100 area to 30 / 45, 300 to the stuck one). Its trail is a streamer: `fxvm.rs` now runs the
streamer ops 0x4d..0x57 (ribbon trails of 14 lev_1_m1 fx; texture 0x55 / uv 0x56 not ported: untextured).
Compared with the original (work/shots/stickB, stickX, stickY vs work/remaster/play/stickB, stickH, stickX): throw
pose, blue trail, sticking, the grunt's "stuck to me" panic, linger and blast timing and sounds match.
Fx look (same day): the white sheet was the blast ring (fxo_blast_ring_snow, a layered material): the layered
shader now runs the original's stage maths (LERP by the mask colour, times t1 alpha x mask alpha) on textures
un-premultiplied when the export flags them premultiplied (DXT2: the snow flakes, the ring), which keeps the snow as
before and makes the ring faint; textured fx objects are tinted by their material diffuse (0.588). Measured on the
blast (light added over the frame before): original mean 30..42 / p90 70..82, engine now 30..36 / 47..61 (was 53..70
/ 108..128). Decals (op 0x6a, `decals.rs`: the landscape inside the fx object's box, clipped, 160 pooled) now leave
the blast's scorch mark and the bullets' small marks. Open: decals on placed objects; penguins are not knocked
about by blasts.

## Companions (Q and Jonesy, 2026-10-13)

`engine/src/ai.rs` Companion, from MolmerObject_Slot47 0x00485910 and the fire cycle (spec in the module comment):
formation on Roman's ref_q_00 / ref_molmer_00 bones (regroup beyond formation_distance + 2.5, speed (d / delay) x
move_speed), nav.rs routes re-planned every 0.25 s, targets within scan_distance (cone, line of sight), and support
fire: with Roman's Fire held, no target and his heading within 45 degrees, they shoot at his aim point. Bursts follow
the fire anims' notes (Jonesy 4 at once per 0.3 s loop, Q 6 per 0.633 s loop), burst / clip delays, reload anims.
Verified against the original's sound log on the fire-held test (gunA): Jonesy's 4-shot bursts every 33..49 frames
(original 38..47), Q's machine-gun loop once per burst every 89..133 (95..140). level1b: both follow the whole level
(q_dist / jonesy_dist in the playtest log). Not ported: the slot swap, orders, retaliation, Q's healthy tea, barks.

Measuring AI against the original (2026-10-14): the hook's camera dump lists every AI object as [x, y, z, vtable,
facing x, y, z, address, target address (+0x588), health (+0x454), anim id (+0x498)]; capture with
`tools/shot.py --scenario level --frames 600,630,... --keys "500:MOUSE1,510:17*1100,700:MOUSE1" --name fightT` and
read work/instances/main/and-frame-N.camera.json. AIDBG=1 logs the engine's companion targets and character hits.

## Grunt AI and companions: what the engine replaces (2026-10-14)

The ai port queue (`python tools/port_queue.py ai`) was reported empty here, but many of its `not needed` rows were
deferred work (2026-10-16 audit: docs/port/README.md, docs/port/deferred-2026-10-16.md); they are now `deferred`
and count as open work. What the engine replaces rather than ports:
- the nav grid Game+0xaa4 (grid marking, A*, path smoothing, the route follower AIRoute_*, the body drive 0x00454d80):
  nav.rs A* over the Jolt world, re-planned every 1 s (grunts) / 0.25 s (companions), velocities set from the
  property sets' speeds;
- object plumbing: the AI position / matrix slots, in-frustum culling, the class descriptors, teardown, physics
  descriptions, collision filters, the alarm controller's global lists (alarm.rs `Markers`), the trench slot
  bookkeeping (ai.rs `Barricade`), the companions' pairing;
- the marker getters (alarm.rs `Marker` fields) and the per-grunt speech handles (the engine keeps none: the taunt and
  the barks take a grunt as silent).
Behaviour state and tests: docs/subsystems/grunt-ai.md 13-14, companions.md 7. New playtest steps: `"template"` with
`spawn` (e.g. "captain"), `"noise": [x, y, z]` (a noise of Roman's, as a bullet's impact), `"barks_ready": true`
(the shared bark delays count as passed); state-log fields `ai.*` (live, minds, reacting, long_hits, spline, markers,
taunts, throws, captains, noise, near.{mind, kind, reaction, targets_player}) and `q_lead` / `jonesy_lead`.
`tools/regress.py --scene DIR --play DIR` runs the suite from a worktree without work/.

## The character body and the game's contact rules (2026-10-16, agent-charbody)

The AI characters' bodies are the original's (ai/body.rs, Object_BuildRadiusCollision 0x004527d0): a dynamic sphere of
`collision_radius` sitting on the feet, translation only. Around it, three pieces that belong to it:
- **contacts.rs** (game-neutral): per-body `ContactRule`s that a Jolt contact listener applies: `group` (bodies of one
  group never touch: the AI's +0x510, a squad's members and its goliath, 0x0042de60 / GenericAIObject_Slot40
  0x00432590 through CollisionFilter_IsEnabled 0x00411710), `ignore` (one body passed through: +0x50c, set by a bump
  while walking somewhere, GenericAIObject_Slot44 0x004320b0), and a run-time `friction` (Body_SetFriction 0x00458250;
  combined sqrt(fA * fB) as 0x00413240: 0 in a squad, the material's otherwise, 0x0042cb80). The listener also keeps
  each ruled body's contacts of the last step (`Physics::touches`): a jumper lands on any contact (the contact note
  0x004320b0 -> 0x0042e1c0), not only on ground under him. `Physics::set_contact_rule` drops a body's cached contacts
  when its rule changes.
- **Bullets** do not hit the sphere: the original's projectile ray meets the collision model (0x0047e580,
  hitparts.rs). The sphere is in `no_bullets`; `Physics::hit_volumes` holds a capsule round the model (0.55 x the
  radius, 0.3..10.1 above the feet) that `raycast_bullet` tests besides the world (a head above the sphere still
  takes a shot).
- Statics have the default material's friction 1.0. force_ground's touch tolerance is 0.3 (world 0.2 + mesh radius
  0.1: the original's lifted grunts end 0.31 off the overhang). Jolt's solver collides with triangles from the front
  only, Havok's from both sides; a turned-round copy of the landscape was tried for B3b and dropped (it did not hold
  the grunts and cost ~12% frame time in lev_1_m1, which also broke audio_voice: its follow-on lines end in real time).
- The AI's line of sight (`line_of_sight`: the squad's scan, a walk's straight leg) passes AI bodies as 0x00426fd0.

The squads and jumpers that broke when only the sphere was tried (c6e6fae): squad members pushed each other apart and
the squad's scan was blocked by its own members (now: one group, sight through AI bodies); jumpers held on an edge
by friction never "landed" because the landing asked for ground under the feet (now: any contact).

## Comparing sounds with the original

The original logs every sound it starts with `[test] log_sounds=1` (hook, and-sounds.txt in the instance: frame
since load, name, volume); the engine logs its own with `AND_SOUND_LOG=<file>` (game seconds, name, volume_db,
2d / 3d). Same inputs, then compare the name counts:

```bash
.venv/Scripts/python.exe tools/shot.py --scenario level --frames 520 --keys "500:MOUSE1,510:17*300" --frames-total 900 --name walkSnd --set test.log_sounds=1
```

```bash
cd engine && AND_SOUND_LOG=../work/remaster/play/walkA.sounds.txt ./target/release/and_remaster.exe --background --size 640x372 --play ../work/remaster/play/walkA.json
```

2026-10-13: footsteps, clothing, wind, the oil pumps' and the pub's loops, music, ambience one-shots and the
grunts' idle chatter (fidget anims' fx notes) match. Open: penguin calls (PenguinObject not ported: the 10 penguins
stand still; the original calls in bursts, every penguin at once, at load and ~frame 238). Music segments after the
first go through the streaming path and are not in the original's log. The ambient voice line is a random pick.

## Audio (subsystem port, 2026-10-14)

engine/src/audio.rs is the sound system as a module (bank + messages; docs/subsystems/audio.md section 8 maps it to
the original), gameaudio.rs the level's use of it. What changed for everything that plays sounds:

- Positional sounds now get the original's distance curve (before, Bevy's spatial sinks never received it: every
  positional sound played at full volume at any distance). Grunts' footsteps beyond 80 units are silent, as in the
  original.
- Sounds on Roman (`ListenerBody`, radius 8) play 2D, as the original plays the player's own sounds.
- The bank's sequences play by themselves: a looping `*_lp` with an intro / outro starts with the intro and ends with
  the outro on a StopSound; callers send only the loop and the stop.
- Option volumes act on playing sounds; menu sounds use `PlaySound::menu` (0 dB, as the original).
- The game pause (`Time<Virtual>`) pauses the voices, except those started during it and the flagged music jingles.
- Playtests: `{"audio": {...}}` steps and the `audio` state-log field (see audio.md section 8); `"x": null` in an
  expect means the field is absent.

A one-shot ends when its length has played in game time (`Voice::length`), not when the output device drains it
(see "Tips and traps": scripted runs must not depend on the machine's speed).

The output device (engine/src/audiooutput.rs, 2026-10-18, player feedback A1): bevy_audio's `AudioPlugin` is
disabled (its output opens the default device once and cannot be reopened); `AudioOutputPlugin` plays the same
components (`AudioPlayer`, `PlaybackSettings`, `AudioSink` / `SpatialAudioSink`, `SpatialListener`) and follows the
Windows default output device: a watcher thread polls it once a second and catches the stream's "device lost" error,
opens the new default off the game thread, and the game re-creates every playing sound on it at its `PlayHead`
(its place in the file in game time, volume / speed / pause / mute carried over; never `try_seek`). With no device,
sounds play into a silent mixer and move to the next device that appears. A movie's soundtrack re-syncs itself
(movie.rs closes the gap > 0.5 s). Unit tests: `cargo test --release audiooutput` (fake devices). A file that fails
to decode is logged and skipped (bevy_audio panicked).

Debt: `ListenerBody.radius` stands in for the original's ownership test (0x0045a480); the characters' own voice
handles (+0xb4c / +0xb50) are not ported (ai.rs's cue sites pass no speaker); music segments chain on a frame timer
(the segment's duration), not on the stream's end.

## Animation, camera and input (subsystem port, 2026-10-14)

docs/subsystems/anim.md, player.md 4 (the remaster's camera), remaster-input.md; queues anim / camera / input empty
(docs/port/anim.csv, camera.csv, input.csv). skin.rs cross-fades every anim change (the AI 0.25 s, Roman by the
kind table 0x005cb3a8), plays movement anims at their speed-driven rate (backwards for the backward modes) and fires
notes over [previous, now); charanim.rs plays Roman from the exe's anim table (work/remaster/<level>/player_anims.json,
made by tools/remaster_export_player_anims.py: rerun it for a new level); footik.rs places his feet. camera.rs owns
the follow / zoom / death camera, the smoothing and the shake (`ShakeCamera`; camera owners go in `CameraPlace`).
Changed feel: running while firing plays the run anim (the table links it), the stick look is linear 3 / 1.9 rad/s
with the original's 0.21 dead zone, the pitch reaches the original's limits, the stick's move speed follows the
original's curve. Tests: anim_blend, anim_notes, camera_follow, camera_death, input_pad, foot_ik.
Traps: Python text-mode rewrites and Git Bash `sed -i` turn CRLF files into LF (compare `git diff --stat` with
`--ignore-cr-at-eol`); `play()` restarts only on a name change, so the same anim as a new entry needs `restart`.

## Level export (every level, 2026-10-15)

`.venv/Scripts/python.exe tools/export_level.py LEVEL` (or `--all`, `--steps a,b`, `--report FILE`) runs every
exporter for a level into work/remaster/LEVEL in order: scene, anims, audio, mission, ui, icons, player
(player.json), instance_props, grunt_ai, player_anims, textures (textures_remaster). It stops a level at the first
failing exporter with its error, prints one line per level (and a table with --all), and checks the fixed paths
engine/src reads from a level folder. All 24 levels export cleanly (about 80 s and 400-600 MB each). player.json,
instance_props.json and grunt_ai.json were hand-made for lev_1_m1 before; tools/remaster_export_player.py,
remaster_export_instance_props.py and remaster_export_grunt_ai.py (shared loader tools/remaster_level.py) make them
for any level and reproduce lev_1_m1's files (grunt_ai.json differs only in the grunt's weapon, now grunt_bow).
Data oddities in other levels: three invisible-wall skins with no vertices (x_3m1box_0 lev_3_m1, w_l4_m2_01
lev_4_m2, w_l5_m4_03 lev_5_m4: placed but not in scene.json); skins without a reference anim (captain_flare_1/9,
shrub_bomber_0, wildwoodgate_1/2); mission.json still has unsent / unreceived trigger ids where a level's logic
uses classes the mission export does not know yet (twiglet_generator (read by twiglet.rs from the props), jump_marker, object_ic, zeppelin, squad,
anim_player, base_turret, peasant). The loadout is the mission object's add / ammo / count flags only: nothing is
carried over from earlier levels (correction 2026-10-16: the player object is built with each level and its reset
0x00475650 gives it the mission's loadout; docs/subsystems/campaign.md; mission.rs `Loadout`).

## Campaign flow (2026-10-16)

campaign.rs: after a won level's Stats screen the end sequence (0x0044a230 states 11-14) plays the level's
success_movie, then loads next_level_name in-process through levelreset.rs (`RestartTo::Level`: the Loading state,
as New Game's restarts), or after lev_5_m5 shows the credits and returns to the title. The level folders are
`campaign::Levels` (work/remaster/<level>); the default asset source reads the loaded one (`LevelAssetReader`: every
asset-server file but ui/icons.png is byte-identical across the 24 exports, checked), and `campaign::enter` points the
spawners' folder (`Launch::scene`), the remaster texture list, the animation library's caches and the level's movies
at the next level before the rebuild. Carried: the money and the pub's once-only intro (the bank, Game+0x570..),
tokens and unlocks, the difficulty; not the weapons (mission.rs `Loadout`, player.rs `apply_loadout`: the mission's
weapons with full ammo, the first by weapon id in hand). Saves record their level and a load enters it; the title
always stands on the launch level (lev_1_m1), so New Game starts it. Extras' Replay Mission / Play Cutscene:
extras.rs. Tests: campaign_next (with failure -> Restart on lev_1_m1_b), campaign_chain, campaign_save,
campaign_movie, campaign_end, campaign_extras. Not ported: the Difficulty screen of New Game (case 71), the profile
choice (SaveGame / ChooseGame before a new game or a replay), an oracle run across a level change (open question).

## Base-defence levels (subsystem port, 2026-10-15)

lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5, lev_5_m5: the army (army.rs: army timers, platoons, the grunt limit, the
release, ladders, the breach, ArmyTuning for a difficulty setting), the base turret Roman starts in (defence.rs),
scripted objects and AIs (icobject.rs: bombers, flak, drops, gates; goliath.rs; shrubpatrol.rs), movers (mover.rs) and
lobbed shells (shell.rs). Spec and debt: docs/subsystems/defence.md. Tests: engine/playtests/defence_*.json (a
script's "scene" names its level; a `defence` step drives the turret: {"aim": "army"}, {"mortar": "platoon"},
{"frame": F}). Debt: the ic_* marker arrival is detected by army.rs from the grunt's position (plan: an ai.rs message);
the failure mob, the carried ladder and the turret's launcher models are not ported. Platoon members are crowd meshes
(see Army crowds).

## Effects (subsystem port, 2026-10-15)

docs/subsystems/fx-vm.md section 7; queue render-fx empty (docs/port/render-fx.csv). engine/src/fxvm/ is a module
folder: `script` (the interpreter, every opcode lev_1_m1's fx bank uses), `object` (the fx object update), `particles`,
`streamers`, `attach` (attach points: the named nodes of a template's default anim, `anims/<name>.json`), `body`
(physics glue and ground hits), `screen` (post-process effects), `materials`, `rng`. Start an fx with
`crate::fx::StartFx` (`Quat::IDENTITY` = unrotated in game space; before, it meant the engine's identity and laid
position-only fx on their side). What changed for everything that starts fx:

- Debris and children start at their attach points (`ref_debri_NN` ...), not all at the fx origin.
- Mesh emitters run: muzzle flashes repeat their flash (0x30) and eject shells (0x36); explosions spawn fx along
  points (0x19).
- Ground hits act: on-hit fx (`fx_imp_rock_large` ... with the surface variant), on-hit area damage (the arch's
  falling blocks deal 300 within 30..60: an `Explode`), on-hit death / delete; on-death fx (0x1d); spins (0x1a).
- Particles carry their age within their birth frame the right way round (they used to advance by the wrong part of
  the frame) and are drawn half a move ahead, as the original.
- Screen effects (`screen::ScreenEffects`): phases and weights as the original; only the fade is drawn (a UI overlay).
- Fx lights are published (`FxVm::lights`) but not drawn: the original shows none (engine-limits.md R4).

Playtests: `{"fx": {"name", "at", "yaw", "clear"}}` steps and the `fx` state-log field (counts per pool and template,
bodies, hits, lights, extent, screen); scripts fx_particles, fx_debris, fx_emit, fx_lights, fx_screen, fx_hits.

Debt (guesses to measure against the original): the impulse / spin scales (body.rs IMPULSE_SCALE, MAX_SPIN), every
fx body is a 1.5-unit box, the ground-hit test is a ray along the velocity (the original: Havok contacts), the glow /
blur / screen-texture post effects are not drawn, the mesh emitter's interpolation weight ([0x005bcb5c] taken as -1).

## Movies (subsystem port, 2026-10-15)

docs/subsystems/movie.md. `python tools/remaster_export_movies.py` (ffmpeg, once) writes work/remaster/movies/<name>/
(video.rmjv: per-frame JPEGs; audio.wav; movie.json with the subtitle lines and styles); the engine looks in
`<scene>/../movies` (`--movies DIR`). engine/src/movie.rs is the game-neutral player (`PlayMovies` / `StopMovies` /
`MovieEvent`, `MovieView::FullWindow` on its own camera or `Behind` on the HUD layer), engine/src/cutscenes.rs says when
A&D plays what: the boot intro list before the title menu (once a session: a level restart keeps the flag; tool runs skip
it; a title playtest asks with `"intro": true`), level start movies (none on lev_1_m1), the pub's pub_01 loop behind its
art (it replaces the guessed dark middle) and the credits movie. Queue movie: empty (docs/port/movie.csv). Playtests:
`{"movie": "a,b"}` or `{"movie": {"play", "at", "skippable", "loop", "behind", "subtitles"}}`, `{"until": "movie_done"}`,
the state-log fields `movie` (name, time, frame, frames, audio_time, drift, rate, subtitle, prompt, left; absent when
none plays) and `game_paused`. `tools/movie_compare.py` matches captures (the original's or the engine's) to movie frames
and checks the subtitle band. Trap: in scripted runs the game clock is a fixed 1/60 s per frame while the soundtrack
plays in real time, so the logged drift is bounded by the 0.5 s seek, not ~0 as in play.

## Presentation presets: Remaster and Original (graphics.rs, 2026-10-15)

docs/decision.md 2026-10-15: the player plays the **Remaster** preset (the default for a normal launch); every tool
run (--play, --snap, --camera, --bench, --background, --size) is pinned to the **Original** preset, the engine's
faithful look, so comparisons and the playtest suite never see a remaster feature. `--preset remaster|original`
overrides the preset for a run and `--graphics key=value,...` changes single settings over it (A/B comparisons,
benches; values JSON or bare words, e.g. `--graphics anti_aliasing=smaa,bloom=0`). No setting reaches physics, AI
or timing: gameplay is identical in both.

Module: `engine/src/graphics/` (GraphicsPlugin; `GraphicsSettings` resource; `resolve(preset, changes)`;
`texture_path` / `world_sampler` for asset loading; `graphics::menu` for the Graphics page). Options file:
`graphics_preset` ("remaster" / "original") and `graphics` (any setting by name over the preset; the Graphics page
writes only differences, a preset choice clears them). Settings and what they use:

| Setting | Original | Remaster | How |
|---|---|---|---|
| remaster_textures (+ remaster_textures_exclude) | off | on | the AI retextures, prepared by `tools/remaster_textures.py LEVEL` into work/remaster/LEVEL/textures_remaster (never committed); chosen when the level loads |
| ambient_occlusion, ao_quality / radius / strength, ao_exempt | off | on, High, 24 units, 1, ["ground", "snow"] | XeGTAO in Bevy's SSAO pass (since 2026-10-19, "Atmosphere, AO, ..." below) read in gamematerial.wesl on the ambient + fill light only (not the sun, not emissive, except the contact term); surfaces whose base / second / normal map name contains an exempt part (the snow landscape) keep only ao_snow of it, blended surfaces get none |
| anti_aliasing, sharpening | MSAA 4x, 0 | TAA, 0.35 | Bevy's TAA (with the temporal shadow filter) or SMAA High, then contrast-adaptive sharpening; Off / MSAA / SMAA / TAA on the Graphics page |
| anisotropy, anisotropic_fx | 16, off | 16, on | world samplers 16x in both (the engine's look since 2026-10-13); the Remaster adds it to decals and effects (the default sampler) |
| shadow_map_size, shadow_cascades, shadow_distance | 2048, 4, 0 (the level's: half its extent) | 4096, 4, 1.0 x the world's far clip | cascades fitted to what is drawn (options.rs world_far), 30 % overlap: every shadow in view is in a cascade |
| soft_shadows, shadow_softness | off | on, 6 | Bevy's PCSS (feature experimental_pbr_pcss) |
| soft_particles | 0 | 6 units | particle.wesl fades a particle over that depth where it meets the depth prepass: no hard line where a quad cuts the ground |
| far_fade | off | on | the far clip (the original's far plane) dissolves over its last 15 % (moving dither, smoothed by TAA) instead of a hard cut; draw distance stays the original's maximum, LOD always the finest |
| bloom | 0 | 0.08 | HDR target; only what is brighter than white blooms (threshold 1: stacked additive fire and flashes; the game's surfaces are clamped to white), added on top. Tonemapping stays off (the game's colours) |
| tessellation (+ _level / _shape / _smooth_angle / _sharp_angle) | off | on, 3, 0.75, 25, 50 | the old remaster's Phong tessellation of skinned meshes, refined once at load, crack-free across UV seams (graphics/geometry/tessellation.rs; "Ported from the old remaster" below) |
| terrain_detail (+ _strength) | 0 | 8, 0.6 | the terrain's normal map sampled again at 8x its tiling, its slope added (gamematerial.wesl) |
| lod_max, lod_scale | off, 1 | on, 2 | the original's LOD switching (Object_SelectLod 0x00452f50) in the Original; the finest everywhere in the Remaster (graphics/geometry/lod.rs) |
| alpha_coverage | off | on | alpha textures use MSAA's alpha-to-coverage (with MSAA only; TAA keeps the blend) |
| aa_mip_bias | 0 | -1 | the camera's MipBias under TAA, now read by the game material's samples |
| shadow_strength | 0 (the level's shadow colour) | 0.55 | a full shadow multiplies by (1 - strength) tinted sky blue (the old remaster's shadow apply pass) |

Engine limits and input (2026-10-19, agent-port-limits): the old remaster's `[limits]` / `[persistence]` / `[input]`
options are settings of the same presets, each group in its own module with its presets (physicsstep.rs: fixed step,
interpolation, solver iterations, body capacity; pools.rs: decal pool, particle pool scale, path search nodes;
debris.rs: kept debris; cursor.rs: the menu pointer and the window clip), flattened into `GraphicsSettings` so
options.json names them directly; the Engine page (enginepage.rs) shows them and the Audio page the master volume.
Table and mapping: docs/engine-limits.md "Remaster engine (Bevy / Jolt)". These do reach physics: the Original steps
the world once per frame of at least 1/60 s (the original's frame-tied physics), the Remaster in whole 1/60 s steps;
at the scripted runs' 60 fps both step exactly 1/60 s, so the playtests see no difference. The solver iterations and
body capacity apply when a level's world is made. Playtests: limits_decal_pool, limits_debris_keep,
limits_master_volume (engine/playtests); unit tests in each module (interpolation, frame-tied step, pools, debris,
cursor grab modes, the page's rows, the path budget).

Traps found on the way:
- Never give the game material a prepass shader of its own: Bevy then binds the material in every depth-only pass
  and the sun's four shadow cascades lose their batching (4K bench 113 -> 64 fps, in both presets). The depth
  prepass (TAA, SSAO, soft particles) therefore has no per-pixel far clip; the camera's far plane is the original's
  (options.rs world_far), so objects wholly beyond it are culled, and the sky dome is never culled (level.rs). The
  far part of an object straddling the far plane used to show the clear (fog) colour instead of the sky in the
  Remaster: since 2026-10-15 the sky fills those pixels (graphics/farclip.rs: the sky's parts use Bevy's standard
  material with a fragment shader that, with a prepass, draws without a depth test and keeps only its own pixels and
  the ones the game material clipped, the same test on the prepass depth; gamematerial_shared.wesl). Rejected: a twin
  material with a prepass shader for just the meshes reaching the clip (~+1.8 ms render CPU, +1.2 ms main, and a 2 s
  pipeline-compile stall when the first twins appear).
  The exception, by type (2026-10-16, player feedback 30): alpha-textured materials are `GameMaterial<AlphaTested>`
  (gamematerial.rs `Coverage`), a second material type whose prepass fragment shader (gamematerial_prepass.wesl)
  discards texels below `shadow_alpha_cutoff` (Remaster 0.5, Original 0), so pine branch cards cast their twigs'
  shadows, not their rectangles. Bevy picks prepass shaders per type, so solid materials keep the shaderless batched
  depth path; the alpha-tested ones are Blend (never in the depth prepass) and the shadow pass binds their material
  anyway (MAY_DISCARD). 4K Remaster bench unchanged within noise (~110 fps avg before and after). Systems that touch
  game materials are generic over `Coverage` and registered for both types (camfade.rs, graphics apply_game_materials).
- Never assume a material plugin can load another plugin's embedded shader before that plugin's build ran: the load
  fails ("Path not found: and_remaster\gamematerial.wesl") and the failed handle is shared, so the game material
  drew nothing at all.
- Bevy's CPU instance buffer path (`PbrPlugin { use_gpu_instance_buffer_builder: false }`) halves the render CPU
  but draws none of the game's meshes (2026-10-15): not usable as is.
- SSAO needs MSAA off (AO is skipped with a warning when the player picks MSAA).
- Retextures: locking the override's colour to the original in sRGB bytes left the snow 6 levels too bright (the GPU
  filters the finer detail in linear light): the lock and the mips are in linear light. The AI normal maps changed
  the lighting itself (n_b_t_snow: the snow +7 brighter): normal maps stay original. A fidelity gate (shrunk back,
  more than 12 levels from the original) keeps 7 originals (fx_snow_flakes' overrides are big white flakes for the
  original's dim dots).

Measured (2026-10-15, walkA same cameras, tools/render_compare.py against the original game's captures; snaps in
work/remaster/render/p4 and p5): Original preset mae 15.8 / 17.8 / 52.7 at frames 520 / 580 / 640, Remaster 14.6 /
17.0 / 52.0 (closer, no colour shift: mean colours within 1-2 levels of the Original preset). The Original preset
matches the build before the presets (differences at the level of two runs of the old build: snow particles).

Performance (4K, visible window, driver cap 119, `--bench 20`): Original 112.0 / 111.9 fps average (75-79 fps 1 %
low; the old build 113.1), Remaster 90.4 / 92.6 (55 1 % low): **below the 100 fps floor, open**. Single settings off
(bloom, AO, soft shadows) each gave back only 1-2 fps; Remaster with SMAA, no AO and no soft particles (no depth
prepass at all) 105.

Profile (2026-10-15, `--bench 20 --size 3840x2160 --background --profile`, main.rs / profile.rs): the background
cap was Bevy's unfocused-window update mode (60 Hz), not the presentation; benches now update continuously, so a
background bench gives the uncapped rate (below the driver's 119 cap): Original 115.6 fps, Remaster 97.4-100.3,
in line with the visible runs. Costs per frame (ms; GPU = sum of top-level pass timestamps):

| | render CPU | main CPU | GPU | fps |
|---|---|---|---|---|
| Original | 5.7-6.7 | 3.7-4.4 | 1.0 | 115.6 |
| Remaster | 8.6-9.5 | 4.1-4.8 | 2.6-2.8 | 97-100 |
| Remaster, no prepass (SMAA, no AO, no soft particles) | 6.3 | 3.8 | 1.7 | - |
| Remaster minus one of: SMAA instead of TAA / no AO / no soft particles / level cascades / 2048 maps / no PCSS / no bloom / original textures | 8.2-9.3 | | | - |

The GPU is far from the limit (SSAO 0.7, TAA 0.2, bloom 0.2, prepass 0.1 ms); the frame is the render world's CPU.
Any prepass costs ~3 ms of it: a chrome trace (bevy/trace_chrome) diff Remaster - Original gives queue submit +0.75,
submit_pending_command_buffers +1.1, write_binned_instance_buffers<Opaque3dPrepass> +1.0, GPU-preprocessing bind
groups / allocate_uniforms / unpack_bins for the extra phase +1.4, bloom encoding +0.5. Open: cut the per-phase GPU
preprocessing overhead (the scene has only 760 draws), bloom's mip chain, then re-measure.

4K floor (2026-10-17, agent-armyperf). Tools: `--profile` now also records `meshes_changed` / `mesh_vertices_changed`
(mesh assets added or modified per frame: each is re-uploaded); a chrome trace of a bench (`cargo build --release
--features bevy/trace_chrome` into its own target dir, `--bench 8`: ~2 GB per 10 s, summed per span name by a
streaming script) gives the render world's systems. Found: `allocate_and_free_meshes` 1.4 ms median every frame,
from the three particle pool meshes rebuilt each frame. Bevy re-uploads a changed mesh by writing each attribute of
each vertex separately into mapped GPU memory (Mesh::write_packed_vertex_buffer_data: strided 8..16-byte writes,
slow on write-combined memory): the same meshes with only a position attribute took 0.10 ms. Fixed (fxvm/particles.rs,
particle.wesl): a pool's mesh only numbers its corners and keeps a power-of-two size; its material reads the corners
from a `ShaderBuffer` (storage buffer) written whole each frame, which Bevy updates in place while its size holds.
Streamer meshes left empty are no longer rewritten each frame. Trace (bench, 4K Remaster): allocate_and_free_meshes
1.43 -> 0.006 ms, Render schedule median 12.6 -> 9.2 ms in the experiment (the trace's own overhead included).
Measured (`--bench 20 --size 3840x2160 --background --preset remaster --profile`, before / after alternated, 2-3
other agents' games running, no builds): before 116.9 / 115.8 / 116.7 fps (1 % low 81 / 92 / 99), render CPU 5.9 /
6.4 / 5.8 ms; after 116.4 / 117.7 / 117.3 fps (1 % low 91 / 98 / 98), render CPU 5.7 / 5.1 / 5.1 ms. Both builds sit
at the driver's ~119 fps cap on this machine when it is not saturated (ms median 8.41 = 118.9 Hz); the uncapped
estimate (the slowest of main CPU, render CPU, GPU) is ~5.3 ms after, ~6.2 before. The 96.9-98.9 fps of the corpses
journal were taken with the CPU saturated by other agents' runs and builds (here, with 5 other games and builds
running: 62-84 fps for both builds). Still open: prepare_preprocess_bind_groups (~1.2 ms: bind groups per view and
phase, rebuilt each frame by Bevy), write_binned_instance_buffers for the opaque and prepass phases (~0.8 ms each),
queue submit (~1.5 ms); the main world's playtest state systems (`level::log_objects` and the other `log_*` systems)
build their JSON every frame in normal play too, because `PlayState` always exists (~0.3 ms; main is not the bottleneck).

### Ported from the old remaster: geometry, surface detail, shadow tone (2026-10-19, agent-port-geom)

Module `engine/src/graphics/geometry/` (GeometryPlugin; `GeometrySettings`, flattened into GraphicsSettings so
options.json names each setting directly; `material_values` feeds `GameMaterialParams::geometry`). Sources: the old
D3D9 hook's registrations (hook/enhance/render.cpp, shadows.cpp: RemasterRegister's original / remaster defaults) and
its Vulkan renderer (src/rhi/vulkan). The Graphics page has a "Detail and Shadows" row opening a page of its own
(Page::GfxDetail: three checkboxes, five steppers `Opt::Number(key)` whose ranges are geometry::STEPPERS, the old
sliders' ranges), edited on the Graphics page's copy (Apply / Cancel there).

- **Tessellation** (vk_tessellation.cpp, vk_device.cpp Tess / TessModules): Phong tessellation (Boubekeur and Alexa
  2008), shape 0.75, the crease rule of 2026-10-12 (an edge curves fully within 25 degrees between its corner
  normals, stays straight beyond 50). wgpu has no tessellation stage, so the same maths runs on the CPU once per mesh
  when a skinned part appears (the old's set: skinned draws only), at a fixed level (3 segments per edge, 9x the
  triangles) instead of the old's screen-space level, in the bind pose; skin weights are blended over the corners
  (four strongest, renormalised). The player's feedback 1-3 (cracks at split vertices, UV seams) is answered by the
  journal's Phong-AEN design (docs/journal/2026-10-12.md): corners are welded by position and skin, the curving normal
  is the welded average, each edge's crease is decided from welded data both triangles share (and is 0 where the two
  sides' own normals differ: a hard edge, or a boundary / non-manifold edge), edge points are evaluated in one
  canonical order, so both sides of a seam get the same bits (unit test `crack_free_across_uv_seams`: a mesh split at
  every edge gives exactly the welded mesh's points; `hard_edges_stay_sharp`: a 24-corner cube stays a cube).
  Displacement capped at a quarter of the edge. The Remaster turns it on (the old shipped it off because of those
  cracks). On lev_1_m1's low-poly characters the 25 / 50 rule curves little (most neighbouring normals are over 50
  degrees apart): heads and shoulders round slightly (work/shots/port-geom/cmp_tessellation.png); both angles are
  settings (0 / 0 = every edge curves).
- **Terrain detail** (vk_shader_translate.cpp, terrain pixel shader 98a72838035bad82): the terrain's tangent-space
  normal map sampled again at terrain_detail x its tiling, its xy slope added at terrain_detail_strength (0.6) and
  renormalised; mip filtering fades it with distance (cmp_terrain_detail.png: fine relief on the snow).
- **LOD** (graphics/geometry/lod.rs): Object_SelectLod 0x00452f50 as the original: the depth along the camera's view,
  m = tan(fov_h / 2) * d at model quality 4 ((A, B) = (1, 0) from the table 0x005b0c78), LODs walked skipping
  collision_lod_index / shadow_lod_index, cutoffs `lodcutoff_<i>` (defaults 0, -1 for the last and the skipped ones:
  never). lod_max sets d = 0, lod_scale divides d (the old's two switches). The Remaster keeps lod_max on (the human's
  2026-10-13 decision; the old's default was off for its CPU cost); with it on, nothing extra is loaded or spawned. In
  the Original, `LodSource` (level.rs: placed instances and spawned grunts) makes the plugin spawn the coarser LODs'
  parts hidden (skinned ones on the object's rig) and switch Visibility; objects with parts on single bones keep their
  finest. after_pub: 188 objects switch, 81 draw a coarser LOD (cmp_lod.png: the distant pine's sparser branches).
  Guesses: the camera's fov +0xf4 is horizontal; a LOD the export lacks falls back to the next finer one.
- **Alpha coverage** (vk_fixed_function.cpp FinalBody): with MSAA, alpha textures draw with alpha-to-coverage
  instead of blending. Under TAA the old's per-frame noise coverage was ported and dropped: on the engine's blended
  semi-transparent foliage (icicles, twigs) Bevy's TAA clamps the noise instead of averaging it, a visible screen door
  (cmp_alpha_coverage_zoom.png), and the old measured no gain under its own TAA (docs/subsystems/remaster-aa.md). So
  in the Remaster (TAA) the setting is on but inert; it acts when the player picks MSAA.
- **Texture sharpness with AA** (vk_pipeline.cpp): Bevy's TAA already adds MipBias(-1) to the camera, but the game
  material sampled without it; it now samples every map with `view.mip_bias`, and the camera gets aa_mip_bias.
- **Shadow darkness** (vk_shadows.cpp apply pass): full shadow multiplies by clamp((1 - s) * (0.82, 0.92, 1.18)), the
  penumbra blends toward it, under the fog; 0 keeps the level's shadow colour blend (0, 0.125, 0.25) at 0.3.
- **cull_radius not ported** (shadows.cpp "Shadow caster range"): the old shadow maps replayed the main scene's draws,
  so casters its frustum culled lost their shadows; Bevy culls the sun's casters per cascade (no near plane), never
  by the camera, so off-screen casters always cast (the original's projected shadows also cover every shadow-list
  object, shadows.md). A setting would do nothing.

Tests: `engine/playtests/geom_remaster.json` (Remaster: 228 tessellated parts, mip bias -1, terrain detail 8, shadow
0.55, no LOD switching), `geom_original.json` (Original: nothing tessellated, no mip bias, LOD switching),
`geom_msaa.json` (run with `--arg=--preset --arg=remaster --arg=--graphics --arg=anti_aliasing=msaa`: alpha-to-
coverage on 9 materials), `geom_menu.json` (the page: toggles and steppers reach options.json), gfxopts (the new
row), unit tests `graphics::geometry`. Playtest state key `geometry` (tessellated, lod_objects, lod_coarser,
mip_bias, alpha_to_coverage, terrain_detail, shadow_strength). Screenshots: work/shots/port-geom/ (the same saved
state, 1920x1080, Remaster with each setting off vs all on, Original vs Original + lod_max; cmp_*.png side by side).
4K Remaster bench (`--bench 20 --size 3840x2160 --background`, alternated, other agents' builds and games running):
all on 86.5 / 76.6 fps, the four rendering settings off 94.1 / 76.7: within this machine's noise, to re-measure idle.

## Atmosphere, AO, motion blur and output pass, ported from the old remaster (2026-10-19, agent-port-atmos)

The old remaster's (hook/enhance + src/rhi/vulkan) fog, AO, motion blur and present-pass options, ported as close to
its shaders and defaults as Bevy allows. Each is a module in engine/src/graphics/ with a typed settings struct
flattened into `GraphicsSettings` under the old and.ini name (options.json `graphics`, `--graphics key=value`),
Remaster = the old remaster's defaults, Original = off (pixel-identical to before: 0.00 mae). Graphics page:
"Effects ..." (right of "Detail and Shadows") opens Page::GfxEffects, checkboxes on the Graphics page's copy (a
number turns on to the Remaster value).

| Setting (old [render] key) | Original | Remaster | Module, where it came from |
|---|---|---|---|
| fog_modern, fog_density, fog_height, fog_sun | off, 1, 0.002, 0.6 | on, 1, 0.002, 0.6 | height_fog.rs/.wesl (vk_fog.cpp ModernFogGlsl, vk_device.cpp ModernFogUniforms): exponential height fog per pixel in the game material in place of the level's linear fog, its colour and range; half the light through half way through the range level with the camera; reference height = the camera's on the level's first frame (`GameLights::fog_reference`); Henyey-Greenstein glow towards the sun, clamped at 1 (no bloom) |
| ao_radius (existing), ao_power, ao_falloff, ao_thickness | AO off | 24, 3, 0.5, 3 | ao_extras.rs + xegtao.wesl: XeGTAO's main pass (third_party/XeGTAO, the old remaster's AO) in place of Bevy's VBAO estimator, inside Bevy's SSAO pass (same shader asset: Bevy's depth prefilter and denoiser stay); constants substituted at runtime; thin-occluder compensation 0.7 and the thickness limit as the old |
| ao_contact, ao_snow (+ multi-bounce) | 0, 0.25 | 0.6, 0.25 | ao_extras.wesl (vk_gtao.cpp GtaoApplyFragmentGlsl): albedo multi-bounce, snow keeps ao_snow of the occlusion (`ao_exempt` names and snow-white albedo; was 0), contact AO darkens the sunlit colour under feet |
| shutter_speed | 0 | 120 | motion_blur.rs + motion_blur_*.wesl (vk_motionblur.cpp): per-object blur, exposure (1/N s)/frame time capped at 4, camera motion subtracted from Bevy's motion vectors, the shorter of world / screen motion (tracked characters stay sharp), 48 px tiles, McGuire reconstruction; after TAA, before bloom; needs the motion-vector prepass and no MSAA |
| tonemap_aces, dither | off, off | off, on | present.rs/.wesl (vk_present.cpp): last post pass (Bevy FullscreenMaterial): Narkowicz ACES on the game's (gamma) values, triangular +-1/255 noise in the sRGB encoding; either puts the 3D view in an HDR target |

Why XeGTAO: with Bevy's estimator flat ground reads ~0.83 visibility (its bitmask counts the thickness strip above
every sample), so the old power of 3 darkened all the snow (mae 31 on the snow); XeGTAO gives flat ground 1 as the old
renderer did (raw term: work/shots/port-atmos/xegtao_raw_term.png). When Bevy is upgraded, xegtao.wesl takes Bevy's
new ssao.wesl bindings and helpers (its header says how). Not ported: half-resolution AO, the separate smoothed-normal
pass, Afterlight (AO + GI, off in both old presets: it needs the scene colour before the AO and a bounce gather, a
pass of its own; deferred), streaks on particles (they write no motion vectors; the GPU snow's port streaks its own).

Tests: engine/playtests/gfx_effects_remaster.json (state key `render_fx`: hdr, dither, aces, motion_blur,
blur_exposure, prepass, fog_*, fog_reference, xegtao, ao_power / falloff / contact / snow), gfx_effects_original.json
(all off), gfx_effects_menu.json (the page reaches options.json), gfxopts / geom_menu updated; unit tests
`graphics::height_fog`, `graphics::ao_extras`, `graphics::motion_blur`. Screenshots (same saved state, 1280x720):
work/shots/port-atmos/ (remaster_before_vs_after, heightfog / ao / aces / dither off vs on, motionblur off / 1/120 /
1/30 on Roman's legs, the menu pages).
GPU (4K Remaster bench `--profile`): SSAO 0.58 -> 0.40 ms (XeGTAO is cheaper than VBAO), motion blur +0.38 ms,
output pass +0.04 ms, main opaque +0.02 ms (fog): gpu_total 2.41 -> 2.64 ms. fps was CPU-bound and swung 43..115 with
other agents' builds running: re-measure idle.

## Jumper pack (2026-10-16, agent-blockers1)

engine/src/jetpack.rs (`JetPackPlugin`, component `JetPack` on Roman): the turbo pack of 0x0047aaf0 / 0x00478200 /
0x00475d50 / 0x0047b8b0..0x0047bba0, described in the module doc. Measured on the original (oracle jetpack_l3m1 and the
probes in work/oracle-blockers1/jet_probe*): a launch is a whole kick of turbo_force x turbo_time (200 on lev_3_m1)
whether Jump is tapped or held, landing in the step after the press; the wings open by themselves when a tap launch
passes its top (Jump up and falling) and fold while Jump is held; the glide's drive is (wanted - current) x power per
second, scaled by max speed / |d| beyond it (0x00471810 without the cone, exponent 0 in the air), settling at 40
forward and -31.8 down with no stick, 90 forward with W, 15 back with S, as the original. The air drive needs no
DRIVE_GAIN (the ground's fitted stiffness): the force is applied over the frame. Remaster engine changes that came with
it: no terminal fall speed (it kept a pack's fall from arming the wings), fall / landing numbers from p_trotter, the
jump and landing breaths, placed pickups with their instance properties (on_enter). Tests: jetpack_l3m1, jetpack_l4m3
(regress), cargo test jetpack; oracle jetpack_l3m1 (suite). Open: R1's input lead (the launch is in step, the walk
still a frame early); the pack's landing drive power (landing_turbo_power: the landing power blend is not modelled for
any landing); the turbo anims' footstep notes in the air come at other frames.

## Vehicles and big units (subsystem port, 2026-10-15)

engine/src/vehicles (VehiclesPlugin): rocket towers and buds, the turret bosses (zeppelin, saucer, colossus), their
rockets and shells, the IC flight paths; the forge cannon is turret.rs's. docs/subsystems/vehicles.md has the
behaviour with addresses, the tests (engine/playtests/vehicles_*.json, each naming its level with "scene") and the open
questions. Unit numbers come from the property sets; the original's code constants and difficulty scales are
`VehicleTuning`. Debt (labelled in the code): the colossus walks straight to its markers (plan: nav.rs routes); a
boss takes Roman as the attacker of any hit (plan: a damage source on Destructible); dropped captains appear on the
ground (plan: IC movers for characters); the forge cannon's mounted view needs a capture of the original.
New playtest steps: `damage` [instance id, amount], `damage_template` [template, amount]. State log: `vehicles.<id>`
(bosses: phase, health, target, shots, volleys, pos.x/y/z; rocket buds; `<id>t` a tower's turret) and
`vehicles.rockets` (fired, ended, hit_roman, near_roman, player_fired, player_ended).

## Twiglets (2026-10-16)

engine/src/twiglet.rs (TwigletPlugin), docs/subsystems/twiglets.md: the twiglet_generator trees (`Generator`, made by
level.rs from the tree's properties and its reference anim's mover nodes), the swarm controller (`Swarm`: one twiglet
every 0.1..0.25 s from the active tree with the most left, at most 100 alive), the twiglet (`Twiglet`, Twiglet_Update
0x0049dcf0 states 1 / 0x32 / 0x0a / 0x46 / 0x1e / 0x1f: out of the tree along its IC mover, the bomb out, the run at
Roman, the bomb set off within 10 units, blast-proof while carrying one: fx.rs `BlastProof`), the monk's summon
(ai/monk.rs `Spell::Twiglets`, states 0xf2 / 0xf3). In the shipped data only lev_4_m1's trees are ever activated (200,
205, 207); elsewhere twiglets come from the monks. Messages: `SpawnTwiglet` (at, mover). State log `twiglets` (live,
made, killed, bombs, idle, emerging, running, generators, left); playtest step `monk_spell` ("twiglets" / "baskets":
the next roll of every monk with a target). Tests: playtests twiglets, monk_summon (regress); oracle twiglets_l4m1
(suite: PASS with B6 known). The run's speed is the original's drive on a wood body with a fitted friction
(`ground_speed`, documented in the subsystem doc): label it if the fit is ever replaced by a real contact model.

## Peasants (2026-10-16)

engine/src/peasant.rs (PeasantPlugin), docs/subsystems/peasants.md: the rescue levels (lev_2_m3, lev_3_m1, lev_3_m3,
lev_4_m1, lev_4_m2, lev_bonus_m2). `Peasant` (PeasantObject 0x005a8550: placed by level.rs, or held in a squad
goliath's cage by goliath.rs and freed when it dies), captured when Roman meets his trigger sphere (a sphere of
sphere_radius centred that high above him, 0x004574e0; Roman's reach `ROMAN_BODY_*` fitted to the original), tied as a
ragdoll on a rope chain (0x00405d10: `Rope`, `choose_leader`, a hard key-frame of the pelvis; ragdoll.rs `Build` gives
him small parts and joints with friction, so he stands until pulled), banked by a door mat (MissionObject_OnTrigger
0x0044d600: the mat's sender comes with mission.rs `VolumeSent`; a one-shot mat banks one and dies), lost through his
kill slot (0x004062a0: failure state 8, `Mission::fail_state`). `PeasantTally` holds Game+0x26e0..+0x26f8. Also: door
mats are drawn (a broadcast with "visible"), bullets and blasts pass peasants (none of the damage lists), the compass
prefers World+0xefc (`CompassList`: peasants not yet tied), the HUD's rescue counters (hud.rs `PeasantCounters`; the
ui_peasant icon no longer shows on every level), a tied peasant ends a bully. State log `peasants` (carried, saved,
casualties, alive, hud, list by id: captured, tied, link, held, x / y / z; a cage peasant is `c<1000000 + squad id>`).
Tests: playtests peasants_l2m3, peasants_bonus_m2, peasants_lost_l4m2, peasants_cage_l3m1 (regress); oracle
peasant_l4m2 (suite: every peasant family same, capture frame 52 in both, bank 158 vs 160). Fitted (labelled in the
code): Roman's reach (two probes) and the joints' friction (the original's strength scaled by the weight). Found: the
original's Roman stands ~40 frames after landing from a 9-unit drop, the remaster's walks at once (open, player.rs).
Thrown villagers (2026-10-18, peasant/toss.rs): a destroyed house with toss_peasants throws out 1..3 PeasantGenAI
villagers in chairs, launched from Roman (ai/launch.rs, Character_Launch 0x00431020), screaming; on landing the chair
is let go and the villager is a ragdoll (playtest peasants_tossed). Guesses: the chair's box, the dialogue test's list
+0xf04 taken as empty.

## Companion tower task (2026-10-16)

engine/src/ai/tower.rs (TowerPlugin), docs/subsystems/companions.md 8: lev_4_m2's Jonesy on 7001 walks to marker 356,
is placed on it healed, sends 802, plants eight bombs on the Glass Tower through the plant IC anim (mover.rs `Mover`,
its root track) and loops the bomb anim; the mission's countdown then blows the doors and wins. With it: companions
waiting for a trigger start where placed, their send_trigger_on_death, hit reactions only in modes 1 / 3, stig / forge
damage multipliers (player.rs `DamageKind`). Tests: playtests tower_l4m2 / tower_l4m2_fail (regress); oracle
lev_4_m2_tower PASS (NAV1 and T1 known, below; suite), which needs a hook with [test] play_triggers
(hook/src/testoracle.cpp, 2026-10-16); to try a hook change without replacing the shared build: `pwsh tools/build.ps1
-BuildDir build/hook-NAME` and `AND_HOOK_BUILD=build/hook-NAME` (tools/instances.py).

| Id | Remaster difference | Evidence (work/oracle-tower/lev_4_m2_tower) |
| --- | --- | --- |
| NAV1 | nav.rs's A* takes the south corridor of lev_4_m2's abbey to the tower, the original's nav mesh the north one (same length, both arrive at ~3800); nav.rs allowed a step at (905, -143) the walker cannot climb (tower.rs routes round a stuck spot) | companion.jonesy.pos frames 200..3800 |
| T1 | the original's Jonesy takes no damage during the plant anim (3800..5680 at 500 in both original runs), the remaster's does and dies at ~5650 unguarded: cause not found (not the force-action release of the arrival, +0x398 / +0x39c) | companion.jonesy.health |

Found on the way: the movie input hold (movie.rs `release_input_hold`) only ran out with no movie playing, so a page's
background movie started within its two frames (the credits after lev_5_m5's end movie, when the schedule shifted)
kept the credits page from taking input; it now runs out while no full-window movie plays.

## Later levels' infantry (2026-10-17, agent-enemyai)

engine/src/ai/: max_fire.rs, grenade.rs, rocket.rs, aim.rs, fall.rs, paratroop.rs, retreat.rs, notes.rs, assault.rs,
ladder.rs, melee.rs, plus additions to jumper.rs, squad.rs, ic.rs (docs/subsystems/grunt-ai.md section 15,
infantry.md section 10, defence.md). New behaviours sit in their own modules with their own playtest logs (max_fire,
grenade, captain_rocket, fall, paratroop, retreat, jump_markers, script_ends, assault, ladders, melee); ai.rs's think
only hands over (the fire state asks the shooter limit, a clip's end the close attack, the gunner's flag 1 the lead).
A playtest step `ai_force` ("throw", "fight") forces the next roll of a behaviour so a test does not wait on luck.

Tuning items (visible or likely differences from the original, not compared by capture yet):

| Id | Difference | Plan |
| --- | --- | --- |
| EA1 | A counted shooter whose sight is lost keeps his place (the engine's fire state stays; the original re-pursues and frees it) | port the fire state's lost-sight exit, then release there |
| EA2 | The wall assault is asked by idle army grunts once a second (the original asks at each hiding-spot search); 41 scales and 17 ladder climbs in lev_1_m4's first 240 s with Roman idle: compare the rate with the original | oracle a defence level's first minutes: over-the-wall count by time |
| EA3 | The planted ladder plays mover_ladder_plant from the grunt's matrix plus the traced offset, its model in its rest pose; the carried ladder hangs at the grunt's origin (Object_Attach's offset not traced) | capture a ladder carrier and a planted ladder |
| EA4 | The parachute model is drawn in its rest pose (parachute_open / _close are not played on it) and removed on landing | play its anims on a rigged copy |
| EA5 | The close attack's "hit" damages Roman 50 each without his knock-back from the 6500 impulse, so a grunt in reach hits on every loop (8 hits in 12 s at 7 units) | the knock-back on Roman (player.rs: the weapons' side) |
| EA6 | The takedown's approach is a straight walk (the original routes, 0x0046f6b0) and gives up after 8 s (the original never does) | nav.rs route |
| EA7 | A knocked-away IC grunt flies at the traced impulse x 0.15 / 80 kg (40..80 / s): the ragdoll's mass model is the engine's | compare a destroyed tree's grunt |
| EA8 | The captain's rocket is shot down inside a 2-unit sphere round it (its collision shape is not traced) | trace the description 0x100's shape |
| EA10 | A climber still walking to his wall marker after twice the straight run plus 10 s gives it up (never offered it again); the original has no limit, but the engine's walk could not reach lev_3_m4_bd's scale 301 and held the grunt (and the army limit) for good | the walk by nav.rs routes to the marker |
| EA9 | Paratroopers' rope anims start at the node with no rotation (the vehicle's heading not applied to the mover's origin) | the node's matrix as the origin |

## Presentation and polish (2026-10-18, agent-presentation)

Fourteen deferred features, each a module or a small hook, each with a playtest (journal 2026-10-18-presentation.md):

| Feature | Where | Playtest |
| --- | --- | --- |
| Key rebinding: keys, mouse and pad buttons on the Controls page (the original's assign rule and capture), saved in the options; the Gameplay context is rebuilt from them and every prompt follows them | bindings.rs, input.rs, gamemenu.rs | controls_rebind |
| Legal splash (b_legal 4 s, exported from lev_shell), loading screen (ad image, bar, text fallback), exit screen; the legal and exit screens hold the music | splash.rs, gameaudio.rs `Music::hold` | legal_splash, loading_screen, exit_screen |
| Save thumbnails (the game camera's view at 128 x 76 beside each save) | thumbnail.rs, saveslots.rs | save_thumbnail |
| HUD meters (peasant counters, wall counter and flag base now spawned; the ticker bombs icon; the timed patience text; the bunch-of-fives hint) | hud.rs, mission.rs | hud_meters, hud_meters_l2m3, hud_meters_l1m4 |
| Run / limp anims at the rate of the speed (anim_run_* / anim_limp_*) | skin.rs `RunRates`, charanim.rs | run_anim_speed |
| Arrows fly with their model and stay stuck 2.5 s in what they hit | arrows.rs, player.rs | arrow_models |
| A blast's kill launches the grunt; his landing starts fx_bodyfall (ground variant); the corpse fade covers the whole hierarchy | bodyfall.rs, fx.rs, ai.rs, camfade.rs | bodyfall_fx, character_fade |
| The wall assault's lines, captains' own lists, the shrub patrol's taunt, the goliath's death line | voices.rs, audio.rs, ai.rs, goliath.rs | ai_voice_lines, ticker_goliath |
| Q's cape (verlet chains on his shoulders) | cloth.rs | cloth_cape |
| camera_startup_path_anim (no shipped level sets it) | campath.rs | - |

Traps found on the way:
- A level restart now waits one frame after it is asked for (levelreset.rs `RestartLevel`), so the loading screen is drawn before the rebuild's long frame. Entities spawned at run time that must outlive a restart carry `levelreset::KeepOnRestart` (the rebuilt Gameplay input context).
- A playtest whose game quits by itself (the exit screen) still prints its PLAYTEST RESULT line (playtest.rs `report_on_exit`).
- The icon atlas (ui/icons.png) holds every key the Controls page can bind: re-run tools/remaster_export_icons.py after adding keys to bindings.rs `KEYS`.

Debt / tuning items (guesses labelled in the code):

| Id | Item | Plan |
| --- | --- | --- |
| PR1 | The save thumbnail's camera is a second Camera3d for four frames: systems that take `Query<.., With<Camera3d>>::single()` skip those frames (saves are made from the pause menu, where that does not show) | a marker on the game camera and `With<GameCamera>` in those queries |
| PR2 | The loading screen's y (bar 0.175..0.225, text 0.139) is taken from the bottom as the texts' (no capture) | capture the original's loading screen |
| PR3 | The cape's damping / shape pull / gravity, and the launch's mass (impact force x 0.15 / 50) are guesses by eye | compare with the original's Q and a sticky-bomb kill |
| PR4 | The shrub patrols' voice sets alternate by load order (the original's test is not traced) | trace ShrubPatrol's setup flags |
| PR5 | The red flag fx on the defence HUD's flag base (fx_red_flag on the UI object's ref_flag, 0x00465d50) is not drawn | trace how an fx on a UI object is placed |

## Running tests without disturbing the desktop

- `--background`: the window opens off-screen, unfocused, behind everything, not in the taskbar; screenshots still
  work. Scripted runs (`--play`) never grab the cursor and ignore the real mouse (it used to turn the camera and
  made runs differ).
- Anchor scripts to the game, not the clock: `{"until": "message"}` waits for the briefing (loading time varies;
  fixed waits drifted into the intro). With both, repeated runs agree within ~0.3 mae.
- **A scripted run must not depend on how fast the machine is** (2026-10-17, agent-flaky). Scripted runs step game
  time by a fixed 1/60 s a frame (main.rs ORACLE_FRAME_STEP; `Time<Real>` is stepped too), so the same script gives
  the same game on an idle or a loaded machine, and two runs give identical sound logs (`--env AND_SOUND_LOG=FILE`;
  check with two runs and a diff). What broke that, and must not come back:
  - the **audio device's clock**: a voice ended when its sink drained (wall-clock time). On a loaded machine (game
    slower than real time) voice lines ended early in game time and a follow-on line started that should not have
    (audio_voice), music segments vanished (audio_stopall), and the AI's "still saying / screaming" checks (taunts,
    death lines) changed which random numbers were drawn (gunner1, infantry_l4m2). A one-shot now ends when its
    length (the bank's duration over its playback rate) has played in game time (audio.rs `Voice::length`); a file
    that loads late starts at the voice's age (its PlaybackSettings `start_position`). Never read
    `AudioSink::empty` / `position` for game logic, and never `try_seek` a voice's sink: it blocks until the audio
    thread answers and froze audio_music for good (the movie player's seek is the one existing use).
  - **asset load timing**: a voice used to start ageing only once its file had loaded (a load-dependent number of
    frames). Any gameplay gate on an asset's load state has the same problem.
  - **`std::collections::HashMap` order**: randomly seeded per process. FxVm's objects were updated in hash order,
    each drawing from the shared random stream and playing sounds, so debris differed from run to run (gunner1's
    first divergence: 14.7 s). Iterate ordered collections (BTreeMap, sorted keys, `bevy::platform` maps) wherever
    the order reaches game state, random draws or messages.
  - **wall-clock waits**: the level settle after a restart waited for quick wall-clock frames; stepped runs now
    count frames only (levelreset.rs `settle`).
  - **wall-clock limits in correctness tests**: frame / main-world times are machine load, not game behaviour. They
    go in `"expect_perf"` steps, checked only by `python tools/regress.py --perf` (AND_PERF=1) on an idle machine
    and logged `EXPECT SKIP (perf)` otherwise. regress.py gives each script 1200 s of wall time (a hang guard:
    defence_l5m5's 66 game seconds take ~300 s under load).
  Known, bounded: a movie's clock waits up to 0.5 s game time for its soundtrack's sink, and the logged
  `movie.audio_time` / `drift` / `rate` follow the device; movie scripts check them with ranges that cover that.
  Do not dismiss a failure as "load": a run that differs on a loaded machine has a wall-clock dependency to find.

## Audio check

Playtest logs carry `audio_check`: every 0.1 s each playing sound's position on the output device is sampled;
`advancing` / `stalled` / `silent` sinks and the distinct names heard (positions advanced) are counted. A run of
gaim.json (10 s, speech and fighting): 29 distinct sounds heard advancing, voice lines among them, 0 stalled.
`--background` runs are muted (master 0; playback and positions still run), so tests do not play over the desk.

## Tips and traps

- **Axes.** Game space is Z up; engine is Y up: `game_to_engine()` maps (x, y, z) to (x, z, -y). Anything the game
  authored in its own frame (fx scripts, attached objects, projectiles' trails, impact fx) needs that rotation in
  its transform, or it comes out sideways or vertical (the vertical tracers were this).
- **Exported rotations** in anims are conjugated (big-anim.md); the ui_clock hand turns the other way for that reason.
- **Model index buffers** in work/remaster/*/models/*.bin are u32.
- **Bevy 0.20**: shaders are WESL; meshes built with `RenderAssetUsages::RENDER_WORLD` cannot be read on the CPU
  later (the skinning check needs MAIN_WORLD: skinned meshes keep it); `Mesh::attribute` panics after extraction,
  use `try_attribute_option`.
- **Windows**: the running exe is locked, so build after a run ends; heredocs with quotes break in Git Bash: write
  edit scripts to the scratchpad with the Write tool and run them with python.
- **Hook DLL** (`pwsh tools/build.ps1`): include `game/game_object.h` for `g_game`.
- **Data, not invention**: every value comes from a property set, the UI / text banks or the recovered code, with
  the address in a comment. When the exe never reads a data key (grunt-ai.md lists them), the engine must not either.
- The original's 3D view: letterbox at y 11.25 %, height 77.5 % (0x005bcf24 / 0x005bcf28), aspect of that strip,
  vertical FOV camera +0xf4 (0.654 rad on lev_1_m1's follow camera), view matrix Game+0x704 (rows: right, up, back).
- Treat the human's in-game observations as the highest-priority bug reports (docs/player-feedback.md).
- **cargo may not be on PATH** in the Bash tool: `export PATH="$HOME/.cargo/bin:$PATH"` first, and never filter the
  build output with only `grep error` (a "command not found" is silent then and the old exe runs). Check for
  `Finished`. Shaders (`*.wesl`) are embedded, so a shader edit needs a rebuild too.
- **Comparing captures**: the original's shot is 640x480 with the 3D view in rows 54..425; crop it
  (`crop((0, 54, 640, 426))`), never resize it to 372 rows.
- **Follow camera collision** (player.rs follow_camera + camfade.rs, from Camera_Update 0x0040e56c): five rays from
  the look point (to the eye and the eye +-5 along the view's right / up) pass through objects, which fade to
  camera_falloff 0.3 at camera_fade_out_rate 3/s and back at camera_fade_in_rate 3/s (p_globalsettings); only the
  landscape (`Physics::landscape`) shortens the arm. The old engine version stopped on any collider with a 3-unit
  minimum and put the camera inside buildings, ahead of Roman (level1b win shot).
- **No tonemapping, near plane 1** (main.rs camera): Bevy's default tonemapping darkened the pale colours (sky at
  the horizon 216 -> 175); the original's near plane of 1 also clips the radius-1 fxo_snow dome (with Bevy's 0.1 it
  drew an extra layer of dense fine flakes).

## Status (2026-10-13)

lev_1_m1 plays start to finish (bot runs 13, 14, 16 won): menus, pub, ticker bombs, subtitles, machine gun, Topsy
Turvy (uncommitted until verified with grunts in range), skinning check. Rendering comparison baseline recorded
above; nothing in rendering is verified yet.
