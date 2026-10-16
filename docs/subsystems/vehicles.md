# Vehicles and big units (rocket towers, rocket buds, zeppelins, saucers, the colossus, the forge cannon)

Static recovery and engine port, 2026-10-15 (branch agent-vehicles). No run of the original reached any of these
classes (every function is "not executed in recorded runs"): the behaviour below is from the recovered code; the
playtests check the engine against it, not against captures.

Engine: `engine/src/vehicles/` (`VehiclesPlugin`), the forge cannon in `engine/src/turret.rs`.

## Where they are

| Unit | Template (class, behaviour) | Levels | Class / vtable |
|---|---|---|---|
| Rocket tower | v_rockettower (no behaviour: the template name is a class, id 82) | lev_2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 | RocketTowerBuilding 0x005a87c0 + RocketTowerTurret 0x005a8b88 (made at load) |
| Rocket bud | v_rocketbud (rocket_bud_turret, 83) | lev_2_m2, 3_m1 | RocketBudTurret 0x005adde8 |
| Zeppelin | mini_zeppelin (zeppelin, 85) | lev_2_m1, 4_m5, 5_m2 (24), 5_m3 | ZeppelinObject 0x005af500 |
| Saucer | saucer (saucer, 86) | lev_2_m1, 2_m3 | SaucerObject 0x005a5e98 |
| Colossus | colossus (colossus, 71) | lev_1_m3, 5_m4 | ColossusObject 0x005aa238 |
| Forge cannon | forge_cannon (forge_cannon, 79) | lev_1_m1_b, 1_m3, 2_m1, 2_m2, 2_m3, 5_m1, 5_m2, 5_m3, bonus_m1 | ForgeTurret 0x005a1050 |

Class ids from the behaviour table 0x0045df60 (names resolved from the exe: v_rockettower 82, zeppelin_turret 80,
ui_forge_crosshair 126). Not placed on any level: zeppelin_turret (ZeppelinTurret 0x005ac088, a mountable gun on a
zeppelin). v_rockettower2 has no class of its own (a default object). The flak (lev_5_m5) is an object_ic (another
port's).

## Behaviour (engine module, original functions)

- **Rockets** (`rockets.rs`): CaptainRocket 0x004829e0 / 0x00482b40 (max_speed x 3; homing unless dumb_fire: every
  vehicle missile is dumb_fire), RocketTowerProjectile 0x00480920 / 0x00480a20 (start_speed along the nozzle, homing
  after homing_delay_duration, up to max_speed over full_speed_distance, let go inside stop_homing_proximity when
  heading at the target), RocketBud / ForgeCannon shells 0x00480db0 / 0x00480ec0 (lobbed with 0x00471a00 keep = 1:
  speed x 3 across, the forge's speed the horizontal distance + 5). Hit 0x0047eae0: area damage to all but the object
  hit, direct damage = damage + area_damage (0x0047eec0), fx_end, fx_end_material + the surface material. Pass-by
  sounds once within 50 / 70 of Roman.
- **Rocket tower** (`rocket_turrets.rs`): the turret at the tower's ref_turret (0x004510b0), the two kill each other
  (0x0049c540, 0x00451160). Out of range (active_range 450): spins up (spin_accel to spin_max_speed, the turn anim's
  fraction a second) with "wpn_rocket_tower_turn_lp". In range: spins down; stopped and ready (recycle_time 6), a
  sight check a second from 10 above the turret; seen (or within blind_fire_duration of it): four rockets, one per
  nozzle, homing on Roman's chest (0x0049c6a0, 0x0049c610).
- **Rocket bud**: in active_range (1000), ready (recycle_time 6 from the load), the same sight check: a shell lobbed at
  Roman's feet spread by +-inaccuracy_range in x and y, the muzzle fx (0x0049cbe0, 0x0049cab0).
- **Turret bosses** (`boss.rs`, `gunnery.rs`, `mover.rs`): see the module doc of boss.rs. Waking by trigger (or sight),
  the IC path flight (state 0x64, ic_loop), the targeting (0x00435b50), the weapon turn and volleys (0x0042ecf0,
  0x00435940), the colossus's marker walk and stand (0x0042a660, states 0x10e..0x110), bullet_scale (0x00436600),
  the path notes (deactivate, anim_loop / anim_intro, captain drops), Roman's zeppelin remark (0x00436260).
  The path flight is a chase, not a playback (2026-10-16, V1): state 0x64 (ai_06.cpp:1900) hands Object_SetTransform
  the pose's rotation with the object's current position and sets the gap to the pose as the body's velocity
  (0x00458510), so a saucer or mini zeppelin starts from rest and runs one second of path behind (lev_2_m1 saucer 285:
  11 units in its first second; the model reproduces the original's trace within 0.03 units over 9 s, the velocity
  applied on the next physics step). ic_loop_snap puts the object back at the start each loop; the end stops the body
  where the chase left it (0x0042c0e0 with 1).
- **Forge cannon** (`turret.rs`): the stig gun's mount / aim / leave with two pivots (0x00499fd0), its sounds, one
  lobbed forge_cannon_bullet a press (recycle_time 1) to the crosshair's point, the one-shot muzzle fx, the recoil
  anim, the Camera01 view aimed at Camera01.Target. Empty, it takes no damage (0x00498400). Grunts do not look for it
  (ai_can_use off: the AI's turret search is the stig gun's), but turret-mode grunts start on it by obj_tag (ai.rs,
  merged from agent-infantry); their shells fly through `vehicles::rockets::shoot` (a one-line hook in ai.rs's gunner
  shot), lobbed as the player's.

## Data and tuning

Every unit number is its property set's (fire ranges, delays, counts, speeds, health, score, triggers, tags). The
code constants are `VehicleTuning` (weapon turn rate 4, retarget 1 + 1 s, colossus range 400, turn 360 degrees / s)
with scales for later settings (health, damage, fire pauses; 1 = the original). The AI bosses' health takes the
difficulty's bad-guy factor (Ai_InitGeneric 0x004247f0), as the grunts' should (gamedata.rs notes it unapplied).

## Tests

engine/playtests/vehicles_*.json (regress.py SUITE), each on its own level ("scene"):
- vehicles_rocket_tower (lev_3_m1): spinning out of range, stopped / seen / a volley of 4 at Roman in range, 3-4
  direct hits, a volley every 6 s, the turret hit (750 -> 450), both turrets and their towers destroyed (husks).
- vehicles_rocket_bud (lev_3_m1): sight, a shell, it lands within its blast of Roman, the next after 6 s, hit and
  destroyed (husk v_rocketbud_d).
- vehicles_zeppelin (lev_2_m1): waiting, trigger 1800, flying its path (position at 10 s), targeting Roman and
  firing, hit (2000 -> 1500), destroyed.
- vehicles_saucer (lev_2_m1): flying from the start (trigger 1000), its path, targeting Roman and firing, destroyed.
- vehicles_colossus (lev_1_m3): waiting, trigger 811, walking to its markers, targeting Roman, missiles hitting him,
  standing after 3 volleys, hit, destroyed (husk colossus_d).
- vehicles_forge_cannon (lev_2_m1): the prompt, mounting, turning, a shell fired and landed, the recycle, leaving.
- Unit tests: gunnery.rs (volley rhythm, aim error), rockets.rs (the lob lands on its target), nodes.rs, mover.rs.

## Open questions and debt

- The colossus walks straight between its markers (the original routes on its grid, 0x00470aa0 / 0x00470c30). Plan:
  routes from nav.rs. Its kick (kick_damage_*: the GenericAI melee) and its drivable side (player_bone_name,
  camera_start / camera_end, fwd_max_speed: the player in a colossus) are not ported (no level here puts Roman in one).
- A hit makes Roman the boss's target (0x00435fa0 takes the record's source; the engine's damage path does not carry
  it). Plan: a source on Destructible's damage.
- The zeppelins' captain drops (create_object_ic / create_rocket_captain_IC, the note handler 0x00409140 at 0x00409abf)
  make a captain on the ground under the ref node; the rappel IC anims are the infantry's (plan: IC movers for
  characters). Since 2026-10-16 (agent-complete-b): the mini zeppelin's model is rigid, so its anim_intro anims
  (mini_zeppelin_attack_village) run as a note clock (boss.rs RigidAnimNotes) and their drop notes fire at the anim's
  ref_repel nodes; create_rocket_captain_IC makes a rocket captain (0x00434ac0) with fire range 50..100 (0x00434a80),
  at most 12 dropped troops alive unless the level starts in the base turret (0x00409b0e, 0x0044e600); the mover's
  notes give its landing orders (ai.rs drop_orders: freefall_attack_village LO HI -> the priority targets +0x858 /
  +0x85c, AI_FindTarget's first step: the houses of tags LO..HI). A placed object as a target is seen when the sight
  ray reaches its collision (0x00426fd0). lev_5_m2: the original loses its first village's five houses at 73..88 s
  standing still; the remaster now at 69..83 s (oracle work/oracle-complete-b/l52_long; before: never).
- The forge cannon's view from Camera01 sits on the barrel's axis behind the breech: the screen is filled by the
  cannon's back (vehicles_forge_cannon.mounted.png). The original's view (Game+0xac8 = 3) needs a capture: is the gun
  hidden, or another camera used (the "ui_forge_crosshair" object class 126, a landing-point marker, is not ported)?
- The rocket buds hurt themselves when a shell lands within 50 of them (area damage reaches every object but the one
  hit: faithful to 0x0047f020 as read; check in the original).
- Captain rockets in flight can be shot down (CaptainRocket max_health 1, 0x00482d60): not ported.
- Grunts take the vehicles' blast damage (the engine's fx.rs blast has no same-team filter; the original's character
  filter is not traced).
- Gravity of the lobbed shells: 64.348 (the launch's own constant); the straight rockets fly without gravity (guess).
- Visuals not compared with the original (no captures): the missiles' facing, the zeppelin's path orientation (the
  path's root rotation matches its travel direction in the data), the tower turret's spin direction.
