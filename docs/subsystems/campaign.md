# Campaign flow: from one level to the next

Static decompilation, 2026-10-16 (src/ui/ui_01.cpp 0x0044a230 / 0x0044a390, src/game/start_level.cpp 0x00403690,
src/game/game_01.cpp 0x00401e80..0x00401f40, src/game/game_02.cpp MissionObject_Load 0x0044c170, src/game/game_05.cpp
0x00475650, src/game/game_09.cpp the weapon state, src/ui/ui_03.cpp UiScreen_Activate 0x004921a0). None of these ran in
the recorded coverage runs; the remaster's behaviour is checked by its playtests (engine/playtests/campaign_*.json).
The remaster: engine/src/campaign.rs (module doc), levelreset.rs (`RestartTo::Level`), mission.rs `Loadout`,
player.rs `apply_loadout`.

## The sequence after a win

1. The win message (success_text) is dismissed; 0x0044a230 state 10 pushes "Stats" (win-sequence.md).
2. State 11, the Stats screen popped: with a `success_movie` (Game+0x2680) the music request is reset (0x0044f4c0),
   the voices stop (audio vt+0xa8) and "PlayMovie" is pushed; state 12.
3. State 11 without a movie, or 12 / 13: when the level is `lev_5_m5` (0x005b253c, `_stricmp`) "Credits" is pushed
   (state 14); otherwise the money is banked (0x00401ef0) and the next level is requested: 0x004907c0 with
   `next_level_name` (Game+0x268c), or NULL (lev_shell) when Game+0x568 bit 2 is set, and "Loading" is pushed.
4. State 14 (after the credits): 0x004907c0(NULL) and "Loading": lev_shell, the front end. The money is not banked
   on this path; lev_shell's load resets it anyway (below).
5. "Loading" (0x00492f80 / 0x00492fd0) loads the level at frame 5 (failure: lev_shell with ui_corruptcontent) and
   starts it at frame 10; Game_StartLevel 0x00403690 tears the old level down (0x00403940: every object of the level's
   list destroyed, the level heap reset) and builds the new one.
6. The new level's MissionObject_Update shows its `start_movie` except on lev_1_m1 (0x0044db80, 0x005b1738), then the
   start text (MissionObject_ShowStart 0x0044daf0).

Game+0x568 bit 2 is the Extras "Replay Level" mode: set by UiScreen_Activate case 41 (Replay Level -> "ChooseGame",
then "ReplayLevel"), cleared by New Game (case 1) and Load Game (case 2). A replayed level returns to the front end
after its Stats instead of going on.

## What is carried, and why

| State | Rule | Evidence |
| --- | --- | --- |
| Money (Game+0x570) | carried. Banked into +0x57c at the end (0x00401ef0, three dwords +0x570..+0x578), taken back at every level start (0x00401f10 from 0x00403690), reset to {5000, 0, 0} when lev_shell loads (0x00401e80 with flag2, start_level.cpp:132). A restarted level so loses what it earned since its start. | game_01.cpp:389-450, start_level.cpp:116-134 |
| Pub intro line (Game+0x574) | carried in the same banked block: the pub's "PE150013_Pub_Once_Intro" plays once per campaign (Pub_PlayAmbientLine 0x00468dd0 sets it); +0x575..+0x577 are the once lines for new weapons (not set up by the remaster's pubs) | audio_01.cpp:1960-1966 |
| Extras tokens, unlocks | carried: the profile (Game+0x200 count, the unlock tables +0x1f8 / +0x20c by category: 0 movies, 1 levels reached, 2 tokens taken, 3 / 4 bought unlockables), saved in progress.dat (0x004a3840) | save_01.cpp:3462-3511 |
| Difficulty (Game+0x550) | carried (a Game member; chosen by the Difficulty screen of New Game, case 71) | ui_03.cpp UiScreen_Activate |
| Weapons, ammo, specials | **not carried.** The player is an object of the level: built with it (PlayerObject_Construct 0x00461840 inits an empty weapon state, 0x004a0d00) and destroyed by the teardown. Its reset 0x00475650 applies the mission's loadout: player_remove_* (on the empty list: no effect), player_add_* in weapon-id order (machinegun 0, snipergun 1, rifle 2, vindaloo 3, gurner 4, sharkgun 5; the primary list holds 3), player_set_ammo_* (-2: clip and spare full, 0x004a14e0) and player_set_num_* for the specials (guyfawkes 6, bunchoffives 7, topsyturvy 8, blackhole 9, stickybomb 10; -1 leaves 0), then equips slot 0: the first weapon by id is in hand. Defaults (MissionObject_Load): add_machinegun 1, other adds 0, set_ammo -2, set_num -1. | game_02.cpp:1898-2139, game_05.cpp:905-960, game_09.cpp:1037-1180, 1528-1560 |
| Health | not carried (a new player object: max_health) | as above |
| Stats counters | per level (the Stats screen shows the level's) | win-sequence.md |

Data quirk: the mission template names `player_set_num_topsy`, the code reads `player_set_num_topsyturvy`
(0x005b1e04), so lev_5_m2's "2 topsy bombs" never apply (mission.md). The remaster reads the same name.

Per level (mission.json), so a pub purchase never outlives its level: lev_1_m1 rifle; lev_1_m1_b and lev_1_m3
machine gun + rifle (the machine gun in hand); lev_1_m4 machine gun only (no rifle); lev_2_m1 machine gun, sniper
gun, rifle; later levels add the vindaloo, the shark gun (not ported in the remaster: logged on the mission log).

## Saved games

The savegame header's first field is the level name (0x0044a660 of Game+0xa64, save_01.cpp:2003); Game_StartLevel's
"-slot" name restores a save, which reloads that level (SaveGame_Load 0x0048c9b0). The remaster writes the loaded
level (campaign.rs `Levels::current_name`) and a load enters that level's folder before the rebuild.

## Front end (Extras)

The original's main menu: New Game (case 1: level lev_1_m1, "SaveGame" to choose a profile slot, then "Difficulty"),
Load Game (case 2), Extras: Cutscenes ("MovieSelect", case 11: the movie list 0x00492d80 of movies/*.pcv seen,
category 0, or all with the unlockmovies cheat, Game+0x4c8 bit 12) and Replay Level (case 41: "ChooseGame" then
"ReplayLevel", the mission lists 0x00492c90: "lev_*" levels reached, category 1 (12 rows), and "lev_bonus_*" bought
with tokens, category 3 (4 rows); all with the unlocklevels cheat, bit 11). Both rows are enabled by the profile
(UiScreen_InitItem cases 11 / 41: 0x004a0720 and Game+0x4c8 bits 0x1000 / 0x800): the token unlockables
ui_extrasmenu_cutscene and ui_extrasmenu_replay (pickup.rs UNLOCKS). The remaster: see docs/subsystems/menus.md.

Observed (2026-10-16, the original off-screen with hooks off, oracle_diff.run_original in an instance of its own,
the trace's `roman` after the briefing): lev_1_m1 rifle in hand (weapon 2), clip 8 / 62, 5 sticky bombs, health 400;
lev_1_m1_b machine gun in hand (weapon 0), 100 / 1000, 5 sticky bombs, 400; lev_2_m1 (machine gun, sniper gun and
rifle added) the machine gun in hand, 100 / 1000, 5. The remaster's campaign tests expect the same.

## Open questions

- The original was not run through a level change (the coverage runs never won a level, and the harness cannot send
  a trigger); the loadouts above were observed on levels loaded directly. A run across lev_1_m1 -> lev_1_m1_b would
  confirm the carry-over (money, the absence of lev_1_m1's pub Topsy bombs) by observation.
- Game+0x568 bit 0x20 gates recording a level as reached at its start (0x0044daf0); what sets it is not traced
  (guess: a profile chosen).
- What the credits screen does at its end in the campaign (0x00415940): the remaster leaves it on a key press.
