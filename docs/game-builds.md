# Game builds

The remaster's assets come from your own copy of the game, and copies differ. This page lists the builds we know, how
to set up each one, and how to add a new one.

Most of the game's data lives in its level banks, which the exporters read by name. A few tables live inside
`game.exe`, and the exporters read those from fixed addresses. A build linked differently keeps these tables at other
addresses, so each build needs its own address list.

## Known builds

`tools/setup_assets.py` prints a fingerprint of your `game.exe` (its SHA-256 hash) and the build it recognises.

| Build | `game.exe` fingerprint | Status |
| --- | --- | --- |
| Steam, app 6090, build 251748 | `960df24c70d788a3...` | Tested: exported and played on Windows |
| GOG, installer 2.0.0.3 | `d59366423d97583d...` | Exports on macOS with its address list. See the [GOG status](#gog-2003) below |

## GOG 2.0.0.3

GOG (a store that sells games without copy protection) ships the game as a Windows installer: `setup_armed_and_dangerous_2.0.0.3.exe` and a `-1.bin` file beside it. On
a Mac or Linux, `innoextract` unpacks it without running any Windows code. The game comes out in a folder named
`game`, and the tools expect `GameData`, so rename it:

```sh
brew install innoextract
innoextract --gog -d "$HOME/Games/Armed and Dangerous" /path/to/setup_armed_and_dangerous_2.0.0.3.exe
mv "$HOME/Games/Armed and Dangerous/game" "$HOME/Games/Armed and Dangerous/GameData"
.venv/bin/python tools/setup_assets.py --game "$HOME/Games/Armed and Dangerous"
```

An install made by running the installer on Windows has not been tried. If its folder layout differs, make the same
change: the folder holding `game.exe` must be named `GameData`.

How it differs from the Steam build:

- Every section of `game.exe` is named `.drunk`, so the exporters can't find sections by name. The audio exporter
  falls back to their order.
- The data tables moved, and not by one shared amount (table below).
- The code moved too, so function addresses in `symbols/functions.csv` and the recovered C++ don't apply to this file.
  The export doesn't need them. The sound export's notes leave out which function uses each sound.
- The oracle tools, which drive the original game, are written for the Steam build and have not been tried on it.

| Table | Steam address | GOG address | How the GOG address was checked |
| --- | --- | --- | --- |
| Difficulty factors | `0x00589188` | `0x00589190` | The columns match docs/subsystems/player.md and win-sequence.md: health 2, 1, 1, 0.5; money 0.5, 1, 1.2, 1.5 |
| Subtitle speakers | `0x005cadd8` | `0x005c9dd8` | The names read Roman, Rome, Q, Rexus, Jones, Jonesy, Lily, Stig |
| Player cross-fade times | `0x005cb3a8` | `0x005ca390` | It ends where the mode table starts, as in the Steam build |
| Player animation modes | `0x005cb650` | `0x005ca638` | It starts at `trotter_aim`, and the rows follow docs/subsystems/player.md section 8 |
| Player rate blocks | `0x005fea3c`..`0x005feb54` | `0x005dca1c`..`0x005dcb34` | Mode 9, run forward, points to the block for `run_fwd` |

## Adding another build

The goal is an address list that has been checked against the table contents. A wrong address may not cause an
error: the exporters can read zeros or unrelated data and still report `ok`.

1. Run `tools/setup_assets.py --game <folder>` and note the fingerprint. If a step fails, the steps after it on that level
   are skipped, so read the whole log.
2. Find each table in the list above in your `game.exe`, using what it holds:
   - The speaker table's first row holds a pointer to the string `Roman`.
   - The animation mode table has rows of `0x28` bytes. Its first named row points to `trotter_aim`, and the next to
     `trotter_fire_sg`.
   - The cross-fade table holds 13 x 13 floats and ends just before the mode table.
   - The difficulty table holds 20 floats, with the columns shown above.
   - The eight rate blocks sit `0x28` bytes apart, and the rows named `trotter_run_aim` point to them.
3. Add the fingerprint and the addresses to `_TABLE_ADDRESSES` in `tools/gamefiles.py`, and the build's name to
   `KNOWN_EXES` in `tools/setup_assets.py`.
4. Add the build to this page, with how each address was checked.
5. Check the results in `work/remaster/lev_1_m1`: `player_anims.json` should have 91 `modes` and 196 `links`, with
   `run_fwd` on mode 9, and `ui/layout.json` should list the speakers. Then run the playtests.

Exports from different builds shouldn't be assumed to match. Anything else the two builds differ in would show up in
play, so record what you find on this page.
