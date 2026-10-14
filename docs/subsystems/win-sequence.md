# Level win sequence (lev_1_m1)

Static decompilation, 2026-10-14. Not seen in a capture of the original: most functions are "not executed in
recorded runs". A capture of the original's win would confirm the order and the `ui_stats_*` labels.

1. **Trigger 123**, MissionObject_OnTrigger 0x0044d600 (mission.md:238-242): Game+0x2730 += score_value (1000);
   the +1000 bonus to +0x272c only applies on lev_1_m1_b / lev_2_m1. Level state (Game+0xa64 +0xc) = 2.
2. **0x0044a390**, at the top of the game update 0x00402e80 (ui_01.cpp:2048-2067):
   - audio vt+0xa8(0) 0x004f24e0 stops every voice except the music (audio_02.cpp:701);
   - 0x0044f560(Game+0xa88, 0): music victory request; Music_Update 0x0044f580 picks state 5 (`l1vic`) at the next
     segment boundary, only while unpaused (audio_01.cpp:1714-1727);
   - success_text (Game+0x2660) through 0x00403410 with flag 1: "<br><br>" + ui_stats_ok appended
     (ui_02.cpp:836-845), Game_SetPaused(1, 1) (engine_01.cpp:218) pauses all voices, the pause effect, the clock;
   - state 10; callback 0x0044a230 armed through 0x00403370, deferred while paused.
3. **Dismiss**: the HUD update 0x00462790 consumes MenuSelect, MenuCancel or Fire and unpauses (0x004031e0,
   ui_01.cpp:2250-2252). No timeout. At pause count 0, 0x00403390 runs 0x0044a230.
4. **0x0044a230, state 10** (ui_01.cpp:1991): pushes the "Stats" screen (UI_PushScreen 0x00490b00, fade in 0.5 s,
   out 0.25 s, ui_03.cpp:409), which pauses again; state 11.
5. **Stats screen** 0x00494440 (ui_03.cpp:2501-2742); on show 0x00494050 the money counters +0x2724..+0x2730 are
   multiplied by Game+0x564 (guess: difficulty).
   - Phase 0 (~3 s, reveal += dt / 3): shots fired +0x2718, shots hit +0x271c ("%d (%d%%)"), enemies killed
     +0x270c of +0x2710, head shots +0x2714, money lines "$%d" / "-$%d" (+0x2724 kills, +0x2728 accidents, +0x272c
     saved, +0x2730 loot / score). Text ids at 0x0058fb50-0x0058fbe0, not resolved.
   - Phase 1: wait for MenuSelect. Phase 2 (0.5 s): the b_wanted_reward bar fills, ui_select. Phase 3 (~3 s):
     money counts into Game+0x570 (0x00401f40). Phase 4: wait for MenuSelect. Phase 5: pop (0.25 s fade).
   - MenuSelect jumps the current reveal to 1 (ui_select when it was waiting); a ui_move tick at most every 0.1 s
     while a value changes.
6. **0x0044a230, state 11**: success_movie (Game+0x2680, none on lev_1_m1: guess) and Credits (lev_5_m5 only)
   skipped; 0x00401ef0 commits the money (+0x570.. to +0x57c); 0x004907c0 sets next_level_name (Game+0x268c,
   "lev_1_m1_b"; NULL -> lev_shell when Game+0x568 bit 2); pushes "Loading".
7. **Loading** 0x00492f80 / 0x00492fd0 (ui_03.cpp:2026-2072): frame 5 loads the level (0x00418730; failure ->
   lev_shell with ui_corruptcontent), frame 10 starts it (0x00403600). Drawn by LoadingScreen_Draw 0x00449ac0.

## The Stats screen (pui_stats), for redrawing

Static decompilation (0x00494440 update, 0x00494050 on show); no capture yet. 640x480, y up.

- Background ui_background_01 / b_background_01, +-427 x +-240 at (320, 240), #969696. Title 9900 ui_stats_title at
  (320, 421.0), centred, scale 1.4, white, shadow.
- Progress bars (ui_progressbar, filled by 0x00494160), x 171.2, scale 1.0, #a0ffa0; label fitted to 144 px
  (ui_02.cpp:3623); bar (0x0046d130, render_r7.cpp:60-96) frame x+77..x+275, y +-9, 0xff204000; fill x+80 ..
  x+80+192 fill, y +-6, 0xff40c040; value text left at x+288:
  - 9904 Shots fired (+0x2718, max +0x2718) y 328.1; 9905 Shots hit "%d (%d%%)" (+0x271c, max +0x2718) y 304.1;
    9906 Enemies killed (+0x270c, max +0x2710) y 280.1; 9907 Headshots (+0x2714, max +0x270c) y 256.1.
  - shown = min(max x reveal, value); fill = shown / max; percent = trunc(fill x 100).
- Money lines (ui_textoption, 0x00494280), scale 1.2, #a0a0ff, label right-aligned ending at x 320, value left at
  x 336, "$%d" / "-$%d" of ftol(amount x reveal): 9911 Kills +0x2724 y 177.8; 9912 Accidents +0x2728 y 153.8;
  9913 Rescues +0x272c y 129.8; 9914 Loot +0x2730 y 105.8.
- Wanted poster 9901 (ui_wantedposter, init 0x00494790, draw 0x004948d0) at (320, 252.44), drawn while v (+0x4f8)
  > 0: rotation v x 6.8068 rad, size 1.5 - v^2, alpha v x 2550 below 0.1; texture b_wanted_reward with black
  "$%d" of Game+0x570 at (0.5, 0.2), scale 4 (WantedReward_Draw 0x00494b70).
- Prompt ui_stats_ok (0x00494640), 0xffc0c0c0, scale 1.2, at (0.5, 0.15) of the screen, centred, only in phases 1
  and 4, blinking by frame count (visible while (frame & 31) < 24).
- Counters: shots fired in PlayerObject_Update (game_06.cpp:2972, 2999); hits and head shots in
  Projectile_ApplyHitDamage (game_06.cpp:1564/1579, player-owned, head/neck bone); kills 0x00431720
  (ai_04.cpp:597); enemy total ++ per spawned enemy (ai_01.cpp:282/1520/2327, game_03.cpp:1101); a killed object's
  score_value to Kills (>= 0) or Accidents (< 0) (game_03.cpp:978, ai_01.cpp:1920): lev_1_m1 grunt 100, captain
  300, barracks_mini 600, rocket_tower_turret 500, oilpump 50, powderkeg 20, penguin -10, molmer / q -500; Loot:
  mission win 1000 (game_02.cpp:2262), token pickup 2500 (0x004a0850).
- Difficulty factor Game+0x564 (table 0x00589188): Easy 0.5, Normal 1.0 (default), Over The Top 1.2, Insane 1.5;
  applied to the four money counters on show; the count-up runs from Game+0x570 to max(0, +0x570 + sum).
- Phases: 0 reveal += dt / 3 (ui_move every >= 0.1 s while a shown value changes); 1 wait; 2 poster v += 2 dt,
  ui_select at 1; 3 money count-up ~3 s (ui_move ticks); 4 wait; 5 pop. MenuSelect sets reveal to 1. Correction to
  step 5 above: confirming a wait plays no select sound (+0x88 is cleared before it is tested, ui_03.cpp:2668/2674).
