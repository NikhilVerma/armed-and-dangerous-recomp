# Remaster engine

A shared runtime for remastering old games, first used for Armed and Dangerous. Rust, Bevy 0.20, Jolt Physics 5.6
(oxijolt, built from source with CMake), bevy_egui menus. Decision: docs/decision.md, 2026-10-13.

## Run (Armed and Dangerous, lev_1_m1)

```bash
python tools/remaster_export.py lev_1_m1
```

```bash
cd engine && cargo run --release
```

Options: `--size WxH` (physical pixels), `--vsync`, `--no-ai`, `--scene DIR`, `--bench SECONDS --out FILE`
(scripted flight, frame times and screenshots to FILE), `--fx-test DIR` (a powder-keg chain close-up, screenshots
every 0.1 s), `--snap FILE` (screenshot after 4 s). `python tools/bench_page.py` turns the benchmark JSON files in
work/remaster/bench into index.html.

Controls: right mouse look, WASD / Q E move, Shift fast, left mouse shoot, F throw a tickerbomb, G airstrike (test),
Esc pause, Tab stats.

## Layout

| Module | Game-neutral? | What |
| --- | --- | --- |
| scene.rs | yes | the exported scene format: models, DDS textures, placements, lights, settings |
| gamedata.rs | yes | property sets, templates, fx scripts as the game stores them |
| physics.rs | yes | Jolt world, static meshes, dynamic bodies, ray casts |
| fx.rs | yes | area damage, deaths, chain fuses (rules from AreaDamage_Apply / FxExplosion_Die) |
| fxvm.rs | A&D's fx format | the game's fx script interpreter and particle system (docs/subsystems/fx-vm.md) |
| bench.rs, flycam.rs, menu.rs | yes | benchmark flight, free camera, egui menus |
| level.rs | A&D | axes (Z up), units, how property sets map to engine behaviour, force_ground, husks |
| ai.rs, player.rs | A&D, stand-ins | until the recovered AI and player code are linked in |

## Not ported yet

Skinned animation (characters stand in their bind pose), the recovered AI and player, sounds, decals (fx 0x6a),
uv scrolling (fx 0x40), terrain blend layers and lightmaps (uv1), the level's scripted mission logic.
