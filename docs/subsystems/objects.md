# Object model (the Object base class, default objects, the factory, triggers)

Recovered code: `src/game/game_03.cpp` (Object base class vtable 0x005b0b20, DefaultGameObject 0x005a2bf0 /
0x005ab650, RocketTowerBuilding 0x005a87c0, ObjectSystem 0x005a826c) and `src/game/game_04.cpp` (the factory
ObjectSystem_CreateObject, constructors, Object_SendTrigger, class descriptors, pub / pickup virtuals). Related:
damage.md (filters per class, what stops a bullet), mission.md (the lev_1_m1 classes and the trigger graph),
fx-vm.md (death fx, area damage). Remaster: `engine/src/level.rs` (spawn, husks, triggers, lists) and
`engine/src/fx.rs` (Destructible, damage, deaths), ported 2026-10-14 (branch agent-objects).

## The Object (base class)

| Field | Meaning | Set by | Remaster |
|---|---|---|---|
| +0x28 | reference count | ReleaseRef pattern, 0x0045a450 | entity lifetime |
| +0x34, +0x424 bits 1/2 | parent (attached) | Object_Attach 0x004539f0, Object_Detach 0x00453b20, Object_GetParent 0x004539d0 | Bevy hierarchy (level.rs attach_weapon) |
| +0x2d8 | the lists the object is in | ObjectList_Link 0x004546d0 / Unlink 0x00454750 | components / queries (below) |
| +0x2e8, +0x32c | world / scaled world matrix (rebuilt from the body, 0x00457c20) | 0x00457d60.. 0x00457f80 | Transform / GlobalTransform |
| +0x37c, +0x424 bit 0x10 | scale | Object_SetScale 0x004535d0 | placement scale ignored (the original draws scaled placements full size, remaster-engine.md) |
| +0x390 | tag | Object_LoadBaseProps 0x00451360; 0x00453290 | read from Props where used (mission.rs tags, obj_tag) |
| +0x394 / +0x396 | spawn mask word / level table id | ObjectSystem_CreateObject 0x0045f020 | InstanceId (every instance spawned; the mask is unused) |
| +0x3a0, +0x3c0 | fx_death, husk (default "null"); husk1..husk9: one of n at random, (rand() * n) >> 15 | 0x00451360 | Destructible.fx_death, Placed.husk (level.rs pick_husk) |
| +0x3ec | shadow / decal flags (shadow_enable 3, shadow_receive 0x10, decal_receive 8) | 0x00451360 | level.rs spawn (shadow casters, decal receivers) |
| +0x414 | class template | 0x0045f020 | Placed.template |
| +0x424 bit 4 | hidden (visible 0) | 0x004536b0 | model not spawned (level.rs `invisible`) |
| +0x424 bit 8 | removed | Object_Remove 0x004547b0 | despawn |
| +0x424 bit 0x200 | a ray hit stops here: Ray_Cast walks a hit on an attached object up its parents (0x004539d0) until one with the bit (set by the AIs' init 0x004247f0, cleared by ICObject_Load, the ladder grunt, the critters' load 0x0048f140) | 0x004559c0 / 0x004559f0 | not modelled: a hit names the body's own entity (feature object-collision) |
| +0x454 / +0x458 | health / max_health (default: the current +0x458) | 0x00451360 | Destructible.health / max_health |

Health and death (game_03.cpp:2059-2155): Object_TakeDamage 0x004555e0 (base damage slot vt+0xd0): a living object
(health > 0) loses the record's +0x14; at <= 0 it calls the death slot vt+0xd4; otherwise the health is capped at
max_health (negative damage heals, never past the maximum). Object_IsDead 0x00454780: vt+0xd8 false or health <= 0.
Object_Kill 0x00455640 (base death slot): vt+0xdc (removal) then Object_SpawnHusk 0x00455660: fx_death at the
object's matrix unless "null", and the husk created through the factory from its template alone (no placement
values, level id 0). Remaster: `Destructible::take_damage` (fx.rs) and `dying` / `husks`.

## DefaultGameObject (objects without a behaviour)

Load 0x0044fd20 (game_03.cpp:432) reads, with defaults: takes_damage 0 (flag 1), takes_bullet_damage 0 (2),
takes_crushing_damage 1 (0x1000), blocks_projectiles 0, no_collide 0 (8), collide_barrier 0 (4),
receive_trigger_kill / _deactivate / _start_blocking 0 (+0x4c4 / +0x4c8 / +0x4cc), send_trigger_on_death 0
(+0x4e8), impact_force **2010** (+0x4e4), area_damage / damage_radius_inner / outer 0, score_value 0, gravity 1,
pinned 1, unpin_health_pct 0, on_hit_damage, crush (crush_speed 10), on_hit_fx "name/count/interval",
play_boulder_sound, on_hit_death, on_death_player_health, on_death_unlock_level, death_timeout, block_path 1,
object_block, platform 1, attach_audio_loop.

Lists (DefaultGameObject_Load game_03.cpp:526-581): a collide barrier gets collision filter 0x40 (0x4000 while it
waits for receive_trigger_start_blocking) and no lists; otherwise, unless no_collide, it joins +0xe9c (collidable:
the crosshair's ray) and, when it takes damage or blocks projectiles, +0xe90 (bullets), +0xe94 (area damage), +0xea0
(sight) and, taking damage, +0xea4. Remaster: level.rs `in_bullet_list` / `collider_lists` (physics.no_bullets); a
no_collide default object gets no collider at all (the big red arrow 258: no longer stops the crosshair).

Damage 0x00450da0: ignores records from itself, while dead / killed (0x10080), bullets (0x400) without
takes_bullet_damage, crushing (0x4000) without takes_crushing_damage; applies with takes_damage. No source filter
(fx.rs `DClass::Default`). Death 0x00450ee0, once (0x10000): score_value to Kills (>= 0) or Accidents,
send_trigger_on_death (Object_SendTrigger), area_damage (0x0047f020, source the object); the next update 0x004505a0
calls Object_Kill (fx_death, husk). Remaster: fx.rs `dying` (same order, same frame), mission.rs `deaths`, level.rs
`husks`.

Trigger handler 0x00450fb0 (vt+0x38): kill -> vt+0xd4 (death), deactivate -> vt+0xdc (removal: no fx, no husk, no
trigger, no score), start_blocking -> filter 0x40 and list +0xe90; the matched field is cleared; it returns false.
Remaster: level.rs `ObjectTriggers::receive` / `object_triggers`; kill is delivered by mission.rs `Action::Destroy`
(mission.json's "destroyed" edges are exactly the receive_trigger_kill fields).

Windows: BuildingObject 0x0040ad80 makes a village_window default object at each ref_win_0N bone and links it to
the building (0x00450980, +0x520); the window's update 0x004505a0 calls its own death slot once the building is
dead, so every remaining pane bursts with fx_explosion_village_window when its house dies. Remaster: level.rs
`husks` puts each live window of a dead building on the death path (was: silently removed).

### lev_1_m1's default objects

| Object | Properties that matter | Remaster |
|---|---|---|
| oilpump (221, 224, 237, 238, 241) | takes_damage, takes_bullet_damage, 300 health, area_damage 150 (40..80), impact_force 35000, husk oilpump_d, score 50 | Destructible Default |
| boom_island 131 | takes_damage, 150 health, husk boom_island_d, send_trigger_on_death 301 (fx_penguin_debri at bb_ear 218) | Destructible Default, mission.rs deaths |
| v_archway_a (87, 144) | takes_damage (no bullets), 300 health, husk v_archway_a_da | Destructible Default |
| v_prison_gate 247 / 248 | takes_damage 0 (instance), receive_trigger_kill 901 / 556, 750 health, husk v_prison_gate_d | killed by trigger only |
| guard_alarm 33 | takes_damage, takes_bullet_damage, fx_explosion_siren, no max_health | Destructible, health 100 (**guess**, see open questions) |
| bigredarrow 258 | no_collide, receive_trigger_kill 900 | no collider; removed by trigger 900 |
| w_l1_m1_01 / 02 | visible 0, collide_barrier, blocks_projectiles 0, receive_trigger_start_blocking 0 | invisible, solid, bullets pass |
| village_window | takes_damage, takes_bullet_damage, 5 health, fx_explosion_village_window, on_hit_death | Destructible; dies with its house |

Multi-stage husks: a two-floor house's husk is itself a destructible building: v_house_2f_a -> v_house_2f_a_da (300
health, fx_explosion_village_house_2) -> _db -> _dc (no damage), the same for 2f_b. The remaster now gives a husk its
own Destructible, collider lists, bounds and husk (level.rs `husks`); a husk carries the `Husk` marker so the
placed object's send_trigger_on_death is not sent again (mission.rs `deaths` runs after fx.rs and before `husks`).

### Movable objects (movable.rs, 2026-10-17)

Boulders (boulder_large / _medium / _small / _tiny on lev_1_m1_b, 2_m1, 2_m2, 2_m3, 3_m1) and the l5 rocks: `movable`
with `pinned` (default 1) held still; a gravity other than 1 unpins, unpin_health_pct > 0 pins until the health falls
below that share (0x00450da0); a husk stage is a new object (large -> medium -> small -> tiny: unpinned). Unpinned: a
convex hull of the collision model (scaled) with the property set's friction, restitution and gravity factor
(0x00450fa0), the speed trap's linear cast (0x00452790). Contacts (0x00450af0, with the velocities before the step):
crush when |v - v_other| and |v| reach crush_speed and the other is slower than it or the relative direction against
its own is below -0.2 ([0x005bce80]); on_hit_damage hits everything it touches (crush clears it at load); the
records (0x004505a0): a crush takes the victim's whole health (characters; a movable only with
takes_crushing_damage), else direct_damage, with the mover's normalised velocity and direct_damage_force as the
impulse, which only Roman's handler reads (his knockback, PlayerObject_OnHit 0x0047afa0; no shipped movable reaches a
living Roman with it: every damaging one crushes). The landscape, at the contact point (+0x49c): on_hit_fx
"name/count/interval", fx_boulder_impact below -50 ([0x005bce7c]); imp_boulder_roll_lp while the horizontal speed
squared is above 16 ([0x005bce84], read from the binary). A dead object (0x80) skips all of these. on_hit_death breaks
a window pane at any body's touch. Playtest boulders_l2m1 (a push step sends a boulder rolling).

## The factory (ObjectSystem_CreateObject 0x0045f020)

The behaviour name (or the template name) is hashed into the behaviour table (0x0045df60, 197 names) to a class id;
ids 1..0xaa select an allocation size and a vtable over Object_Construct 0x004511d0 or a dedicated constructor
(game_04.cpp kCreateCases); unknown ids make a DefaultGameObject; id 161 is the player (stored in Game+0x748). Then
+0x394 = spawn mask, +0x396 = level id, +0x414 = template, Load (vt+0x18) and, outside a save restore, vt+0x20
(0x0045d9b0: e.g. RocketTowerBuilding_SpawnTurret). The level's objects come from 0x0045d930 / 0x0045d7a0 (every
table entry with the pass's spawn bit). lev_1_m1's classes are listed in mission.md ("Placed objects"). Remaster:
the `behavior` match in level.rs `spawn` (grunts: ai::make_grunt; Q / Jonesy: ai::Companion; trotter: the player;
penguin; buildings, trees, kegs: Destructible classes; the rest: Placed with a collider).

## Triggers

Object_SendTrigger 0x00461870 offers the id to every object's vt+0x38 (list +0xe70) and stops at the first handler
returning true; every handler on lev_1_m1 returns false (mission.md), so it is a broadcast. Trigger 0 only logs
"sent 0 trigger BAD BAD BAD". Remaster: mission.rs dispatch (SendTrigger messages) plus level.rs `object_triggers`;
the state log's `triggers` counts every trigger sent (`"triggers.301": 1`).

## Tests

- Unit (`cargo test --release`, regress.py entry `unit`): fx.rs `take_damage_kills_once_and_caps`,
  `damage_filters`, `blast_falloff`; level.rs `trigger_receive_is_one_shot`, `husk_pick`,
  `default_objects_in_the_bullet_list`.
- Playtest engine/playtests/objects1.json (14 s, 22 expects, regress.py): a bad guy's blast hurts the oil pump 224
  (300 -> 250), not the prison gate 247 (takes_damage 0) nor house 174 (badguy_damage 0); trigger 556 kills gate 248
  (husk v_prison_gate_d; one death so far besides a window the house blast broke); house 174 goes _da (300 health,
  its 2 remaining windows die with it: 5 deaths) -> _db -> _dc; boom island 131 dies and sends 301.
  State log fields added: `objects.<id>` {template, husk, health, max_health} or {removed}, `triggers.<id>`,
  `object_deaths`.

## Queue disposition (python tools/port_queue.py game --all, game_03 / game_04)

Ported or replaced, cited in the engine source: the readers (gamedata.rs Props), Object_Construct / Destruct,
Object_Update, the lists, attachment, matrices and scale, visibility, health getters, Object_TakeDamage /
Kill / SpawnHusk / Remove, the DefaultGameObject virtuals used on lev_1_m1, the factory and the AI / player
constructors used on lev_1_m1, ObjectSystem construction / search / teardown, the force action pool (Jolt
impulses), Object_SendTrigger, the pub doorway / exit, the ui_ear Load, PickupObject_Update (its fade not ported).

The functions left in the queue, each with its reason, are rows of docs/port/game.csv (address, disposition,
reason; `python tools/port_queue.py game --all --explained` lists them): collision reactions, gravity getters,
base-class answers, the alpha override, empty virtuals, container callbacks, the unidentified +0x424 bits, the IC
spawn, class 98, per-class constants, RTTI getters, constructors of classes not on lev_1_m1, the pub markers,
the turret paths turret.rs does not need.

Saves (savegame.rs, 2026-10-15): the husk swap is level.rs `StageSwap`, shared by a death (`husks`) and a
save's restore (`RestoreStage`): a placed object is put straight at its saved stage (a house saved at _db loads
at _db) without a Died message, so its fx_death, score and send_trigger_on_death are not repeated; the windows
of a destroyed building go with it, a shot-out window is saved as (building id, window index).

## Open questions

- **guard_alarm 33's health.** p_guard_alarm has no max_health; Object_LoadBaseProps defaults it to the object's
  own +0x458, which Object_Construct does not clear: whatever the heap block held (PMS_heapAlloc 0x004e07f0 flags
  0x10, zero fill not established). If it is 0 the siren cannot be destroyed (Object_TakeDamage ignores an object
  at health 0) and Object_IsDead reports it dead. The engine keeps 100 (labelled guess in level.rs). Settle with a
  hook read of +0x454 / +0x458 of object 33 in the original.
- **no_collide bodies:** a default object with no_collide skips its collision filter set-up; the engine gives it
  no collider (guess: such a body collides with nothing).
- **collide_barrier filter 0x40** (the invisible walls): which groups it stops (player only? AI?) is not traced;
  the engine's walls are solid to everything but bullets.
- **0x00456180's 9 calls** in the coverage runs: which lev_1_m1 objects. (**Class 98** is settled, 2026-10-18: the
  behaviour table 0x0045df60 maps id 98 to "ui_listbox" (string 0x005b03cc): the menus' list boxes.)
- **Saves and multi-stage husks:** settled 2026-10-15 (see "Queue disposition": RestoreStage).
