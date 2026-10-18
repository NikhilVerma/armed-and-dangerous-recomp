# Brief template for a background agent (remaster engine)

Copy, fill the brackets, send as the agent's prompt. Every field is there because leaving it out once cost a
rebuild, a rebase or lost work (docs/journal/2026-10-14.md).

```
Task: [one mechanic, e.g. "the mounted gun stig_mgun 32: enter / fire / leave"]. Original: [addresses, source files,
doc sections]. Oracle: [capture names already taken, or what to capture with tools/shot.py].

Setup:
- Work in your own git worktree, branched from wip/full-decomp at [commit] (NOT main: main has no engine/).
- Build only into engine/[target-wt-NAME] (CARGO_TARGET_DIR). Never engine/target: it is the player's copy.
- `cargo check` until clean, then `cargo build --release`. Bevy systems take at most 16 parameters: bundle extras
  in a tuple, e.g. `(a, b): (Res<A>, Res<B>)`; `python tools/engine_map.py params` lists the full ones.
- Edit CRLF files with tools/crlf_edit.py (or the Edit tool), never with tools that rewrite line endings.
- Find code with `python tools/engine_map.py writes|reads|sends|receives|addr|fn ...` before grepping.
- Look object fields up instead of re-deriving them: `python tools/struct_atlas.py field CLASS 0xOFF` (names from the
  game's member tables, property names, type, a tested meaning such as countdown timer / position / velocity /
  pointer to X, each with its evidence), `class CLASS --min-n 3`, `prop NAME` (docs/structs/README.md; summaries in
  docs/structs/<Class>.md). Meanings are inferred with a confidence: check a low one against the code before use.

Testing:
- While the human's game runs (`tasklist | findstr and_remaster` showing engine\target\release), do NOT build
  or run anything: it eats their CPU (2026-10-15: "100% CPU and i cant play anymore"). Wait, or work by reading.
- Every run --background (off-screen, muted, no mouse grab): never a visible window, not even a short bench,
  unless the human has given a time slot (their screen got spammed, 2026-10-15). Captures of the original game
  need the human's OK too.
- Headless only: `python tools/playtest.py work/remaster/play/SCRIPT.json --exe target-wt-NAME`.
- Start from a saved state ("load": "states/NAME.json"; states written by a {"save": "NAME"} step; existing:
  [list, e.g. after_pub]) and check with {"expect": {...}} steps. A test should take under a minute.

Done when: [the measurable result, e.g. "expect steps pass: entering at 71,173 gives the gun's view; 30 rounds
fire; health unaffected by own shots"; and the checklist row in docs/level1-mechanics.md updated].

Checkpoints: commit on your branch after each working step (message says built / tested or not), so a stop never
loses work. Final message: branch, commits, what was verified (expect lines), what is left.
```

Scope: one mechanic per agent. If it grows past that, stop at a checkpoint commit and report instead of widening.

## Subsystem port (2026-10-14: the work order is by subsystem)

```
Subsystem: [name]. Engine files you own: [files]; do not edit others' files except small, stated hooks.
Original: [recovered source files / classes]; their fields: `python tools/struct_atlas.py class CLASS` / `field CLASS
0xOFF` (docs/structs/). Queue: `python tools/port_queue.py [subsystem]` lists the functions
the original executes (work/coverage.json) that no engine file cites yet, by source file; `--all` adds the ones
the coverage scenarios did not reach. Read each source file ONCE, in order, and port everything in it that the
remaster needs for this subsystem, citing addresses. For each queued function decide (docs/port/README.md): ported
(cite it), replaced (name where), not needed (ONLY when it has no effect: dead in the shipped game over all 24
levels, debug, platform plumbing), or deferred ("feature: <slug>; ..." with the slug in docs/port/features.csv):
game behaviour you did not port is deferred, never not needed, even if your level does not use it. Rows go in
docs/port/<subsystem>.csv (address,disposition,reason); `python tools/port_queue.py --lint` must pass (the
pre-commit hook runs it); deferred rows stay in the queue (`--deferred` ranks them).
Isolation tests: headless playtests from saved states with expect steps (or Rust unit tests for pure logic), one
per behaviour area, never passing by luck (gunner1 once relied on a keg blast and one stray bullet: any code change
that shifts the random sequence broke it); drive the outcome directly (kill / hurt / warp steps, aim at the real
body) and give ranges a margin, added to tools/regress.py's SUITE. `python tools/regress.py --exe [target]` must pass before
you report.
The goal (docs/decision.md 2026-10-15): a clean, idiomatic Rust / Bevy rewrite with the game as the oracle — model the
behaviour, don't transliterate the C++ (no field offsets as struct design, no ported control flow for its own sake);
no hacks: a workaround is replaced by the real mechanism or logged as debt with a plan; outcome >= the original.
Since 2026-10-17 (decision.md): features first. Build a feature the way you would in a from-scratch Bevy game:
typed components, systems in a plugin, typed stage / level data (serde structs deserialised once, not string
property lookups in systems). It lands when it works, has a playtest, and every level using it can be finished;
the oracle comparison is the later tuning pass (log visible differences as tuning items, don't block on them). The mechanics
still come from the original code: read the recovered C++ (src/) for each feature's numbers, timings, rules and
states and replicate them as faithfully as you can (best-shot faithful); only the structure is yours.
Before porting a function, check nothing already implements it: `grep -rn 0x00XXXXXX engine/src` (and again after
merging wip before your final tests). Parallel agents built the same function twice (the companions' close kill and
the Q112's swing, 2026-10-18); each original function gets one implementation, extended rather than duplicated.
Code quality (docs/decision.md 2026-10-14: the human will build their own games from these mechanics): keep
each mechanic a self-contained module / Bevy plugin with a clear public API, game specifics in data (properties,
templates, tuning) rather than code, names that say what things do (addresses as provenance in doc comments),
no copy-paste, split sprawling systems; label any hack as debt in docs/remaster-engine.md.
Done when: the queue for your subsystem is empty or every remaining entry has a stated reason; tests pass;
docs/subsystems/<name>.md updated. Report: branch, commits, queue before / after, tests, open questions.
```

Test budget (human, 2026-10-18): the slow checks (full `tools/regress.py`, `level_smoke.py`, `oracle_suite.py`,
`--long`) run at most once a day or after a massive change, never per fix. Per fix: a release build, the playtests
for what you changed, `launch_newgame`. The human's play feedback is the main check.
