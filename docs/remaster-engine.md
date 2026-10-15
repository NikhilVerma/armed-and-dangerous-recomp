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
  virtual clock steps 1/60 s per presented frame, so speed does not matter. compare.launch refuses a hook DLL without
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
  still walking; the remaster gave him one unpaused frame without the stick. The mission's systems now run before the
  player's update and `control` does not run while the game is paused. Intro trace (trace_from -900): roman.pos same.
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
inclusive range; else equality) and prints EXPECT PASS / FAIL; any failure exits 1. `{"blast": D}` sets off a
badguy explosion of area damage D at Roman. States: `after_pub` (state_after_pub.json, 23 s to make). Example:
topsyhit2.json (24 s; the same check replayed from the start took about 4 minutes).

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

## Cheats

Options > Cheats: the 12 codes of the table 0x00589090 (read from the exe: SHOOTMOR full ammo, AMBULNCE full
health, VERYHIIQ big heads, EATGRENZ big hands, SHOESIZE big boots, NDMRBLTS infinite ammo, SHIELDME invincible,
PRAYTOME god mode, MISSIONS / AMATINEE unlock levels / movies, WAAAAAAH upside-down screen, DAJIGSUP list), toggled
as Cheats_Check 0x004107c0 does, messages from the text bank (cheats.rs). The big-part scale (x2) is a guess.
Trap: systems that change joints must run after skin::AnimateSet (PostUpdate), or the animation overwrites them.

## Ragdolls

`engine/src/ragdoll.rs`: a dying character with a skeleton becomes a Jolt ragdoll (oxijolt `RagdollSettings`), with
11 capsule parts (center, chest, head, shoulders, arms, thighs, shins) joined by swing-twist joints. Each part is
built from the bone's world pose at death and launched with the hit's push. The `drive` system (PostUpdate, after
`skin::AnimateSet`, before transform propagation) writes the bodies back into the bones. Characters without those
bones keep the single-box fallback in `ai.rs` die(). The original's GoRagdoll parameters (Havok) are not recovered,
so the sizes and limits are guesses judged by eye. Test: `work/remaster/play/death2.json` (`{"kill":1000}` gives a
heavy hit); the b1..b4 shots show the grunt down, splayed and settled, with no jitter.

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

The ai port queue (`python tools/port_queue.py ai`) is empty: each function is cited in engine/src or listed with a
reason in docs/port/ai.csv (`--explained` prints them). What the engine replaces rather than ports:
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
uses classes the mission export does not know yet (twiglet_generator, jump_marker, object_ic, zeppelin, squad,
anim_player, base_turret, peasant). player.json's loadout is the mission object's add / remove flags only: in the
campaign the weapons carried over from earlier levels add to it (not modelled).

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
| ambient_occlusion, ao_quality / radius / strength, ao_exempt | off | on, High, 4 units, 1, ["ground", "snow"] | Bevy's SSAO (an XeGTAO implementation) read in gamematerial.wesl on the ambient + fill light only (not the sun, not emissive); surfaces whose base / second / normal map name contains an exempt part (the snow landscape) and blended surfaces get none |
| anti_aliasing, sharpening | MSAA 4x, 0 | TAA, 0.35 | Bevy's TAA (with the temporal shadow filter) or SMAA High, then contrast-adaptive sharpening; Off / MSAA / SMAA / TAA on the Graphics page |
| anisotropy, anisotropic_fx | 16, off | 16, on | world samplers 16x in both (the engine's look since 2026-10-13); the Remaster adds it to decals and effects (the default sampler) |
| shadow_map_size, shadow_cascades, shadow_distance | 2048, 4, 0 (the level's: half its extent) | 4096, 4, 1.0 x the world's far clip | cascades fitted to what is drawn (options.rs world_far), 30 % overlap: every shadow in view is in a cascade |
| soft_shadows, shadow_softness | off | on, 6 | Bevy's PCSS (feature experimental_pbr_pcss) |
| soft_particles | 0 | 6 units | particle.wesl fades a particle over that depth where it meets the depth prepass: no hard line where a quad cuts the ground |
| far_fade | off | on | the far clip (the original's far plane) dissolves over its last 15 % (moving dither, smoothed by TAA) instead of a hard cut; draw distance stays the original's maximum, LOD always the finest |
| bloom | 0 | 0.08 | HDR target; only what is brighter than white blooms (threshold 1: stacked additive fire and flashes; the game's surfaces are clamped to white), added on top. Tonemapping stays off (the game's colours) |

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

## Running tests without disturbing the desktop

- `--background`: the window opens off-screen, unfocused, behind everything, not in the taskbar; screenshots still
  work. Scripted runs (`--play`) never grab the cursor and ignore the real mouse (it used to turn the camera and
  made runs differ).
- Anchor scripts to the game, not the clock: `{"until": "message"}` waits for the briefing (loading time varies;
  fixed waits drifted into the intro). With both, repeated runs agree within ~0.3 mae.

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
