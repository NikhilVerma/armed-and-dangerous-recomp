# Port dispositions

`docs/port/<subsystem>.csv` (columns `address,name,disposition,reason`) records, per original function, what the
remaster does about it. `tools/port_queue.py` reads these files; the queue it prints is the port's open work, so a
disposition must never hide work that is still to do. That happened once: 571 rows said `not needed`, and at least
327 of them meant "not ported yet" ("not ported", "not in lev_1_m1", "later"). The peasants were among them, and six
levels' objectives need the peasants (2026-10-16 audit, docs/port/deferred-2026-10-16.md).

## The four dispositions

| disposition | meaning | leaves the queue | the reason must say |
| --- | --- | --- | --- |
| `ported` | the engine does what the function does. Normally implied by an engine comment citing the address; a row is only needed to record where | yes | the engine file |
| `replaced` | the behaviour is provided by a different mechanism in the remaster (Bevy, Jolt, Rust collections, a remaster module that models the same outcome differently) | yes | **where**: the engine file / mechanism that gives the same outcome |
| `not needed` | the behaviour has **no effect** in the remaster, for one of these reasons only: dead code in the shipped game (no caller, a state nothing enters, data no level uses); debug-only; platform / D3D / DirectSound / Havok / CRT plumbing that Bevy / Jolt / Rust replace with no visible difference; RTTI or other class-registry plumbing; empty or constant virtual slots; editor-only | yes | why it has no effect, with the evidence (the survey of the 24 levels, the caller list, the PC no-op) |
| `deferred` | game behaviour not ported yet | **no**: open work | `feature: <slug>;` first, then what it does. The slug is a row of `docs/port/features.csv`, which says what the feature does, its effect (blocks objective / visible gameplay / unknown / cosmetic), the levels whose objective involves it, the templates that place it and its owner |

Rules:

- "Not on lev_1_m1", "not in this port", "later", "not ported", "open" are never reasons for `not needed`. A function
  reached on any of the 24 levels (work/coverage.json, `tools/coverage_level.py`), or belonging to a class any level
  places (work/remaster/<level>/scene.json / mission.json), is game behaviour: it is `ported`, `replaced` (with
  where) or `deferred`.
- "Dead in the shipped game" needs evidence over all 24 levels: no level places the class, no fx / sound / script
  of any level uses the opcode / flag / tag, or no code path sets the state.
- A partial port is `deferred` for the parts that are missing. An engine comment that cites an address as "not
  ported" does not make it ported: give it a `deferred` row (the tool counts `deferred` as open even when cited).
- An unidentified function the original runs is `deferred` with `feature: untraced`, not `not needed`.

## Tools

```
python tools/port_queue.py                 per subsystem: reached / ported / explained / deferred / left / open
python tools/port_queue.py ai              the open queue of one subsystem (left and deferred)
python tools/port_queue.py --deferred      deferred work by feature, ranked (effect, objective levels, levels affected)
python tools/port_queue.py --deferred --md docs/port/deferred-<date>.md
python tools/port_queue.py --lint          exit 1 on a dishonest `not needed`, an unknown disposition, a deferred row
                                           without a known feature; warnings for `replaced` rows that mention lev_1_m1
                                           and engine comments citing an address as not ported
```

Run `--lint` after editing any of these files. From a worktree the tool finds `work/` in the main checkout (git's
common dir) or in `$AND_WORK`.
