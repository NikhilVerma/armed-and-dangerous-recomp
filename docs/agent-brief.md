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

Testing:
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
Original: [recovered source files / classes]. Queue: `python tools/port_queue.py [subsystem]` lists the functions
the original executes (work/coverage.json) that no engine file cites yet, by source file; `--all` adds the ones
the coverage scenarios did not reach. Read each source file ONCE, in order, and port everything in it that the
remaster needs for this subsystem, citing addresses. For each queued function decide: ported (cite it), replaced
by the engine (say by what, in docs/remaster-engine.md), or not needed (say why).
Isolation tests: headless playtests from saved states with expect steps (or Rust unit tests for pure logic), one
per behaviour area, never passing by luck (gunner1 once relied on a keg blast and one stray bullet: any code change
that shifts the random sequence broke it); drive the outcome directly (kill / hurt / warp steps, aim at the real
body) and give ranges a margin, added to tools/regress.py's SUITE. `python tools/regress.py --exe [target]` must pass before
you report.
Done when: the queue for your subsystem is empty or every remaining entry has a stated reason; tests pass;
docs/subsystems/<name>.md updated. Report: branch, commits, queue before / after, tests, open questions.
```
