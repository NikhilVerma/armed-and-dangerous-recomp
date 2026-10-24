# 2026-10-23 merge fixes after audit wave 2 (branch agent-merge-fixes, from de92e7e)

Six open problems left by merging the wave-2 fix branches, each decided by the recovered C++.

| problem | cause | change (evidence) | commit |
| --- | --- | --- | --- |
| Q stuck in mode 2 (companion_fidget) | g05's formation_route: a point within 9 units needed no route; arrival = within 4 of the point. Q's point lay inside a solid object south of Roman (x 189..231, y -128..-79, lev_1_m1), whose column still has a floor | arrival = the route used up (ai_08.cpp:2056-2081, AIRoute_HasRoute 0x00470c30 after AIRoute_Update 0x00470aa0); the route is never empty: the point if a knee-high ray reaches it, else nav.rs's route to the nearest cell, the point appended when open from there | 1b66b87 |
| grunt aim during the reload in place | Mind::Reload (state 0x32) reset the aim weights to the centre | state 0x32 calls 0x0042df70 (45 degrees) each frame and plays the reload set by the weights (ai_06.cpp:1224-1227) | 8d82250 |
| AI_FindTarget twice | ai.rs `search` and squad.rs `find_target` | one `ai::find_target` (`Looker`, `TargetCandidate`); the squad's scan 0x0042be80 calls the same 0x00426920; its SetTarget alerts every AI of its group through `alerted` (0x0042cc70 / 0x0042c700, no "has no target" test) | be08296 |
| can-act after a camera path | turret.rs and the companions' gates used `holds_camera` | 0x00410150 is camera +0x17c == 1, cleared the frame the path anim ends (game_01.cpp:3191-3206): `active()` everywhere | 38d6104 |
| tests testing the wrong thing | see the commit | ai_sight_pursuit forces the lost sight (captain in Fire, Roman put behind the object); engage_exit checks "a fight begun" (new q_fights log); the sticky tests use new ai_force "grunts_hold" (bullets shot the bomb down, 0x00481320) | a83b34c |
| small | duplicate 0x0044e600 readers; a doc comment running into the next field | merged into mark_base_defence; plain comment | b4935f0 |

Learned: sticky_scare failed on de92e7e too (its second flee came from a placed grunt that no longer stands
there, and the bomb's 4 s linger_lifetime kills the carrier, so "still fleeing" must be checked before 4 s).
GNU sed -i in this shell rewrites CRLF files to LF: use python or the Edit tool on CRLF files.

Tests (engine/target-wt-mfix, headless): launch_newgame 38/38, launch_options 25/25, companion_attack_order 6/6,
companion_carried 24/24, companion_close_kill 13/13, companion_engage_exit 12/12, companion_fidget 4/4,
companion_idle_cue 8/8, companion_leader_swap 10/10, companion_orders_regroup 22/22, companion_death_fail 3/3,
grunt_aim_turn 4/4, ai_sight_pursuit 5/5, ai_sticky_scare 5/5, sticky_scare 8/8, ai_fire_bursts 3/3,
squad_sees_companion 4/4, player_gates 12/12, camera_follow 15/15, defence_l1m4 17/17. Not run: regress.py,
unit tests. The intermediate commits were not built one by one (the head was).

Open: the engine still holds the camera up to 4 s after a path with no mission text (camera.rs `holds_camera`);
the original returns it at once (+0x17c cleared at the anim's end). Camera behaviour, not changed here.
