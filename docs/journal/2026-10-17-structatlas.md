# 2026-10-17: struct atlas (agent-structatlas)

Branch agent-structatlas (from wip/full-decomp 211d2a2). Hook build build/hook-structatlas, original instance
oracle-structatlas, snapshots in work/oracle-structatlas (15 scenarios, every 4 play frames, 4 objects per vtable) and
work/oracle-structatlas-ai (8 scenarios, every 2 frames, 12 per class, AI / player / peasant / twiglet / vehicle
classes, plus the Game and World singletons). Every original run off-screen; tests (check.ps1) not run.

## Done

- `tools/struct_atlas.py` (docs/structs/README.md has the method, labels and rebuild steps):
  - `classes`: the class model from the game's own descriptors (vtable slot 1 -> `CLASS(Name)` / `CLASSV(Name)`, size
    at +8, `BASECLASS` entry -> base descriptor = the hierarchy); vtables without a descriptor named from the factory
    tables (kCreateCases + kBehaviours) as `obj:<behaviour>`. 162 classes, 170 vtables.
  - `static`: parses src/**/*.cpp: 21,873 `base + 0x..` member uses (accessors detected per file, `Field<T>`, raw
    casts, field addresses passed along); 11,586 attributed to a class (8,612 to object classes, the rest Game /
    World). Class of a base: the method's `this` (vtable LCA, ecx through the ABI wrapper), the comment's register /
    stack description, the tracker-name prefix, caller propagation (4 rounds), aliases, getters, getter lambdas.
    Properties: 694 reads, 570 attributed (reader calls, ParamStep tables, file-local `Rd(dest, name, default)`
    wrappers, bool-to-bit reads, statement associations).
  - `run`: the original on oracle scenarios with `[test] struct_snap` (hook/src/teststructsnap.cpp, wired from
    testoracle.cpp; tools/instances.py takes AND_HOOK_BUILD for a private hook build).
  - `infer`: per (class, offset), folded onto the class that owns the offset; dynamic facts merged over subclasses;
    hypotheses with evidence and confidence (property, flag bit, countdown, stopwatch, timestamp, frame counter,
    position, matrix translation, velocity, health-like, pointer to class, flags, state/enum, AI-state link, bool,
    constant); contradictions; coverage and precision reports.
  - `field CLASS OFF`, `class CLASS [--all] [--min-n N]`, `prop NAME [--like]`: a few lines each.
  - `docs`: docs/structs/<Class>.md for Object, GenericAIObject, PlayerObject, FxObject, DefaultGameObject,
    PeasantObject, TeamateObject, GoliathObject, TurretGun, Twiglet, Projectile, Game; 218 proposed names in
    symbols/types/data_members_proposed.csv (status inferred, provenance `struct_atlas:<evidence>`).
- docs/agent-brief.md: porting agents look fields up with the atlas.

## Results (work/struct_atlas_report.txt)

- Coverage: object-class accesses 8,612: on a typed field 96%, on a named field 62% (game tables / properties /
  trace), named or with an inferred meaning 81%, on a dynamically tested field 21%. (class, offset) pairs used by the
  C++: 1,860; typed 1,689, named 979, name or meaning 1,235, dynamically tested 208. Per class: GenericAIObject 218
  offsets (205 typed, 175 name/meaning, 37 tested), PlayerObject 246 (241 / 170 / 14), Object 151 (136 / 67 / 25),
  Game 327 (298 / 128 / 38), Twiglet 38 (37 / 24 / 7).
- Precision against independent ground truth (the game's member tables and the fields the oracle trace reads):
  1,157 checks, 1,128 agree (97%): meanings 70/71 (timer 18/18, pointer 18/18, flags 16/16, bool 6/6, state 5/6,
  position 4/4, health 2/2, velocity 1/1), types vs traced kinds 32/32, widths 639/640, property names vs table
  names 386/410 (token match; most "disagreements" are synonyms: fwd_max_speed_min / m_YSpeedMin, num_ammo /
  m_giveAmount). Against docs/subsystems words after an offset: 1/4 (that text is too loose to be ground truth).
- Examples: GenericAIObject +0x490 m_stateTimer countdown (2895/3091 changes = -dt), Twiglet +0x468 / +0x46c /
  +0x578 timers, Object +0x1d0 m_stamp frame counter, +0x370 m_transformStamp grows with the clock, PlayerObject
  +0xc0c m_vLocation = position, GenericAIObject +0x588 target -> PlayerObject, Twiglet +0x55c bomb -> FxExplosion,
  Object +0x454 health falls in 68/69 changes; PlayerObject +0x9f0 (the human's example) = m_turnPowerMin,
  property turn_power_min, default 40.
- Reported contradictions worth a look: FxObject +0x524 is named m_hitDelay in the table but holds mapped heap
  pointers at run time; CameraSystem +0x15c m_easeIntoPlayerMode only stores 0 / 1 (a bool, the name says mode).

## Learned

- The member tables are the game's save-data descriptors: `BASECLASS(X::X_saveData)` with the base descriptor in
  word 4 gives the full hierarchy (PlayerObject and GenericAIObject derive directly from Object, size 0x460).
- Body_getPosition 0x00457930 (vt+0x54 of most objects) dereferences the Havok entity +0x1c4 without a null test:
  calling it on a dying projectile faults (the hook's crash logger then writes a dump per fault, about 1 s each).
- The recovered C++ copies floats as dwords (`D(a) = D(b)`): static types alone undercount floats; the snapshots fix
  the type.

## Next / open

- The `jump` scenario's original run hung once before the briefing (no snapshot; log stopped, process idle; rerun).
- Game / World have no pointer-target records (the hook records them only for objects): many Game pointers stay
  "pointer (heap)".
- Health tests rarely see a traced death of a sampled object: snapshot the objects the trace reports hit (or more
  per class in combat scenarios).
- Non-object structs (renderer, loader, audio) are unattributed (48% of all uses); they need their own classes.
- Snapshots take ~900 MB in work/; `struct_atlas.py infer` takes ~3 minutes over all of them.
