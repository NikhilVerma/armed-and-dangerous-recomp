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

## The verification loop

A piece is done only when its comparison with the original shows no difference that matters. Guesses stay labelled
in the source (`guess:`), with the evidence that would settle them.

### Same-camera render comparison (rendering pieces)

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
