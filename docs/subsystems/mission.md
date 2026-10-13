# Mission logic: objectives, triggers, prompts, win and lose

Static reconnaissance (2026-10-13) for the remaster engine (engine/), which must play `lev_1_m1` as the game does.
Sources: the recovered C++ in `src/` (comments carry the original addresses), string addresses resolved in
`orig/pc/GameData/game.unpacked.exe`, and the level banks. Export: `tools/remaster_export_mission.py <level>` writes
`work/remaster/<level>/mission.json` (schema at the end). Guesses are labelled **guess**. Nothing here was checked
at runtime: almost every function below is marked "not executed in recorded runs".

## Answer in one paragraph

A level's mission is **data, not code**. There is no script language and no per-level C++ for the flow. The
instance bank (`*_big10_inst.big`) places logic objects whose class comes from their `behavior` property. The
classes include the `mission` object, the `broadcast` trigger volumes (`bb_40x40`, `bb_300x300`), `ui_ear`,
`sound_ear`, `bb_ear` (fx_ear), `camera_fx`, `marker`, the `pub`, ticker-bomb sites, barracks and AI. They talk only
through **numbered triggers**: an integer property such as `on_enter = 808` or `send_trigger_exit = 900` is sent with
`Object_SendTrigger` (0x00461870). Every object whose `receive_*`, `trigger_id`, `text_trigger_receive_0N`, ... equals
that number reacts. The mission object owns the start text, the success and failure triggers and texts, the next level
and the player's loadout. game.exe has only two pieces of code specific to `lev_1_m1`, both about the opening (see
"Level-specific code"). The "press X" prompts are not mission data either: they are generic context widgets from
the UI bank, each formatted with the key bound to an input action.

## Object creation and the class of an instance

- Instances are read in stored order. The **id is the 1-based position** (`docs/formats/big-inst.md`).
- The class name is the resolved `behavior` property, else the template name. Resolution
  (`ObjectSystem_CreateObject` 0x0045f020) takes the instance's property, then the template's property set.
- The name hash is looked up in the behaviour table that `ObjectSystem_InitBehaviourTable` (0x0045df60, 197 names)
  builds. The id picks the vtable from `kCreateCases` (`src/game/game_04.cpp:286`). An unknown name makes a
  `DefaultGameObject` (vtable 0x005a2bf0).
- Property reads (0x00453400 int, 0x00453450 trigger, 0x004534a0 float, 0x004534f0 bool, 0x004533a0 string) take
  the instance block first, then the template's set: `ParamSet_*`, `src/game/game_03.cpp:3083` and above.

The `lev_1_m1` logic classes:

| Template | Behaviour (class id) | Class / vtable | Load, trigger handler |
| --- | --- | --- | --- |
| `mission` | mission (136) | MissionObject 0x005a49c8 | `MissionObject_Load` 0x0044c170, `MissionObject_OnTrigger` 0x0044d600, `MissionObject_Update` 0x0044db80 |
| `bb_40x40`, `bb_300x300` | broadcast (138) | TriggerObject 0x005a7c20 | `TriggerObject_Load` 0x00497460; enter 0x004976a0; send 0x00497640; enable/disable 0x00497720 |
| `ui_ear` | ui_ear (150) | 0x005aa750 (name **guess**: UiEar) | 0x0046acf0, 0x0046afa0 |
| `sound_ear` | sound_ear (66) | SoundEar 0x005a2d28 | `SoundEar_Slot6` 0x00497750, 0x00497900 |
| `bb_ear` | fx_ear (149) | FxEarObject 0x005a8f70 | 0x0041bdd0, `FxOwner_OnMessage` 0x0041c050 |
| `camera_fx` | camera_fx (151) | CameraFXObject 0x005ad058 | 0x004101a0, 0x004102b0 |
| `camera_path` | camera_path (135) | CameraPathObject 0x005a0f18 | 0x00410170 (no trigger handler) |
| `marker` | marker (75) | MarkerObject 0x005ad720 | 0x0044b130; AI arrival 0x0044ba30 |
| `pub` | pub (55) | PubObject 0x005a1dc8 | 0x0046a200, 0x0046a770 |
| `player_tickerbomb` | player_tickerbomb (52) | PlayerTickerBomb 0x005ae380 | 0x0044de90, 0x0044e5e0, update 0x0044e150 |
| `grunt_barracks_mini` | building (139) | barracks 0x005aeeb8 | 0x0040ad80, `BarricadeObject_Slot14` 0x0040d1b0 |
| `grunt_generator` | alarm_generator (140) | AlarmGenerator 0x005a2058 | 0x00405490; controller 0x00404e50 |
| `pickup_generator` | (template name, 51) | PickupGenerator 0x005a6258 | 0x00471b70, 0x00471c30 |
| `pickup_treasure1` | pickup_token (42) | PickupObject (a TriggerObject) | 0x00471e40 |
| `trotter` | trotter (161) | **PlayerObject**: the player start | 0x0045f020 stores it in Game+0x748 |
| `q111`, `molmer` | q111 (154), molmer (153) | teammates Q and Jonesy | |
| `snow_pipe*`, `powderkeg` | fx_explosion (90) | FxExplosion 0x005a4300 | die 0x0041c570 sends `send_trigger_on_death` |
| `v_prison_gate`, `bigredarrow`, `boom_island`, `oilpump`, `guard_alarm` | (template name: none) | DefaultGameObject | `DefaultGameObject_Load` 0x0044fd20, `_OnTrigger` 0x00450fb0, `_OnDeath` 0x00450ee0 |

The player is not placed by code: the `trotter` instance (id 4) is the player start. Its template's property set
`p_trotter` holds the player's turn and aim speeds, and behaviour `trotter` is class 161, which
`ObjectSystem_CreateObject` stores as Game+0x748.

## The trigger system

- **Ids** are plain integers chosen by the level designer: 1, 7, 98, 556, 900 ... in `lev_1_m1`. Id 0 means "none".
  Sending 0 logs `"%s sent 0 trigger\nBAD BAD BAD"` (0x005aff44).
- **Dispatch:** `Object_SendTrigger(objsys, id, sender)` (0x00461870, `src/game/game_04.cpp:602`) walks the
  listener list (object system +0xe70, every object) and calls each object's vtable slot +0x38 `(sender, id)`. The
  walk stops at the first handler that returns true. Every handler read for this level returns false, so in
  practice it is a **broadcast to all objects**. Each handler compares the id with its own fields.
- **One trigger, many receivers:** `900` (pub exit) opens the next objective text, retargets the compass, enables a
  bomb site and removes the red arrow, all at once.
- **One-shot:** a `broadcast` with `one_shot` clears its `on_enter` after the first send (0x00497640). A
  `sound_ear` removes itself after playing unless `reapeat` (sic) is set (0x00497900). The `receive_trigger_kill`
  and `_deactivate` fields of a DefaultGameObject clear themselves when they fire (0x00450fb0).
- The only constant trigger id in game.exe is 27, sent when a platoon grunt dies (0x00433199). A scan of all 27
  call sites of 0x00461870 for a preceding `mov ebx, imm32` found no other.

### Senders

| Property | Class | When | Evidence |
| --- | --- | --- | --- |
| `send_trigger_start` | mission | first `MissionObject_Update` frame | 0x0044db80 sends +0x460 and clears it |
| `on_enter` / `on_exit` | broadcast, pickup_token | an object of the trigger's collision group enters or leaves | 0x004976a0 sets "pending", 0x00497640 sends |
| `send_trigger_set` | player_tickerbomb | the bomb is armed | 0x0044e4a0 -> 0x0044e0c0 |
| `send_trigger_explode` (`2`, `3` read but not sent by the update) | player_tickerbomb | fuse ends | 0x0044e150 |
| `send_trigger_non_enabled` | player_tickerbomb | the player, holding ticker bombs, stands at a disabled site | 0x00467450 -> 0x0044e0e0 |
| `send_trigger_entry`, `_entry_failure`, `_exit` | pub | the player enters the pub, is refused, leaves the pub screen | 0x0046a200; 0x0046a720 from 0x004690c0 |
| `send_trigger_on_death` | DefaultGameObject, FxExplosion, AI, buildings | death | 0x00450ee0, 0x0041c570, AI +0x528, barracks +0x5a8 |
| `trigger` | marker | an AI reaches the marker | 0x0044ba30 |
| `peasant_success_trigger` | mission | peasants saved >= `peasant_max_saved` | 0x0044d600 |

### Receivers

| Property | Class | Action | Evidence |
| --- | --- | --- | --- |
| `receive_trigger_success` | mission | **win** (below) | 0x0044d600, +0x46c |
| `receive_trigger_pre_success` | mission | teammates retreat in the direction of the "pre-success marker" | 0x0044d600, +0x470 |
| `receive_trigger_failure`, `_2` .. `_4` | mission | **lose**, states 4..7 | 0x0044d600, +0x474..+0x480 |
| `alarm_trigger_activate` / `_deactivate` | mission | Game+0xdcc = 1 / 0 (forces the alarm) | 0x0044d600 |
| `text_trigger_receive_0N` | ui_ear | show `text_display_0N` | 0x0046afa0 -> 0x00403410(text, 3 or 0x13) |
| `objective_trigger_receive_0N` | ui_ear | compass target = objects whose `tag` = `objective_tag_0N` (Game+0x2658) | 0x0046afa0 |
| `trigger_id` | sound_ear | play a voice line (below) | 0x00497900 |
| `trigger_id` | fx_ear | toggle the fx `fx_name` (with an optional random initial delay) | 0x0041c050 |
| `trigger_start` (0 = at load) / `receive_trigger_kill` | camera_fx | start / remove the camera fx | 0x004101a0, 0x004102b0 |
| `receive_trigger_enable` | player_tickerbomb | enable the site (a site with a non-zero value starts disabled) | 0x0044e5e0, 0x0044e380 |
| `enable` / `disable` | broadcast | enable / disable (without `enable` the volume starts enabled) | 0x00497460, 0x00497720 |
| `listen_trigger_open` | pub | open the pub | 0x0046a770 |
| `listen_activate_trigger` | pickup_generator | start generating | 0x00471c30 |
| `receive_trigger_activate` / `_deactivate` / `_reset` / `_kill` | barracks | start / stop releasing troops, refill, destroy | 0x0040d1b0 |
| `receive_trigger_activate` | AI | wake up and run its `mode` | GenericAIObject set-up (`src/ai/ai_01.cpp:2290`); tower 0x00497xxx `ai_08.cpp:3370` |
| `receive_trigger_kill` / `_deactivate` / `_start_blocking` | DefaultGameObject | destroy (death fx, husk, its own `send_trigger_on_death`) / deactivate / start blocking | 0x00450fb0 |

### Broadcast volumes (`bb_40x40`, `bb_300x300`)

- **Properties:** `p_tools`: `behavior broadcast`, `force_ground 1`, `collide_poly 1`, `sphere_radius 0`,
  `ai_can_trigger 0`, `one_shot 0`, `on_enter`, `on_exit`, `enable`, `disable`. `TriggerObject_Load` also reads
  `door_mat` (flag 0x10: keeps `on_enter` after a one-shot send) and `visible` (hidden unless set).
- **Shape:** with `sphere_radius` > 0, a sphere of that radius around the object (`TriggerObject_BuildCollision`
  0x004975d0). With 0, the model's own collision applies (**guess** from `collide_poly`). The models' vertex boxes
  are:
  - `bb_40x40`: x and y -20..20, z 0..40;
  - `bb_300x300`: x -150..150, y -10..10, z -150..150, i.e. a 300 x 300 wall 20 units thick.
- **Who triggers:** the collision descriptor's group is 8, or 0x18 with `ai_can_trigger`. **guess:** group 8 is
  the player only.
- **Firing:** entering sets "pending" (flag 4). The update sends `on_enter` while the volume is enabled (flag 8).
  `one_shot` without `door_mat` then zeroes `on_enter`.

## The mission object

`MissionObject_Load` (0x0044c170, `src/game/game_02.cpp:1918`) reads its properties into the Game object (property
names resolved from the exe):

- **Triggers:** `send_trigger_start`, `receive_trigger_success`, `receive_trigger_pre_success`,
  `receive_trigger_failure[_2.._4]`.
- **Texts:** `start_text`, `success_text`, `failure_text[_2.._4]`, `timeout_text` (default `ui_timeout`).
- **Movies:** `start_movie`, `success_movie`.
- **Next level:** `next_level_name`.
- **Compass:** `compass_objective_tag`.
- **Player loadout:** `player_has_tickerbomb`, `player_has_jetpack`, `player_add_*`, `player_remove_*`,
  `player_set_ammo_*`, `player_set_num_*`. **Original data quirk:** the template set `p_mission` names
  `player_set_num_topsy`, but the code reads `player_set_num_topsyturvy` (0x005b1e04), so the template value is
  never used.
- **Other modes:** `start_in_turret_tag`, `camera_startup_path_anim`, `ai_target_tag`, alarm and army settings,
  peasant counts, `score_value` (template 1000), voice-delay timers, shadow colour, `env_type` (1 = cold: grunts
  shiver, `src/anim/anim_01.cpp:1114`).

## Start of a level

1. **Camera fx.** A `camera_fx` with `trigger_start` 0 starts its fx at load (0x004101a0 -> 0x004102e0).
2. **Camera path.** An fx opcode 0x73 starts a camera path: `Camera_StartPath(name, camera, 1)` (0x0040ff50).
   - It finds the object named `camera_path`, plays the animation `name` on it and locks the camera to its
     `Camera01` bone.
   - Mode 1 sets Game+0xdce (cinematic).
   - The anim bank holds `camera_path_l1m1` and `camera_path_l1m1_island`.
3. **Start trigger.** The first mission update sends `send_trigger_start`.
4. **Start movie.** Once the mission timer passes, `start_movie` is pushed as "PlayMovie", **except on `lev_1_m1`**
   (`_stricmp(level, "lev_1_m1")`, 0x005b1738). `pre_1_1_opening` already plays in the boot intro list
   `LucasAnim,pm_logo,pre_1_1_opening` (0x005bc108, `src/game/start_level.cpp:120`).
5. **Start text.** `MissionObject_ShowStart` (0x0044daf0) shows `start_text` once.
   - It waits while a camera path is active (0x00410150) or a cinematic runs (Game+0xdce).
   - It calls `0x00403410(text, flags 1)`.

## Messages, objectives and the compass

`0x00403410(game, text, flags)` (`src/game/game_01.cpp:801`) is the one message box. The widget is
`ui_mission_briefing`, update 0x00466060 (`src/ui/ui_02.cpp:792`). Flags:

| Flag | Effect |
| --- | --- |
| 1 | pause the game (0x004031e0) and append `"<br><br>" + ui_stats_ok` ("Press %s to continue", key of ControlActionMenuSelect 0x1b) |
| 2 | queue: the message shows 1.0 s later (Game+0x15d8 timer) |
| 4 | render the short form |
| 8 | do not pause |
| 0x10 | also store the text as the current objectives text (Game+0x1de4) |

- **Long and short forms:** the text markup holds both. The renderer drops `<short>..</short>` blocks normally and
  `<long>..</long>` blocks with render flag 8, which message flag 4 sets (`Renderer_TextSkipTaggedBlock`
  0x005072b0). Briefings use the long form.
- **Where the short form shows:** **guess:** the pause menu's objectives text, from Game+0x1de4.
- **Other markup:** `<br>`, `<rgb=RRGGBB>`, `<ll=N>` (line width) and `<tab=N>` (0x005071a0, 0x00507220).
- **Objectives are only texts.** There is no objective state machine. Each `l1m1_*` text spells out every objective
  with "Not Complete" or "Completed!" baked in. A `ui_ear` shows the next text when a trigger arrives:
  - flags 3 (queued, pause, continue prompt);
  - flags 0x13 when an objective matched the same trigger: the text is also stored as the objectives text, and
    0x0044f560 runs, which **guess:** plays a jingle.
- **Compass:** `ui_compass_objective` (0x00466790, `src/ui/ui_02.cpp:1209`) points at the nearest object of lists
  +0xefc / +0xf04, else at the nearest object whose `tag` (object +0x390) equals Game+0x2658.
  - Game+0x2658 starts as `compass_objective_tag`.
  - A ui_ear's `objective_tag_0N` replaces it.

## Voice lines (`sound_ear`)

`trigger_id`; `roman_sound` (the player), `q_sound`, `jonsey_sound`, `ai_sound` + `ai_tag`; `max_distance` (300);
`reapeat`. On its trigger (0x00497900):

1. Candidates: Roman always, Q and Jonesy when that teammate exists and is not dead (0x00475d40 / 0x00475d30 on the
   player).
2. A teammate within `max_distance` of the player counts as a choice; if no teammate is in range, the nearest
   present one is used.
3. One choice is picked at random (`rand * count >> 15`).
4. Roman's line plays through audio vt+0xd0, unpositioned. A teammate's line plays through vt+0xc4 on that speaker.
5. The ear removes itself unless `reapeat` is set.

Subtitles: the text bank has an entry with the sound's name ("Roman: Oh, I'm freezing."). **guess:** it is shown
as the subtitle when subtitles are on (option `SubtitlesGame`, `src/save/save_01.cpp:192`).

## Tutorial prompts ("Press X to ...")

- **Source of the texts:** the text bank `<level>_big01_text.big`. `lang.dat` holds `engl`; the installed banks are
  English.
- **Widgets:** the prompts are UI objects from the UI bank (`big12_ui`, identical in every level). They are spawned
  as classes from the behaviour table (`ui_bof_hint` 109, `ui_tickerbomb_hint` 110, `ui_vehicle_entry` 114,
  `ui_pub_entry_hint` 116, ...). Each one's per-frame update decides from game state whether to show:

| Text id | Text | Action whose key fills %s | Shown when | Evidence |
| --- | --- | --- | --- | --- |
| `ui_pub_entry_hint` | Press %s to enter pub | Use (0x14) | the player is within the pub's `doorway_entry_radius` | 0x00467b60 |
| `ui_pub_fail` | CLOSED until troops are destroyed | none | the pub is closed | 0x00467b60 |
| `ui_tickerbomb_hint` | Hold down %s to set the ticker bomb! | Use | an enabled, unarmed site in range and the player has ticker bombs | 0x00467450 |
| `ui_tickerbomb_none` | You need Ticker Bombs! | none | a site in range, no ticker bombs | 0x00467450 |
| `ui_tickerbomb_patience` | Wait for the other Ticker Bomb to explode! | none | another bomb is armed | 0x0044e3a0 |
| `ui_vehicle_entry` / `_exit` | Press %s to enter / exit mounted gun | Use | near an enterable turret / inside one | 0x004662a0, 0x00466210 |
| `ui_bof_hint` | Press %s to punch! | Fire (8) | once (Game+0x578) when the punching glove comes out (Game+0xcbc, player update `game_06.cpp:2929`); **guess:** out of ammo | 0x004671b0 |
| `ui_stats_ok` | Press %s to continue | MenuSelect (0x1b) | appended to paused messages | 0x00466060 |
| `pub_award_*`, `pub_notify_*` | You gain the Ticker Bombs! Press %s to Continue, ... | MenuSelect / MenuCancel | pub screen | `src/game/game_04.cpp:1320` |

Key-name substitution (`0x00465280`, `src/ui/ui_01.cpp:3640`):

1. For each of the prompt's two actions, the first binding on the active device is looked up (0x00415750); the
   second action is 0x23 (none).
2. `Input_CodeText` (0x00415580) turns each binding into its `PMS_INPUT_*` string id (0x00414c90) and looks that up
   in the text bank: "Left Stick Up", key names, ...
3. The prompt's text is formatted with `_vswprintf` (0x00465970).

`hook/enhance` can override the key through `g_promptKey`. Action names: `ControlAction*` ids, switch 0x004155a0,
jump table 0x00415688. Use = 0x14, Fire = 8, MenuSelect = 0x1b, MenuCancel = 0x1c.

## Win and lose

**Win: `receive_trigger_success`** (0x0044d600):

1. On `lev_1_m1_b` and `lev_2_m1`, Game+0x272c += 1000.0 (**guess:** bonus).
2. Score Game+0x2730 += `score_value`.
3. Level state (Game+0xa64 +0xc) = 2.

0x0044a390 then:

1. Stops the sounds and shows `success_text` with flags 1.
2. Sets state 10. The end sequence 0x0044a230 pushes "Stats", then "PlayMovie" with `success_movie` when one is set,
   then "Credits" after `lev_5_m5`.
3. Pushes "Loading" with `next_level_name` (Game+0x268c), unless Game+0x568 bit 2 is set.

**Lose:**

| Cause | State | Screen |
| --- | --- | --- |
| Player dead for 5 s (`game_06.cpp:2160`, +0x588 = 5.0 countdown) | 3 | "MissionFailed" with `failure_text` (Game+0x2664) |
| `receive_trigger_failure` | 4 | `failure_text` |
| `receive_trigger_failure_2..4` | 5..7 | `failure_text_N` (Game+0x2654 + state * 4) |
| Peasant casualties >= `peasant_max_casualties` (0x004062a0) | 8 | |
| Start turret destroyed (0x0049ac60) | 3 | |

While Game+0xdd2 (finished) is set, failure triggers are ignored.

**Checkpoints:** none found. No checkpoint code turns up in `src/`: "respawn" by id (0x0048cb30) is save-game
restore. The game splits long missions into levels instead (`lev_1_m1` -> `lev_1_m1_b`). What "MissionFailed" offers
(retry or quit) is not recovered (open question).

## Level-specific code (all of it, for lev_1_m1)

- `MissionObject_Update` skips the start movie on `lev_1_m1` (above).
- `PlayerObject_Update` (0x004788a0, `src/game/game_06.cpp:2451`): while the camera path `camera_path_l1m1`
  (0x0059ed8c) plays on `lev_1_m1`, the stick's forward value is 1.0. **The player walks forward by himself during
  the opening fly-in.**
- `MissionObject_OnTrigger` adds the success bonus on `lev_1_m1_b` and `lev_2_m1` (not this level).

## lev_1_m1 data, by trigger

Instance ids from `mission.json` (1-based). Positions are game units, Z up.

| Trigger | Sender (event) | Receivers (action) |
| ---: | --- | --- |
| 1 | mission 6, level start | grunts 82 (67, 237) and 196 (82, 137) wake (`trigger_los 0`, alert group 2, by the archway gun) |
| 808 | bb_40x40 194, sphere r 50 at (16, 384) | grunt 34 runs to marker 10 (71, 150, `hide`); sound_ear 254: Roman "Oh, I could use a drink ta warm me up." |
| 810 | bb_40x40 153, sphere r 100 at (185, -64), at the pub | barracks 25 (-229, -450) start releasing 5 regulars to marker 20 (hide); sound_ear 255: Jonesy "Wish I had more explosives." |
| 900 | pub 225 (210, -106), exit | ui_ear 65: `l1m1_sub2` + compass -> tag 55 (bomb sites 234, 252); bomb site 252 enabled; bigredarrow 258 over the pub destroyed |
| 7 | bomb site 252, not yet enabled while the player has ticker bombs | ui_ear 253 shows `l1m3_bomb` ("You must destroy ALL 5 scaffolds ..."). **Original data bug:** the level's own `l1m1_bomb` ("Go to the Pub to pick up Ticker Bombs to get through this gate!") is never used. In practice 7 can hardly fire: the player only gets ticker bombs in the pub, and leaving it (900) enables site 252. |
| 556 | bomb site 252 at gate 248 (-84, 41), explodes | gate 248 destroyed; grunt 67 (-180, -87) wakes and runs marker path 14 |
| 98 | bb_40x40 261, 262, 263 (one-shot boxes across the road near (-8, -490)) | barracks 235 (309, -559) release 6 regulars along markers 33/34 |
| 99 | bb_40x40 256 (22, -551), the second archway | sound_ear 257: Q "Why don't we have a picnic under the trees?" |
| 901 | bomb site 234 at gate 247 (272, -956), explodes | gate 247 destroyed; ui_ear 65: `l1m1_sub3` + compass -> tag 56 (boxes 64, 259, 260) |
| 123 | bb_40x40 64, 259, 260 (one-shot, just past gate 247, y -975 .. -1005) | **mission success** |
| 711 | bb_300x300 269 (wall at (-200, 1042), one-shot) and snow_pipe_curved 96 when blown up | sound_ear 270: Roman "Oh, I'm freezing." / Jonesy "It's a bit nippy, ain't it?" |
| 713 | bb_300x300 271 (wall at (-194, 55), one-shot) | sound_ear 272: Q "If I had balls ..." / Jonesy "The snow's ruinin' my good leather boots ..." |
| 301 | boom_island 131 (795, 493), destroyed | bb_ear 218 starts `fx_penguin_debri` at (-55, 733, 151) |

Triggers that never fire, because no sender exists in the data or in game.exe:

- **712:** sound_ear 268 by the island. **guess:** cut, or meant to come from boom_island, which sends 301
  instead. Its lines would be Q "You had to blow up the entire island, didn't you!" and Jonesy "Oh, great, you've
  gone and made the penguins angry".
- **61:** bb_ear 222 `fx_camera_path_l1_m1_island`. The fx is not in the fx bank, although its camera animation
  `camera_path_l1m1_island` is in the anim bank.
- **768:** bb_ear 282 `fx_trotter_talk_1`. The fx is not in the bank.

### Tag links

- The mission's `compass_objective_tag` 50 is the pub.
- `objective_tag_01` 55 is the two bomb sites; `objective_tag_02` 56 is the three goal boxes.
- Marker 151 (`alarm_tag 4`) is the siren `guard_alarm` 33 (-201, -65). The grunt that reaches it starts
  `fx_siren` and joins the alarm list (0x0044ba30).
- While any siren runs, the alarm controller (0x00404e50) spawns troops from the `grunt_generator` alarm generators
  152 (-130, -131) and 215 (-34, -364), up to `alarm_generator_max_troops` (10). The troops go to markers 12 to 14
  (36, 205: `trigger_charge`; 151).

### Other level content

- **Snow pipes and powder kegs:** pipes, kegs and oilpumps are explosive. Pipe 113 (588, 475) does `area_damage` 800
  over 400 units, enough to destroy `boom_island` 131 (max_health 150) from 210 units away. **guess:** this is the
  "blow up the island" secret.
- **Penguins:** they score -10 each.
- **Extras token:** `pickup_treasure1` 246 at (-216, 974), text `pickup_token` "Extras token".
- **Window grunts** 211 to 214 (`mode ready_window`).
- **Mounted gun** `stig_mgun` 32 at the archway (71, 173) gives the "Press %s to enter mounted gun" prompt.

## lev_1_m1 as the original plays it

Confidence: **high** = data and code agree, the code is read; **medium** = data plus a code path read only in part;
**low** = inferred.

1. **Boot (high).** The intro movies `LucasAnim`, `pm_logo` and `pre_1_1_opening` play. The last is the story
   opening; its 71 subtitle lines are in the UI bank. A new game loads `lev_1_m1`; its own `start_movie` is skipped.
2. **Fly-in (high for the mechanism, medium for the look).**
   - `camera_fx` 83 runs `fx_camera_l1_m1`: three layers of falling snow, and opcode 0x73 starts the cinematic
     path `camera_path_l1m1` on `camera_path` 209.
   - `camera_fx` 84 runs `fx_camera_l1_m1_locked`: a 3 s white fade (`fxo_white`), the card `fxo_true_story`
     (**guess:** a "true story" title card), snow spray and a red glow.
   - The player (Roman, at (-215, 1255, 185) on the northern hill, with Q and Jonesy beside him) walks forward on
     his own while the path plays.
   - Mission trigger 1 wakes the two guards at the archway gun.
3. **Briefing (high).** When the path ends, the game pauses on `l1m1_start`: "Storm the Base! The King's men have
   taken Rexus to their base." It lists two objectives, "Get Ticker Bombs from Pub" and "Blow 2 gates between you
   and base", both Not Complete, then "TIPS: Use compass to locate your objectives." and "Press <MenuSelect key> to
   continue".
   - Loadout: the rifle is added and the machine gun not (`player_add_rifle 1`, `player_add_machinegun 0`), plus
     the template's 5 sticky bombs.
   - The compass points at the pub (tag 50), where the big red arrow floats.
4. **Down the hill (high for the triggers, low for the exact route).**
   - Crossing the 300-unit wall 269 at y ~1042 plays Roman's or Jonesy's cold line (711).
   - The extras token is near (-216, 974); the nature-reserve sign and penguins are near (-135, 750).
5. **Village approach (high).** At (16, 384) trigger 808 sends a grunt to hide by the archway, and Roman wants a
   drink. The archway at (71, 173) has the mounted gun ("Press %s to enter mounted gun") and the alert-group-2
   guards. Guard groups 6 and 7 hold the west slope.
6. **The pub (high).**
   - Near the pub, 810 wakes the mini-barracks 25, which releases 5 regulars, and Jonesy wishes for explosives.
   - "Press %s to enter pub" enters the pub screen: "You gain the Ticker Bombs! Press %s to Continue" (pub
     `has_tickerbomb`, sets Game+0xd90), the Topsy Turvy bomb (`has_topsyturvy`), and health from the pub's pool.
   - Leaving sends 900: the arrow vanishes and, 1 s later, the paused `l1m1_sub2` ("You have the Ticker Bombs!",
     objective 1 Completed!, "Try the Topsy Turvy Bomb if you encounter too many enemies").
   - The compass now points at the nearest bomb site (tag 55), and site 252 at gate 1 is enabled.
7. **Gate 1 (high for the bomb, medium for the alarm).**
   - At the prison gate 248 (-88, 41): "Hold down %s to set the ticker bomb!". Holding Use for 2 s within 30
     units arms it: Roman says "ticker armed", and a clock starts. After 6 s it explodes (556).
   - The gate is destroyed, and grunt 67 wakes and runs his marker path to marker 151, where he raises the siren
     at `guard_alarm` 33. While the siren runs, the two alarm generators feed up to 10 troops toward markers 12
     to 14.
   - **guess:** shooting the siren or the runner prevents this.
   - Window grunts 212 and 213 cover this area. Crossing the 300-unit wall 271 near (-194, 55) plays Q's or
     Jonesy's cold line (713).
8. **South road (high).** The one-shot boxes near (-8, -490) (98) make mini-barracks 235 at (309, -559) release 6
   regulars along markers 33/34. Nine guards stand at the second archway (24, -554), where Q suggests a picnic (99).
9. **Gate 2 (high).**
   - Site 234 at gate 247 (272, -956) has no enable trigger: it works as soon as the player has ticker bombs. Only
     one ticker can be armed at a time ("Wait for the other Ticker Bomb to explode!").
   - The explosion (901) destroys the gate and shows `l1m1_sub3`: "You have achieved all of your objectives!" with
     the single objective "Run through blown gate!" (Not Complete). The compass points at the goal boxes (tag 56).
10. **Win (high).** Entering any of boxes 64, 259 or 260 behind the gate sends 123: mission success.
    - Score +1000 (`score_value` from `p_mission`).
    - "Well Done! Q and Jonesy will stay here to guard your back!" with a continue prompt, then the Stats screen,
      then loading `lev_1_m1_b` ("Rescue Rexus!").
11. **Lose (high).** Death: 5 s later "MissionFailed" with "You failed!". There are no failure triggers in this
    level.
12. **Optional (medium).** Blowing pipe 113 or the oilpumps by `boom_island` destroys the island (301) and starts
    `fx_penguin_debri` over the start area. The island's voice lines (712) never play.

Order notes:

- Gate 2's site is enabled from the start, so a player who has the ticker bombs may blow gate 2 before gate 1. The
  texts would then come out of order: `l1m1_sub3` arrives at 901 whatever happened before.
- Nothing requires gate 1 for the win; the boxes behind gate 2 are the only win condition. **guess:** gate 1 is
  simply on the way.

## mission.json (tools/remaster_export_mission.py)

| Key | Content |
| --- | --- |
| `level`, `up`, `next_level` | level name, `"z"`, `next_level_name` |
| `mission` | `{id, resolved}`: the mission object's properties, template defaults under instance values |
| `player_start` | the `trotter` instance(s): `{id, position, transform}` |
| `rules` | code rules the data does not show: `start` (list of `{what, evidence}`), `win`, `lose`, `checkpoints`, `message_flags`, `spawn_mask` |
| `triggers` | `{"<id>": {senders: [{id, template, class, property, event, evidence, position}], receivers: [{..., action, detail?}], note?, code_sender?}}` |
| `edges` | flat `[{trigger, from: {id, template, event}, to: {id, template, action, detail?}, evidence}]`; `id: null` marks a missing side |
| `unsent_triggers`, `unreceived_triggers` | ids with no sender (61, 712, 768) / no receiver |
| `tag_links` | `[{from, template, property, value?, meaning, targets: [ids], evidence}]`: compass, alarm, marker paths |
| `text` | `{text_id: English text}` for every id the logic uses, the level's `l1m1_*` texts and the hint texts |
| `missing_text_ids` | ids referenced but absent from the bank (`ui_timeout`, `ui_tickerbomb_set` is a widget name) |
| `voice` | `{sound: {speaker, subtitle, in_audio_bank, sound_ear or played_by}}` |
| `hints` | `{text_id: {text, action, action_code, when}}` |
| `input_text` | `PMS_INPUT_*` key names and `ControlAction*` names from the text bank (for %s) |
| `subtitles` | `{movie, lines: [{start, end, speaker, text, string}]}` for the mission's `start_movie` |
| `instances` | every instance: `{id, template, property_set, model, class, behaviour, class_id, mask, transform, position, props (own), template_props, resolved}` plus `volume` (broadcast / pickup_token: sphere `{radius, centre}` or box `{local_min, local_max, world_corners, world_aabb}`, `one_shot`, `ai_can_trigger`, `force_ground`), `camera_path` (model, anims), `fx` (`{name, in_fx_bank}`) |

Transforms are the game's 3 rows of (r0, r1, r2, t): world = row · local + t (as `engine/src/scene.rs`
`game_matrix`).

## Open questions

- **Broadcast volume shape:** is a `sphere_radius 0` volume the model's box (`collide_poly`)? Which collision
  group is 8: player only, or the player and teammates?
- **Spawn mask:** what does Game_StartLevel's pass value mean? The mission and ui_ear 253 use mask 0x1, the rest
  0x3FF.
- **Short form:** where does the `<short>` form show? Pause menu (Game+0x1de4)? Not traced.
- **0x0044f560:** what does it play when an objective updates?
- **Alarm:** does destroying `guard_alarm` 33 stop the siren and the alarm spawns? The siren fx and its object
  lifetime are not traced.
- **Ticker bombs:** are they unlimited once Game+0xd90 is set? No count was found.
- **MissionFailed:** what does the screen offer (retry, load, quit)?
- **Alert groups:** how does `alert_group` propagate alerts between grunts? `trigger_los 1` (template default)
  presumably wakes an AI by sight; not read.
- **Trigger 711:** why does `snow_pipe_curved` 96 send it (a cold-weather line when a pipe blows up)? Probably a
  designer slip; reproduce as data.
