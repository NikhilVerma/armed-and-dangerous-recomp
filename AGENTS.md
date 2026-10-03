# AGENTS.md: operating rules

Copied from section 13 of the handover document "Armed and Dangerous decompilation: agent handover"
(https://claude.ai/artifact/XEDfhTM5H1QSEQK5GPR3i4). Re-read this file at the start of every session and
after every context compaction. Long-running agent projects lose their rules when context is compressed;
re-reading is the fix.

## Project state at a glance

- Primary target: PC Steam build (app 6090, build 251748), `orig/pc/GameData/game.exe`.
- The Steam executable is wrapped by SteamStub (`.bind` section, encrypted `.text`). The human unwraps it
  with Steamless; agents never unpack or patch protected executables. See `docs/decision.md`.
- Current milestone: M0 (reconnaissance). Read `docs/recon.md` and the latest `docs/journal/` entry.

## Tools, not dumps

- Wrap every recurring operation as a script or tool call that returns a small structured result: decompile
  one function, list references to an address, show a struct member at an offset, diff one function, run one
  test, set a function's stage.
- Never load a whole disassembly listing or a whole generated file into context. Query for the one function
  you are working on.

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
- Never commit on red tests. Never commit anything from `orig/`, `work/` or `ref/`. The pre-commit guard in
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
