# Remaster engine

A shared runtime for remastering old games, first used for Armed and Dangerous. Rust, Bevy 0.20, Jolt Physics 5.6
(oxijolt, built from source with CMake), bevy_egui (debug panel only). Decisions: docs/decision.md 2026-10-13 (the
engine) and 2026-10-13 "one verified piece at a time" (how it is ported). Workflow, verification loop, tips and the
porting order: **docs/remaster-engine.md** (read it before working here).

## Run (Armed and Dangerous)

Export the game's assets from your own copy once (README "Getting started"; everything goes to work/remaster/,
never committed):

```bash
.venv/Scripts/python.exe tools/setup_assets.py --game "C:/Program Files (x86)/Steam/steamapps/common/Armed and Dangerous"
```

One level only, or one exporter step: `tools/export_level.py LEVEL [--steps scene,anims,...]` (its docstring lists
the steps and what each writes).

Build and play (cargo is in %USERPROFILE%/.cargo/bin; add it to PATH in Git Bash). After a build, `engine/play.bat`
(double-click) starts the game on the title menu; `engine/play.sh` on macOS and Linux (**docs/macos.md**: building
and running on a Mac, where the game keeps its files there, what differs).

```bash
cd engine && cargo build --release && ./target/release/and_remaster.exe --size 1600x900
```

After a build, once per export: `python tools/bake_env_maps.py --exe target` bakes each level's environment cube
map (the reflections on ice, water and metal; graphics/env_map.rs). Without it those surfaces reflect the fog colour.
Platform differences (data folders, the system font, the local time zone, how the pointer is held in play) live in
one module, `src/platform/`; GPUs without BC texture support get their DDS textures decoded on load
(`src/graphics/bc_decode.rs`).

Options: `--size WxH`, `--vsync`, `--no-ai`, `--scene DIR`, `--skip-title` (straight into the level),
`--play SCRIPT.json` (playtest bot, see docs/remaster-engine.md), `--camera DUMP.json` (render from a camera dumped
from the original), `--no-hud`, `--snap FILE` (screenshot after 4 s, then exit), `--bench SECONDS --out FILE`,
`--fx-test DIR`, `--bake-env-map` (with `--scene` and `--preset`: render the level's environment cube map to
`<scene>/env_map_<preset>.png`, then quit).

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

The table above is the early core; the engine has since grown many more modules (ai/, weapons/, vehicles/, display/,
graphics/, devtools/, ...): `python tools/engine_map.py fn NAME` finds a system.

## Not ported yet

What the original does that the engine does not yet: docs/port/features.csv (the deferred features, ranked by
`python tools/port_queue.py --deferred`) and the "Still missing" list in docs/play-guide.md.
