# The remaster on macOS

Status (2026-10-24): **builds in CI, not yet played.** The engine is built and its unit tests run on Apple silicon
in GitHub Actions (`.github/workflows/ci.yml`, job "Engine macOS arm64"), and Intel Macs are covered by a cross
check from the same runner ("Engine macOS x86_64"). Nobody has played it on a Mac yet: the list of things to look at
first is under [To verify on the first run](#to-verify-on-the-first-run). Apple silicon (M1 or later) is the first
target, Intel Macs the second.

Nothing in the engine is Windows-specific any more: wgpu renders through Metal, cpal plays through CoreAudio, Jolt
builds with Apple's clang. What differs per system lives in one module, `engine/src/platform/` (data folders, the
system font, the local time zone, how the pointer is held in play).

## 1. Prerequisites

- macOS 13 or later recommended (Metal 3 GPUs: every Apple silicon Mac).
- The Xcode command line tools (clang, the macOS SDK, git): `xcode-select --install`.
- [Homebrew](https://brew.sh/), then the tools the build and the exporters use:

  ```sh
  brew install cmake python@3.12
  brew install ffmpeg        # optional: the cut-scene movies
  ```

  CMake builds Jolt Physics from its sources during the first `cargo build`.
- [rustup](https://rustup.rs/) with the stable toolchain (tested with 1.99):
  `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`.
- About 12 GB free for the exported assets plus a few GB for the build (and the size of the game folder if you
  copy it over).

## 2. Your game files on the Mac

*Armed and Dangerous* is a Windows game on Steam, so a Mac cannot install it through the Steam client. The tools
need only the game's files, read only: no installer, registry, Steam client or Windows, and a plain copy of the
install folder works exactly like the install (`tools/setup_assets.py` checks `GameData/game.exe` and the level
banks; nothing else). Use your own copy, one of these ways:

1. **Copy the install folder from a Windows PC.** On the PC, Steam > right-click the game > Manage > Browse local
   files opens it (`...\steamapps\common\Armed and Dangerous`, the folder holding `GameData`). Copy that whole folder
   to the Mac (external drive, network share, `scp`), for example to `~/Games/Armed and Dangerous`. Keep the folder
   names as they are (`GameData`, `movies`, `streams`, ...).
2. **Or copy the already exported assets.** If the PC has run `tools/setup_assets.py`, its `work/remaster/` folder
   (every level, the movies, the texture pack) is all the engine reads: copy it into the Mac checkout as
   `work/remaster/` and skip step 3. The exports are the same on every system.
3. **Or download it with SteamCMD** (Valve's command line Steam client, which runs on macOS:
   https://developer.valvesoftware.com/wiki/SteamCMD), logged in with your own account that owns the game, asking
   for the Windows depot:

   ```sh
   steamcmd +@sSteamCmdForcePlatformType windows +force_install_dir "$HOME/Games/Armed and Dangerous" \
            +login YOUR_STEAM_NAME +app_update 6090 validate +quit
   ```

   This is the same download the Windows client makes. `setup_assets.py` prints the build it finds; build 251748
   is the tested one.

The Steam executable stays as it is: the exporters read only its data sections (tools/gamefiles.py), and the
project never unwraps or patches it.

## 3. Export the assets

```sh
git clone https://github.com/NikhilVerma/armed-and-dangerous-recomp.git
cd armed-and-dangerous-recomp
python3.12 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python tools/setup_assets.py --game "$HOME/Games/Armed and Dangerous"
.venv/bin/python tools/setup_assets.py --remaster-textures     # optional: the AI texture pack (176 MB)
.venv/bin/python tools/setup_assets.py --check                 # what is set up
```

`orig/pc` becomes a symbolic link to the game folder (a junction on Windows); everything written goes to
`work/remaster/`. `--levels lev_1_m1` exports only the first level (minutes instead of half an hour). Without ffmpeg
the movies are skipped with a note and the game runs without its cut-scenes.

File names: the exporters write every file under its lower-case name, and the engine and the tools look names up
in lower case or without regard to case, so the default (case-insensitive) APFS volume and a case-sensitive one both
work.

## 4. Build

```sh
cd engine
cargo build --release
```

The first build compiles Bevy and Jolt (a few minutes on an M-series Mac). The binary is
`engine/target/release/and_remaster`.

## 5. Play

```sh
engine/play.sh                       # the title menu
engine/play.sh --level lev_3_m1      # straight into a level
```

`play.sh` is `play.bat`'s twin: it runs from `engine/`, writes the session's log to `engine/logs/` and passes its
arguments on. A binary you built yourself is not quarantined, so Gatekeeper does not ask. Controls, the developer
menu and the cheat codes: docs/play-guide.md (on a MacBook keyboard the F keys need Fn: F3 the stats panel, F8 a
bookmark).

## Where the game keeps its files

| What | Where |
| --- | --- |
| options (`options.json`) and saves (`saves/`) | next to the binary: `engine/target/release/` (as on Windows: they stay with the build) |
| session recordings (docs/play-guide.md) | `~/Library/Application Support/AndRemaster/sessions` (Windows: `%LOCALAPPDATA%\AndRemaster\sessions`); `tools/session.py` finds them there |
| the session's stdout log | `engine/logs/session-<time>.txt` (play.sh) |
| exported assets | `work/remaster/` in the checkout |

## Known differences and limitations

- **The pointer in play is locked, not recentred.** macOS cannot confine the pointer to a window (winit offers only
  a lock there), and moving it every frame stalls mouse events. So in play the pointer is hidden and locked while
  the window has the focus, and the look reads the mouse's raw motion; it is released when the window loses the
  focus or a menu opens. "Keep the mouse in the window" (`clip_cursor`) has no effect in the menus on macOS.
  (`engine/src/platform/mod.rs` `PlayPointer`, `engine/src/cursor.rs`.)
- **Mouse speed** may feel different: macOS reports motion in points, with its own acceleration, where Windows
  reports raw counts. The mouse sensitivity option scales it; a default for macOS is a tuning item once played.
- **The system font**: the menus' outline text is drawn from Arial (`/System/Library/Fonts/Supplemental/Arial.ttf`,
  shipped with macOS). Without it the text falls back to the game's own font atlas.
- **Textures**: every Mac GPU samples the game's BC1 / BC3 (DXT) textures directly (wgpu enables
  TEXTURE_COMPRESSION_BC on macOS). A GPU without BC gets them decoded to RGBA8 on load
  (`engine/src/graphics/bc_decode.rs`; `AND_DECODE_BC=1` forces it for a test).
- **Upscaling**: there is no DLSS or other vendor upscaler in the engine on any system; the render scale option and
  TAA work the same on Metal.
- **Performance**: the 4K / 100 fps floor is measured on the Windows PC's RTX 4090. On a Mac start with the
  Remaster preset at the display's resolution and lower the render scale, or use the Original preset, if it is slow.
- **The oracle tools are Windows-only**: everything that drives the original game.exe (`tools/shot.py`,
  `oracle_diff.py`, `oracle_suite.py`, `oracle_show.py`, `render_compare.py`, `ui_compare.py`, `movie_compare.py`,
  `sound_mix_compare.py`, `compare.py`, `compare_gallery.py`, `coverage.py`, `coverage_level.py`, `instances.py`)
  and the archived DLL-era tools (`archive/tools`). The retexture tools (`tools/retex/`) talk to a local Media Lab
  server on the Windows PC.
- **Portable tools**: the exporters (`setup_assets.py`, `export_level.py`, `remaster_export*.py`,
  `remaster_textures.py`), the playtests (`playtest.py`, `regress.py`, `level_smoke.py`), `session.py`,
  `engine_map.py`, `port_queue.py`, `struct_atlas.py`, `progress.py`, `bench_page.py`.

### To verify on the first run

What CI cannot see, in the order worth checking:

1. The title menu shows and the menus react to the trackpad / mouse and the keyboard.
2. New Game: the look turns with the mouse and the trackpad, the pointer stays hidden, Cmd+Tab away and back gives
   the pointer back and then locks it again.
3. Retina: the window's size and sharpness (`--size` is in pixels; the Display page's render scale and the DPI
   setting), borderless fullscreen and the windowed mode, switching between them.
4. Sound plays, and follows a change of the output device (AirPods on / off).
5. A playtest headless: `python tools/playtest.py engine/playtests/launch_newgame.json --exe target` (its window
   opens off-screen and unfocused; check it really stays out of the way on macOS).
6. Frame times with the Remaster preset (`--bench 30 --out ...`, tools/bench_page.py).

## Next: iOS

Not implemented; what this pass found would block an iOS build:

- **Jolt**: oxijolt-sys ships bindings for macOS, Linux, Android and Windows targets only; `aarch64-apple-ios` needs
  a bindings family added upstream (or `bindgen`) before the physics builds.
- **Input**: no keyboard or mouse. Touch controls (move stick, look pad, fire / special / jump buttons) and game
  controllers through Apple's GameController framework (gilrs, Bevy's gamepad backend, has no iOS support); the
  menus assume a pointer or the arrow keys; the developer tools sit on F keys.
- **The game data**: the exporters are Python on a desktop. The export would be made on a Mac or PC and brought in
  through the Files app / document sharing into the app's container; 12 GB is far too much for an app bundle. The
  engine's default scene path (`../work/remaster/...` relative to the working directory) and `user_files_dir`
  (options and saves next to the executable, read-only in a bundle) must move to the app's data folder
  (`platform::app_data_dir`).
- **Memory**: iOS ends apps far below a desktop's budget. Textures would want ASTC at export (iPhones have no BC; the
  RGBA8 decode fallback takes 4x the memory), streamed per level; audio and animation exports are large too.
- **GPU**: BC exists only on M-series iPads; the compute passes (XeGTAO, GPU snow, motion blur tiles) use storage
  textures (r32uint, rgba16float) and storage buffers in the vertex stage, which Apple GPU families support, but
  thermals would make a mobile preset (no snow compute, half-resolution AO) necessary.
- **App lifecycle**: suspend / resume (the Metal surface goes away), the safe area around the notch, no windowed
  mode or resolution options, audio session setup (AVAudioSession) instead of following output devices.
- **Packaging**: an Xcode project or cargo-mobile2, signing and provisioning.
