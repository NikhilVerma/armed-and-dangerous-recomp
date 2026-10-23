# AGENTS.md: operating rules

**Precedence: the maintainer's (the human's) instructions override everything in this file and in the handover.**
The handover is the human's seed document, not a fixed specification. When a human decision changes a rule, record
it in `docs/decision.md` and update this file.

Rules for AI agents (and a useful map for human contributors: CONTRIBUTING.md is the short version). They started as
section 13 of the human's private handover document "Armed and Dangerous decompilation: agent handover". Re-read this
file at the start of every session and after every context compaction. Long-running agent projects lose their rules
when context is compressed; re-reading is the fix.

## Repository layout

| Path | What |
| --- | --- |
| `engine/` | the remaster: Rust, Bevy 0.20, Jolt Physics 5.6. engine/README.md runs it; **docs/remaster-engine.md** is the working handover (verification loop, comparison tools, playtest bot, traps) |
| `engine/playtests/` | headless playtest scripts with `expect` steps (tools/playtest.py, tools/regress.py) |
| `tools/` | asset export from your own game copy (`setup_assets.py`, `export_level.py`, `remaster_export*.py`), playtests, the oracle tools that drive the original game (`shot.py`, `oracle_diff.py`, `render_compare.py`, `compare.py`), queries (`engine_map.py`, `port_queue.py`, `struct_atlas.py`), `progress.py` |
| `archive/` | the DLL-injector era, not maintained: the recovered C++ of the whole game (`archive/src`, the reference spec), the hook DLL, its tools. archive/README.md |
| `symbols/` | every function of game.exe: `functions.csv` (address, size, name, subsystem, stage), globals, ABI, member tables |
| `docs/` | decisions, subsystem notes (`docs/subsystems/`), struct atlas (`docs/structs/`), port dispositions (`docs/port/`), formats, the journal (`docs/journal/`), player feedback, progress SVGs |

Never commit anything from `orig/` (your game copy, linked by tools/setup_assets.py), `work/` (exports, captures) or
`ref/` (reference checkouts).

## Project state at a glance

- Primary target: PC Steam build (app 6090, build 251748), `orig/pc/GameData/game.exe`.
- The Steam executable is wrapped by SteamStub (`.bind` section, encrypted `.text`). The human unwraps their own copy
  for the oracle tools (the hook needs patchable code); agents never unpack or patch protected executables, and the
  repository gives no instructions for it. The asset exporters read only data sections, identical in the wrapped
  exe, so players need nothing of the kind (tools/gamefiles.py). See `docs/decision.md` 2026-10-03 and 2026-10-23.
- **Open source (docs/decision.md 2026-10-23):** public under MIT OR Apache-2.0, full history; players build from
  source and bring their own game copy; no binary release until a signed Windows build is possible. Never put game
  data, captures, personal paths or anyone's e-mail address in the repository.
- The decompilation is complete: 3,496 game functions recovered as C++ (`archive/src`), 2,254 of them verified in
  the running game (symbols/functions.csv; `python tools/progress.py`). The hook route is archived (2026-10-23).
- **Human's direction (docs/decision.md, 2026-10-05):** decompile everything except proprietary/library code (Havok,
  qhull, D3DX, CRT, zlib), then rebuild the game in a modern way. "When in doubt, decompile."
- **Engine limits:** `docs/engine-limits.md` lists the original engine's hard limits and whether the remaster lifts
  them. Lift them, do not accept them; add new ones as they turn up.
- **Remaster engine (`engine/`, Rust + Bevy + Jolt):** a manual port of the game, one verified piece at a time
  (docs/decision.md 2026-10-13), ported **by subsystem, each tested in isolation, levels after** (2026-10-14).
  **Code quality matters:** the human will build their own games from these mechanics (decision.md 2026-10-14):
  each mechanic a clean, self-contained module / Bevy plugin with a clear API, game specifics in data, mod-friendly,
  no garbage. **The game is the oracle; the Rust is a clean, idiomatic rewrite, not a transliteration** (decision.md
  2026-10-15): behaviour is the spec, the recovered C++ explains it, no hacks, outcome >= the original; features
  first, then tuning toward the original (2026-10-17); phase 2 adds remaster features (level select, difficulty
  scaling enemy counts, rendering). Read **docs/remaster-engine.md** first: the porting order, the same-camera render
  comparison (tools/shot.py camera dumps, `--camera`, tools/render_compare.py), the playtest bot, and the traps.
  Update that file and the journal at the end of every session.
- **The spec of an original function:** `grep -rn "HOOK(0x00XXXXXX" archive/src` (the recovered C++, with the
  original's addresses); object fields: `python tools/struct_atlas.py field CLASS 0xOFF`.
- **One implementation per original function:** before porting an address, `python tools/engine_map.py addr
  0x00XXXXXX` (or grep engine/src); extend the existing implementation rather than writing a second one.
- **Open player feedback:** `docs/player-feedback.md` is the backlog of the player's in-game feedback. Read it at
  session start and after every compaction, and update its status column as items land.
- **Human's goals (docs/decision.md, 2026-10-03), as they stood for the hook route:** (1) every function exported
  and recompilable (done), (2) migrate piece by piece via hooks (archived), (3) early enhancements, (4) the rest,
  including a move from Havok to an open-source physics engine (the engine uses Jolt).

## Tools, not dumps

- Wrap every recurring operation as a script or tool call that returns a small structured result: decompile
  one function, list references to an address, show a struct member at an offset, diff one function, run one
  test, set a function's stage.
- Never load a whole disassembly listing or a whole generated file into context. Query for the one function
  you are working on.
- Remaster engine (human, 2026-10-14: "be smart and strategic" about tokens and wasted effort):
  - Find code with `python tools/engine_map.py writes|reads|sends|receives|addr|fn|params ...` before grepping.
  - After a merge or a change to shared code: `python tools/regress.py` (the fast suite).
  - Test with `python tools/playtest.py engine/playtests/SCRIPT.json`: headless, prints only EVENT / EXPECT /
    PLAYTEST lines. Start scripts from a saved state (`"load": "states/NAME.json"` next to the script, written by a
    `{"save": "NAME"}` step; `state_after_pub.json` writes the common one and runs first when it is missing) and
    check with `{"expect": {...}}` steps, so a check takes seconds, not a replay of the intro and the pub.
  - `cargo check` until clean before `cargo build --release`; bundle system parameters past 16 in tuples.
  - Edit CRLF files with tools/crlf_edit.py or the Edit tool (GNU `sed -i` turns CRLF into LF; the pre-commit
    hook rejects flipped endings). Brief background agents with docs/agent-brief.md (branch, own target dir,
    tools, a measurable "done", checkpoint commits).
  - Before handing the human a build: launch, fullscreen / window, the options apply, gamepad, start a level.

## One tracker

- `symbols/functions.csv` is the single record of the decompilation: address, size, name, subsystem, stage (S0
  to S4), approach, name provenance, test that covers it. The port's state per function is docs/port/*.csv plus
  the engine's address citations (`python tools/port_queue.py`). Progress graphs come from these
  (`python tools/progress.py`); do not maintain a hand-written progress document.

## Session routine

1. Start: read `AGENTS.md`, `docs/decision.md` and the latest journal entry; `cargo check` in engine/ (into your
   own target dir) is clean.
2. Work: pick from the queue (`python tools/port_queue.py`, docs/port/features.csv, docs/player-feedback.md).
3. End: the playtests of what you changed pass, commit, write `docs/journal/<date>.md` with what was done, what
   was learned, what is next, and open questions.

## Written memory

| File | Content |
| --- | --- |
| `docs/subsystems/<name>.md` | What the subsystem does, its key structs, entry points and unknowns |
| `docs/patterns.md` | Recurring compiler idioms, inlined helpers and how to recognise them |
| `docs/original-bugs.md` | Defects found in the original, reproduced faithfully and not fixed |
| `docs/open-questions.md` | Things you could not resolve, with the evidence gathered so far |
| `docs/journal/` | One entry per session |

## Commits

- Small and coherent. The message gives the original's addresses, what changed in play, and what was tested.
- Never commit on red tests. Tests run after the human has validated a change in play, or when they ask
  (docs/decision.md, 2026-10-11); say so in the commit message when they did not run. Never commit anything from
  `orig/`, `work/` or `ref/`. The pre-commit guard in `tools/hooks/` enforces size, extension, line-ending and
  port-disposition rules (`git config core.hooksPath tools/hooks`).

## If several agents run in parallel

- Each works in its own git worktree (own `CARGO_TARGET_DIR`, engine/target-wt-NAME; never engine/target, the
  player's copy) and owns a subsystem or a set of files, so two agents never take the same function.
- One agent only reviews: evidence present, name provenance honest, test attached, no behaviour change
  beyond the stated scope.
- Each running game instance uses its own control-channel port.

## Do not

- Fix original bugs silently. Reproduce them faithfully and log them in docs/original-bugs.md; a remaster fix goes
  behind a setting.
- Skip the oracle because a function looks obviously right.
- Invent a confident name without evidence.
- Change `archive/` beyond keeping its tools runnable: it is the frozen reference.

## ASK the human before

- Anything at the DRM gate.
- Downloading pre-release builds, old compilers or old SDK installers.
- Pushing to any remote, publishing anything (releases, binaries, the texture pack, artifacts), or changing the
  repository's settings.
- Changing the primary target from PC to Xbox, or switching approach for the whole project.
- Exceeding the agreed token or time budget for a milestone (current budget: no fixed limit; report at each
  milestone).

## Ground rules (handover section 1)

- Never commit original binaries, game assets, captures or bulk disassembly to git.
- Do not download copies of the game. The only retail build used is the one the human installed.
- Do not defeat copy protection.
- Every claim in notes carries evidence: an address, a tool output, or a test. Guesses are labelled.
- Each session starts cold. Anything not written into the repository is lost.
