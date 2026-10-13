# Remaster engine

A shared runtime for remastering old games, first used for Armed and Dangerous. Rust, Bevy 0.20, Jolt Physics 5.6
(oxijolt, built from source with CMake), bevy_egui (debug panel only). Decisions: docs/decision.md 2026-10-13 (the
engine) and 2026-10-13 "one verified piece at a time" (how it is ported). Workflow, verification loop, tips and the
porting order: **docs/remaster-engine.md** (read it before working here).

## Run (Armed and Dangerous, lev_1_m1)

Export the level once (all exporters write work/remaster/lev_1_m1/, never committed):

```bash
.venv/Scripts/python.exe tools/remaster_export.py lev_1_m1
```

```bash
.venv/Scripts/python.exe tools/remaster_export_anim.py lev_1_m1
```

```bash
.venv/Scripts/python.exe tools/remaster_export_audio.py lev_1_m1
```

```bash
.venv/Scripts/python.exe tools/remaster_export_ui.py lev_1_m1
```

```bash
.venv/Scripts/python.exe tools/remaster_export_mission.py lev_1_m1
```

Build and play (cargo is in %USERPROFILE%/.cargo/bin; add it to PATH in Git Bash). After a build, `engine/play.bat`
(double-click) starts the game on the title menu.

```bash
cd engine && cargo build --release && ./target/release/and_remaster.exe --size 1600x900
```

Options: `--size WxH`, `--vsync`, `--no-ai`, `--scene DIR`, `--skip-title` (straight into the level),
`--play SCRIPT.json` (playtest bot, see docs/remaster-engine.md), `--camera DUMP.json` (render from a camera dumped
from the original), `--no-hud`, `--snap FILE` (screenshot after 4 s, then exit), `--bench SECONDS --out FILE`,
`--fx-test DIR`.

Controls (the original's PC bindings where ported): WASD move, mouse look, left mouse fire, right mouse Fire Special
(sticky bomb / Topsy Turvy), Tab next special, Q next weapon, R reload, Space jump, Z or E zoom (held), F or X
Activate, Enter continue,
Esc pause menu, F3 debug stats panel.

## Layout

| Module | Game-neutral? | What |
| --- | --- | --- |
| scene.rs | yes | the exported scene format: models, DDS textures, placements, lights, settings |
| gamedata.rs | yes | property sets, templates, fx scripts as the game stores them |
| physics.rs | yes | Jolt world, static meshes, dynamic bodies, walkers (CharacterVirtual), ray casts |
| fx.rs, fxvm.rs | fx format is A&D's | area damage, deaths, chain fuses; the game's fx script interpreter and particles |
| skin.rs, charanim.rs | skin.rs yes | skeletons, anims, layers; which anim each A&D character plays; skinning check |
| level.rs | A&D | axes (Z up), units, property sets -> engine behaviour, force_ground, husks, hand weapons |
| player.rs, ai.rs, topsy.rs, pickup.rs | A&D | Roman, weapons, grunts, companions, Topsy Turvy, pickups |
| mission.rs, pubscreen.rs | A&D | the level's trigger graph, prompts, ticker sites, the pub |
| hud.rs, gamemenu.rs, options.rs | A&D data | HUD and menus from the UI bank, options |
| audio.rs, gameaudio.rs, campath.rs | A&D data | sounds, music, ambience, subtitles, camera paths |
| playtest.rs, nav.rs, camdump.rs, bench.rs | yes | the playtest bot, its A* nav, original-camera renders, benchmark |
| menu.rs, flycam.rs | yes | the F3 debug panel, free camera |

## Not ported yet (see docs/remaster-engine.md for the porting order)

Rendering fidelity: terrain layers (snow) and lightmaps, materials (environment / specular: Q's metal), the
original's fog and lighting model; Roman's and the grunts' 4-way aim blend; ragdolls; decals; several fx opcodes;
the treasure tokens; save / load; cheats; credits.
