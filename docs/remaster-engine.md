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

The bot is debug tooling, not game logic: its tactics (holding at range, backing off, keg awareness) live in
player.rs under `bot` and must never change how the game plays for a human.

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
went from 168 to 121 entries; every remaining one is replaced by the engine or not needed:

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
| UI_GetLoadingText 0x00447600, movie screens 0x00493150 / 0x004931b0 / 0x00493240 / 0x00493270 / 0x004932e0 / 0x00493320 / 0x004933b0 / 0x004933e0, timed splash 0x00493520 / 0x00493590, exit screen 0x00493770 | none | no loading screen (the level loads before the title menu), no movies yet, who pushes LegalText is not traced |
| 0x00467220, 0x00467740, 0x004677c0 | none | their UI elements are not identified (hud.md open question 7) |

## Mission, triggers, pickups and camera fx (subsystem port, 2026-10-14)

docs/subsystems/mission.md "Remaster port": every logic object (volumes, ticker sites, counters, timers, camera fx,
the pub, generators, the mission object) compares the broadcast trigger with its own fields, from the instances.
Replaced or not needed: docs/port/game.csv. Tests: mission_start, volumes, ticker, pickups, pubflow, winfail.
Trap: a playtest expect value that is a two-number array is a range, so list-like state fields are strings
(`mission_obj.compass_target` "234,252").

## Save / load

Pause menu Save Game writes saves/quicksave.json next to the exe (savegame.rs); Quick Load, Resume Game and Restart
from last save start a fresh process with `--load FILE`. Restored: Roman (position, facing, health, weapons, ammo,
specials), the mission (one-shot triggers, bomb sites, barracks, pub, compass, objectives, voice lines), dead placed
grunts, destroyed objects (husks, no explosion). Not the original's format; not restored: live grunts a barracks had
released, companions' positions, the camera pitch. Verified: saved after gate 1, loaded, played on to the win.

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
