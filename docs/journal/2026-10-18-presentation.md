# 2026-10-18: presentation and polish (agent-presentation)

Branch `agent-presentation` from wip/full-decomp 453c30f (wip merged again at ee20859); target dir
engine/target-wt-presentation. Fourteen deferred features (docs/port/features.csv), each with a playtest in the SUITE.

## Done

| Feature | Addresses | Engine | Playtest |
| --- | --- | --- | --- |
| key-rebinding | 0x00447d20, 0x00447e20, 0x00447f50, 0x004481b0 (with 0x00447fc0, 0x00448260, 0x00447e70) | bindings.rs (new), input.rs, gamemenu.rs, options.rs, levelreset.rs `KeepOnRestart` | controls_rebind (keys, the assign rule's three cases, cancel, a key in play, the prompt, the pad page) |
| legal-splash | 0x00493520, 0x00493590 | splash.rs (new), cutscenes.rs boot; b_legal exported from lev_shell (tools/remaster_export_ui.py `--shell-only`) | legal_splash, movie_intro, launch_newgame |
| loading-screen | 0x00447600 (LoadingScreen_Draw 0x00449ac0) | splash.rs; levelreset.rs waits a frame | loading_screen |
| exit-screen | 0x00493770 | splash.rs, gamemenu.rs `Page::Exit`; playtest.rs prints the result when the game quits | exit_screen |
| music-pause-screens | 0x0044f510, 0x0044f540 | gameaudio.rs `Music::hold`, audio.rs `Voice::held` | legal_splash, exit_screen (`music_held`) |
| save-thumbnails | 0x0044f3a0 | thumbnail.rs (new), saveslots.rs, savegame.rs | save_thumbnail (picture written, its luma, shown, deleted with the slot) |
| hud-meters | 0x00465be0..0x00465d30, 0x00465de0, 0x00465f30, 0x00466010, 0x004671b0, 0x00467220, 0x004677c0 | hud.rs, mission.rs | hud_meters (ticker icon, clock, patience 2 s), hud_meters_l2m3 (peasant counters), hud_meters_l1m4 (wall counter, flag base), weapon_glove (bof hint) |
| run-anim-speed | 0x0043bc50, 0x0043c560 | skin.rs `RunRates`, charanim.rs, ai.rs | run_anim_speed |
| arrow-models | 0x0047e9f0, 0x00482460, 0x004824e0, 0x00482780 | arrows.rs (new), player.rs, ai.rs | arrow_models |
| bodyfall-fx | 0x0043a8d0 | bodyfall.rs (new), fx.rs, ai.rs | bodyfall_fx |
| character-fade | 0x0042eb60 | camfade.rs (log), corpses.rs | character_fade |
| ai-voice-lines | 0x00437690, 0x00437860, 0x00437760, 0x00438a60, 0x00438c10, 0x00438c40, 0x00439dc0, 0x00438cb0, 0x00438e80, 0x0043a310, 0x0043a480, 0x00439f30 | voices.rs (new), audio.rs, ai.rs, army.rs, shrubpatrol.rs, goliath.rs | ai_voice_lines, ticker_goliath, unit class_voice_lists |
| cloth-cape | 0x00486f40, 0x004876b0, 0x00487910, 0x00487960, 0x00487990 (replaced), 0x004879c0, 0x00487a40 | cloth.rs (new) | cloth_cape |
| camera-startup-path | 0x0044d150 | campath.rs `startup`, mission.rs | none possible: no shipped level sets the property (all 24 empty) |

## Learned

- The HUD's mode counters and meters (peasants, the wall counter, the flag base) were coded in hud.rs but never
  spawned (missing from its SHOWN list): no level showed them. The ticker bombs icon ui_tickerbomb is 0x00467220's
  element: shown while ticker bombs are held and sites are left (captures cov-gate1-*), not only with the clock.
- The coverage counts tell an element's update from its class (228101 calls = one element every frame, menus
  included; 183311 = one element in play): 0x004677c0 drives three timed texts, 0x00467740 one unidentified element.
- CapnBase (0x00437510, captain slot +0x260) has no caller; the mission's scan through the neighbouring +0x25c drops its
  result. The "Molmer" voice rows were the Wildwood Monk's (MO1500xx); 0x00439f30 plays GOL50001_Dead (the goliath).
- A blast's kill launches a grunt in the original (an area record goes through Character_Die's velocity branch, state
  0x195), so blast deaths are ragdolls with a launch now; the landing is the body fall.

## Next / open

- The red flag fx on the defence HUD's flag base (0x00465d50, deferred hud-meters); 0x00467740's element (untraced).
- The shrub patrol's friendly fire (0x0043a480) has only a unit test (no playtest reaches a shrub patrol with Roman's own gun).
- Debt PR1..PR5 (docs/remaster-engine.md "Presentation and polish").
