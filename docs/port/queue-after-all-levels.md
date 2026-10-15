# Port queue after the all-levels coverage

Functions the original executes on the other levels (tools/coverage_level.py --level all) that the coverage before (coverage_before_all_levels.json) did not reach, by subsystem and recovered source file. State: **left** = the remaining port work (no engine file cites it, no reason in docs/port/*.csv); ported / explained = already handled. Levels: the levels whose coverage runs execute it. Regenerate: `python tools/port_queue.py --new-since work/coverage_before_all_levels.json --md docs/port/queue-after-all-levels.md`.

| subsystem | newly reached | left | ported | explained |
| --- | ---: | ---: | ---: | ---: |
| ai | 108 | 0 | 42 | 66 |
| game | 131 | 0 | 62 | 69 |
| ui | 2 | 2 | 0 | 0 |
| audio | 20 | 0 | 3 | 17 |
| anim | 11 | 6 | 5 | 0 |
| camera | 1 | 1 | 0 | 0 |
| render-fx | 7 | 0 | 3 | 4 |
| havok-ext | 2 | 2 | 0 | 0 |
| math | 5 | 4 | 1 | 0 |
| physics-glue | 3 | 3 | 0 | 0 |
| render-post | 5 | 0 | 5 | 0 |
| render-scene | 1 | 1 | 0 | 0 |
| unknown | 3 | 3 | 0 | 0 |
| all | 299 | 22 | 121 | 156 |

## Left: the remaining port work

| subsystem | file | address | name | size | levels |
| --- | --- | --- | --- | ---: | --- |
| ui | src/ui/ui_01.cpp | 0x00406740 | fn_00406740 | 66 | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5, bonus_m3 |
| ui | src/ui/ui_02.cpp | 0x00465b70 | UiText_ShowTime | 112 | bonus_m1, bonus_m2 |
| anim | src/anim/anim_01.cpp | 0x0043aaa0 | fn_0043aaa0 | 42 | 3_m1, 4_m1 |
| anim | src/anim/anim_01.cpp | 0x0043bcf0 | fn_0043bcf0 | 126 | 2_m4 |
| anim | src/anim/anim_01.cpp | 0x0043c600 | fn_0043c600 | 33 | 1_m4, 2_m4, 4_m5 |
| anim | src/anim/anim_01.cpp | 0x0043c630 | fn_0043c630 | 8 | 2_m1, 2_m3 |
| anim | src/anim/anim_01.cpp | 0x0043c6e0 | fn_0043c6e0 | 79 | 1_m3, 5_m4 |
| anim | src/anim/anim_02.cpp | 0x004ebe60 | fn_004ebe60 | 98 | 2_m1, 2_m3 |
| camera | src/game/game_01.cpp | 0x0040fa70 | fn_0040fa70 | 22 | bonus_m3 |
| havok-ext | src/engine/missing_01.cpp | 0x004a8840 | SpeedTrapAgent_CreateSwapped | 52 | 2_m3 |
| havok-ext | src/physics/physics_04.cpp | 0x004a9180 | SpeedTrapSwappedAgent_arePenetrating | 23 | 2_m3 |
| math | src/engine/engine_02.cpp | 0x004e7760 | Vec2_Length | 22 | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |
| math | src/engine/engine_02.cpp | 0x004e77e0 | Vec3_Sub | 25 | 4_m2 |
| math | src/engine/engine_02.cpp | 0x004e78d0 | Vec4_Normalize | 93 | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| math | src/engine/engine_02.cpp | 0x004e8040 | Vec2_Distance | 27 | 4_m2 |
| physics-glue | src/physics/physics_01.cpp | 0x00405d10 | fn_00405d10 | 1266 | 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, bonus_m2 |
| physics-glue | src/physics/physics_01.cpp | 0x0042cb80 | Object_ApplyMaterialFriction | 143 | 3_m1, 5_m4, bonus_m3 |
| physics-glue | src/physics/physics_02.cpp | 0x004583d0 | Body_ApplyAngularImpulse | 106 | 1_m1_b, 1_m3, 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m4, bonus_m1 |
| render-scene | src/renderer/render_r4.cpp | 0x00488130 | fn_00488130 | 15 | 5_m1, 5_m2, 5_m3 |
| unknown | src/engine/engine_01.cpp | 0x00407fe0 | fn_00407fe0 | 6 | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5, bonus_m3 |
| unknown | src/engine/engine_01.cpp | 0x004090b0 | Tokens_NextInt | 27 | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| unknown | src/engine/engine_01.cpp | 0x0041f4f0 | fn_0041f4f0 | 5 | 1_m4, 3_m4_bd, 4_m5, 5_m5 |

## Every newly reached function

## ai

### src/ai/ai_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00409110 | fn_00409110 | 41 | explained | 5_m5 |
| 0x0040c710 | fn_0040c710 | 47 | explained | 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0040dd70 | JumpMarker_Slot6 | 144 | ported | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1, bonus_m2 |
| 0x0040de00 | JumpMarker_Slot55 | 42 | explained | bonus_m1 |
| 0x0040de40 | JumpMarker_Slot82 | 14 | explained | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |
| 0x00424630 | fn_00424630 | 99 | explained | bonus_m1, bonus_m3 |

### src/ai/ai_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00426600 | fn_00426600 | 14 | explained | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x00426640 | fn_00426640 | 98 | ported | 2_m4, 5_m5 |
| 0x004271c0 | fn_004271c0 | 65 | explained | 2_m4 |
| 0x00427c80 | fn_00427c80 | 503 | explained | 2_m3, 3_m1, 3_m3, bonus_m1 |
| 0x00428340 | fn_00428340 | 402 | ported | 1_m1_b, 1_m3, 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m4, bonus_m1 |
| 0x004292f0 | fn_004292f0 | 732 | ported | 1_m3, 2_m2, 2_m3, 3_m2, 4_m1, 4_m2, 5_m2, 5_m4, bonus_m1, bonus_m3 |
| 0x00429a50 | fn_00429a50 | 102 | ported | 2_m4, 3_m4_bd, 5_m5 |
| 0x00429ac0 | fn_00429ac0 | 532 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x00429ce0 | fn_00429ce0 | 534 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0042a2c0 | fn_0042a2c0 | 564 | ported | 2_m3, 3_m1, 3_m3 |
| 0x0042a660 | fn_0042a660 | 327 | ported | 1_m3, 5_m4 |

### src/ai/ai_03.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0042b480 | fn_0042b480 | 991 | ported | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |
| 0x0042b9e0 | fn_0042b9e0 | 69 | explained | 2_m4 |
| 0x0042bb10 | fn_0042bb10 | 333 | explained | 2_m4 |
| 0x0042c670 | fn_0042c670 | 115 | explained | 1_m3 |
| 0x0042de30 | fn_0042de30 | 47 | explained | 5_m4, bonus_m3 |
| 0x0042de60 | fn_0042de60 | 264 | ported | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x00430140 | fn_00430140 | 510 | explained | bonus_m3 |

### src/ai/ai_04.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00432cb0 | FakeGruntObject_Slot6 | 292 | ported | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00432e60 | FakeGruntObject_Slot7 | 126 | explained | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00432ee0 | fn_00432ee0 | 10 | explained | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00432ef0 | fn_00432ef0 | 268 | ported | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x004331f0 | fn_004331f0 | 35 | ported | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00433220 | Squad_Init | 2023 | ported | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x00433a10 | SquadObject_Slot14 | 127 | ported | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x00433a90 | SquadObject_Slot55 | 202 | explained | 5_m4 |
| 0x00433b60 | SquadObject_Slot7 | 28 | explained | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x00433b80 | fn_00433b80 | 7 | explained | bonus_m3 |
| 0x00433b90 | fn_00433b90 | 7 | explained | 3_m1, 5_m4, bonus_m3 |
| 0x00433ba0 | fn_00433ba0 | 13 | explained | bonus_m3 |
| 0x00433bc0 | fn_00433bc0 | 168 | explained | 3_m1, 5_m4, bonus_m3 |
| 0x00433c70 | SquadObject_Slot143 | 499 | ported | 3_m1, 5_m4, bonus_m3 |
| 0x00433fe0 | GoliathObject_Slot6 | 298 | explained | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x00434150 | GoliathObject_Slot7 | 43 | explained | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x00434290 | GoliathObject_Slot96 | 20 | explained | 3_m1 |
| 0x004342b0 | GoliathObject_Slot109 | 26 | explained | 3_m1 |
| 0x004342d0 | GoliathObject_Slot14 | 93 | explained | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x004348a0 | SniperCaptain_Slot6 | 139 | ported | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 |
| 0x00434ac0 | RocketCaptain_ReadProperties | 526 | ported | 3_m1, 3_m3, 5_m1, 5_m3, bonus_m1, bonus_m3 |
| 0x00434ea0 | GruntObject_Slot66 | 270 | explained | 1_m3 |
| 0x00435090 | Q112Object_Slot6 | 218 | ported | 2_m2, 3_m2, 5_m1, 5_m4, bonus_m1 |
| 0x00435260 | PeasantGenAI_Slot6 | 201 | explained | 1_m4, 2_m4, 4_m5 |
| 0x00435330 | PeasantGenAI_Slot55 | 77 | explained | 4_m5 |
| 0x00435380 | PeasantGenAI_Slot64 | 40 | explained | 4_m5 |
| 0x004353b0 | PeasantGenAI_Slot95 | 112 | explained | 1_m4, 2_m4, 4_m5 |
| 0x00435420 | fn_00435420 | 835 | ported | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x00435770 | Q112Boss_Slot8 | 223 | ported | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x00435940 | Q112Boss_Slot102 | 515 | ported | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x00435b50 | fn_00435b50 | 1097 | ported | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x00435fa0 | SaucerObject_Slot52 | 97 | ported | 5_m3, 5_m4 |
| 0x00436090 | SaucerObject_Slot6 | 228 | ported | 2_m1, 2_m3 |
| 0x00436180 | ZeppelinObject_Slot6 | 221 | ported | 2_m1, 4_m5, 5_m2, 5_m3 |
| 0x00436390 | ColossusObject_Slot6 | 287 | ported | 1_m3, 5_m4 |
| 0x00436600 | Q112Boss_Slot52 | 36 | ported | 5_m4 |
| 0x00436630 | ShrubPatrol_Slot6 | 234 | ported | 3_m4_bd, 4_m5, 5_m5 |
| 0x004367a0 | ShrubPatrol_Slot18 | 19 | explained | 3_m4_bd, 4_m5, 5_m5 |
| 0x004367c0 | ShrubPatrol_Slot14 | 87 | ported | 3_m4_bd, 4_m5, 5_m5 |

### src/ai/ai_05.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004369a0 | MonkObject_Init | 393 | ported | 4_m1, 4_m2 |
| 0x00436b30 | fn_00436b30 | 32 | explained | 4_m1, 4_m2 |
| 0x00436b50 | fn_00436b50 | 12 | explained | 4_m2 |
| 0x00436b60 | fn_00436b60 | 39 | explained | 4_m2 |
| 0x00436b90 | fn_00436b90 | 25 | ported | 4_m2 |
| 0x00436bb0 | MonkObject_Bamf | 1395 | ported | 4_m2 |
| 0x00437130 | fn_00437130 | 175 | ported | 4_m2 |
| 0x004371e0 | fn_004371e0 | 96 | explained | 4_m2 |
| 0x00437860 | fn_00437860 | 216 | explained | 1_m1_b, 1_m3, 2_m1, 2_m2, 2_m3, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x0043a200 | fn_0043a200 | 167 | explained | 2_m2, 3_m2, 5_m4, bonus_m1 |
| 0x0043c640 | fn_0043c640 | 8 | explained | 2_m1, 4_m5, 5_m2, 5_m3 |
| 0x0043c650 | fn_0043c650 | 40 | explained | 1_m3, 5_m4 |
| 0x0043ce10 | fn_0043ce10 | 22 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0043ea70 | fn_0043ea70 | 47 | explained | 1_m3, 3_m2 |
| 0x0043ec60 | fn_0043ec60 | 139 | explained | 1_m1_b, 1_m3, 2_m1, 2_m2, 2_m3, 3_m2, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m4, bonus_m1, bonus_m3 |

### src/ai/ai_07.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00446440 | ShrubPatrol_Update | 30 | explained | 3_m4_bd, 4_m5, 5_m5 |
| 0x00446460 | FakeGrunt_Update | 1823 | ported | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00446ba0 | SquadObject_Update | 568 | ported | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x00447040 | GoliathObject_Update | 832 | ported | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x00447400 | Q112Boss_Update | 243 | ported | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x00447500 | MonkObject_Update | 137 | explained | 4_m1, 4_m2 |
| 0x0044b470 | Marker_GetFaceDir | 10 | ported | 1_m3, 5_m4 |
| 0x0044b710 | Spline_GetEndPoint | 68 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0044b760 | Spline_EndsAtScaleWall | 74 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0044b7b0 | fn_0044b7b0 | 12 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0044b7e0 | fn_0044b7e0 | 12 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0044b7f0 | fn_0044b7f0 | 30 | explained | 2_m4, 5_m5 |
| 0x0044b810 | Marker_GetNextRange | 23 | explained | 1_m1_b, 2_m2, 2_m4, 3_m2, 3_m3, 4_m2, 5_m1, 5_m4, 5_m5, bonus_m1, bonus_m2, bonus_m3 |
| 0x0044b830 | AIMarker_NextTag | 77 | ported | 1_m1_b, 2_m2, 2_m4, 3_m2, 3_m3, 4_m2, 5_m1, 5_m4, 5_m5, bonus_m1, bonus_m2, bonus_m3 |
| 0x0044b910 | Marker_GetActionName | 6 | explained | 2_m4, 3_m4_bd, 5_m5 |
| 0x0044b980 | AIMarker_Advance | 167 | ported | 1_m1_b, 2_m2, 2_m4, 3_m2, 3_m3, 4_m2, 5_m1, 5_m4, 5_m5, bonus_m1, bonus_m2, bonus_m3 |
| 0x0044bff0 | AIMarker_Step | 63 | explained | 1_m1_b, 2_m2, 2_m4, 3_m2, 3_m3, 4_m2, 5_m1, 5_m4, 5_m5, bonus_m1, bonus_m2, bonus_m3 |
| 0x0046f280 | Grid_IsFreeAt | 83 | ported | 4_m2 |

### src/ai/ai_08.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00486c70 | Q111Object_Slot112 | 14 | explained | 5_m2 |
| 0x00486c80 | Q111Object_Slot149 | 8 | explained | 5_m2 |
| 0x00487040 | Q111Object_Slot78 | 16 | explained | 1_m3 |
| 0x0048fac0 | SheepObject_Slot105 | 103 | explained | 2_m1, 2_m2, 2_m3, 2_m4, 3_m4_bd, 5_m5 |
| 0x0048fc60 | SheepObject_Slot86 | 37 | explained | 2_m2, 2_m3 |
| 0x0049cdc0 | fn_0049cdc0 | 54 | explained | 3_m2, 4_m1, 4_m2 |
| 0x0049ce00 | fn_0049ce00 | 104 | explained | 3_m2, 4_m1, 4_m2, 4_m3, 4_m5, bonus_m3 |
| 0x0049ceb0 | TwigletController_Slot6 | 120 | explained | 3_m2, 4_m1, 4_m2 |
| 0x0049d010 | TwigletController_Slot9 | 107 | explained | 3_m2, 4_m1, 4_m2 |
| 0x0049d080 | TwigletController_Slot47 | 821 | explained | 3_m2, 4_m1, 4_m2 |
| 0x0049d3c0 | TwigletGenerator_Slot6 | 368 | explained | 3_m2, 4_m1, 4_m2, 4_m3, 4_m5, bonus_m3 |
| 0x0049d530 | TwigletGenerator_Slot14 | 68 | explained | 3_m2, 4_m1, 4_m2, 4_m3, 4_m5, bonus_m3 |

## game

### src/engine/missing_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0049a0b0 | MountedTurret_BoneCallback | 50 | explained | 1_m1_b, 1_m3, 2_m1, 2_m2, 2_m3, 5_m1, 5_m2, 5_m3, bonus_m1 |

### src/game/game_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004056b0 | PeasantObject_Load | 412 | explained | 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, bonus_m2 |
| 0x00405880 | fn_00405880 | 108 | explained | 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, bonus_m2 |
| 0x004063f0 | fn_004063f0 | 7 | explained | 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, bonus_m2 |
| 0x004087b0 | fn_004087b0 | 24 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0040a700 | AnimPlayer_Load | 194 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0040a7d0 | AnimPlayer_OnMessage | 181 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0040df90 | fn_0040df90 | 8 | explained | 1_m3, 3_m3, bonus_m2 |
| 0x0040dfa0 | fn_0040dfa0 | 16 | explained | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |
| 0x0040dfb0 | fn_0040dfb0 | 77 | explained | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |

### src/game/game_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00415900 | fn_00415900 | 7 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00436820 | ICObject_Load | 320 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00436960 | fn_00436960 | 20 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00436980 | fn_00436980 | 29 | ported | 3_m4_bd, 5_m5 |
| 0x0043a8c0 | fn_0043a8c0 | 5 | explained | 2_m2, 4_m2, 5_m4, bonus_m3 |
| 0x0043aa00 | fn_0043aa00 | 6 | explained | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |

### src/game/game_03.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00451070 | RocketTowerBuilding_Remove | 60 | ported | bonus_m1 |
| 0x004510b0 | RocketTowerBuilding_SpawnTurret | 169 | ported | 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x004539e0 | fn_004539e0 | 9 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00454be0 | fn_00454be0 | 51 | explained | 1_m3 |
| 0x00457aa0 | fn_00457aa0 | 7 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |

### src/game/game_04.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00461570 | SniperCaptain_Construct | 18 | explained | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 |
| 0x00461590 | Q112Object_Construct | 18 | explained | 2_m2, 3_m2, 5_m1, 5_m4, bonus_m1 |
| 0x004615d0 | PeasantGenAI_Construct | 18 | explained | 1_m4, 2_m4, 4_m5 |
| 0x004615f0 | FakeGruntObject_Construct | 18 | explained | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00461630 | ColossusObject_Construct | 18 | explained | 1_m3, 5_m4 |
| 0x00461650 | GoliathObject_Construct | 18 | explained | 3_m1, 3_m4_bd, 4_m1, 4_m5, 5_m5 |
| 0x004616b0 | MonkObject_Construct | 18 | explained | 4_m1, 4_m2 |
| 0x004616f0 | ZeppelinObject_Construct | 18 | explained | 2_m1, 4_m5, 5_m2, 5_m3 |
| 0x00461710 | SaucerObject_Construct | 18 | explained | 2_m1, 2_m3 |
| 0x00461730 | SheepObject_Construct | 18 | explained | 2_m1, 2_m2, 2_m3, 2_m4, 3_m4_bd, 5_m5 |
| 0x00461770 | SquadObject_Construct | 18 | explained | 2_m1, 3_m1, 4_m1, 5_m4, bonus_m3 |
| 0x004617c0 | ICObject_Construct | 18 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x004617e0 | ShrubPatrol_Construct | 18 | explained | 3_m4_bd, 4_m5, 5_m5 |
| 0x00461cb0 | fn_00461cb0 | 12 | explained | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x00461ec0 | fn_00461ec0 | 7 | explained | 5_m2 |
| 0x0046a6d0 | PubObject_SendEntryFailure | 30 | ported | 1_m3 |
| 0x004720e0 | fn_004720e0 | 137 | ported | 2_m2, 3_m3, 4_m3, 5_m4, bonus_m1, bonus_m2, bonus_m3 |
| 0x00472470 | fn_00472470 | 46 | ported | bonus_m3 |
| 0x004724e0 | fn_004724e0 | 49 | ported | 5_m4 |

### src/game/game_05.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00472850 | fn_00472850 | 132 | ported | 3_m2, bonus_m2, bonus_m3 |
| 0x004728e0 | fn_004728e0 | 68 | ported | 3_m3, 4_m3, 5_m4, bonus_m1, bonus_m2 |

### src/game/game_06.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0047b8e0 | fn_0047b8e0 | 29 | ported | 3_m1, 3_m2 |
| 0x0047bba0 | PlayerObject_PlayChestFx | 58 | explained | 2_m3, 3_m1, 3_m2, 3_m3, 4_m3, bonus_m1 |
| 0x0047df70 | fn_0047df70 | 82 | explained | 4_m2 |
| 0x0047dfd0 | fn_0047dfd0 | 127 | ported | 2_m3, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0047e050 | fn_0047e050 | 108 | explained | bonus_m3 |
| 0x0047f210 | fn_0047f210 | 139 | explained | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x0047f2a0 | fn_0047f2a0 | 550 | explained | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x0047f4d0 | fn_0047f4d0 | 11 | explained | 2_m3, 3_m1, 3_m3, 4_m1, 5_m3 |

### src/game/game_07.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00480510 | VindalooProjectile_ReadProps | 203 | explained | bonus_m3 |
| 0x004805e0 | VindalooProjectile_Flags | 11 | explained | bonus_m3 |
| 0x004805f0 | VindalooProjectile_Update | 801 | explained | bonus_m3 |
| 0x00480920 | RocketTowerProjectile_ReadProps | 256 | ported | 2_m3, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x00480a20 | RocketTowerProjectile_Update | 830 | ported | 2_m3, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x00480d60 | RocketBudProjectile_ReadProps | 69 | explained | 2_m2, 2_m3, 3_m1, bonus_m1 |
| 0x00480db0 | RocketBudProjectile_Launch | 111 | ported | 2_m2, 3_m1 |
| 0x00480e20 | RocketBudProjectile_Update | 155 | explained | 2_m2, 3_m1 |
| 0x00480ec0 | Projectile_Launch_00480ec0 | 155 | ported | 1_m4, 2_m2, 2_m3, 2_m4, 3_m4_bd, 4_m5, 5_m5, bonus_m1 |
| 0x00480f60 | ForgeCannonProjectile_Update | 159 | explained | 2_m2, 2_m3, bonus_m1 |
| 0x00481000 | ForgeCannonProjectile_Flags | 11 | explained | 2_m2, 2_m3, bonus_m1 |
| 0x00481020 | fn_00481020 | 11 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5 |
| 0x00481030 | fn_00481030 | 223 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00481c90 | fn_00481c90 | 59 | explained | 1_m1_b, 1_m3, 1_m4, 2_m3, 2_m4, 3_m1, 3_m3, 3_m4_bd, 4_m1, 4_m2, 4_m3, 4_m5, 5_m1, 5_m4, 5_m5, bonus_m1, bonus_m3 |
| 0x00481ce0 | fn_00481ce0 | 18 | explained | bonus_m3 |
| 0x00481d00 | fn_00481d00 | 9 | explained | bonus_m3 |
| 0x00481d10 | fn_00481d10 | 149 | explained | bonus_m3 |
| 0x00481db0 | BlackHoleProjectile_ReadProps | 310 | explained | 5_m2 |
| 0x00481ef0 | BlackHoleProjectile_Update | 443 | explained | 5_m2 |
| 0x004820b0 | fn_004820b0 | 111 | ported | 1_m1_b, 1_m3, 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, 5_m1, 5_m4, bonus_m1 |
| 0x00482120 | BlackHoleBox_ReadProps | 120 | explained | 5_m2 |
| 0x004821b0 | BlackHoleBox_Launch | 252 | explained | 5_m2 |
| 0x004822b0 | BlackHoleBox_Update | 430 | explained | 5_m2 |
| 0x00482870 | GrenadeProjectile_Launch | 357 | ported | 1_m1_b, 1_m3, 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m4, bonus_m1 |
| 0x004829e0 | CaptainRocket_ReadProps | 199 | ported | 1_m3, 2_m1, 2_m3, 3_m1, 3_m3, 4_m5, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x00482ab0 | CaptainRocket_SetOwner | 129 | explained | 1_m3, 2_m1, 2_m3, 3_m1, 3_m3, 4_m5, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x00482b40 | CaptainRocket_Update | 461 | ported | 1_m3, 2_m1, 2_m3, 3_m1, 3_m3, 4_m5, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x00482d10 | CaptainRocket_InitCollision | 70 | explained | 1_m3, 2_m1, 2_m3, 3_m1, 3_m3, 4_m5, 5_m1, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x00482d60 | CaptainRocket_Damage | 263 | explained | 5_m1, bonus_m1 |
| 0x004830e0 | MonkBasketProjectile_ReadProps | 150 | explained | 4_m2 |
| 0x00483180 | MonkBasketProjectile_Launch | 131 | explained | 4_m2 |
| 0x00483210 | Projectile_Update_00483210 | 33 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00496160 | TimerObject_ReadProps | 875 | ported | 1_m3, 1_m4, 2_m4, 3_m3, 3_m4_bd, 4_m2, 4_m5, 5_m2, 5_m5, bonus_m1, bonus_m2 |
| 0x004964d0 | TimerObject_Command | 251 | ported | 3_m4_bd, 4_m5, 5_m5 |
| 0x004965d0 | TimerObject_OnTrigger | 140 | ported | 1_m3, 1_m4, 2_m4, 3_m3, 3_m4_bd, 4_m2, 4_m5, 5_m2, 5_m5, bonus_m1, bonus_m2 |
| 0x00496660 | TimerObject_Fire | 34 | ported | 1_m3, 1_m4, 2_m4, 3_m3, 3_m4_bd, 4_m2, 4_m5, 5_m2, 5_m5, bonus_m2 |
| 0x00496690 | Timer_Advance | 178 | ported | 1_m3, 1_m4, 2_m4, 3_m3, 3_m4_bd, 4_m2, 4_m5, 5_m2, 5_m5, bonus_m1, bonus_m2 |
| 0x00496750 | TimerObject_Update | 352 | ported | 1_m3, 1_m4, 2_m4, 3_m3, 3_m4_bd, 4_m2, 4_m5, 5_m2, 5_m5, bonus_m1, bonus_m2 |
| 0x004968b0 | ArmyTimerObject_ReadProps | 942 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00496c60 | ArmyTimerObject_Fire | 312 | ported | 1_m4, 3_m4_bd, 5_m5 |

### src/game/game_08.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00496da0 | fn_00496da0 | 109 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00497f60 | fn_00497f60 | 97 | explained | 1_m3, 1_m4, 2_m2, 2_m4, 3_m2, 3_m4_bd, 4_m1, 4_m2, 4_m5, 5_m4, 5_m5, bonus_m1, bonus_m3 |
| 0x00498320 | fn_00498320 | 33 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00498350 | fn_00498350 | 57 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x00498890 | fn_00498890 | 75 | explained | 1_m1_b, 2_m1, 2_m2, 3_m2, 4_m1, 5_m2, bonus_m1 |
| 0x004993b0 | fn_004993b0 | 12 | explained | 3_m2, 4_m1, bonus_m1 |
| 0x00499fd0 | fn_00499fd0 | 217 | ported | 1_m1_b, 1_m3, 2_m1, 2_m2, 2_m3, 5_m1, 5_m2, 5_m3, bonus_m1 |
| 0x0049a530 | fn_0049a530 | 137 | explained | 2_m2, 2_m3, bonus_m1 |
| 0x0049a5c0 | fn_0049a5c0 | 224 | ported | 2_m2, 2_m3, bonus_m1 |
| 0x0049a6a0 | fn_0049a6a0 | 1350 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049abf0 | fn_0049abf0 | 105 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049aeb0 | fn_0049aeb0 | 290 | ported | 2_m4 |
| 0x0049b030 | fn_0049b030 | 3158 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049bc90 | fn_0049bc90 | 24 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049bcc0 | fn_0049bcc0 | 182 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049be70 | fn_0049be70 | 173 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |

### src/game/game_09.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0049bf20 | fn_0049bf20 | 896 | ported | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049c2a0 | fn_0049c2a0 | 123 | explained | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0049c320 | fn_0049c320 | 408 | ported | 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c4c0 | fn_0049c4c0 | 92 | ported | bonus_m1 |
| 0x0049c520 | fn_0049c520 | 10 | ported | 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c530 | fn_0049c530 | 13 | ported | 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c5b0 | fn_0049c5b0 | 88 | ported | 2_m2, 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c610 | fn_0049c610 | 137 | ported | 2_m3, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c6a0 | fn_0049c6a0 | 772 | ported | 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049c9b0 | fn_0049c9b0 | 209 | ported | 2_m2, 3_m1 |
| 0x0049ca90 | fn_0049ca90 | 21 | explained | 2_m2, 2_m3, 3_m1, 3_m3, 5_m4, bonus_m1, bonus_m2 |
| 0x0049cab0 | fn_0049cab0 | 297 | ported | 2_m2, 3_m1 |
| 0x0049cbe0 | fn_0049cbe0 | 479 | ported | 2_m2, 3_m1 |
| 0x004a0be0 | fn_004a0be0 | 43 | ported | 5_m2, bonus_m3 |
| 0x004a0c70 | fn_004a0c70 | 90 | ported | 5_m2, bonus_m3 |
| 0x004a0cd0 | fn_004a0cd0 | 46 | ported | 3_m2, 5_m3 |
| 0x004a0f30 | fn_004a0f30 | 52 | ported | 3_m2, 5_m3 |
| 0x004a1690 | fn_004a1690 | 31 | ported | 1_m1_b, 1_m3, 2_m1, 2_m3, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4, bonus_m2, bonus_m3 |
| 0x004a16b0 | fn_004a16b0 | 240 | ported | 1_m1_b, 1_m3, 2_m1, 2_m3, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4, bonus_m2, bonus_m3 |
| 0x004a1e90 | fn_004a1e90 | 117 | explained | 1_m3, 2_m1, 2_m3, 4_m5, 5_m3, 5_m4 |
| 0x004a22e0 | fn_004a22e0 | 117 | ported | 2_m1, 2_m2, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4, bonus_m1, bonus_m3 |
| 0x004a2480 | fn_004a2480 | 69 | explained | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x004a24d0 | fn_004a24d0 | 118 | explained | bonus_m3 |
| 0x004a2550 | fn_004a2550 | 949 | explained | bonus_m3 |
| 0x004a2a70 | fn_004a2a70 | 294 | explained | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |

## ui

### src/ui/ui_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00406740 | fn_00406740 | 66 | left | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5, bonus_m3 |

### src/ui/ui_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00465b70 | UiText_ShowTime | 112 | left | bonus_m1, bonus_m2 |

## audio

### src/audio/audio_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0040d250 | fn_0040d250 | 317 | explained | 1_m4, 2_m4, 4_m5 |
| 0x00434180 | fn_00434180 | 257 | explained | 3_m1 |
| 0x00435170 | fn_00435170 | 165 | ported | 3_m2, 5_m4, bonus_m1 |
| 0x004375e0 | AIVoice_PlayHelp | 167 | explained | 1_m1_b |
| 0x00437940 | AIVoice_PlayFight | 213 | explained | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 |
| 0x00437a20 | AIVoice_PlayScream | 145 | explained | 1_m3, 2_m3, 5_m1, 5_m4 |
| 0x00437b60 | AIVoice_PlayStickyScream | 504 | explained | 5_m1, 5_m4 |
| 0x00437d60 | AIVoice_PlayHurt | 508 | explained | 1_m1_b, 3_m1, 3_m3, 5_m1, 5_m4, bonus_m3 |
| 0x00437f60 | AIVoice_PlayHurtShort | 369 | explained | 1_m1_b, 3_m1, 3_m3, 5_m1, 5_m4, bonus_m3 |
| 0x004380e0 | AIVoice_PlayDead | 506 | explained | 1_m1_b, 5_m4 |
| 0x00438cb0 | AIVoice_PlayClimbtaunt | 456 | explained | 2_m4 |
| 0x00438e80 | fn_00438e80 | 35 | explained | 2_m4 |
| 0x0043a100 | AIVoice_Q12Hurt | 253 | explained | 3_m2, 5_m4, bonus_m1 |
| 0x0043a5e0 | AIVoice_MoTaunt | 156 | explained | 4_m2 |
| 0x0043a6e0 | AIVoice_MoHurt | 198 | explained | 4_m2 |
| 0x0043c7b0 | fn_0043c7b0 | 20 | explained | 3_m2, 5_m4, bonus_m1 |
| 0x0044ec60 | AIVoice_PlayFriendlyfire | 234 | explained | 1_m3, 2_m2, 4_m2, 5_m1, 5_m2, 5_m3, 5_m4, bonus_m3 |
| 0x0047bb30 | Jumper_PlayWingSounds | 102 | ported | 2_m3, 3_m1, 3_m2, 3_m3, 4_m3, bonus_m1 |

### src/audio/audio_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004871e0 | Q11Voice_PlayFriendlyFire | 248 | explained | 1_m3, 2_m2, 4_m2, 5_m1, 5_m2, 5_m3, 5_m4, bonus_m3 |
| 0x004f32a0 | fn_004f32a0 | 68 | ported | 2_m3, 3_m1, 3_m2, 3_m3, 4_m3, bonus_m1 |

## anim

### src/anim/anim_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00434930 | fn_00434930 | 332 | ported | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 |
| 0x0043aaa0 | fn_0043aaa0 | 42 | left | 3_m1, 4_m1 |
| 0x0043b110 | fn_0043b110 | 844 | ported | 2_m1, 2_m3, 3_m1, 3_m3, 4_m1, 4_m3, 5_m3 |
| 0x0043bcf0 | fn_0043bcf0 | 126 | left | 2_m4 |
| 0x0043c330 | fn_0043c330 | 315 | ported | 2_m2, 3_m2, 5_m1, 5_m4, bonus_m1 |
| 0x0043c600 | fn_0043c600 | 33 | left | 1_m4, 2_m4, 4_m5 |
| 0x0043c630 | fn_0043c630 | 8 | left | 2_m1, 2_m3 |
| 0x0043c6e0 | fn_0043c6e0 | 79 | left | 1_m3, 5_m4 |
| 0x0043cb40 | fn_0043cb40 | 171 | ported | 4_m1, 4_m2 |
| 0x004580a0 | fn_004580a0 | 285 | ported | 1_m3, 2_m1, 2_m2, 2_m3, 3_m2, 4_m1, 4_m2, 4_m5, 5_m4, 5_m5, bonus_m1, bonus_m3 |

### src/anim/anim_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004ebe60 | fn_004ebe60 | 98 | left | 2_m1, 2_m3 |

## camera

### src/game/game_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0040fa70 | fn_0040fa70 | 22 | left | bonus_m3 |

## render-fx

### ?

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0041ddc0 | Fx_SetFaceDirection | 68 | explained | 4_m5 |
| 0x0041fbd0 | Fx_SetOnHitDeath | 71 | ported | 4_m3, bonus_m3 |
| 0x0041fe00 | Fx_SetBackground | 348 | explained | 5_m1, 5_m2, 5_m3 |
| 0x004202b0 | Fx_SetUvScale | 416 | ported | 5_m2 |
| 0x00420450 | fn_00420450 | 203 | ported | 5_m2 |

### src/renderer/render_r3.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0046d950 | Particle_AllocZeroed | 69 | explained | 1_m4, 3_m4_bd, 4_m5, 5_m5 |

### src/renderer/render_r6.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00419250 | fn_00419250 | 7 | explained | 5_m1, 5_m3 |

## havok-ext

### src/engine/missing_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004a8840 | SpeedTrapAgent_CreateSwapped | 52 | left | 2_m3 |

### src/physics/physics_04.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004a9180 | SpeedTrapSwappedAgent_arePenetrating | 23 | left | 2_m3 |

## math

### src/engine/engine_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0040de50 | fn_0040de50 | 319 | ported | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |

### src/engine/engine_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004e7760 | Vec2_Length | 22 | left | 2_m3, 3_m1, 3_m3, 4_m3, bonus_m1 |
| 0x004e77e0 | Vec3_Sub | 25 | left | 4_m2 |
| 0x004e78d0 | Vec4_Normalize | 93 | left | 1_m3, 2_m1, 2_m3, 4_m5, 5_m2, 5_m3, 5_m4 |
| 0x004e8040 | Vec2_Distance | 27 | left | 4_m2 |

## physics-glue

### src/physics/physics_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00405d10 | fn_00405d10 | 1266 | left | 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, bonus_m2 |
| 0x0042cb80 | Object_ApplyMaterialFriction | 143 | left | 3_m1, 5_m4, bonus_m3 |

### src/physics/physics_02.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x004583d0 | Body_ApplyAngularImpulse | 106 | left | 1_m1_b, 1_m3, 2_m3, 3_m1, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m4, bonus_m1 |

## render-post

### src/renderer/render_r3.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x0047bda0 | PostEffects_AddFade | 92 | ported | 5_m1, 5_m2, 5_m3 |
| 0x0047bec0 | PostEffects_AddType3 | 90 | ported | 5_m2 |
| 0x0047bf20 | PostEffects_AddBlurRings | 89 | ported | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x0047d230 | Post_DrawBlurRings | 905 | ported | 2_m1, 3_m1, 3_m2, 3_m3, 4_m1, 4_m2, 4_m3, 5_m1, 5_m2, 5_m3, 5_m4 |
| 0x0047d5c0 | Post_DrawType3 | 268 | ported | 5_m2 |

## render-scene

### src/renderer/render_r4.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00488130 | fn_00488130 | 15 | left | 5_m1, 5_m2, 5_m3 |

## unknown

### src/engine/engine_01.cpp

| address | name | size | state | levels |
| --- | --- | ---: | --- | --- |
| 0x00407fe0 | fn_00407fe0 | 6 | left | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5, bonus_m3 |
| 0x004090b0 | Tokens_NextInt | 27 | left | 1_m4, 2_m4, 3_m4_bd, 4_m5, 5_m5 |
| 0x0041f4f0 | fn_0041f4f0 | 5 | left | 1_m4, 3_m4_bd, 4_m5, 5_m5 |
