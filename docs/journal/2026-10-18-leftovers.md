# 2026-10-18: the leftovers (agent-leftovers)

Branch `agent-leftovers` from wip/full-decomp 453c30f; build dir engine/target-wt-leftovers. Four deferred features:
the untraced functions, the knockout bomb with the punch launch, the jaws' target test, the peasants' remainder.

## Done

### Knockout bomb and punch launch (features knockout-bomb, punch-launch)

engine/src/weapons/glove.rs (`GlovePlugin`, `Glove`, `Gloved`), engine/src/ai/launch.rs (Character_Launch 0x00431020,
`LaunchClock` = Game+0xb34), hooks in player.rs (Fire Special with special 7 asks the glove; Roman held, Fire blocked
while it is on), charanim.rs (modes 0x24 windup, 0x25..0x27 punches), level.rs (the glove model in the hand),
alarm.rs (the alarm troops wait while it is on: Game+0xcbc). The original: 0x004788a0 case 1 (game_06.cpp:2900), the
timers 0x00479142 (game_06.cpp:2391), the anim states game_05.cpp:1825-1850, the contact callback 0x00477c90
(game_05.cpp:2009), GenericAIObject slots 70..74 (0x0043a9b0, 0x0042f190, 0x0042f1d0, 0x0042f2f0, 0x0042f470),
GoRagdoll 0x00431310's launch. Numbers from p_globalsettings: autoaim_bof_factor_* 0.2 / 0.3, bof_range 500,
bof_speed_factor 3, bof_gravity_factor 1. Playtests weapon_glove, weapon_glove_miss.

Learned: the punch's "speed" argument (mass x bof_hit_force_factor) is unused by slot 74; the record's amount is 100,
so only GoRagdoll's Character_Launch (once per 10 s, Game+0xb34) throws the punched grunt far. bof_up_force_punch_1..3
are read and never used. Character_Launch flies the character toward a point 40..60 along the attacker's line from the
attacker (forward on an even roll, else back): a launched character can fly over the attacker.

### Jaws' target test (feature armed-jaws)

GruntObject_Slot147 0x00434e10 is vt+0x24c, asked by the shark gun's pass (Hud_UpdateCrosshair, call at 0x00407140)
and the shark's search (0x0047fa70, call at 0x0047fae9): weapons/targeting.rs `can_be_bitten_now`. can_be_sharked
(three levels clear it) is now on `Grunt`. Changes against the old guess: a gunner on his gun can be bitten (+0x484
0x8000), a grunt on anything but the landscape cannot (0x00454be0). The black hole kept the old guess as
`on_their_feet` (its vt+0x254 is not traced). Physics::static_below is new (the first static body under a point, past
the dynamic ones: a ray from inside a character's sphere hits the sphere first). Playtest weapon_shark_prey;
weapon_shark still passes.

### Peasants' remainder (feature peasants)

engine/src/peasant/toss.rs (`TossPlugin`): a destroyed house with toss_peasants throws out 1..3 PeasantGenAI villagers
in chairs (0x0040d390's villager part, PeasantGenAI 0x004615d0 / Slot6 / Slot55 / Slot95, the chair anims 0x0043c600),
with the scream cues (four tables read from the exe) under the peasant delay (shared with the drop-off lines) or the
voice delay (shared with the grunts' barks), and the tossed dialogue line (ai/dialogue.rs's table, no longer dead
code). Playtest peasants_tossed (lev_3_m3). 0x00439f70 is GoliathObject vt+0x260 and nothing calls it (not needed).

### Untraced (15 functions)

| address | what it is (evidence) | filed |
| --- | --- | --- |
| 0x00431f70 | AI vt+0xb8: the ground contact (object stood on +0x46c: struct atlas targets BuildingObject / DefaultGameObject), 15M calls through 0x00454bc0.. | replaced |
| 0x00451040 | class 38 = behaviour turret_upgrade (kBehaviours 0x005b2c24) = p_shootable_turret_upgrade; death sets Game+0xdd1, read by the base turret 0x0049b030 | ported (icobject.rs) |
| 0x004559c0 / f0 | +0x424 bit 0x200: Ray_Cast's walk of a hit up the attach chain stops at it (physics_01.cpp:3125) | object-collision |
| 0x00461790 | class 98 = ui_listbox (0x005b03cc): the menus' list boxes | replaced (gamemenu.rs) |
| 0x0047f4d0 | class 5 proj_ai_sniper_bullet's flag 0x20000: Roman's / Q's "Sniper" lines (audio_01.cpp:1597, audio_02.cpp:247) | ai-voice-lines |
| 0x00481010 | class 3 proj_stig_bullet's flag 0x80000 | ported (player.rs DamageKind::Stig) |
| 0x00481020 / 30 | class 13 proj_base_turret_bullet: flag 0x100 (the AI's 0x77.. hit anims, ai_04.cpp:450) and its scattered aim (base_defend_spread_pct) | base-turret |
| 0x00481790 / a0 | the sticky bomb's linger +0x58c: the penguin carrier and the jumper captain's take-off (ai_06.cpp:2507) | sticky-carriers (new) |
| 0x004a1e20 | the weapons' two_handed: 1 in every set of the 24 levels, never executed | not needed |
| 0x00477c90 | the player's contact callback: ground normal / object, steep friction, the punch | player-ground-object (new; punch part ported) |
| 0x00414580 | clears Controls +0x18, which nothing reads | not needed |
| 0x00419250 | the background fx's draw-order key (opcode 0x62); its only callers are the compare callback 0x00487ee0 (rel calls found by a .text scan) | fx-opcodes |

Method that paid off: the class id of an object is its behaviour name in kBehaviours (game_03.cpp:3023, strings read
from the exe), and a "no callers" function is often reached by a vtable or a list callback: a scan of .text for
`call rel32` and `call [reg+disp32]` (scratch scripts) settled 0x00419250 and 0x00439f70.

## Tests

Fast regress (tools/regress.py --exe target-wt-leftovers): see the end of this entry. level_smoke: see the end.

## Next / open

- player-ground-object: when the original clears +0x580 (the object stood on) is not traced; the Topsy Turvy bomb's
  refusal on an object waits on it.
- sticky-carriers: the penguin's run with a stuck bomb and the jumper captain's take-off.
- Twiglets answer the glove too (Twiglet vt+0x120 0x004a0420, its fling ai_09.cpp:1721): twiglet-extras.
- The alarm controller's other holds (Topsy Turvy's Game+0xc80, a live black hole Game+0xadc, the Guy Fawkes wait
  Game+0xae0) are not in alarm.rs: only the glove's was added.
- GoRagdoll's launch of flagged records (bit 7: the sticky bomb's 0x81 among them) when 10 s passed since the last
  launch is not in ai.rs's death (only the glove's punch uses ai/launch.rs): a candidate for the enemy AI's owner.
