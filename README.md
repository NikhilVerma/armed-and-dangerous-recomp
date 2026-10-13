# Armed and Dangerous: decompilation and remaster

Start with **AGENTS.md** (operating rules, read every session) and **docs/decision.md** (the human's decisions).

- `src/`: the recovered game code (C++), hooked into the original executable by the hook DLL (`hook/`, built with
  `pwsh tools/build.ps1`). Resume the recovery with docs/handover-recovery.md.
- `hook/enhance/`: remaster enhancements for the original executable, each behind an and.ini switch.
- `engine/`: the new engine (Rust, Bevy, Jolt), a manual port of the game one verified piece at a time. Read
  **docs/remaster-engine.md** for the workflow, the comparison tools and the porting order; engine/README.md for
  running it.
- `tools/`: exporters (`remaster_export*.py`), capture and comparison (`shot.py`, `render_compare.py`,
  `ui_compare.py`), tests (`check.ps1`, `compare.py`).
- `docs/`: subsystem notes (`docs/subsystems/`), formats, the journal (`docs/journal/`), player feedback.

Never commit anything from `orig/`, `work/` or `ref/` (game data and captures).
