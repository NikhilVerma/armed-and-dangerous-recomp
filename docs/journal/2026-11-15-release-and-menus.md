# 2026-11-15: release packaging, the playtest bot on all levels, remastered menus

## What was done

**Release packaging** (docs/decision.md 2026-11-15). The goal is a playable game on the internet: Windows x64 and
macOS Apple silicon, unsigned, on GitHub Releases, with the playtest bot as the release bar.

- tools/package_release.py builds one zip per system: the engine, the asset exporters and a Python to run them. No
  game data goes in. The player runs Setup on their own copy (tools/release_setup.py), then Play.
- tools/test_package_release.py checks the tool list is complete (every module a packaged tool imports is packaged).
- .github/workflows/release.yml builds both systems. A tag `v*` opens a draft release; a manual run only builds zips.
  It has not run yet: that needs a push, which waits for the maintainer.
- The macOS package was built and its Setup ran end to end for lev_1_m1 from a real game copy (textures, movies,
  texture pack; 3.9 GB under work/). It took 80 minutes of wall time only because it waited while playtests ran.

**The bot on all levels** (`tools/regress.py --long`): 22 of 25 scripts pass. Three failed in that run:
complete_lev_5_m4, complete_lev_4_m1, complete_lev_3_m2. See "Open questions" for each.
All three pass now, each re-run alone; the four other scripts with bomb or shoot orders (2_m1, 1_m3, 2_m3, 2_m2) were
re-run after the bot change and pass. The long suite as a whole did not run again.

**Remastered menus** (player feedback U1 to U4, docs/subsystems/menus.md section 5):

- One Settings screen with eight tabs replaces the original's nested option pages: Game, Controls, Audio, Display,
  Graphics, Effects, Engine, Flags. Rows are data (gamemenu/settings.rs `TABS`), each with a one-line description.
- Shared parts in gamemenu/list.rs: tab bar, scrolling list with a select bar, scroll bar and mouse wheel, description
  line, framed yes/no dialog. Same font, colours, pulse and sounds as the original.
- Level Select on the main menu and the pause menu: every exported level, campaign levels first, then bonus levels.
- The selected row no longer pulses (feedback U5): it is white and 5 % larger, steady. Lists show a bordered select
  bar. Set checkboxes on the Settings screen are green.
- All of it sits behind `modern_menus` (Remaster on, Original off). The Original preset shows the 2003 menus.

## What was tested

- New playtests: modern_menus.json (29 pass), level_select.json (20 pass).
- Menu playtests on the new build: menus, flow, display_page, limits_menu, launch_newgame, launch_restart,
  restart_parity, gpu_snow pass.
- Unit tests: gamemenu (5 pass).
- The full fast suite did not run (maintainer's standing choice: smoke tests only).

## What was learned

- The game does not carry weapons or ammo between levels. Each level sets its own loadout; only money, tokens and
  difficulty carry. So a level started from Level Select needs no "what Roman would have by now" step.
- The completion scripts are scripted (teleports, cheats), not an autonomous player. They prove a level can be
  finished and its objectives count; they do not prove the route is fair.
- Playtests fail under heavy machine load (load average 26 during the long run). launch_newgame failed then and
  passed alone. Re-run a failure alone before reading it.
- A page without a list must hide the list bars itself, or the last list's select bar shows behind a dialog.
- The pause menu has one more row with the new menus. Scripts that count Downs past Options need care.

## Open questions

- gfxopts, geom_menu and launch_options fail on the build from before this session too (window.width 1920 against a
  640..1600 range; `window.graphics.*` keys missing from the state log; "Remaster (custom)" against "Remaster").
  Not caused by the menus. Not yet looked into.
- complete_lev_5_m4: the script was stale against the pub's carry rule (a third Select picks up the rifle, the first
  Back puts it down). Script fixed with a second Back.
- complete_lev_4_m1: a script fault, not a game fault. The ticker site 367 is locked to a goliath's bone
  (`lock_parent_tag`), so it moves with him. `land_by [367, 12]` put Roman at the edge of its reach and the goliath
  walked it out of reach before the 2 s wind-up ended ("ticker site 367 armed" never logged). Now `land_by [367, 6]`:
  19 pass.
- complete_lev_3_m2: a bot fault. A house has damage stages (husk `_da`, `_db`, `_dc`, each destructible again). The
  first blast sends the objective's trigger, but the bot's bomb order kept the damaged shell as its target and threw
  two more bombs; their blasts wrecked the civilian house beside it (1699 x6 -> 5001 fails the mission). bot.rs: a
  bomb or shoot order ends when its object is a `Husk`.
- The Mac journal notes 18 fast-suite scripts fail on Mac (guess: GOG against Steam data). Untouched.

## Next

- The maintainer plays the menu build and confirms U1 to U3.
- Push, then run the release workflow by hand to get the first Windows zip. Ask the maintainer first.
- Ideas held back: a difficulty choice on Level Select, key bindings as a tab, scrolling lists for Replay Mission and
  Play Cutscene.
