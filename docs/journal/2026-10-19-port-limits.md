# 2026-10-19: the old remaster's engine-limit, input and master-volume options in the Bevy engine (agent-port-limits)

Branch `agent-port-limits` (from wip/full-decomp 38eb2ad), target dir engine/target-wt-port-limits.

## Done

The old remaster's options (hook/enhance/limits.cpp, persistence.cpp, input.cpp, audio.cpp) as typed settings of the
graphics presets, one module per group, each with its Original / Remaster presets and its source cited:

- physicsstep.rs: `physics_fixed_step` (Original: PhysicsManager_Step 0x00411d40's frame-tied step of at least 1/60 s;
  Remaster: Bevy's fixed 1/60 s steps), `physics_interpolation` (the old DrawMatrix rule, plus rotation for bodies
  the physics turns; drawing only, true poses put back in `First`), `physics_solver_iterations` (Jolt velocity steps,
  10 / 20), `physics_body_capacity` (the old `force_pool`'s intent: the engine has no force pool; Jolt's body table,
  10240 / 32768). physics.rs: `Physics::step`, `WorldConfig`, the `PhysicsSync` set.
- pools.rs: `decal_pool` (160 / 1024; decals.rs `Decals::capacity`), `particle_pool_scale` (1 / 16; fxvm Pools
  `scale`), `path_nodes` (nav.rs budget, 150000 in both: see engine-limits.md).
- debris.rs: `debris_keep` / `debris_max` (KeepDebris: fxo_debri*, gravity >= 0.99, ground test; 1000 per level),
  `debris_time` (remaster addition: stays N s, then a 1.5 s fade; 0 = whole level). FxObject gained `gravity` (opcode
  0x11) and `kept`.
- cursor.rs: `hardware_cursor` (Original: the layout's ui_mouse_cursor sprite over a hidden pointer; hud.rs
  `spawn_element`), `clip_cursor` (Confined while focused, play and menus). player.rs `grab_cursor` asks `cursor::grab`.
- options.rs `master_volume` -> audio.rs `Mixer::master_volume` / `master_gain` (voices and movies).
- Menu: Options > Engine page (enginepage.rs: 5 checkboxes, 7 steppers over value lists), Master Volume on the Audio
  page.
- Playtests: steps `"graphics": {...}` and `"options": {...}` (playtest.rs `apply_options`); new scripts
  limits_decal_pool, limits_debris_keep, limits_master_volume in engine/playtests.
- Docs: engine-limits.md (mapping table), remaster-engine.md (presets section).

## Learned

- The Original preset now steps physics frame-tied; at the scripted runs' 16.6667 ms frame both modes step exactly
  1/60 s (frames within 0.1 % of 1/60 s snap to it, the old remaster's tolerance), so the suite is unaffected.
- The Original's particle pools are now the original's size (x1); a full pool refuses without drawing random numbers.

## Next / open

- Interpolation is checked by unit tests only (headless runs are 60 fps, where it is off by rule); a bench at 144 Hz
  with an fx-heavy scene would show it in play.
- Solver iterations 20 and 1000 kept debris: measure against the 4K 100 fps floor in a heavy level.
