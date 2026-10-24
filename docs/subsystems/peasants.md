# Peasants

The little villagers Roman rescues: he walks up to one, ties him on a rope behind him (at most three at a time) and
banks them on a door mat; the mission counts the saved and the lost. Recovered code: archive/src/game/game_01.cpp:1153-1385
(PeasantObject's load, remove, collision, contact, damage and kill slots, the bank helpers), archive/src/physics/physics_01.cpp:
560-765 (the update 0x00405d10: tie and rope), archive/src/audio/audio_01.cpp:76-120 / 1430-1494 (pick-up and drop-off lines),
archive/src/game/game_02.cpp:2187-2348 (MissionObject_OnTrigger: the bank), archive/src/ai/ai_04.cpp:1425-1450 (the squad goliath's
cage), archive/src/ui/ui_02.cpp:556-645 (the HUD counters). Remaster: engine/src/peasant.rs (PeasantPlugin), with small hooks
in mission.rs, level.rs, goliath.rs, ai/bully.rs, ragdoll.rs, hud.rs, oracle.rs.

## 1. Where they are

| Level | Peasants | Door mats | max_saved / max_casualties | saved_trigger -> success_trigger |
| --- | --- | --- | --- | --- |
| lev_2_m3 | 5 placed (tags 81..85, on_enter 333: kills the big red arrow 263) | 5, one_shot | 5 / 0 | 13 -> 3499 (objective text, counter 214) |
| lev_3_m1 | 3 in the squad goliaths' cages | 3, one_shot | 3 / 0 | 13 -> 4000 (the win) |
| lev_3_m3 | 9 placed | 9, one_shot | 9 / 1 | 7000 -> 4000 (the win) |
| lev_4_m1 | 3 in the squad goliaths' cages | 3, one_shot (tag 9999) | 3 / 0 | 13 -> 4001 |
| lev_4_m2 | 10 placed (bullied by grunts in mode bully) | 10, one_shot | 10 / 1 | 7000 -> 7001 |
| lev_bonus_m2 | 75 placed | 1, not one_shot | 75 / 1 | 7000 -> 4000 (the win) |

`peasant_max_casualties` 0 and 1 act alike: the first loss reaches it (0x004062a0 tests casualties >= max).

## 2. The class (PeasantObject, vtable 0x005a8550, behaviour `peasant`, p_peasants)

A TriggerObject (TriggerObject_Load 0x00497460: on_enter +0x460, sphere_radius +0x470, flags +0x474) with its own
slots. PeasantObject_Load 0x004056b0: count +0x484 (default 1), saved_score_value +0x490 (500), killed_score_value
+0x494 (-100); rope +0x478 = -1, link +0x47c = -1, +0x480 = 0; lists World+0xe9c (crosshair: AimClass Peasant, the
friendly crosshair), +0xef8 (the peasant list), +0xefc (the compass's first list); peasant_man_stand; key-framed
(Object_setPhysicsEnabled 0x004577b0(1): the argument is "keyframed"); FX_peasant on him (+0x498, released by
PeasantObject_Remove 0x00405850 and at the tie). p_peasants: max_health 2000, sphere_radius 20, no fx_death, no husk.

He is on none of the damage lists (projectiles World+0xe90, blasts +0xe94): bullets pass him (level.rs keeps him out
of the bullets' list), blasts spare him (`BlastProof`). His damage slot 0x00406280 takes a direct hit only while not
attached. No shipped source of a direct hit on a peasant was found: casualties are reachable through the kill slot
only (the playtest `damage` step).

## 3. The capture (slot 7 0x00405880, contact slot +0xb4 0x00406210)

His collision adds a phantom (0x004574e0): a sphere of sphere_radius centred sphere_radius above his body (its
bottom at his feet), collision group 8 (the player). Roman entering it while the peasant is alive, free (not attached,
0x004539e0) and fewer than three are carried (Game+0x26f4 < 3) captures him (+0x48c = 1) and sends his on_enter once.
The carried count rises at the next update (the tie), so two caught in one contact step both pass.

Roman's side of the contact is not traced (his Havok collision); peasant.rs models it as a sphere of 3.3 units 5.5
above his feet (`ROMAN_BODY_*`), fitted to two oracle measurements: on the flat the original catches him when Roman is
17.2..17.4 units off (peasant_l4m2 and an earlier run, frames 52 / 86), and falling straight onto him when Roman's feet
are 37.3..38.7 above the peasant's (a drop probe, frame 33).

## 4. The tie and the rope (the update 0x00405d10)

First update after the capture (unless attached): physics on (no longer key-framed), the pick-up sound and line
(0x004058f0), off the compass list, FX_peasant released, carried += count, a ragdoll (Object_SetRagdoll 0x00459000,
Body_addConstraints 0x00456570: limited hinges and ragdoll joints with a strength of (mass A + mass B) x 20), each
part's convex radius set to 1.0; link 0, rope 0, reeling.

Every update while tied (+0x478 >= 0):
- The chain: among the other peasants, the largest place below his own is his leader (place + 1); new on the chain
  (place 0), the largest of all; with neither, Roman (place 1). A change of place restarts the rope (length 0,
  reeling). peasant.rs `choose_leader`, worked out in the object list's order (placed ones by entry, cage ones after).
- The rope (`Rope::step`): its length takes the distance from the leader's point to his pelvis when 0; when the
  distance is over 3 and over the length, the pelvis is pulled by the excess with a hard key-frame (0x004df0d0,
  Havok's applyHardKeyFrame with 10: the velocity that gets there in 0.1 s, the rotation kept); reeling, the length
  shortens 5 units / s; above 5 it keeps reeling; below 3 it stops at 3.
- The leader's point: Roman's "center" bone (vt+0x90 with the name "center"); a peasant's pelvis (the centre of mass
  of his body named "center", 0x004586a0).

The tied peasant stands until pulled (the original's pelvis stays 2.7..3.3 above the ground), then is dragged
upright. ragdoll.rs `Build`: parts of radius 1.0, joints with a friction torque of 0.8 N m per kg of the two parts
(the original's 20 per kg in its units, where gravity is 64.35 units / s^2, scaled to keep the ratio to the weight
under the world's gravity, the game's 12.87 m/s^2 at 0.2 m / unit; 0.61 under Jolt's 9.81 before 2026-10-22). With it the remaster's pelvis is 1.1..1.7 lower than the original's while
dragged (oracle peasant_l4m2), within the trace's tolerance; limp joints left it 2.3 lower.

## 5. The bank (MissionObject_OnTrigger 0x0044d600, Game+0x26f0)

A door mat (behaviour broadcast, p_doormat: door_mat 1, so a one_shot mat keeps its on_enter; drawn: TriggerObject_Load
hides only a volume without "visible") sends peasant_saved_trigger when Roman steps on it. With any carried, while the
mat lives: the peasant with the largest place (0x00406330) is banked (0x004063a0: saved += count, carried -= count,
saved_score_value to the Rescues, count 0), killed (his kill slot: killed_score_value to the Accidents as well, the
original's quirk, docs/original-bugs.md), announced (0x0044d1f0), and the mat's own death slot runs (0x00497620: a
one_shot mat dies, Object_Kill: removed, fx_doormat_die, husk doormat_d). So a one-shot mat banks the last of the chain
and is gone; lev_bonus_m2's mat banks every carried one. Then, once saved >= peasant_max_saved,
peasant_success_trigger is sent and cleared (once).

mission.rs passes the sender with each volume's trigger (`VolumeSent`); a dead volume sends nothing more.

## 6. Sounds and lines

- Pick-up (0x004058f0): ui_peasant_pickup on him; at most every 10 s of game time (Game+0xbcc, from 0 at the level
  start: none in the first 10 s) r = (rand() x span) >> 15 + base: 21 PE1 / PE2 peasant lines (0..0x833, 100 each, on
  him), Roman's three (0x834..0x95f, unplaced), Q's two (0x960..0xa28, with Q). A woman starts at 0x834 (no peasant
  line).
- Drop-off (0x0044d1f0): ui_peasant_release (on the mission object in the original; unplaced here, guess); at most
  every 10 s (Game+0xba4) fourteen peasant lines at the mat + 5 up (0..0x577), Roman's two, Q's three and Jonesy's one
  by who is with him (top 0x63f / 0x6a3 / 0x76b / 2000). A woman starts at 0x578.
- The bully's lines (pe150038_peasantcaptured, ...) are ai/bully.rs's.
- The voice picks draw from peasant.rs's own CRT stream (`PeasantRng`), so levels without peasants draw as before.

## 7. Losses (the kill slot 0x004062a0)

killed_score_value to the Accidents (fx.rs pays the record's score); with a count left: carried -= count when he was
captured, casualties += count, and at casualties >= peasant_max_casualties the level is lost: 0x0044a580 state 8, whose
text is Game+0x2674, timeout_text (default ui_timeout, a text id missing from every level's bank): Mission::fail_state.

## 8. The cage on a squad goliath (Squad_Init 0x00433220)

With goliath_cage the squad's goliath carries a goliath_cage at its chest and a random peasant_man / peasant_woman in
it at ref_peasant_1 by his "center" (Object_Attach flags 0x14). While attached he cannot be captured (0x004539e0) and
takes no damage. When the goliath dies the cage (attached without flag 4) is removed and the peasant (flag 4) is
detached; flag 0x10 makes him dynamic (Object_Detach 0x00453b20 -> 0x004577b0(0)): he falls free. peasant.rs
`cage_parent_died` / `free_fall` (a box body round peasant_coll until he is tied).

## 9. Others

- Bully (ai/bully.rs): the bully lets go when his peasant is dead or ragdolled (+0x421): a tied peasant ends it.
- Compass (ui_compass_objective 0x00466790): while World+0xefc holds anything (the peasants not yet tied, caged ones
  included) the arrow points at its nearest member; a tagged objective wins only when strictly nearer
  (mission.rs `CompassList`).
- HUD (behaviour ids 105 / 106 / 107 / 129 from the behaviour table 0x0045df60, slot 47 of their vtables):
  ui_captured_count 0x00465be0 the counts on the list (0x004063f0) less the carried (still to find), ui_safe_count
  0x00465c90 saved, ui_return_count 0x00465ce0 carried, ui_peasant 0x00465d30 the icon; all only while
  peasant_max_saved > 0. (hud.md had the first three swapped as guesses; the icon used to show on every level.)

- Thrown villagers (engine/src/peasant/toss.rs, 2026-10-18): a house (behaviour building) with toss_peasants
  (generator flag 0x800, 0x0040ad80) that is destroyed (0x0040cf40) pays score_value_peasant_building and throws out
  1..3 villagers (0x0040d390 with peasants): peasant_man / peasant_woman (50/50) made as class peasantgenai
  (PeasantGenAI 0x004615d0, move_speed 40: Slot6 0x00435260) at the house + 5 up, a prop_peasant_chair at their
  "center" bone, launched from Roman by Character_Launch 0x00431020 (ai/launch.rs: speed 2.5..3 x 40, spread 75,
  the arc toward a point 40..60 from Roman: they fly at him or past him), flailing in Peasant_blown_up_chair / _2
  (0x0043c600, every anim request). A man screams one of twelve new cues when the peasant delay (Game+0xba4, shared
  with the drop-off lines; interval peasant_delay_min..max) has passed, else one of ten; a woman nine / nine under the
  grunts' voice delay (Game+0xb44, ai.rs Barks Vo); after a man's scream a line of the tossed table (0x005fb6d0)
  by whoever can say it (0x0040d250: ai/dialogue.rs). On landing (0x0042e1c0's fall block -> Slot95 0x004353b0) the
  chair is let go with the villager's velocity and 5..8 up, the villager goes down as a ragdoll (GoRagdoll quiet, its
  body 5 s); the chair goes with the body (Slot55 0x00435330). Houses with it: lev_1_m4, 2_m2, 2_m3, 2_m4, 3_m1, 3_m2,
  3_m3, 3_m4_bd, 4_m5, 5_m2, 5_m5 (instances' own toss_peasants over their sets).
- The "captured / ambient" voice pick 0x00439f70 is GoliathObject's vt+0x260 (PeasantCaptured lines when it has a
  squad, else GOL5000x_Ambient): nothing in the game calls that slot (no call [reg+0x260] in .text), so it never plays.

## 10. Oracle

Hook (archive/hook/src/testoracle.cpp): "peasants" (World+0xef8: id, position, captured, link, rope, count, attached, and a
tied one's pelvis "center": his object position stays where he was tied) and "peasant_count" (carried, saved,
casualties). The remaster writes the same (oracle.rs, peasant.rs `trace_list`); a banked or lost peasant is not a
"deaths" event (the hook's deaths are DefaultGameObject / FxExplosion deaths). tools/oracle_diff.py families:
peasants.alive / .captured and peasant_count.carried / .saved / .casualties (the other game's value reached within
peasant_frames, 10), peasant.center (the pelvis within peasant_dist, 6) and peasant.rope (within peasant_rope, 1.5) of
the same peasant at the nearest line in time.

Scenario peasant_l4m2 (suite): Roman warped onto the ground 30 units east of peasant 291, walking backwards past him
onto door mat 314. 2026-10-16: every peasant family same. Captured at frame 52 in both (16.7 vs 16.4 units off),
ui_peasant_pickup at 52 in both, the pelvis 1.0..1.7 units from the original's while dragged, banked at 158 vs 160,
trigger 7000 and ui_peasant_release within 2 frames. Known in it: C1 (companions 12.7 units off the original's), G1
(the stig_mgun gunner 364 hits the original's Roman once at 188, the remaster's misses), B17 sounds.

Found on the way: the original's Roman stands for ~40 frames after landing from a 9-unit drop (the first warp at z
125: no movement from frame 20 to 60 with S held); the remaster's walks at once. The scenario warps onto the ground
instead; the landing hold is a player-movement difference for player.rs (open).

## 11. Tests

- engine/playtests/peasants_l2m3.json (regress): all five peasants of lev_2_m3 by warps: on_enter 333 at each capture,
  three carried and a fourth refused, the chain's places 1 / 2 / 3, each one-shot mat banking the last of the chain,
  the HUD counters, peasant_saved_trigger 13 five times, peasant_success_trigger 3499 once at five, Rescues 2500 and
  Accidents -500 (the quirk).
- engine/playtests/peasants_bonus_m2.json (regress): two caught in one step, the limit of three, lev_bonus_m2's
  non-one-shot mat banking all three at once (trigger 7000 once), then a fourth.
- engine/playtests/peasants_lost_l4m2.json (regress): a tied peasant survives a blast (not on the blast list) and a
  1000 hit; 1500 more kill him: casualties 1, carried 0, Accidents -100, the level lost (max_casualties 1).
- engine/playtests/peasants_tossed.json (regress, lev_3_m3): two destroyed houses throw out 1..3 villagers each, in
  the air at once, all landed within 8 s.
- engine/playtests/peasants_cage_l3m1.json (regress): the three cage peasants held (no capture while attached), the
  squad's kill trigger 2001 kills the goliath, its peasant falls free, is captured and banked at mat 66.
- peasant.rs unit tests: the rope (takes the distance, pulls the excess, reels to 3), the chain's leaders.
- Oracle peasant_l4m2 (oracle_suite).

## 12. Open

- Roman's reach (`ROMAN_BODY_*`) is a two-point fit, not his collision; a third probe at another height would test it.
- The dragged pelvis rides 1.1..1.7 units lower than the original's (the joints' friction is the original's strength
  scaled by the weight, the masses are Jolt's).
- ui_peasant_release plays unplaced (the original plays it on the mission object).
- Thrown villagers: the chair's box (2.4 x 2.4 x 4) is a guess from the model; the chair does not fade with the body
  (Slot64 0x00435380: feature character-fade); the dialogue's speaker test takes the level's list +0xf04 as empty
  (JON50172 / Q1150162 never chosen: which list it is was not traced); the flight lands on anything its step meets.
- Peasants are not saved (savegame.rs): a game saved mid-rescue restores them at their placements, and the counters
  at 0.
- No oracle scenario for the cage yet (the original's goliaths need killing by hand in a scripted run); the playtest
  covers it.
