# AGENTS.md: operating rules

**Precedence: the human's instructions override everything in this file and in the handover.** The handover
is the human's seed document, not a fixed specification. When a human decision changes a rule, record it in
`docs/decision.md` and update this file.

Copied from section 13 of the handover document "Armed and Dangerous decompilation: agent handover"
(https://claude.ai/artifact/XEDfhTM5H1QSEQK5GPR3i4). Re-read this file at the start of every session and
after every context compaction. Long-running agent projects lose their rules when context is compressed;
re-reading is the fix.

## Project state at a glance

- Primary target: PC Steam build (app 6090, build 251748), `orig/pc/GameData/game.exe`.
- The Steam executable is wrapped by SteamStub (`.bind` section, encrypted `.text`). The human unwraps it
  with Steamless; agents never unpack or patch protected executables. See `docs/decision.md`.
- Current milestone: M0 (reconnaissance). Read `docs/recon.md` and the latest `docs/journal/` entry.
- **Human's direction (docs/decision.md, 2026-10-05):** decompile everything except proprietary/library code (Havok, qhull,
  D3DX, CRT, zlib), then rebuild the game in a modern way. "When in doubt, decompile."
- **Engine limits:** `docs/engine-limits.md` lists the original engine's hard limits and whether the remaster lifts them. Lift them, do not accept them; add new ones as they turn up.
- **Remaster engine (`engine/`, Rust + Bevy + Jolt):** a manual port of the game, one verified piece at a time
  (docs/decision.md 2026-10-13), ported **by subsystem, each tested in isolation, levels after** (2026-10-14). Read **docs/remaster-engine.md** first: the porting order (rendering, player
  movement, sticky bomb, guns, zoom, ...), the same-camera render comparison (tools/shot.py camera dumps,
  `--camera`, tools/render_compare.py), the playtest bot, and the traps. A piece is not done while its comparison
  with the original shows a difference; update that file and the journal at the end of every session.
- **Open player feedback:** `docs/player-feedback.md` is the backlog of the player's in-game feedback. Read it at session start and after every compaction, and update its status column as items land.
- **Resuming the recovery round:** read `docs/handover-recovery.md` (pipeline, tools, pitfalls, how to resume) and run
  `python tools/recovery_status.py`. Work in progress lives on branch `wip/full-decomp`; main stays green.
- **Human's goals (docs/decision.md, 2026-10-03):** (1) every function exported and recompilable, (2) migrate
  piece by piece via hooks, (3) early enhancements: resolution, HDR, frame rate, FOV, (4) the rest, including a
  later move from Havok to an open-source physics engine. Enhancements go in `hook/enhance/`, each behind an
  `and.ini` switch that is off by default; tests always run with them off; `src/` stays faithful. This
  amends the "do not modernise before M5" rule below for `hook/enhance/` only.

## Tools, not dumps

- Wrap every recurring operation as a script or tool call that returns a small structured result: decompile
  one function, list references to an address, show a struct member at an offset, diff one function, run one
  test, set a function's stage.
- Never load a whole disassembly listing or a whole generated file into context. Query for the one function
  you are working on.
- Remaster engine (human, 2026-10-14: "be smart and strategic" about tokens and wasted effort):
  - Find code with `python tools/engine_map.py writes|reads|sends|receives|addr|fn|params ...` before grepping.
  - After a merge or a change to shared code: `python tools/regress.py` (the fast suite, about 3 minutes).
  - Test with `python tools/playtest.py SCRIPT.json`: headless, prints only EVENT / EXPECT / PLAYTEST lines.
    Start scripts from a saved state (`"load": "states/NAME.json"`, written by a `{"save": "NAME"}` step) and
    check with `{"expect": {...}}` steps, so a check takes seconds, not a replay of the intro and the pub.
  - `cargo check` until clean before `cargo build --release`; bundle system parameters past 16 in tuples.
  - Edit CRLF files with tools/crlf_edit.py. Brief background agents with docs/agent-brief.md (branch, own target
    dir, tools, a measurable "done", checkpoint commits).
  - Before handing the human a build: launch, fullscreen / window, the options apply, gamepad, start a level.

## One tracker

- `symbols/functions.csv` is the single record of state: address, size, name, subsystem, stage (S0 to S4),
  approach, name provenance, test that covers it, tokens and minutes spent.
- The work queue is a query over that file, for example the smallest S0 functions in one subsystem whose
  callees are all at S2 or later. Do not maintain a hand-written progress document.

## Task size

- One task is one function or a tight cluster of a few.
- Agents drift toward small easy functions and postpone large ones. Each session must take at least one
  function from the largest tenth of what remains. Bring large functions to S2 first, block by block, before
  trying to understand them.

## Session routine

1. Start: read `AGENTS.md`, `docs/decision.md` and the latest journal entry. Run `tools/check.ps1` and
   confirm green.
2. Work: pick from the queue, follow the per-function loop of the current approach.
3. End: tests green, commit, update the tracker, write `docs/journal/<date>.md` with what was done, what was
   learned, what is next, and open questions.

## Written memory

| File | Content |
| --- | --- |
| `docs/subsystems/<name>.md` | What the subsystem does, its key structs, entry points and unknowns |
| `docs/patterns.md` | Recurring compiler idioms, inlined helpers and how to recognise them |
| `docs/original-bugs.md` | Defects found in the original, reproduced faithfully and not fixed |
| `docs/open-questions.md` | Things you could not resolve, with the evidence gathered so far |
| `docs/journal/` | One entry per session |

## Commits

- Small: one function or one coherent cluster. The message gives addresses, the stage change and the oracle
  result.
- Never commit on red tests. Tests run after the human has validated a change in play, or when they ask
  (docs/decision.md, 2026-10-11); say so in the commit message when they did not run. Never commit anything from `orig/`, `work/` or `ref/`. The pre-commit guard in
  `tools/hooks/` enforces size and extension limits (`git config core.hooksPath tools/hooks`).

## If several agents run in parallel

- Each works in its own git worktree and owns a subsystem or an address range, so two agents never take the
  same function.
- One agent only reviews: evidence present, name provenance honest, test attached, no behaviour change
  beyond the stated function.
- One writer for the Ghidra project. Everyone else reads exports.
- Each running game instance uses its own control-channel port.

## Do not

- Modernise, restructure or rename for taste before M5.
- Fix original bugs. Reproduce them and log them.
- Skip the oracle because a function looks obviously right.
- Invent a confident name without evidence.

## ASK the human before

- Anything at the DRM gate.
- Downloading pre-release builds, old compilers or old SDK installers.
- Pushing to any remote or publishing anything.
- Changing the primary target from PC to Xbox, or switching approach for the whole project.
- Exceeding the agreed token or time budget for a milestone (current budget: no fixed limit; report at each
  milestone).

## Ground rules (handover section 1)

- Never commit original binaries, game assets, or bulk disassembly to git.
- Do not download copies of the game. The only retail build used is the one the human installed.
- Do not defeat copy protection.
- Recover first, improve later. No modernising, style renames or bug fixes before M5.
- Every claim in notes carries evidence: an address, a tool output, or a test. Guesses are labelled.
- Each session starts cold. Anything not written into the repository is lost.
