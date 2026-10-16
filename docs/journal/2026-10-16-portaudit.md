# 2026-10-16: port disposition audit (agent-portaudit)

Done:
- The human asked how the peasants could be unported in a decompiled game: docs/port/*.csv marked them `not needed`
  ("not ported"), and tools/port_queue.py counted that as done, so every queue read empty. 571 `not needed` rows were
  re-classified against all 24 levels: 144 stay `not needed` (no effect: RTTI, empty / constant slots, PC no-ops,
  debug, dead over all 24 levels with the survey named), 330 are `deferred` (the 10 `debt` rows and 7 engine
  comments that cite an address as "not ported" included), the rest `replaced` (with where) or `ported` (cited).
- docs/port/README.md defines ported / replaced / not needed / deferred; docs/port/features.csv lists the 65 deferred
  features (effect, objective levels, templates, owner).
- tools/port_queue.py: `deferred` is open work (even when cited); `--deferred [--md]` ranks the features;
  `--lint` fails on a dishonest `not needed` and runs in the pre-commit hook; finds work/ from a worktree.
- docs/port/deferred-2026-10-16.md: the ranked list. docs/port/queue-after-all-levels.md regenerated.

Learned:
- Levels that cannot be finished as the original for want of a deferred feature: peasants (2_m3, 3_m1, 3_m3, 4_m1,
  4_m2, bonus_m2), ticker sites locked to the goliath squads (3_m1, 4_m1: lock_parent_tag, 0x0044e120), the
  companions' tower mode (4_m2), the jet pack (4_m3).
- An engine citation is not proof of a port: 11 cite an address in a "not ported" comment (`--lint` warns).

Next / open:
- Audit the 53 `replaced` rows whose reason mentions lev_1_m1 or "not ported" (`--lint` warnings).
- Trace the 16 `untraced` functions (e.g. class 98 0x00461790, constructed 40 times).
- Verify the "guess" effects in features.csv (barracks windows on 1_m3) in play.
