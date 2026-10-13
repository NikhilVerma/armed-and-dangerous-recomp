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
direction, ids 0x21..0x24 in fn_00428980; the engine plays one, so guns point up), dead grunts sometimes floating
(the ragdoll stand-in), treasure tokens (pickup_treasure1: "You have found a token! ... 1 out of 21"), speech
subtitles (the original shows them in the top message feed as "Speaker: line" in the speaker's colour; the engine
puts them at y 0.15 without the name: wrong, fix), the grunts' shot cadence (fire anim 0.47 s, engine 0.6 s).

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

Earlier list of open differences at 580 (several fixed since: arch scale, token, aim pose, camera): the ground's shading (engine brighter cyan, stronger normal-map relief;
original softer blue-grey), falling snow density, the far horizon band (engine a flat pale strip), the ruined arch
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
`shot_after` seconds, `fight` true / false, debug `hurt` and `spawn`, `quit`); `"title": true` starts on the title
menu. It logs the game state every second to `<script>.log.jsonl` (position, health, mission events, prompts,
subtitles, sounds, skinning check) and saves `<script>.<shot>.png`. The full-level script is
work/remaster/play/level1b.json (pub, both gates, win). Run it in the background and stop on Won / Failed:

```bash
cd engine && ./target/release/and_remaster.exe --size 1600x900 --play ../work/remaster/play/run.json > ../work/remaster/play/run.stdout.txt 2>&1
```

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
  at the left of frame 580); the engine's is slightly darker and differently shaped (open).
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

## Save / load

Pause menu Save Game writes saves/quicksave.json next to the exe (savegame.rs); Quick Load, Resume Game and Restart
from last save start a fresh process with `--load FILE`. Restored: Roman (position, facing, health, weapons, ammo,
specials), the mission (one-shot triggers, bomb sites, barracks, pub, compass, objectives, voice lines), dead placed
grunts, destroyed objects (husks, no explosion). Not the original's format; not restored: live grunts a barracks had
released, companions' positions, the camera pitch. Verified: saved after gate 1, loaded, played on to the win.

## Cheats

Options > Cheats: the 12 codes of the table 0x00589090 (read from the exe: SHOOTMOR full ammo, AMBULNCE full
health, VERYHIIQ big heads, EATGRENZ big hands, SHOESIZE big boots, NDMRBLTS infinite ammo, SHIELDME invincible,
PRAYTOME god mode, MISSIONS / AMATINEE unlock levels / movies, WAAAAAAH upside-down screen, DAJIGSUP list), toggled
as Cheats_Check 0x004107c0 does, messages from the text bank (cheats.rs). The big-part scale (x2) is a guess.
Trap: systems that change joints must run after skin::AnimateSet (PostUpdate), or the animation overwrites them.

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

## Status (2026-10-13)

lev_1_m1 plays start to finish (bot runs 13, 14, 16 won): menus, pub, ticker bombs, subtitles, machine gun, Topsy
Turvy (uncommitted until verified with grunts in range), skinning check. Rendering comparison baseline recorded
above; nothing in rendering is verified yet.
