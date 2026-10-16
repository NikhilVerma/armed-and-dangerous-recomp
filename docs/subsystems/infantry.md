# Infantry of the later levels (lev_2 .. lev_5, bonus)

The grunt-class AI beyond lev_1_m1 (docs/subsystems/grunt-ai.md covers GruntObject, BowGruntObject, CaptainObject and
the state machine they share). Static reading of the recovered code (src/ai, src/anim, src/game); **none of these paths
ran in the recorded coverage runs**, so every rate and range below comes from the code and the property sets, not
from a capture. Guesses are marked **guess**. Engine port: engine/src/ai.rs and its submodules (ai/*.rs).

## 0. Census (work/remaster/<level>/instance_props.json, effective mode = instance over template)

| Template (class) | Levels | Notes |
| --- | --- | --- |
| captain (CaptainObject) | 1_m1_b, 1_m3, 2_m3, 3_m1..3, 4_m1, 4_m3, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 (167) | jumper_captain on 65, rocket_captain on 64; modes attention_roaming 117, attention_guard 25, trigger_charge 15, trigger_marker 8, attention_window 2. **Before this port placed captains were never created** (level.rs took only models named grunt_*) |
| captain_sniper (SniperCaptain) | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 (31) | ready_guard 29 |
| grunt / grunt_regular in mode turret | 134 on 17 levels | start on the mounted gun (or forge cannon) whose tag is obj_tag |
| grunt / grunt_regular in mode bully | 3_m3, 4_m2, bonus_m2 (12) | hold a peasant by tag |
| squad_* (SquadObject) | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 | spawns captains / grunts (/ a goliath) in formation |
| jumper_marker (JumpMarker) | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1, bonus_m2 | landing points of the jumper captains |
| monk (MonkObject) | 4_m1, 4_m2 (17) | ready_roaming mostly |
| q112 (Q112Object) | 2_m2, 3_m2, 5_m1, 5_m4, bonus_m1 (7) | heavy chaingun grunt, not the boss |
| peasant_man / peasant_woman (PeasantObject) | 2_m3, 3_m3, 4_m2, bonus_m2 (99) | rescue objects, no AI states |

Class choice: ObjectSystem_CreateObject 0x0045f020 (game_04.cpp:474-513) hashes the property set's "behavior", else the
template name, into the behaviour table (0x0045df60, game_03.cpp:3023-3074): monk 0x4c MonkObject vtable 0x005aac28,
q112 0x40 Q112Object 0x005a2440, q112_boss 0x41 Q112Boss 0x005a4098, peasant 0x35 PeasantObject 0x005a8550 (a physics
object, not GenericAI), peasantgenai 0x43 PeasantGenAI 0x005aa9c0. Weapon table indices (0x004a0ef0, records of 0x150
at game_01.cpp:596-609): 0xe grunt_bow, 0xf grunt_rifle, 0x10 captain_mg, 0x11 captain_rocketlauncher, 0x12
captain_snipergun, 0x13 q112_chaingun, 0x19 monk_gun, 0x1a monk_gun2.

## 1. Sniper captain (SniperCaptain, vtable 0x005ae868)

Vtable = CaptainObject's (0x005acb78) but for slot 1 (class), 6 (init 0x004348a0), 105 (+0x1a4 anims 0x0043b110),
120 (+0x1e0 close attack: no, 0x00435250) and 143 (+0x23c SetTarget 0x00434930).
- Init 0x004348a0 (ai_04.cpp:2090): Ai_InitGeneric, the weapon 0x12 captain_snipergun at ref_chest. No 0x00428250(1):
  the fire anim is one-shot (a plain captain's loops).
- SetTarget 0x00434930 (anim_01.cpp:936-990): a new target while +0x488 0x4000000 is clear: the voice cue vt+0x13c
  (else vt+0x140), +0x588 = target, the bit set, the anim Captain_sniper_attach (0x00408e30, flags 0x80, blend 0.25),
  state 0x104; a null target with the bit set: release, clear, Captain_sniper_detach, state 0x104. State 0x104
  (ai_06.cpp:2746) holds still until the anim ends, then vt+0x23c(target, 1) takes the base path (0x0042cc70, engage).
  The bit is read nowhere else. This is the sniper's aim delay. **There is no laser**: no "laser" string in the exe;
  fx_trail_cap_sniper is in the data but not referenced by the exe; the bullet's trail is fx_trail_sniper.
- Anims 0x0043b110: ids 0..3 Captain_sniper_aim_tl..br while +0x588 is held, else captain_sniper_stand; 15
  Captain_sniper_stand; 23 / 24 captain_sniper_scan_1 / 2; 33..36 Captain_sniper_fire_*; 40..43 captain_sniper_reload_*;
  error anim sniper_captain_error_anim; the rest as the captain's.
- Fire: the shared cycle 0x00428980 with p_captain_sniper: 1 round, 1 burst, 1 clip, clip delay 3..6 s, range
  300..1000, scan_distance 1000, ready_guard. Every shot ends the clip (reload, clip delay). Aim error 0x004284e0
  (ai_02.cpp:1610-1731): sideways rand[-A, A] (A = fire_inaccuracy_angle, default 15: the data's fire_inaccuracy_xy is
  not read), up rand[0, fire_inaccuracy_height = 0.5], scaled by s(d) from fire_accuracy_min / max (100 / 700): d <=
  min 0.1, beyond max 1, else max(0.1, (d^2 - min^2) / (max^2 - min^2)); "infinite" accuracy 700 / 1500 for a guard
  beyond its range (+0x484 0x400000).
- Bullet p_captain_sniperbullet: speed 500 (x3 at load), damage 60, head_shot_multiplier 5, lifetime 2.

## 2. Rocket captain (CaptainObject with rocket_captain)

CaptainObject_Slot6 0x00434cd0 -> RocketCaptain_ReadProperties 0x00434ac0 (ai_04.cpp:2135-2211): the rkt_fire_* set
over the fire parameters (template keys; defaults range 200 / 350, inaccuracy height 10 / angle 15, accuracy 50 / 150,
infinite 400 / 8000; p_captain: 1 clip, 1 round, 1 burst, clip delay 3..6, range 300..500, accuracy 200 / 300,
infinite 210 / 1500), weapon 0x11 captain_rocketlauncher at ref_rhand, 0x00428250(0): one-shot fire anim, +0x484
0x8000000 set, the counters re-rolled. Anims (0x0043b610): ids 0..3 captain_rocket_tl..br (before the glide variants),
33..36 captain_rocket_fire_* (after the glide variants). Rocket p_captain_rocket: speed 40..80, dumb_fire, damage 100,
area damage 50, radius 10 / 20, fx_trail_captain_rocket, damages_buildings.

## 3. Jumper captain and jump markers

- Set-up 0x00425d30 (ai_01.cpp:2648-2675): jumper_captain creates the "turbo" pack (+0xb08, PlayerObject_SetTurboPack
  0x0047b9f0, at "chest", anim turbo_idle). Being a jumper = +0xb08 != 0. Fire range jumper_fire_range_min / max
  (400 / 450).
- Parameters (ai_01.cpp:2354-2390; default, p_captain): turbo_fwd_max_speed +0xae4 (2, 23), turbo_force +0xae8 (175,
  150), turbo_anti_gravity +0xaec stored x0.5 (0.25, 1.0 -> 0.5), turbo_glide_fwd_max_speed +0xaf4 (4, 65),
  turbo_glide_zvel +0xafc (10, 11), turbo_glide_xyvel +0xaf8 (30, 23), turbo_glide_power +0xb00 (10, 5),
  turbo_glide_anti_gravity +0xb04 stored x0.5 (default +0xaec x 0.25; 0.125 -> 0.0625), jumper_tag +0xb14 (0),
  jump_distance +0xb18 max (300) and +0xb1c min (125) read from the same key (original quirk: set, min = max).
- Gravity scale (GenericAIObject_Slot35 0x004326b0, ai_04.cpp:943): gliding (+0x484 0x30000000) +0xb04, jet only
  (0x10000000) +0xaec, else 0 (normal). Gravity is 64.348 (K 0x005bcd64).
- JumpMarker (vtable 0x005ac628, ai_01.cpp:2042-2111): list World+0xf10; marker_tag +0x464, marker_range +0x46c (50),
  occupant +0x468. Its point (vt+0x14c -> 0x0040de50) zeroes +0x46c first: exactly the marker's position (marker_range
  has no effect: original quirk). lev_3_m1: 45 markers, marker_tag 700.
- Marker choice 0x0042b480(self, rescue) (ai_03.cpp:589-720): no target and not a rescue -> the hiding-spot search.
  Skip markers +0x918 / +0x91c (the last left), occupied, or (both tags non-zero and different). 2D squared distances:
  nearer than min rejected; beyond max "far". dt = marker-to-target, dS = self-to-target. Until one qualifies, take a
  candidate unless one is held, it is far and its dt is not smaller; a candidate not far with dt < dS sets `closer`.
  With `closer` a candidate replaces the best only when dt < best, dt < range^2, dt < dS and not far. Success: occupy
  (+0x904), +0x934 = the point, launch velocity by the ballistic solver 0x00471a00 (horizontal speed 2 x move_speed,
  gravity +0xaec x 64.348), +0x484 |= 0x10000010 (glide bit cleared), fx FX_turbo_Launch_captain on the pack, anim 0x6b
  captain_jumper_forward looped, state 0xc4 with `closer`, else 0xc5.
- When: the pursuit vt+0x184 0x004295d0 (ai_02.cpp:2278-2313): a jumper for which 0x00454c20 is false (vt+0xb8's
  first out-value is 0: **guess**: standing on the landscape rather than an object) jumps (0x0042b480(self, 0));
  otherwise it walks 1000 ahead (state 0xbc) and at the walk's end (or near +0x934 facing it, dot > 0.65) launches a
  glide (0x0042a2c0: glide_xyvel, glide gravity, state 0xbd). Also the rescue from a knock-back (states 0xe7 / 0xea).
- Flight: 0xc4 (ai_06.cpp:2393-2430): rising faces its velocity; once falling, re-aims horizontally at v = dist /
  sqrt(|2 (z - z_target) / (+0xb04 x -64.348)|), opens the wings (Jumper_PlayWingSounds 0x0047bb30:
  veh_jumper_wings_out, loop veh_jumper_glide_lp), flags |= 0x30000000, glide set ids 0x6f, 0x70, 0x6d, 0x6e, the pack
  turbo_forward_cycle, +0x4ac = 0xc3, state 0xbd. 0xc5: faces its velocity only. 0xbd (glide, 2327-2353): faces the
  target, runs the fire cycle with flag 4 (shoots while gliding; never leaves on a lost line of sight, ai_02.cpp:1894),
  anims captain_jumper_glide_* / captain_jumper_glide_fire_*.
- Landing (contact with vz <= 0, GenericAIObject_Slot44 0x004320b0 -> 0x0042e1c0, game_02.cpp:986-1084): the turbo
  stopped (wings in, turbo_idle), anim 0x6c captain_jumper_land, state 0xbf; its end: state +0x4ac (0xc3): with flag 4
  and a target re-engage (vt+0x230), else reload (0x32).

## 4. Squad (SquadObject, vtable 0x005ab3e8)

- Data: all templates use p_squad (model squad_1, no collision): trigger_marker, marker_looping 1, marker_patrol 1,
  trigger_los 0, move_speed 40, patrol_speed 10, squad_tag 0, squad_sequence 0, resume_delay 10..20, charge_delay
  5..10, charge_count 1..1, goliath_cage 1. The formation is the template's default anim `<template>_ref`: static bone
  offsets ref_captain_0N / ref_regular_0N / ref_grunt_0N / ref_goliath_00 in the squad's frame.
- Load Squad_Init 0x00433220 (ai_04.cpp:1318-1464): Ai_InitGeneric (trigger_marker: wait for receive_trigger_activate,
  state 0x8c); group id +0x574 = -squad_tag; +0x488 0x10000 (a squad); members ref_grunt_0N "grunt", ref_regular_0N
  "grunt_regular", ref_captain_0N "captain" (each list stops at the first missing index), joined by 0x0042de60
  (ai_03.cpp:2252-2295: +0x574 the group, +0x9bc the squad, +0x9c0 the bone, placed at the bone, state 0xde idle);
  ref_goliath_00 makes a "goliath" (+0xb7c; with goliath_cage a goliath_cage with a peasant at its ref_chest) that takes
  over the squad's kill / death triggers; the squad leaves the target lists and joins World+0xf08.
- Trigger SquadObject_Slot14 0x00433a10: the goliath and every member to state 0xdc (follow the bone), then the base:
  the marker spline (0x0042d1b0; the squad and its members lose friction). End of a non-looping spline: state 1.
- Member state 0xdc (ai_06.cpp:3062-3089): steer to the squad's bone each frame, at most 2 x own move_speed, face the
  velocity. 0xde: stand.
- Contact: members do not engage while the squad is not halted (0x0042f8d0 lists 0xdc / 0xde as busy). The squad scans
  itself (0x0042be80, range scan_distance, states 0x96 / 0x97, not on a strict marker): SetTarget 0x00433c70: halt timer
  +0x490 = max(itself, rand resume_delay); state 0x98 (halted); with the flag and the charge timer out, rand
  charge_count members leave the squad and charge (0x0042ce70); the charge timer = rand charge_delay; the group hears of
  the target (+0x578 0.25..1 s). A member hit or killed alerts the squad (+0x23c(attacker / player, 1)). Members alerted
  while in 0xdc / 0xde stand and fight in place (state 0xdd) while the squad is halted, back to 0xdc when it moves on.
- Squad state 0x98 (ai_06.cpp:1819-1830): counts down; at 0 clears its target and resumes the spline. Update
  0x00446ba0 (ai_07.cpp:384-453): the goliath dead -> all members charge the player; no members and no goliath ->
  destroyed; while halted and a member still fights, the halt is re-armed.
- Same-tag squads with a higher squad_sequence share the halt (0x00433bc0): no shipped squads share a tag.

## 5. Grunt modes turret and bully

- turret (+0x488 0x40000000, obj_tag +0x534): AI_ResolveStartObjects 0x00425e20 (ai_02.cpp:203-244) finds the gun of
  that tag (list +0xea8); missing or occupied: the grunt is removed. Else he is on it at once: +0x898, the gun's flag 4,
  +0x484 |= 0x8000 | 0x200 (hold: he does not leave for a target out of the arc until his first firing cycle starts,
  ai_06.cpp:698 / 718), attached at the gun's origin (no ref_grunt_08), anim 0x69, state 0x7d. Afterwards an ordinary
  gunner. forge_cannon guns (kind != 1) use a 55 degree window, anims grunt_fcannon_fire / sit, forge_fire_delay_* and
  never get off.
- bully (+0x488 0x10000000, obj_tag): the peasant of that tag (list +0xef8; not found: ready roaming); state 0x118
  (ai_06.cpp:2546-2642): the stand set; every 10..30 s grunt_scare (0x7d) with the peasant's Peasant_man_scare, and,
  under the bully delay (60..75 s, game-wide), 70 % a peasant line / 30 % a grunt line. Ends when the peasant dies or
  when a target is found (the target update runs each frame): the peasant released (peasant_man_stand), the target
  pursued.

## 6. Monk (MonkObject, vtable 0x005aac28)

Init 0x004369a0 (ai_05.cpp:168-205): weapons monk_gun (0x19, projectile lightning_ball) and monk_gun2 (0x1a) at
ref_shoot_00, smoke fx, special counter +0xb58 = rand 1..3. No trenches, guns, taunt, melee, scare, turn helper. Anims
0x0043cb40 (monk_stand, monk_walk, monk_scan_1..3, monk_fire, monk_cast_spell_1 / 2, monk_die_1..3, monk_turbo,
monk_hanging, ...). Combat vt+0x220 0x00437130: +0xb58 > 0: decrement, fire; at 0 re-roll and n = rand 500: < 50 (no
+0x484 0x4000) twiglet summon (state 0xf2 -> 0xf3: turn to a point 30..40 ahead, cast_spell_2, up to 5 idle twiglets
round him at its end: docs/subsystems/twiglets.md section 5), < 200
basket rain (0xf1: cast_spell_1, 5 monk_basket at 40 around the target, falling from 450 at 150..200), else fire. The
hiding-spot search is MonkObject_Bamf 0x00436bb0: a teleport flight (0xf0) to a point 30..200 from the target, anim
monk_turbo, fol_monk_bamf_1 / 2. Knocked over: monk_hanging (0xf4) then bamf.

## 7. Q112 (Q112Object, vtable 0x005a2440)

A heavy chaingun grunt through the ordinary states (the boss is Q112Boss, spawned by Q's notes). Init 0x00435090: melee
range 25 / damage 35, bullet_scale (0.05 in data: bullets do 5 %), q112_chaingun at ref_chaingun, looping fire anim.
All-fours (knees) reaction yes; chase engages within 0.9 x range (as the archers); melee q112_gun_hit within 25. Anims
0x0043c330: q112_aim_*, q112_fire_*, q112_reload_*, q112_run, q112_walk, q112_hit_*, q112_die*, ...

## 8. Peasants (PeasantObject 0x005a8550; PeasantGenAI 0x005aa9c0)

PeasantObject (placed peasant_man / peasant_woman, p_peasants): no AI. Picked up (carried total < 3) they follow Roman on
a rope chain; a doormat (on_enter = peasant_saved_trigger) banks one (score 500); killed: killed_score, casualties, the
level lost at peasant_max_casualties. PeasantGenAI: civilians thrown from destroyed buildings with toss_peasants
(blown_up_chair anims, ragdoll), not counted.

## 9. The port (2026-10-15, agent-infantry)

Engine: `engine/src/ai.rs` (the shared state machine, `Kind` with the class tables, the class slots as `Kind` methods)
and its modules `ai/jumper.rs` (turbo pack, jump markers, flight), `ai/squad.rs` (squad anchor, formation, halt and
charge), `ai/monk.rs` (special roll, teleport, basket rain), `ai/bully.rs` (bully and peasant); level.rs makes every
grunt-class template (`ai::Kind::from_template`), squads and jump markers; charanim.rs plays the class anims through
`Grunt::anim` (`AnimFlags`: cold level, rocket, glide, sniper aiming).

What each class does here, with the tests (headless playtests from each level's start, tools/regress.py SUITE):
- **Placed captains** (all levels): created now (level.rs took only grunt_* models before); counts checked on lev_3_m1.
- **Sniper** (infantry_l2m1): the gun raised (captain_sniper_attach, a pose that holds him) when a target is taken,
  lowered when lost; one shot per clip, reload, clip delay 3..6 s; the captain_snipergun at ref_chest; bullets of
  p_captain_sniperbullet (damage 60, speed 500 x 3). Expect: a spawned sniper hears Roman, aims, keeps aiming at 120
  units and fires 1..6 shots in 8 s.
- **Rocket captain**: the rkt_fire_* set, the rocket launcher and its projectile, the rocket aim / fire anims (one-shot).
  Counted on lev_3_m1 (6); its firing is not driven by a test yet.
- **Jumper captain** (infantry_l3m1): on the ground the pursuit walks 0.2..0.4 s then glides at the target (launch at
  turbo_glide_xyvel under the glide's gravity, firing in the glide); in the air it flies to a jump marker (0x0042b480's
  choice, unit-tested in jumper.rs); lands (captain_jumper_land) and engages or reloads. Expect: jumps and landings
  after Roman is heard and steps 500 away.
- **Squad** (infantry_l2m1): members made at the formation bones, idle until the squad's trigger, then march with it
  along the spline; the squad's scan or a member's hit halts it, charge_count members charge, the rest fight in place;
  resumes when no member fights. Expect: 2 squads / 15 members, 5 marching after trigger 1600, a halt and a charge when
  Roman stands ahead.
- **Turret mode** (infantry_l2m1): six grunts on their stig_mgun at the start (the three forge-cannon grunts stand guard:
  the engine has no forge cannon yet).
- **Bully** (infantry_l4m2): four bullies scare their peasants every 10..30 s (grunt_scare, Peasant_man_scare, lines
  under the shared 60..75 s delay); Roman heard by one ends it (the peasant released, the target pursued).
- **Monk** (infantry_l4m2): the pursuit is a teleport flight round the target (5 headings, 30..200 out, on the ground),
  then it engages; the special roll casts the basket rain. Expect: a monk spawned 600 away teleports and arrives.
- **Q112** (infantry_l2m2): its anims, chaingun at ref_chaingun, bullet_scale (bullets do 5 %: player.rs), knees
  reaction; expect it engages Roman.

Debt and guesses (each with its plan):
- Fire notes: most fire anims carry no "fire" note in the export (q112_fire_tl 1.3 s, none); the engine fires one round
  per anim length (looped sets: per note when there are notes). The Q112's chaingun therefore fires far slower than
  its 30..40 rounds a burst suggest. Plan: read the note tracks from the .anm sources (tools/remaster_export_anim.py) or
  measure the original's rate with the oracle.
- Jumper on an object (0x00454c20 true, guess: standing on something): this port treats "ground under the feet within 3"
  as on the ground; the walk 1000 ahead (state 0xbc's grid path) is the chase's own movement. Plan: oracle a jumper
  scenario on lev_3_m1.
- Squad speed: patrol_speed with marker_patrol, else move_speed (guess at the speed getter); members ease into their
  bone within a few units (the original steers by force). The goliath member (2026-10-16, B11): the squad reserves its
  entity and goliath.rs makes it (`SpawnGoliath`): health 3000, goliath_stand, then goliath_walk to its bone while the
  squad moves (speed: the AI default 46, guess: p_goliath has no move_speed), never fighting; the cage and its peasant
  at the chest are drawn only (peasants not ported); it takes the squad's receive_trigger_kill, send_trigger_on_death
  and tag (the squad keeps none: mission.rs leaves a squad out of receive_trigger_kill removals). The tag reaches no
  ticker site yet (mission.rs lock_parent_tag, 0x0044e120). Test: engine/playtests/squad_goliath.json; oracle lev_3_m1
  / lev_4_m1 start: the goliaths' kind, position and health agree.
- Monk: the teleport's speed (2 x move_speed, ramp 1 s, slow within 20, no gravity) is a guess at the force's result; the
  knock-over hang (0xf4) is not ported. The twiglet summon is (2026-10-16, ai/monk.rs `Spell::Twiglets`, twiglet.rs).
- Q112: the melee (q112_gun_hit within 25), the low-health eye loop and the breadcrumb route are not ported;
  bullet_scale applies to every direct projectile hit (the original: bullet records, flag 0x400). Plan: carry the
  damage record's flags on player.rs Projectile.
- Turret mode on a forge cannon: the gun is not in the engine, the grunt stands guard. Plan: when the forge cannon is
  ported, `Grunt::start_gun` resolves to it (55 degree window, never gets off).
- Peasants (PeasantObject: rope chain, doormat drop-off, casualties) and PeasantGenAI are a mission / pickup subsystem,
  not grunt-class AI: not in this port. Bully lines use the peasant line names lower-cased (not checked against the
  bank).
- Not compared with the original by capture: every rate above comes from the code and the property sets.
