# Bevy idioms review of engine/ (2026-10-16)

Read-only review of `engine/src` (Bevy 0.20.0, bevy_enhanced_input, bevy_egui 0.43) against Bevy's own API and
examples (registry source `~/.cargo/registry/src/index.crates.io-*/bevy-0.20.0/examples`, `bevy_ecs-0.20.0`,
`bevy_state-0.20.0`, `bevy_time-0.20.0`; the docs.rs pages are generated from these same sources). Nothing was
built or run. Line numbers are from the working tree at commit 236f306.

Context: decision.md 2026-10-14 (mechanics reused in the human's own games: self-contained plugins, data-driven,
moddable) and 2026-10-15 (idiomatic Rust/Bevy, not transliteration; phase 2 adds remaster features).

Measured over `engine/src` (grep counts): 44.5k lines in one binary crate, 63 modules declared in main.rs;
105 `#[allow(clippy::too_many_arguments)]`, 5 `#[derive(SystemParam)]`; 84 ad-hoc `.before/.after`, 8 SystemSets,
0 `configure_sets`; 0 `OnEnter/OnExit`, 3 `in_state` uses; 1 system in `FixedUpdate` (physics step); 0
`#[require]`, 0 component hooks, observers only for screenshots; 0 `Single`/`Populated`, 139 `Option<Res<..>>`;
0 `Reflect`; 0 `AssetLoader`, ~96 `std::fs` reads in 33 files; 399 string-keyed property reads (`p.i/f/s("..")`);
65 `NonSend<Physics>` uses; 35 systems take `ResMut<FxStats>` (the shared RNG).

---

## A. Real bugs and risks (correctness, determinism)

### A1. Simulation runs in `Update` on a variable time step; only physics is fixed. [do now, high]

- What: every gameplay system (player, AI, fx, mission, army, vehicles...) is in `Update` and reads `Res<Time>`
  (= `Time<Virtual>`, variable). Physics alone steps in `FixedUpdate` at 60 Hz (physics.rs:295-297). The oracle
  harness hides this by forcing `TimeUpdateStrategy::ManualDuration(16.6667 ms)` (main.rs:216, 434), so every
  test runs at exactly one 60 Hz step per frame, which a normal launch never does.
- Why not idiomatic: Bevy's `examples/movement/physics_in_fixed_timestep.rs` and `examples/ecs/fixed_timestep.rs`
  put simulation in `FixedUpdate` (reading `Time<Fixed>`, which `Res<Time>` resolves to inside it) and keep only
  presentation in `Update`, interpolating with `Time<Fixed>::overstep_fraction()` (bevy_time fixed.rs:205).
- Risk today: the original's logic is per 60 Hz tick (remaster-engine.md:82; B16's release period needed the
  exact 16.6667 ms step). At the human's 100+ fps (4K floor) gameplay sees ~10 ms steps, 0 or 2 physics steps per
  frame, per-call RNG rolls happen more often per second (e.g. ai.rs:2131 `rng.rand() < 0.9` in the hide
  decision), and any per-frame counter (player.rs:694 CursorSettle frames, etc.) runs fast. The tests cannot
  catch this because they never run at another rate.
- Idiomatic alternative: a `GameplaySet` in `FixedUpdate` at 60 Hz (or 16.6667 ms via `Time::<Fixed>::from_duration`)
  holding input sampling results, player, AI, fx, mission; `Update`/`PostUpdate` keep camera, HUD, audio, render
  interpolation. The oracle then only needs `TimeUpdateStrategy` to drive whole fixed steps, the same code path
  as play.
- Payoff: frame-rate independence (matches the original at any fps), deterministic replays, tests exercise the
  play path. Cost/risk: medium-high: input must be latched for fixed ticks (bevy_enhanced_input supports
  FixedUpdate contexts), camera/HUD smoothing needs interpolation, and every `dt` user must be reviewed. Do it
  now, before more subsystems land on the variable step.

### A2. One RNG shared by ~35 unordered systems. [do now, high]

- What: `FxStats::rand` (fx.rs:181-196) is the RNG for AI (think, search, scare, die, squads, bullies), player
  weapons, mission dispatch, army, alarm, audio ambience/music, pickups, pub lines, vehicles boss, ... (engine_map
  `writes FxStats`: 35 systems in 20 files). Most are in different plugins with no order between them.
- Why: Bevy runs conflicting but unordered systems in an order that is not specified and can change run to run
  (`examples/ecs/nondeterministic_system_order.rs`). With a shared draw stream, who draws first decides every
  roll that follows. The twiglets already had to be split off for this reason (commit c2ea977, `TwigletRng`), and
  vehicles have `VehicleRng`; the rest still share one stream.
- Also: the music/ambience/pub-line rolls draw from the same stream as combat, so turning the music option off or
  opening the pub changes AI outcomes.
- Idiomatic alternative: (1) a total order for gameplay (A3) and (2) one RNG per consumer where the original has
  separate streams (the CRT `rand()` is one global in the original, so faithful order matters more than
  splitting: order the systems as the original calls them), and separate non-gameplay streams (audio, UI) from
  the gameplay one. Turn on `ScheduleBuildSettings { ambiguity_detection: LogLevel::Warn }` for the gameplay
  schedule (example above, line 27) and drive the warning count to zero.
- Payoff: deterministic oracle comparisons (many "random picks / rand() rolls (expected R2)" known differences in
  remaster-engine.md may be this), reproducible bug reports. Cost: low to turn on detection; medium to resolve.

### A3. Cross-plugin ordering is ad hoc; message latency depends on executor order. [do now, high]

- What: there is no schedule-level order between subsystems; each plugin chains its own systems and a few say
  `.after(crate::fx::FxSet)`. `player.rs:426` (control .. projectiles .. death) has no order relative to `FxSet`,
  `mission`, or AI; sticky.rs, shell.rs and mission.rs write `fx::Explode` with no order to `fx::damage`, while
  vehicles/rockets.rs:33 writes it *after* FxSet. A message read in the same frame or the next frame therefore
  depends on scheduling: a 0/1 frame jitter in damage, deaths, triggers (`mission::SendTrigger`), sounds.
- Why: Bevy's pattern is a handful of `SystemSet`s ordered once with `configure_sets(Update, (A, B, C).chain())`
  (bevy_ecs schedule docs; `examples/ecs/ecs_guide.rs`), with plugins putting systems `.in_set(..)`. There are 0
  `configure_sets` calls; 84 point-to-point `.before/.after` edges reference other modules' functions
  (e.g. `.after(crate::turret::setup)` ai.rs:1044, `.before(crate::mission::deaths)` in 5 files), which couples
  plugins to each other's internals.
- Idiomatic alternative: a public `GameSet` enum in a small core module: `Input, Player, Ai, Projectiles,
  Damage(FxSet), Deaths, Mission, Fx(Vm), Presentation`, chained once in FixedUpdate (A1). Plugins only name
  sets. Combine with A2's ambiguity detection.
- Payoff: deterministic frame-exact behaviour, plugins that do not import each other just to order. Cost: low-
  medium, mostly mechanical. Do now (it is the precondition for A1 and A2).

### A4. Pause is two separate switches set by hand in seven places; gameplay keeps running while paused. [now]

- What: pausing sets `NextState(AppState::Paused)` and separately calls `Time<Virtual>::pause()` at
  gamemenu.rs:903-904 and 1038-1039, pubscreen.rs:252-253, statscreen.rs:113-114, plus `vtime.pause()` in
  mission.rs:2015, cutscenes.rs:160, menu.rs:59, gameaudio.rs:792. Many gameplay systems have no run condition
  (fx.rs:214 FxSet, the whole AI chain ai.rs:1039-1045, charanim.rs:16, level.rs:271, decals, turret, vehicles
  mod.rs:37, boss/rocket_turrets deaths), so they run every paused / title / loading frame with dt = 0. Any per-call
  roll or counter advances during pause (not verified per system; a 300-frame pause playtest with an `expect` on
  the RNG state / grunt states would settle it).
- Why: Bevy's `examples/state/sub_states.rs` / `computed_states.rs` model this as `InGame` with an `IsPaused`
  sub-state, and side effects go in `OnEnter(Paused)` / `OnExit(Paused)` schedules (0 uses today). Gameplay sets
  get one `run_if(in_state(..))` on the set (A3), not per system and not by early return.
- Note: the world deliberately runs behind the title menu (levelreset.rs:20-22). Model that explicitly
  (`AppState::Title` + a computed `SimulationRunning` state true in Title-attract and Playing) instead of relying on
  "no run condition".
- Payoff: one place owns pause; cutscene / movie / pub pauses cannot desync from the state. Cost: low-medium.

### A5. Per-level lifecycle reimplements states, scoped despawn and `Local` reset. [phase 2, keep for now]

- What: levelreset.rs (402 lines) despawns "everything not pre-existing" (levelreset.rs:9-11, 249-290), re-inits
  60 `init_level_resource` registrations, empties `add_level_message` queues, and `PerLevel<T>` (levelreset.rs:71)
  resets `Local`s by generation (40 uses).
- Why: Bevy has `DespawnOnExit(state)` / `DespawnOnEnter` (bevy_state state_scoped.rs; `examples/ecs/state_scoped.rs`)
  and `OnEnter/OnExit` for setup/teardown; `Local` used as per-level state is a smell in itself (state that
  should be a component or a level resource). The restart-from-scratch approach also has to work around Bevy
  0.20's resources-as-entities (levelreset.rs:249-250 comment).
- Idiomatic alternative: a `LevelState` (or `AppState::InLevel` with a `LevelId` resource) whose `OnEnter`
  runs the spawners and whose entities carry `DespawnOnExit`; level resources inserted in `OnEnter` and removed
  in `OnExit`; restart = exit + re-enter. Keep `PerLevel` only for what truly must be a system local.
- Payoff: less custom machinery; any reused plugin "just works" with restarts. Cost: medium-high and the current
  code works and is tested (restart_parity). Phase 2, unless a new restart bug appears.

### A6. Synchronous file I/O and JSON parsing inside frame systems. [now, cheap]

- What: turret.rs:205-240 `setup` (an `Update` system) does `std::fs::read_to_string` + `serde_json::Value`
  parsing of skeleton and anim files for each new gun; ~96 `std::fs` reads in 33 files, several reachable from
  `Update` (e.g. level / mission / hud loading paths). A first-sight frame hitch and an I/O error is a silent
  `None`.
- Why: Bevy loads data through `AssetServer` + an `AssetLoader` (`examples/asset/custom_asset.rs`,
  `asset_loading.rs`), off the main thread, with hot reload (`hot_asset_reloading.rs`) and asset sources for mods
  (`extra_source.rs`; campaign.rs already implements an `AssetReader`, campaign.rs:108-129).
- Alternative: typed assets (`Template`, `PropertySet`, `AnimClip`, `SkeletonInfo`, `FxScript`) with loaders;
  level setup waits on `LoadState` in `AppState::Loading` (which exists). Payoff: no hitches, hot reload, mods via
  an asset source, errors surfaced. Cost: medium. Do the hot-path ones (turret/defence/goliath setups) now.

### A7. Hidden per-frame costs. [now, cheap]

- `std::env::var("AIDBG")` inside per-grunt loops (ai.rs:1498, 1603, 1700, 2790, 3302, 3822, 4024, 4052; 28 sites
  outside main.rs): an OS environment lookup with a lock each call. Use `tracing` targets
  (`debug!(target: "ai", ..)` filtered by `RUST_LOG`/`LogPlugin::filter`) or read once into a resource.
- `GameData::template_props` clones the whole JSON map per call (gamedata.rs:112-122, 38 callers);
  `instance_props` clones again. FX instances clone their whole `Vec<serde_json::Value>` script
  (fxvm/object.rs:636) and each op's args are cloned out of `Value` (fxvm/script.rs:276-277).
- Payoff: perf at the 4K/100 fps floor; cost low.

---

## B. Structure and style (reuse, moddability, testability)

### B1. One binary crate; mechanics are not separable. [phase 2 start, plan now]

- What: 63 modules in `main.rs`'s crate; no `lib.rs`, no workspace. Coupling is high: twiglet.rs imports 19
  sibling modules, camera.rs 12 (including playtest and cheats), sticky.rs 10 (including ai and player); `crate::playing`
  and `AppState` live in main.rs.
- Why: Bevy itself and its ecosystem are crates of plugins; the human's goal (reuse mechanics in other games)
  needs a dependency boundary the compiler enforces.
- Alternative: a workspace: `engine_core` (time/sets/states, physics wrapper, levelreset, audio bank, fx VM,
  input, materials), mechanic crates (`sticky`, `camera`, `ai_grunt`, ...) depending only on core + their own
  data types, and the `and_game` crate wiring Armed-and-Dangerous data. First step (now, cheap): move `AppState`,
  `playing`, `GameSet`, `Launch` out of main.rs into a `core` module and stop new `crate::player`-style imports
  in mechanics (communicate via messages/components).
- Payoff: reuse, faster incremental builds (several agents build in parallel), integration tests. Cost: high
  overall; staged.

### B2. ai.rs (5,052 lines) and `think` (1,200 lines, 14 parameters in tuples). [now, block by block]

- What: `think` is ai.rs:2598-3801. One `Grunt` component of ~190 lines of fields (ai.rs:729-922) carries
  `Option<Cover>`, `Option<Jumper>`, `Option<Monk>`, `Option<Gun>` whose presence is asserted by `expect`
  ("cover state without a cover" ai.rs:3342, "gunner state without a gun" 3453, "a jumper in flight" 3644, ...).
  Snapshot vectors (`TrenchView`, `GunView`, jump marker views, ai.rs:2635-2660) are rebuilt every frame to work
  around query borrow rules.
- Why: Bevy's model is small components and focused systems; role data as separate components (`Jumper`,
  `Monk`, `Gunner`, `InCover`) lets each role be its own system / plugin and makes "state without data"
  unrepresentable (or put the payload in the enum: `Mind::Cover(CoverState)`). Disjoint queries / `ParamSet` /
  `Query::get_many_mut` replace most view copies.
- Alternative: split by role into ai/ (the folder exists: bully, ic, jumper, monk, squad) with one system per
  role over `(Grunt, &mut Role)`; the shared per-tick decisions stay in a small core. Keep the call order the
  original uses (A2) by chaining the role systems.
- Payoff: readable, testable per role, reusable AI pieces. Cost: high; risk of behaviour drift, guarded by the
  oracle scenarios. Start now with one role per session (jumper and monk are already half separate).

### B3. Untyped property bags instead of typed, reflected data. [now for new code; migrate in phase 2]

- What: `GameData { props: HashMap<String, Map<String, Value>> }` (gamedata.rs:23-27) and `Props::f/i/s` with the
  default at each call site (399 calls); behaviour dispatch by string (`p.s("behavior") == Some("goliath")`
  goliath.rs:168, defence.rs:220, army.rs:296, level.rs:577-680 match on behaviour strings). A typo or a mod's
  wrong type silently yields the default; the schema lives in scattered call sites. 0 `Reflect` derives.
- Why: Bevy data-driven config is `#[derive(Asset/Resource/Component, Reflect, Deserialize)]` structs with
  `#[serde(default)]` (`examples/reflection/*`, scene serialization), giving one schema, validation at load,
  inspector/editor support and `bevy_remote` access.
- Alternative: per-behaviour typed structs (`TurretProps { pivot_bone: String, emitter_bone: String, ... }`)
  deserialized once from the merged property set at spawn, stored as a component; a behaviour registry
  (`HashMap<BehaviorName, fn(&mut EntityCommands, &Props)>` registered by each plugin) instead of each plugin
  scanning all placed objects (B4).
- Payoff: moddability (the human's stated goal), errors at load, faster. Cost: medium per subsystem; do it as
  each subsystem is next touched.

### B4. "Setup" systems poll every frame with a `PerLevel<HashSet<Entity>>` done-set. [now, cheap]

- What: turret.rs:205-215, and the same pattern in army, defence, goliath, icobject, pickup, shrubpatrol,
  vehicles mod/boss/rocket_turrets (10 files): query all `InstanceId` entities `Without<Turret>` each frame, read
  `Name`, look up the template, skip if not mine, remember in a HashSet.
- Why: Bevy offers `Added<InstanceId>` filters (`examples/ecs/change_detection.rs`), observers on
  `On<Add, Placed>` (`examples/ecs/observers.rs`), or component hooks (`examples/ecs/component_hooks.rs`). Better
  still, the level spawner inserts a typed behaviour marker (B3) and the plugin reacts to `On<Add, TurretSpec>`.
- Payoff: no per-frame scans, no HashSets of stale entities, plugin decoupled from level internals. Cost: low.

### B5. Test instrumentation inside gameplay plugins. [now for new code]

- What: 33 files take `Option<ResMut<playtest::PlayState>>` and write a `serde_json` map (e.g. ai.rs:1050
  `log_cover`, army.rs:901, goliath.rs:296, icobject.rs:332), and the logging systems sit inside gameplay chains
  (ai.rs:1041: `log_cover, log_ai, squad::log_squads, ic::log` between `dismay` and `grenades`). 112
  `info!("EVENT ..")` / `AIDBG` lines form a log-parsing protocol.
- Why: a reusable plugin should not depend on the test harness. Bevy's idiom is that tests/tools observe the
  world (queries, `World::run_system_once`, messages, `bevy_remote` + `Reflect`) rather than the code writing
  into a test resource.
- Alternative: each plugin exposes state through its public components/resources (and later `Reflect`); the
  playtest plugin owns all `*_state` systems in its own set after gameplay. Events of interest become public
  `Message`s (`GruntDied`, `TriggerSent`) the playtest plugin listens to.
- Payoff: plugins portable to other games; the playtest protocol unaffected. Cost: low-medium, mechanical.

### B6. `too_many_arguments` + tuple bundling instead of `#[derive(SystemParam)]`. [incremental]

- What: 105 allows; tuples like ai.rs:2610-2627, player.rs:1537 (`(rtime, topsy, mounted, mut noise, mut bombs,
  mut rng, mut sounds): (...)`). Only 5 SystemParam derives (one is levelreset `PerLevel`).
- Why: `examples/ecs/system_param.rs` / `custom_query_param.rs`: named `SystemParam` structs (e.g. `AiWorld {
  physics, markers, squads, data }`, `AiOutput { sounds, fx, triggers, arrivals }`) and `#[derive(QueryData)]`
  for wide queries make signatures readable and reusable across systems.
- Payoff: readability, reuse. Cost: low per system; do it while touching a system, starting with think/companions/
  projectiles/weapons.

### B7. Resources as globals written from many plugins. [phase 2, partly now]

- What: `HudState` is written by 14 systems in 9 modules (ai companions/team_orders, defence, gamemenu, hud,
  main.rs hud_state, mission sites/pub_door/messages/outcome, options, player, turret); `PlayerStats` by 6
  (player, sticky, topsy, turret, defence). main.rs:749 `hud_state` is a game-to-HUD adapter living in main.rs.
- Why: Bevy's guidance: send `Message`s for one-off happenings, keep derived UI state owned by the UI plugin
  (`examples/ecs/message.rs`). Many writers to one resource also serialise the schedule.
- Alternative: gameplay emits messages (`ShowObjective`, `CrosshairMode`, `AmmoChanged`) or exposes components;
  the HUD plugin alone writes `HudState`. Cost: medium; do the new HUD pieces this way.

### B8. `Option<Res<..>>` (139) and `.single()` + `else return` instead of fallible params. [incremental]

- `Single<D, F>` / `Option<Single<..>>` / `Populated` (bevy_ecs query.rs:2995, 3037; `examples/ecs/fallible_params.rs`)
  skip the system cleanly; `run_if(resource_exists::<Mission>)` on the set replaces `Option<Res<Mission>>` checks.
  Result-returning systems (`-> Result`, `examples/ecs/error_handling.rs`; one use, menu.rs:51) give `?` and a
  single error handler instead of `expect` / silent `unwrap_or_default`. Cheap, readability; low risk.

### B9. `NonSend<Physics>` everywhere. [phase 2, with the physics boundary]

- 65 uses; every system touching physics is pinned to the main thread and serialised. Jolt bindings may not be
  `Send`, but a thin `Resource` wrapper (`Send + Sync` if the binding allows it behind a `Mutex`, or a
  command-queue API: systems write `RaycastRequest`/`SetVelocity` components, a single physics system applies
  them) is how avian/rapier present physics to Bevy. Payoff: parallelism, a physics API the human can reuse and
  swap. Cost: high; ties into the Havok -> open-source physics plan. Phase 2.

### B10. FX VM keeps a parallel world in a resource. [phase 2]

- `FxVm { objects: HashMap<Entity, FxObject>, instances: Vec<Instance>, time, rng, out: Outbox ... }`
  (fxvm/mod.rs:64-83) with its own clock and an outbox later flushed to messages. Idiomatic: `FxObject` and
  `FxInstance` as components on entities, `Time` from the schedule, `MessageWriter`s directly; scripts decoded once
  into a typed `enum FxOp` (serde tagged enum) at load (A7). Cost: medium-high; the VM is tested and works.

### B11. Rendering. [mostly fine]

- Good: `GameMaterial` is a proper `AsBindGroup` `Material` with embedded WESL (gamematerial.rs:69-121), the sky
  uses `ExtendedMaterial` (graphics/farclip.rs:24-51), fonts a `Material2d`; profiling uses `RenderApp` properly.
- Not idiomatic: particles and streamers rebuild CPU meshes every frame through `Assets<Mesh>::get_mut`
  (fxvm/particles.rs:332-350, streamers.rs:242; a zero-area triangle works around empty meshes) and HUD parts
  rewrite meshes each frame (hud.rs:973, 1720). Bevy's pattern for many sprites/particles is GPU instancing
  (`examples/shader_advanced/custom_shader_instancing.rs`) or a storage buffer. Phase 2 (perf at 4K).
- main.rs:497-517 builds the landscape material and a 1x1 white image inside the level setup: move to
  `GameMaterialPlugin` (a `Startup` default image) so the material plugin is self-contained.

### B12. main.rs is an application and a dumping ground. [now, cheap]

- 860 lines: `setup` (main.rs:474.., ~15 asset/resource params, spawns the level, landscape, lights, fx models,
  sound bank, markers), `play_state` / `hud_state` adapters (680, 749), bench scenarios, and three debug closures
  registered inline (main.rs:401, 409, 447). Move `setup` into `LevelPlugin`, the adapters into the playtest and
  HUD plugins, debug closures into a `DebugPlugin` behind a feature. Bevy apps' main is plugin wiring only.

---

## Priority list

| # | Item | Kind | When |
| --- | --- | --- | --- |
| 1 | A3 one `GameSet` order via `configure_sets` | bug risk | now (first) |
| 2 | A2 shared RNG + ambiguity detection on | determinism | now |
| 3 | A1 simulation in `FixedUpdate` | bug risk (frame-rate dependence) | now, after 1 |
| 4 | A4 pause as (sub)state + OnEnter/OnExit, set-level run conditions | bug risk | now |
| 5 | A7 env-var lookups, JSON clones in hot paths | perf | now, cheap |
| 6 | A6 I/O in frame systems -> AssetLoader | hitch / mods | now (hot paths), rest phase 2 |
| 7 | B4 setup polling -> Added / observers / behaviour registry | structure | now, cheap |
| 8 | B5 playtest writes out of gameplay plugins | reuse | now for new code |
| 9 | B3 typed, reflected property structs | moddability | per subsystem |
| 10 | B2 split ai.rs / think by role components | structure | block by block |
| 11 | B12 main.rs to plugin wiring only; B1 workspace plan | reuse | start now, finish phase 2 |
| 12 | B6/B8 SystemParam derives, Single, Result systems | style | incremental |
| 13 | B7 HUD fed by messages | structure | new code now |
| 14 | A5 levelreset -> states + DespawnOnExit | structure | phase 2 |
| 15 | B9 physics off NonSend, B10 FX VM as entities, B11 instanced particles | perf/structure | phase 2 |

Not reviewed in depth: menu/egui code (gamemenu.rs, options.rs), audio mixing internals, movie decoding, skinning
(skin.rs). Claims marked "not verified" (A4's paused-frame effects) need a playtest to confirm.
