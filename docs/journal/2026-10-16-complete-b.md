# 2026-10-16/17: level completability, levels 4, 5 and bonus (agent-complete-b)

Branch `agent-complete-b` (from wip/full-decomp f4db787; wip merged at 236f306 and 5628de1 (peasants); agent-complete-a
merged at 08af991). Target dir engine/target-wt-complete-b. The question: can each level be finished?
`engine/playtests/complete_<level>.json` plays a level through its real objective chain (the mission data's triggers:
volumes entered, objects destroyed, counters filled, timers run out, peasants banked), `land` / `land_by` / `damage`
steps skipping travel and fights and `SHIELDME` keeping Roman alive, then checks the win, the Stats screen and the move
to the next level (the credits after lev_5_m5, the title after a bonus level played from Extras).

## Table

| Level | Finishable | Blockers found | Fixed (commit) | Open |
| --- | --- | --- | --- | --- |
| lev_4_m1 | yes (complete_lev_4_m1) | player_has_tickerbomb ignored: the goliaths' ticker sites said "You need Ticker Bombs!" (no pub on the level) | 790ba7d (wip has the same fix) | the sites now ride the goliaths (wip ticker lock); the test arms them before the goliaths set off |
| lev_4_m2 | yes (complete_lev_4_m2) | Jonesy's tower run (tower_marker_tag 30) was not ported: he stayed in defend_me and died | the tower task (wip 6fbda00, ai/tower.rs, agent-tower); the test guards him by the tower with the bot's fight | - |
| lev_4_m3 | yes (complete_lev_4_m3) | none in the chain | - | the jumper pack is ported on wip (agent-blockers1) but the test does not use it; holes / off-map areas east of x 3250 (with SHIELDME a fall there never ends, "fell to his death" every frame) |
| lev_4_m5 | yes (complete_lev_4_m5) | none: 500 platoon kills (27 -> counter 203 -> 456 -> timer 234 -> 123) | test aid: auto mortar (229db12) | 15 min wall per run |
| lev_5_m1 | yes (complete_lev_5_m1) | placed grunts never sent send_trigger_on_death: the Q1-12s' 2999 never reached counter 58 | e0dc1ec | - |
| lev_5_m2 | yes (complete_lev_5_m2) | the zeppelins never dropped their rocket captains (the rigid mini zeppelin had no Animator, so anim_intro's drop notes never fired): nothing attacked the villages, the level could not be lost | 229db12, 7348741, 896db8d | the dropped captains reach village 3 as in the original but stop ~120 units from house 60 and never fire (the original destroys village 3 at 113..141 s, village 2 from 148 s); the rappel IC anim is not played |
| lev_5_m3 | yes (complete_lev_5_m3) | none | - | - |
| lev_5_m4 | yes (complete_lev_5_m4) | only the level's last pub was kept: pub 154's ticker bombs could not be had, so no gate could be blown | f4ca5aa (agent-complete-a's equal 63d393d kept at the merge) | - |
| lev_5_m5 | yes (complete_lev_5_m5): the end of the campaign | none: 600 platoon kills -> 456 -> 123 -> Stats -> post_5_5_the_end -> credits -> title | - | - |
| lev_bonus_m1 | yes (complete_lev_bonus_m1) | none (survive 300 s: timer 16 -> 5000) | - | 10 min wall; SHIELDME through the assault |
| lev_bonus_m2 | yes (complete_lev_bonus_m2) | none (75 peasants to mat 36 -> 4000 in 86 s of the 600) | - | - |
| lev_bonus_m3 | yes (complete_lev_bonus_m3) | Q's / Jonesy's deaths (5001 / 5002) did not fail the level | b72ee74 | - |

Bonus levels are played as Extras' Replay Mission plays them (MISSIONS standing in for the tokens): after the Stats
they return to the title. Their next_level_name (lev_bonus_m1 itself, lev_bonus_m3_bd, lev_bonus_m4) is never loaded,
and success_movie "none" / "0" asks for a movie that does not exist (logged, skipped, as the original's failed open).

## What was learned

- **Placed AIs' and teammates' death triggers** (e0dc1ec; b72ee74, replaced at the wip merge c44eab9 by wip's own
  Companion::death_trigger): mission.rs built death_senders from the export's
  edges but only read them on fx::Died (destructibles). Placed grunts and Q / Jonesy now carry DeathTrigger like
  spawned troops; a dead grunt restored from a save is despawned before troop_deaths reads (savegame apply `.before`).
  companion_death_fail checks it (Q killed on lev_bonus_m3 -> 5001 -> Failed).
- **Every pub of a level** (f4ca5aa / 63d393d): Mission held one pub; lev_5_m4 and lev_1_m3 have two.
- **player_has_tickerbomb** (790ba7d): MissionObject_Load sets Game+0xd90 from it (game_02.cpp:1994), the flag the
  pub's award sets; the remaster only gave ticker bombs in a pub.
- **Zeppelin drops** (229db12): the note handler 0x00409140 (a 3 KB note-command function missing from
  symbols/functions.csv; read with capstone at 0x00409abf) makes create_rocket_captain_IC a "captain" with
  RocketCaptain_ReadProperties 0x00434ac0 and 0x00434a80(50, 100), counts it (Game+0x2710), lists it at +0xf24 (no drop
  while 12 live, unless the level starts in the turret) and plays the IC anim the note names; that anim's
  freefall_attack_village 40 60 (GenericAIObject_HandleEvent 0x0043d180) sets the priority tags +0x858 / +0x85c.
  AI_FindTarget 0x00426920 walks them first: the first live object of the lowest tag, its 2D distance stored as the scan
  range +0x5dc (so the 2 x +0x5dc keep test holds it however far, ai_03.cpp:801); the walk's end sets +0x5dc = +0x5d8 x
  1.5 (0x005bce20). A placed target is seen when the sight ray reaches its own collision (0x00426fd0). Oracle (original
  standing still on lev_5_m2, work/oracle-complete-b/l52_long): 12 captains at 70 s, village 1 (tags 40..44) 73..88 s,
  then the same captains walk to village 3; the remaster now 67..83 s and the same walk.
- Tooling (docs/remaster-engine.md "Completion tooling"): land, land_by (a pub: its doorway; 0: onto the object; farther
  rings when a big object hides the ground), probe, damage on AIs, kill_dropped, until-wait, mortar auto;
  vehicles.dropped in the state log; agent-complete-a's mission_chain.py, shoot / bomb orders, grunt_ids / turrets.
- Line endings: Git Bash `sed -i` rewrote ai.rs to LF once (restored); python text-mode writes turned playtest.rs CRLF
  once (restored). Patch CRLF files with binary reads / writes.
- Audio: a run where the output device went away ("rodio::stream: The requested device is no longer available") froze
  the engine when the success movie started (lev_5_m2: the state log stopped). Not reproduced; open.
- Noise: mission.rs logs "ticker site N: lock_parent_tag ... not ported" every frame until the site nags (6800 lines a
  run on lev_4_m1); the ticker-lock port will remove it.

The priority target's range (896db8d) is the recovered code's mechanism, not a guess: AI_FindTarget's tag step
(0x00426920, src/ai/ai_02.cpp:641) stores the found object's 2D distance in the scan range +0x5dc, the keep test drops a
target only beyond 2 x +0x5dc (ai_03.cpp:801), and the walk's end sets +0x5dc = +0x5d8 x 1.5 (0x005bce20 = 1.5 read from
the exe, ai_02.cpp:654). The earlier guess (f135597) was replaced by it.

## Next

- lev_5_m2: why the captains stop short of house 60 and never fire (range 50..100 vs the original's hits from ~120:
  their nav, or the house's aim point).
- Use the jumper pack in complete_lev_4_m3 and arm lev_4_m1's sites on the moving goliaths (both ported on wip).
