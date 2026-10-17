# Grunt AI (lev_1_m1)

Status 2026-10-14: the states lev_1_m1's grunts reach are traced and ported (section 13: classes, anim tables,
stance, hit reactions, lost targets, marker splines, alarm troops, noises, taunt, the captain); every function of the
ai port queue is ported or explained in docs/port/ai.csv (`python tools/port_queue.py ai --explained`). Sections
1-12 are the earlier research (some of their "not traced" notes are now settled in 13). Port numbers:
`work/remaster/lev_1_m1/grunt_ai.json` (`tools/remaster_export_grunt_ai.py LEVEL`, any level; the engine does not
read it, it is the reference). Guesses are labelled **guess**. The grunt's weapon is grunt_bow (engine/src/ai.rs
Kind::weapon, 0x004a0ef0), settled since the table below was written.

## 1. Templates and instances in lev_1_m1

| Template | Property set | Count | Weapon | Notes |
| --- | --- | --- | --- | --- |
| grunt_regular | p_grunt_regular | 18 | grunt_rifle (bullet speed 150*3 = 450, damage 15) | most override hit_level_1 15, fire_num_clips_min 3, bof_anti_gravity 0.5, friction 0.9 |
| grunt | p_grunt | 9 | **guess** grunt_bow (arrow speed 25*3 = 75, damage 10) | p_grunt has no max_health (code default) |
| captain | p_captain | 0 placed; 2 spawned by generator 215 | captain_mg (bullet 300*3, damage 30), captain_grenade | |

Projectile speed x3: Projectile_Load 0x0047e0c0 (src/game/game_06.cpp:959).

Modes used (instance overrides, big10_inst): attention_guard (most), ready_guard (196, 208), ready_window (211-214,
obj_tag 60-63 + generic_txt window bone), trigger_marker (34 -> marker tag 10 "hide", trigger 808; 67 -> marker 14
"ready_roaming", trigger 556), default attention_roaming (181, 201). Alert groups 2, 6, 7. Alarm: guard_alarm
instance 33 (tag 4) and marker 151 alarm_tag 4; generators 152 (5 regulars) and 215 (5 regulars, 2 captains), both
marker tags 12-14 (markers 36, 205 "trigger_charge", 151 "ready_roaming"). Mission: alarm_generator_max_troops 10,
alarm delay 2-4 s, alarm_ai_max_distance 1000.

## 2. Set-up: Ai_InitGeneric 0x004247f0 (src/ai/ai_01.cpp:2289-2590)

- Health +0x458 = max_health * Game+0x558 for bad guys (vt+0xe8: easy 0.5, normal 1, hard / very hard 2), * Game+0x554
  for the player's side (vt+0xe4: easy 2, normal 1, hard 1, very hard 0.5) (ai_01.cpp:2329-2340, table 0x00589188;
  corrected 2026-10-15: the factor names were swapped here and in gamedata.rs). The remaster applies neither yet.
- Parameters with code defaults (offset): scan_distance +0x5dc (300), hear_distance +0x5e0 (20), alert_group +0x574,
  fire_num_clips/rounds/bursts min/max (1), clip/burst delays (0), fire_inaccuracy_height +0x5e4 (10),
  fire_inaccuracy_angle +0x5e8 (15), fire_accuracy_min/max +0x5ec/+0x5f0 (50/150), fire_infinite_accuracy_min/max
  (400/800), hit_level_1 +0x9e8 (10), hit_level_2 +0x9ec (25), hit_ragdoll +0xa0c (50), long_hit_pct +0x9e4 (60),
  allfours_pct +0x9e0 (20), move_power (30), move_speed (46), limp_speed (move_speed*0.35), limp_pct, patrol_speed
  (move_speed*0.25), bof_anti_gravity +0x888 (0.5), pickup_drop_percent (100, stored *0.01), pickup_drop_specific.
- fire range (0x00425d30): fire_range_min/max (200/350); the grunt's engagement range +0x5d8 = min + rand * (max -
  min) / 32768, drawn once per grunt.
- 50 % chance of flag +0x484 bit 21 = second voice set.
- **Data keys the exe never reads** (no such string in the exe): `sticky_scare_dist`, `fire_inaccuracy_xy` (the code
  reads `fire_inaccuracy_angle`, so the default 15 applies), `pickup_grab_percent`, `marker_tag_scared`,
  `target_tag`, `stig_fire_clip_delay_minin/maxax` (typos), marker `follow` and `ic`. A faithful port ignores them.
- Mode -> set-up:

| Mode | Action (ai_01.cpp:2444-2585) |
| --- | --- |
| attention_roaming (default) | vt+0x19c(state 0xf, 1, ...) when a2, vt+0x1b8(1) |
| ready_roaming | vt+0x1a0(2, 3, 0, 1, 1, 0, 0.5, 0.5), vt+0x1b8(1) |
| attention_guard | 0x0042d0f0(o, 0, 1, 1) |
| ready_guard | 0x0042d0f0(o, 0, 0, 1) |
| attention/ready_window, _tower | flag +0x488 0x80 (window) / 0x200000 (tower); obj_tag +0x534, generic_txt +0x4cc; 0x0042d0f0(o, 0, attention?1:0, 1) |
| trigger_marker | receive_trigger_activate +0x52c, marker_tag_min/max +0x558/+0x55c (invalid -> logged, object deactivated); looping (+0x488 bit 1), patrol (bit 0), startloc (+0x484 0x10000), trigger_los (+0x488 0x40), strict patrol; vt+0x19c(0xf), vt+0x1b8(0x8c) |
| trigger_charge | trigger_los, +0x488 0x100, timer +0x494 = 15 + rand*0.000275 (15..24 s), vt+0x1b8(0x8c) |

"attention" vs "ready": the third argument of 0x0042d0f0 (**guess**: attention = idle/relaxed stance with scan
animations, ready = weapon raised).

## 3. Target acquisition: AI_FindTarget 0x00426920 (src/ai/ai_02.cpp:585-810)

Runs only when timer +0x490 <= 0; re-armed with 0.75..1.25 s (or 2..4 s when 0x0044e600 holds).

1. Window/tower tags [+0x858, +0x85c]: first live one wins.
2. Enemy list Game+0xa84 +0xee0 within the entry radius (3D) and vt+0x180(obj, 2*scan) -> closest in 2D.
3. Own list +0x604 within range (scan_distance): the **player inside hear_distance is taken at once** (no facing or
   line-of-sight test). Otherwise, unless Game+0xae8 or +0x488 bit 2: facing test vt+0xf0(target, 190 deg) (180 deg
   when hear_distance is 0); a target outside the cone is still accepted when inside hear_distance (or within 100
   units = sqrt(10000) when hear_distance is 0); +0x484 bit 17 skips the cone. Then line of sight 0x00426fd0 to the
   target's "ref_hit_00" point. Closest valid candidate wins.

Port reading: sight = within scan_distance, inside a 190-degree cone (**guess**: full angle, +-95 deg from facing), and
visible; hearing = within hear_distance (any direction, no LOS).

Mission target (agent-aiparity2, oracle lev_bonus_m1): with the mission's ai_target_tag (Game+0xae8, read by
MissionObject 0x0044c170) a bad guy (vt+0xe8) searches at range 1e8, keeps only candidates whose tag (+0x390) is that
tag (or his own +0x534), skips the cone and the line of sight, and vt+0x180 (0x0043ecf0) keeps such a target at any
distance: all 50 grunts of lev_bonus_m1 have Roman from the load frame (engine: ai.rs ArmyTarget). The line of sight
0x00426fd0 excludes the looker's list +0x608 = World+0xe7c: other AIs never block it (engine: Physics::ai_bodies).
Fidgets: the timer +0x494 is 0 after Ai_InitGeneric except for trigger_charge (15..24 s), and 0x0042be80 / state 0x8c
call 0x00427330 only when the slot-0 anim reached its end (+0x1c bit 1): the first fidget comes at the end of the
first attention loop (1.97 s), on every level at once (the `_ic` sneeze / cough lines at play frame 122 / 146).
Spline speed: vt+0x80 (0x004297a0) in states 0x96 / 0x97 is the leg's marker speed (0x0044b4f0: the previous
marker's; the head's leg takes the last marker's), else patrol_speed (+0x954) with marker_patrol, else +0x94c / +0x948.

## 4. Alert groups

Set target 0x0042cc70 (vt+0x23c): with a group id (+0x574 != 0) every other AI in World+0xe7c whose vt+0x10c holds
gets 0x0042c700: same group, alive, not flag +0x488 bit 14, its timer +0x578 expired -> after a random 0.25..1.0 s it
takes the same target (+0x57c). (src/ai/ai_03.cpp:1355-1590.) Guarding AI (+0x484 bit 25) whose target goes beyond
range +0x5d8 sets both ranges to 99999 (0x0042c790).

## 5. Combat decision: 0x00427910 / 0x00427c80 (src/ai/ai_02.cpp:1150-1280)

With a target: squared 2D distance vs engagement range +0x5d8.
- Out of range: state 0xa2 (anim 0x66) / state 0x79 / vt+0x18c(0) and 0x0042b8e0 (**guess**: move closer).
- In range: roll 0..89; under 30 with close attack allowed -> anim 0x27, state 0x10 (**guess**: melee); otherwise
  state 0xa or 0x11 (fire).
- Firing cadence: fire_num_clips x fire_num_rounds x fire_num_bursts with burst/clip delays, per the parameter names
  (per-shot code not traced). Aim error: fire_inaccuracy_height / angle with accuracy ranges fire_accuracy_min/max
  (formula not traced).

## 6. Per-frame state machine GenericAI_Update 0x0043ee90 (src/ai/ai_06.cpp)

State +0x460; dispatch at 0x0043f4ee with jump tables 0x004460c0 / 0x00446248 / 0x00446290 / 0x0044631c. States seen
in the switch (ai_06.cpp:437-535): 0x1, 0x2, 0xa-0x14, 0x1e-0x21, 0x28, 0x29, 0x32-0x34, 0x3c-0x3e, 0x50, 0x5a,
0x5b, 0x64-0x67, 0x6e, 0x78-0x7f, 0x82, 0x8c, 0x96-0x98, 0xa0-0xae, 0xb4-0xc8, 0xd2, 0xdc-0x118, 0x11a,
0x12c-0x132, 0x190-0x197, 0x19b-0x19d, 0x1f4. Meanings known: 0xf = initial roaming/marker state, 0xa/0x11 =
fire, 0x10 = close attack, 0x79/0xa2 = approach (**guess**), 0x3e = forced pursuit at range 5000 (ai_03.cpp:1635).
Every state ends at 0x004460a2. **The per-state actions and exits are not yet recovered for the port.**

## 7. Voice lines (src/audio/audio_01.cpp:655-1060)

Sets: gr1500xx (first), GR2/gr2 (+0x488 bit 29), GR3 (+0x484 bit 21). Index = ((rand*range)>>15)/100.
general_scream (0x004383a0), topsy_scream (0x004385c0), sticky_scream (0x00438810, shared delay Game+0xb64),
ClimbTaunt, Help (0x00438eb0), Fight (0x004390e0), Hurt (0x004392f0 shared delay; 0x004394c0 at most every 4.0 s),
Dead (0x004396a0 / 0x00439890, once: state flag 0x400), Taunt (0x00439ae0), Ambient (0x00439cb0). lev_1_m1 bank:
fight 19, spotted 4 (no code reference found), help 6, hurt 26, dead 24, taunt 14, ambient 7, sticky_scream 11,
topsy_scream 15, ic 12.

## 8. Animations available (lev_1_m1_big03_anim)

grunt_attention, grunt_stand_scan_1/2, grunt_stand_{tl,tr,bl,br}, grunt_fire_{tl,tr,bl,br}, grunt_reload_*,
grunt_run, grunt_run_scan, grunt_run_scared, grunt_scare, grunt_march, grunt_limp, grunt_turn, _turn_180,
_turn_left/right_90, grunt_hit_1..4, hit_long(_2), hit_in_back, hit_mgun_2/3, hit_fall_1/2, hit_window,
grunt_die_1..6, grunt_blown_up(_2, _into, _die), grunt_crash_land, grunt_dazed_1..3, grunt_grenade_stuck_1/2,
grunt_peek_1/2, grunt_hang_into/hanging, grunt_window_blown_out, grunt_taunt1..3, grunt_idle_* (blow_nose, cough,
fly_buzz, shiver, sneeze), barricade_*, bow_* variants. tl/tr/bl/br = aim quadrant blend (**guess**). The anim id ->
name mapping used by vt+0x19c / vt+0x1a0 was not traced.

## 9. Hits, death, bof

hit_level_1 / hit_level_2 / hit_ragdoll are damage thresholds per hit (**guess** from names: small / medium hit
reaction / ragdoll), long_hit_pct the chance of the long reaction. bof_anti_gravity (+0x888) scales gravity on a
grunt blown off by the punching glove or explosions (**guess**; global bof_speed_factor 3, bof_hit_force_factor
30000). ledge_death sets +0x484 bit 31. pickup_drop_percent chance of a drop on death. Not traced in code.

## 10. Open questions

- Per-state behaviour of GenericAI_Update (the bulk of the work); anim ids of vt+0x19c/+0x1a0.
- Shot timing and aim-error formulas; hit-reaction thresholds; bof physics; alarm run to guard_alarm and
  generator spawning (0x00425ff0-area code in ai_01.cpp:900-1710 not yet read).

## 11. State machine, second pass (2026-10-13; static reading of the recovered code)

Vtable (GruntObject 0x005a9cf0): vt+0x19c PlayAnim(id, loop) 0x004262d0, vt+0x1a0 PlayAnim4 (aim blend +0x580/+0x584),
vt+0x1b8 SetState (+0x460) 0x004262c0, 0x22c target update 0x0042be80, 0x230 engage 0x00427910, 0x23c SetTarget
0x0042cc70, 0x240 FindHidingSpot 0x0042a7b0, 0x184 pursue 0x004295d0, 0x198 fire cycle 0x00428980, 0xf0 view cone
0x00455d90, 0xd4 Character_Die 0x004319e0, 0x17c GoRagdoll 0x00431310, 0xd0 damage 0x00430340, 0xcc scare 0x0042fb60.
Grunts never close-attack (vt+0x1e0 false). Anim id -> name: vt+0x1a4 = 0x0043bd70 (anim_01.cpp:1647-1849).
The PC exe has no `sticky_scare_dist` and no `guard_alarm` string; marker actions `ready_roaming`/`trigger_marker` are
not parsed (only hide, trigger_charge, attention_guard, ready_guard, trigger_charge_tag, ic_*).

| State | Behaviour | Evidence |
| --- | --- | --- |
| 1 idle/roaming | target update: none -> AI_FindTarget(scan); target -> engage. No target: fidget anims 0x12-0x1a every 15-24 s | ai_06.cpp:2624; ai_03.cpp:996 |
| 0x79 / 0xdd guard | holds place; target -> fire cycle vt+0x198; none -> face within 45 deg, fidget; strict patrol resumes spline | ai_06.cpp:2644-2698 |
| 0x7a re-guard | to 0x79 when grounded, else falling (0x82) | ai_06.cpp:566 |
| 0x8c wait for trigger | trigger_los: AI_FindTarget -> SetTarget + activate 0x0042f5c0; hurt also activates | ai_06.cpp:1625; ai_04.cpp:420 |
| 0x3c chase | drop target beyond 2 x scan; every 0.2-0.4 s ray: clear and 3D distance < fire range -> engage; route end: wait 5-15 s (state 2), then run home (0x50), then state 1 | ai_06.cpp:1420-1546, 799 |
| engage 0x00427910 | out of range (2D vs +0x5d8): guard -> 0x79, else pursue (point within +-20 of target, state 0x3c); in range: state 0xa (with a shooter limit: counted, or 50 % an extra, else wait 0x11: section 15) | ai_02.cpp:1151-1256 |
| 0xa firing | while target within 2 x scan: fire cycle; else drop target | ai_06.cpp:869 |
| fire cycle 0x00428980 | turn within 45 deg; clip pause +0x5c8, burst delay +0x5cc; fires only inside the 180 deg cone with LOS; LOS lost: guard -> 0x7a, else re-pursue; after a clip: taunt (shared timer) or reload | ai_02.cpp:1784-2001 |
| 0x32 / 0xc1 reload | grunt_reload_*; then re-engage when in range with LOS, else 50 % pursue / 50 % hide | ai_06.cpp:1223, 2241 |
| 0xc8 flee | sticky bomb stuck within 25 units (hardcoded, game_07.cpp:1240): flee 100 units from the player (250 from an attacker), run_scared, 3-5 s, then charge the player (0x0042ce70); a guard plays grenade_stuck + Sticky_Scream instead | ai_03.cpp:2875-2958; ai_06.cpp:2492 |
| limping | health < limp_pct % of max: limp anim and limp_speed (default 0.35 x move_speed) | ai_04.cpp:388; ai_02.cpp:2358 |

Aim error (0x004284e0): L = rand[-fire_inaccuracy_angle, +angle] sideways, V = rand[0, fire_inaccuracy_height] up, scaled
by s: d <= fire_accuracy_min -> 0.1; between: max(0.1, (d^2 - lo^2) / (hi^2 - lo^2)); beyond max -> 1 (infinite
accuracy ranges when a guard's target is beyond range). Aim point = target ref_hit_00 + side * L * s + up * V * s.
Hits (0x00430340): damage <= hit_level_1 small hit (state 0x12f), <= hit_level_2 medium (0x130), else big (0x131);
long_hit_pct dazed; Character_Die ragdolls when damage >= hit_ragdoll (default 50), GoRagdoll impulse = direction x
amount x 0.15, at most 3 bodies down. Drops: chance 1 - (1 - pickup_drop_percent/100)^(...). Alarm: a marker with
alarm_tag starts fx_siren on the alarm object and the Help bark; the controller 0x00404e50 spawns troops from the
nearest valid generator (weights num_bow/num_regular/num_captain/...) up to alarm_generator_max_troops, every
alarm_min..max_delay; spawned troops walk out of ref_exit_00, follow the generator's markers, then charge.

## Barks: when each line plays (traced 2026-10-14; static, mostly not executed in recorded runs)

GruntObject vtable 0x005a9cf0 (BowGrunt the same slots). Voice set (GruntObject_Slot6, ai_04.cpp:2340): rand*300>>15
< 100: +0x484 bit 21 = GR1; < 200: +0x488 bit 29 = GR3; else GR2 (the audio_01.cpp comments name the sets in the wrong
order). Shared timers, set to the level start time (game_02.cpp:2057-2110), each re-rolling its interval: death_delay
Game+0xb64 30..45 s, vo_delay Game+0xb44 30..45 s, taunt_delay Game+0xb84/+0xb90 300..360 s.

| Line | Slot / wrapper | Played when | Delay | Evidence |
|---|---|---|---|---|
| Spotted cue | +0x144, 0x004390b0 | SetTarget 0x0042cc70: a new target when there was none | vo_delay | ai_03.cpp:1554 |
| Fight cue | +0x13c, 0x004392c0 | the fire cycle 0x00428980 starts the fire anim with a target | vo_delay | ai_02.cpp:1860 |
| Taunt | +0x15c, 0x00439ed0 (range 400/400/500) | after the last burst: target, vt+0x1d8, taunt_delay, +0x484 bit 17 clear, angle < 180, +0xb50 silent; taunt anim 0x32..0x34, state 0x6e | taunt_delay | ai_02.cpp:1956-1981 |
| Help | +0x264, 0x00438f90 | a marker with alarm_tag when its alarm first starts | none | ai_07.cpp:1336 |
| Hurt (shared pool) | +0x14c, 0x00439e00 | a non-lethal hit, tried first; sets +0xb48 | death_delay | ai_04.cpp:382 |
| HurtShort | +0x148, 0x00439e30 | the same hit when Hurt was refused | per grunt 4 s (+0xb48) | ai_04.cpp:385 |
| Dead | +0x154, 0x00439e60 | Character_Die, GoRagdoll, Character_Launch; once | death_delay, scream +0xb4c silent | game_02.cpp:1246 |
| DeadAtPosition | +0x158, 0x00439ea0 | when Dead is refused (Launch: General_Scream instead) | none, once | same |
| General_Scream | +0x160, 0x00438590 | Scare 0x0042fb60 without an attacker (near a stuck bomb, 25 units), Sticky's fallback, launches, state 0x19d | scream not playing | ai_03.cpp:2943 |
| Sticky | +0x164, 0x00439de0 | Scare with an attacker: only the grunt the bomb is stuck to | death_delay, else General_Scream | ai_03.cpp:2930 |
| Topsy | +0x168, 0x004387e0 (range 500) | knocked / thrown without recovering, state 0x19c | scream not playing | ai_03.cpp:3025 |
| ClimbTaunt / LadderPlaced / OpenGate | +0x16c / +0x170 / +0x174 | ladder states, gate opening | vo_delay / none / vo_delay | ai_06.cpp:2020, ai_02.cpp:2563 |

Spotted and Fight cannot play in the first 30..45 s of a level (vo_delay starts at level load): the recorded first
fight has no line.

## 12. Cover: trenches (and the mounted gun) (traced 2026-10-14; static, not executed in recorded runs)

Engine port: `engine/src/ai.rs` (`find_hiding_spot`, `pick_trench`, `CoverPhase`, `Barricade`), playtests
engine/playtests/cover1..3.json. Evidence by address; **guess** where marked.

**The trench object.** Behavior "barracade" is class 141 (string table game_03.cpp:3067) = BarricadeObject, vtable
0x005a7068. Its vt+0x138 is 0x004563c0 (returns 3), so its load (ai_01.cpp:1019) gives it one ground slot with limit 1
(+0x46c = +0x484 = 1): one grunt per trench. vt+0x14c 0x0040dcb0 = ref_exit, vt+0x150 0x0040dd10 = ref_barracade (point
= the node's translation, direction = -(its y axis)); vt+0x148 0x0040c100 free, vt+0x158 0x0040c400 occupy, vt+0x15c
0x0040c4a0 free the slot. trench_0: ref_barracade (0, 5.03, 0.72), ref_exit (0, 22.2, 6.13), identity rotations; on
lev_1_m1 the five trenches face game +y (north, 0..10 degrees).

**Who searches, when** (GenericAI_FindHidingSpot 0x0042a7b0 = vt+0x240(need_target, a1, hold)):
- a hide marker's arrival: Marker_ProcessArrival 0x0044ba30 -> vt+0x240(0, 0, 1), else AI_Wander(60, 100). On
  lev_1_m1: barracks 25's troops (marker tag 20 -> marker 179 at (-238, -379), 80..140 from the trenches) and grunt 34
  (tag 10 -> marker 35 at (71, 150), 22 from the mounted gun 32: no trench within its scan_distance);
- a non-guard's clip end: fire cycle 0x00428980 -> 0x0042b8e0 -> vt+0x240(1, 0, 0) while the target is within 2 x
  scan_distance; guards (+0x484 0x2000000) go to 0x7a instead;
- a reload's end (state 0x32, ai_06.cpp:1223): in range (+0x5d8, 2D) with sight -> engage; else 50 % vt+0x240(1, 0, 0),
  50 % pursue;
- leaving a trench whose arc lost the target (0x0042b8e0 with +0x484 0x20000) -> vt+0x188, vt+0x240(1, 1, 0);
- (other callers: states 0x34, 0x96/0x97, 0xf3, 0x0042b480: not lev_1_m1 grunt paths, not ported).

**The search** (0x0042a7b0): dead -> 1; flag 4 of +0x484 (fire counters rolled, 0x00427600) clear, a target, and
0x004531e0(target) false (**guess**: true for an airborne target): rand*100>>15 < 90 -> reload in place (anims 0x2a..0x29,
state 0x32), return 0; rand < pickup_grab_pct (+0x86c, default 0, 0x0042533f; no property set has the key: dead) -> 0x0042a500; a held
cover is left (vt+0x188, history); need_target without a target -> nothing; the candidates turret = 0x0042a100 and
trench = 0x00429f00, compared by 2D squared distance from the grunt (the trench wins when nearer; the winner's distance
x 0.25 bounds the next stage); the generic slot walk of World+0xed4 (kinds 1 / 6 of objects other than type 3: none on
lev_1_m1, barracks have only ref_exit / ref_spawn); then the turret (path to it, state 0x7b) or the trench (path to
ref_exit via 0x0046f6d0 == 1, occupy slot 0, +0x908 = ref_exit's direction, state 0xa0), +0x484 0x200 = hold (a2).
Nothing: flag 4 set -> pursue (vt+0x184), else reload (state 0x32); returns 0 (a hide marker then wanders).

**0x00429f00 (trench)**: needs vt+0x1f4 (grunts: 0x00433fa0, true); list World+0xed4, type 3, slot 0 free, not +0x918 /
+0x91c (the last left), within scan_distance (+0x5dc, 2D from its origin); with a target also: within +0x5d8 of the
target, target within 80 degrees of ref_barracade's direction seen from ref_barracade (0x0042b860), sight from
ref_barracade + 5 (+ 5 again in 0x00426fd0) to the target's ref_hit_00 passing through the trench. Nearest wins.
**0x0042a100 (mounted gun)**: vt+0x1f0, list +0xea8, free (vt+0x158 0x004987a0: ai_can_use, kickout timer over, no
player (flag 8), no AI (0x14)), within scan_distance; with a target within range of it, target within 65 degrees of the
gun's heading (0x00453150), sight from the gun + 10. The gunner's states: docs/subsystems/turret.md (ported, agent-gunner).

**The trench states** (ai_06.cpp:2055-2218; anims by 0x0043bd70):
| State | What | Exit |
|---|---|---|
| 0xa0 | run to ref_exit | arrived: ref_barracade into +0x934 / +0x908, 0x004324d0, path there, 0xa1 |
| 0xa1 | run to ref_barracade | arrived: no target -> AI_FindTarget; +0x484 0x20000; anim 0x66; 0xa2 |
| 0xa2 | grunt_barricade_reload (0x66, 1.333 s), face +0x908 | end: flag 4 cleared and counters rolled (0x00427600); breathe 1..6 s; 0xa3 |
| 0xa3 | grunt_barricade_breathe (0x65, loop) | no target or target outside 80 degrees (not with hold): timer 0. At 0: rand(1000) >= 100 and (target or out of arc) -> stand set (ids 2, 3, 0, 1), 0xa4; else peek 0x67, 0xa5 (fire spray 0x68 / 0xa6 needs vt+0x1fc: false for grunts) |
| 0xa5 | grunt_barricade_peek (0x67, 2.633 s, no notes) | end: flag 4 and rounds left -> breathe 0xa3; else 0x66 / 0xa2 |
| 0xa4 | stand (1.967 s), turn to the target | end: no target -> 0x66 / 0xa2; else engage 0x00427910 (in range with flag 4: fire state 0xa keeping the trench; else 0x66 / 0xa2) |
In the slot (+0x484 0x20000): AI_FindTarget skips the view cone (ai_02.cpp:771); the fire cycle's lost sight -> 0x66 /
0xa2; its clip end (0x0042b8e0): target within 80 degrees of the slot -> 0x66 / 0xa2, else leave (vt+0x188: history =
this trench) and search (1, 1, 0); no taunt after the last burst (ai_02.cpp:1959); a reload's end engages; a hit
reaction keeps the trench (0x00430340 stopMove skips vt+0x18c) and its end ducks (state 0x132 -> 0x66 / 0xa2); on the
way (0xa0 / 0xa1) a hit lets it go (vt+0x18c) and retaliation offers are refused (0x0042c820). Death: 0x00427100 plays
0x56 grunt_barricade_death (after the ragdoll / ledge / window cases of Character_Die). No death path frees the slot
(Character_Die / 0x00427100 / GoRagdoll call neither vt+0x188 nor vt+0x18c): a dead grunt keeps his trench (static
reading).

Engine deviations (**guesses**): the paths are the engine's straight line or nav.rs route; the fire state with no target
in the slot ducks; a hit on the way to a trench pursues after the reaction; a grunt whose mind another system changed
(scare, topsy) lets his trench go.

## 13. The port of 2026-10-14 (agent-ai: engine/src/ai.rs, alarm.rs, charanim.rs)

Static reading of the recovered code, then headless playtests (engine/playtests, in tools/regress.py). Not compared
with the original by capture in this round (see 14).

**Classes.** BowGruntObject_Slot6 0x004347b0 runs 9 times per level load in the coverage runs: the 9 `grunt` instances
are archers (`Kind::Bow`), `grunt_regular` GruntObject, `captain` CaptainObject (alarm generator 215 only). Their
vtables (diffed with tools/vtable.py) differ in: the anim table vt+0x1a4 (0x0043bd70 / 0x0043abc0 / 0x0043b610, ported
whole as ai.rs `anim_name`: e.g. the archers' grunt_bow_idle_* stand set, grunt_bow_fire_*, grunt_bow_reload_* 1.3 s,
bow_die_1..3; the captain's captain_idle_*, captain_fire_* 0.633 s looped by +0x484 0x1000), the weapon (0x004a0ef0
index 0xe bow at ref_lhand, 0xf rifle, 0x10 captain_mg at ref_rhand: level.rs now hands the captain his gun), and yes /
no slots: trenches vt+0x1f4 (not the archers), mounted guns vt+0x1f0 (not the captain), the close attack vt+0x1e0 and
the trench fire spray vt+0x1fc (the captain), the all-fours fall vt+0x200 (the captain), turn anims vt+0x218 (not the
captain), the chase's ray-only engage vt+0x1cc (not the archers: they first need 0.9 x +0x5d8).

**Stance.** Ai_InitGeneric: attention_* modes and the trigger modes stand at attention (anim 0xf) and fidget
(0x00427330 only fires while 0xf plays); ready_* modes (196, 208, the window grunts) hold the stand set and never
fidget; every return to rest after a fight plays the stand set (0x0042bc60, 0x7a, L_442860), so a grunt that has fought
stops fidgeting.

**Hit reactions** (0x00430340, ai_04.cpp:444-535; exits ai_06.cpp:3191-3412): on a gun none; the captain's all-fours
fall when the health crosses allfours_pct (0x35, then 0x36 breathing 3..7 s, 0x38 standing up); damage <= hit_level_1
small 0x3f..0x42 (not while one plays), <= hit_level_2 medium 0x43 / 0x44 alternating, else big 0x45 / 0x46. A hit
leaves a trench held on the way, not the slot. Ends: a small one with health <= long_hit_pct % starts a long hit (0x4b..0x4f:
grunt_hit_long(_2), grunt_dazed_1..3, up to 8 s); small / long: in the slot duck, a guard re-guards, else pursue with
flag 4 or search (vt+0x240(1, 0, 0)); medium / big: in the slot duck, a guard re-guards, else with a target and flag 4
fire when in range and in sight, else pursue; without: search.

**Losing the target** (0x0042bc60): a guard re-guards, the slot ducks, else a new search, then with the counters spent
the reload, else walk home (+0x4c0, run_scan, state 0x50) and stand. (The pursuit of a still-live enemy has no target
to follow and ends there too.) State 2 (5..15 s wait at a chase's route end) lasts one frame with a target (the target
update engages at once) and is not ported separately.

**Marker splines** (0x0044b130, 0x0044b540, 0x0042d1b0, 0x0044ba30, states 0x96 / 0x97): a tag picked in the range,
its head (sequence 0) walked to, then each marker's arrival: trigger, alarm, then hide (search (0, 0, 1), else wander
60..100), ready / attention_guard (a guard there), trigger_charge (the player), a positive scatter_min (wander
scatter_min..max: 0x0042d430 turns the facing by -45..+45 degrees and rolls the distance per axis, state 0x3d, home =
there), else the next marker (marker_next_min..max switches spline; the end stops unless marker_looping). A strict
marker (the one walked from) keeps targets from interrupting. `ready_roaming` and `trigger_marker` are not parsed by
the PC exe: markers 151 / 266 / 267 act through their scatter. So barracks 235's troops (tags 33..34) walk to marker
266 or 267 and wander 5..100, never reaching 264 / 265 (their hide markers): faithful.

**Alarm troops** (0x00404e50, 0x00405490, 0x0042dd50): made at the generator's ref_spawn_00, walk to its ref_exit_00,
then follow the spline of a tag in 12..14 (36 / 205 trigger_charge, 151 scatter + alarm), with no tags 20..50 ahead and
the charge (+0x488 0x200). Generator choice: the nearest unseen one not among the last three spawn points, else the first
such in list order: once both lev_1_m1 generators are recent, generator 152 sends nearly every troop (215, the only one
with captains, only when 152 is seen): the original's rule, reproduced.

**Noises** (0x00404810, AIInterestObject 0x00404680, AI_FindTarget step 2): Roman's own projectiles post a noise
following each bullet (fx_start: both his guns' bullets have one) and one at its end for 2 s, radius 200. A grunt whose
ref_hit_00 lies within a noise's radius (3D) takes the player when he is within 2 x scan_distance, with no cone or line
of sight: shooting near grunts alerts them (noise1: eight courtyard guards 400+ units from Roman turn on him).

**Taunt** (0x00428980 ai_02.cpp:1955-1981, state 0x6e): after the last clip with a target, outside a trench, when
taunt_delay (300..360 s, game-wide) has passed: the Taunt bark and anim 0x32..0x34; its end as the clip's end (a guard:
its reload, guess). The heading test (< 180 degrees) passes but for opposite facings; the speech-handle test is taken as
silent (the engine keeps no per-grunt handle).

**The captain's grenade** (engage 0x00427910 roll < 30 of 200 with flag 1, anim 0x27 captain_grenade_throw, the "throw"
note at 1.333 s -> 0x00428340): a captain_grenade from his hand at Roman's ref_hit_00, the target pulled back 20 toward
him (0x00482870), launched at speed 35 x 3 under g 64.348 (the lower arc: guess), ending at its first hit (0x0047eae0):
area damage 30 (10..30), 50 to what it hit, fx_imp_sticky. Flag 1 is set again when the counters are rolled after a
spent cycle. The trench spray (0x68, 0xa6) fires at each of the anim's 15 "fire" notes.

Tests (headless, tools/regress.py): hits1 (small, long, medium hits and their ends), noise1 (a noise wakes guards out
of sight), noise2 (Roman's shots post and expire noises), splines1 (barracks 235's troops reach 266 / 267 and wander),
alarm1 (grunt 67's run to marker 151 raises the siren; generators send troops that follow tags 12..14), captain1 (a
captain spawns with his class and fights; a grenade was thrown in the run), taunt1 (with the bark delays forced, a
spawned grunt taunts after a clip), leader1 (companions, companions.md 7).

**Guards fire from their guard state (2026-10-15, oracle finding B8).** State 0x79 (ai_06.cpp:2645-2698) runs the fire
cycle itself (vt+0x198 with may_move clear) while +0x588 is held; at a clip's end it plays the reload set 0x2a.. in
place and rolls the counters (0x00427600) when the anim ends. ai.rs: a guard engages as Mind::Guard with
`guard_firing`, the fire cycle's Reload becomes his guard state with `reloading`. The later classes and modes:
docs/subsystems/infantry.md. **Roman's sticky bomb** is a projectile to the grunts' ears (B6): its start and end
effects post noises like his bullets' (sticky.rs -> Interests::follow / end_at).

## 14. Open points

- **Hidden grunts** (ported 2026-10-16, engine/src/ai/ic.rs): trees (lev_1_m1 pine trees 47, 61, 76: ic_tree_slide_N,
  triggers 99 / 98 / 99), buildings (ic_door_N, ic_window_dive_*, ic_climb_v2f_1, roof and wall jumps: 59 on 10
  levels) and powder kegs (ic_powder_N) each hide a grunt_regular at the property's ref node (0x00496e90 /
  0x0040ad80 / 0x0041c180 -> 0x00456180 -> 0x0042d000: state 0x8c holding the mover's first frame); the trigger or a
  wake-up plays the mover (0x0042f5c0 -> GenericAI_PlayICAnim 0x0042c410, state 0x64 = Mind::Scripted), then state
  0x65 (Roam, home there) or "ready_roof" (guard). Oracle: their count, positions and states agree on every level.
  lev_1_m1_b's spheres 376 / 387 send 700 / 703 at load because their sphere_radius is a float read as a dword (the
  whole level: original bug, docs/original-bugs.md; B19, agent-aiparity2).
- The captain's own voice lines (AIVoice_* 0x00437690 / 0x00437860 / 0x004375c0 ...): captains use the grunt barks.
- The run anims' playback speed scaled to the move speed (vt+0x1a8, anim_run_* / anim_limp_*): played at speed 1.
- The falling watchdog 0x0042e910 (state 0x82): not ported. The contact reactions (slots 40 / 42 / 44) are ported in
  part (2026-10-16, ai/body.rs and contacts.rs; remaster-engine.md "The character body"): the body (a sphere of
  collision_radius on the feet, 0x004527d0), the collision veto of Slot40 0x00432590 (never the +0x50c object, never
  the same +0x510 group: a squad, set by 0x0042de60 to -squad_tag and kept after a charge), friction 0 in a squad
  (0x0042de30 / 0x0042de60 / ai_06.cpp:2650) and the material's otherwise (0x0042cb80 via 0x0042cc10), Slot44's bump
  (a walker with a route that touches a mover: +0x510 = 0, +0x50c = it; a marker walker 0x96 / 0x97 re-plans: not
  ported) and the contact note a flight's landing reads. Slot42's contact veto (0x004548f0: a route refresh on a
  contact with a non-mover) is not ported.
- Bullets on captains use the grunt's collision parts (hitparts.rs grunt_coll_9; captain_coll_0 not loaded).
- Nothing in this round was compared with the original by capture (the rates and ranges come from the code and the
  property sets); first candidates: the alarm troops' arrival times, a captain's damage rate (Roman 400 -> 0 in ~10 s
  at 50 units in captain1), the long-hit frequency.

## 15. The later levels' behaviours (2026-10-17, agent-enemyai)

Each its own module under engine/src/ai/ (typed state, systems in the AI plugin), the mechanics from the recovered code,
static reading; not compared with the original by capture (tuning items: docs/remaster-engine.md "Later levels'
infantry"). Corrections found on the way: the "16 %" rolls of 0x00427910 / 0x00427e80 are 50 % ((rand() x 100) &
~0x7fff < 0x190000 is rand x 100 >> 15 < 50); 0x00427c80's close-attack roll is 20 %; pickup_grab_pct's default is 0.

| Behaviour | Module | Original | Test |
| --- | --- | --- | --- |
| The shooter limit (ai_max_fire 1 on the defence levels, 5 on lev_5_m5): counted shooters, 50 % extras, the rest wait (state 0x11, idle bases 0x74..0x76) until a counted one stops and hands over (n = rand x count >> 15, n = 0 nobody: original bug, kept) | max_fire.rs | 0x00427910 / 0x00427c80, 0x0042bb10, 0x0042b9e0, 0x004271c0 | max_fire_l1m4 |
| The captain's grenade: launch at the target pulled back 20, Ballistic_LaunchVelocity's arc, spin, the hit's area and direct damage | grenade.rs | 0x00482870, 0x00471a00, 0x00481c90 / ce0 / d00 / d10 | captain_grenade |
| The rocket captain fires the CaptainRocket (rockets.rs's flight); it can be shot down (max_health 1; a bad guy's shots and blasts do not count, its owner's do) and explodes where it is | rocket.rs | 0x00482ab0 / d10 / d60 | captain_rocket (+ unit tests) |
| A gunner whose counters ran out together aims and fires at the target led for 360 / s, without the aim error | aim.rs | 0x004580a0 in 0x00428fc0 / 0x004284e0 | gunner_lead |
| The falling watchdog (vz < -20 for 0.2 s, no contact) and state 0x82 (anim 0x61, landing back to the saved state, 5 s to live); the parachute's 25 / s descent | fall.rs | 0x0042e910, ai_06.cpp:1567-1623 | ai_fall |
| Paratroopers: the rope IC anim from the vehicle's ref_repel node, the freefall_* note into 0x82 with the parachute, the landing into the drop orders | paratroop.rs | ai_05.cpp:1513-1550 | paratroopers_l2m1 |
| The pre-success retreat (0.01..1 s, marker -1's direction, 2000, 20 % scared) | retreat.rs | 0x0042db00, ai_06.cpp:373-399 | defence_l1m4 |
| Jump markers held from the launch until the jumper leaves or dies (not freed on landing) | jumper.rs | 0x0040dfa0 / 0x0040dfb0, 0x004273a0 | jump_markers_bm1 |
| Script ends: "kill" a death, "deactivate" the death bookkeeping (the death trigger) and a silent removal | notes.rs | 0x00409140, 0x004090f0, 0x00409110 | ladders_l1m4 |
| The wall assault and the ladders | assault.rs, ladder.rs | 0x00429ce0, 0x00429ac0, 0x004298a0 / 0x0042d650 | ladders_l1m4 |
| The companions' takedown (50 % at a burst / clip end, within 50, facing within 45, ref_fight_00; the paired IC anims) | close_kill.rs (docs/subsystems/companions.md 10) | 0x00427e80, 0x00434ea0, 0x0042c670, 0x00487040 / 0x0044ea40 | melee1 |
| The grunts' close attack (within m_h2hMin 20 at a burst's or the clip's end, anim 0x5f / 0x60, "hit" within m_h2hMax 10: 50; Q112 25 / 35, 100) | melee.rs | 0x00428160, 0x0043eae0 | close_attack |
| A hidden grunt knocked away when his host (during the move or at the trigger) or his landing building is destroyed | ic.rs | 0x0042f5c0, 0x00425be0, ai_06.cpp:278-300 | ic_knock_l1m3 |
| Same-tag squads share a halt down squad_sequence (no shipped pair shares a tag) | squad.rs | 0x00433bc0 | unit test |

Left: punch-launch (0x0042f470, an AI punched by Roman: the player's side is the weapons'), the turbo captain's airborne
end into 0x82 (0x0042e1c0), 0x7a's own entry to 0x82, the knocked-off ladder planter, the voices ClimbTaunt /
LadderPlaced.
