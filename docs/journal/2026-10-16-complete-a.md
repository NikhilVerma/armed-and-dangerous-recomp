# 2026-10-16/17: level completability, levels 1 to 3 (agent-complete-a)

Branch agent-complete-a (from wip/full-decomp f4db787, wip merged again at 236f306 and 5628de1 for the cheats step
and the peasants; agent-complete-b merged twice for land / land_by / damage-by-id and the placed-grunt death
trigger). Remaster in engine/target-wt-complete-a. The question: can each level be finished through its real
objective chain? Each `engine/playtests/complete_<level>.json` plays the level from its start to mission complete
and on into the next level: travel is skipped (warp / land / land_by / land_sight), every objective is done by its
real action (shoot, bomb, ticker site, pub, mounted gun, peasants), each trigger of the chain is expected, then the
success text, Stats, the success movie and the next level's load. `{"cheat": "SHIELDME"}` keeps Roman alive (the
tests are about objectives, not survival); no `send` of a mission trigger anywhere.

## Status

| level | finishable | blockers found | fixed (commit) | open |
| --- | --- | --- | --- | --- |
| lev_1_m1 | yes (complete_lev_1_m1) | none | | |
| lev_1_m1_b | yes (complete_lev_1_m1_b) | placed grunts never sent send_trigger_on_death (forge gunners -> counter 416); a gunner dead in a loaded save kept his gun held (could never be entered); the forge cannon's glass sight was opaque (the mounted view a dark plate) | e0dc1ec (complete-b); 943b4ae; 7558893 | |
| lev_1_m3 | yes (complete_lev_1_m3) | two pubs: the second (283) replaced 119 and its ticker bombs; sticky bombs did no damage to the building they stuck to | 63d393d; 8685a2a | window grunts stand inside the wall middle (ref_front_2f_m), only a blast reaches them (the original's trace: same spot, they shoot Roman) |
| lev_1_m4 | yes (complete_lev_1_m4, 207 s of defence) | platoon members only died to a bullet within 5 units of a member (10 kills a minute, 300 needed) | 96c9ec2 | |
| lev_2_m1 | yes (complete_lev_2_m1) | snipers on ledges (the bot needs spots in sight: land_sight); sniper 28 on the bridge over the canyon: the crosshair never closes on him, only lobbed bombs kill him | c9433c7 (bot) | why the crosshair misses sniper 28 (rigged, has a body, in view) |
| lev_2_m2 | yes (complete_lev_2_m2) | sticky bombs vs buildings; the machine gun's muzzle fx leak made the Q1-12 fight crawl (16 -> 270 ms frames) | 8685a2a; 260b758 | a bomb at the Q1-12 can take civilian house 100 with it (one allowed, two fail) |
| lev_2_m3 | yes (complete_lev_2_m3) | peasants (agent-peasants, merged); rocket tower turrets (the bot aims at the turret) | 081462f | |
| lev_2_m4 | yes (complete_lev_2_m4, 238 s of defence) | none beyond lev_1_m4's | 0f4c942 | 123000 entities by the end: dead army grunts are never removed |
| lev_3_m1 | yes (complete_lev_3_m1, with agent-blockers1 merged) | the ticker sites on the goliaths: "lock_parent_tag not ported (0x0044e120)", the goliaths never died, the peasants stayed caged | agent-blockers1 f722986 (in wip 6fbda00); b6a3994 | |
| lev_3_m2 | yes (complete_lev_3_m2) | sticky bombs vs buildings (above) | d2f8450 | 4 of the 6 civilian deaths allowed happened (enemy fire or blasts beside targets): close to the failure |
| lev_3_m3 | yes (complete_lev_3_m3, 35 s) | none (peasants merged) | dde4aec | |
| lev_3_m4_bd | yes (complete_lev_3_m4_bd, 415 s of defence) | none beyond lev_1_m4's | (in the suite) | |

## Fixes (cause -> fix -> test)

- **Placed AI death triggers** (agent-complete-b e0dc1ec, merged): mission::deaths read only object deaths; placed
  grunts with send_trigger_on_death never sent it. Found on both branches (lev_1_m1_b gunners 286 / 323).
- **Gun held by a removed gunner** (943b4ae): savegame apply despawned the gunners dead in the save without letting
  their guns go (ai.rs die does): the gun stayed "held" for good. Test gun_save_write / gun_save_read.
- **Several pubs** (63d393d): Mission kept one pub; lev_1_m3 places 119 (ticker bombs, opened by 804) and 283 (the
  second village); the last loaded won, so the ticker bombs and the whole level could not be had. Now a Vec of
  pubs, each with its doorway, triggers, entered flag and offers; saves keep every pub (old saves: the first).
- **Platoon hit box** (96c9ec2): the original's damage record on the platoon object kills every member within
  kill_range of the hit point (FakeGruntObject_Slot52 0x00433000); the record comes from the platoon's collision,
  its model platoon_1 (a 50 x 4 x 14 box round its row of five). The remaster counted a hit only within 5 units of a
  member's chest. Now a bullet that enters the box is the record at its entry point.
- **Sticky bombs on buildings** (8685a2a): StickyBombProjectile_Explode 0x00481ab0 gives the stuck object the direct
  hit (damage + area_damage = 300, 0x0047eec0) and spares it from the area blast (0x0047f020). The remaster gave
  the direct hit to grunts only: a bomb stuck to a house or barracks did nothing.
- **Machine-gun muzzle fx** (260b758): every shot started a new looping fx_muzzle_flash_flemming that never ended
  (its loop makes the flashes and the shell casings): long fights reached 76000 entities and 1.9 million casings
  (frame 16 -> 270 ms). Now one held instance while firing, released the frame after (0x004a1f20 / 0x004a1dd0).
  Test mg_muzzle_fx.

## Tooling (docs/remaster-engine.md "Level completion")

- tools/mission_chain.py LEVEL: the win / fail chains traced back from the mission's triggers.
- State log: `triggers.<id>` (existing), `grunt_ids.<id>` (placed AIs: pos, mind, health), `turrets.<id>` (mounted
  guns: distance, AI held, mounted), `mission_obj.pubs.<id>` (open, done, door); an EVENT where the player's shell
  lands.
- Playtest steps: bot orders `shoot` / `bomb` INSTANCE_ID (a placed object, a placed AI, a rocket tower's turret);
  `land_sight` [id, distance] (a spot from which a bullet reaches the object, from Roman's eye and his camera);
  `until` "trigger:N" with "wait" as its limit, `until` "message" with "wait"; defence aim help "platoon", `fire`.

- **Translucent materials** (7558893): the forge cannon's glass sight was drawn opaque, so the mounted view was a
  dark or orange plate. Material kind bit 0x10 is translucency (fn_004897d0: blend mode from byte +0x37, 2 =
  SRCALPHA / INVSRCALPHA; the opacity +0x48 is the draw state's constant alpha): k_glass_warm 0x02000111 with 0.35
  and 0.1, the bomber's canopy 0.54, the saucer 0.31. Export: material "blend" and "opacity";
  tools/remaster_patch_materials.py added them to every level in work/remaster (atomic replaces). Engine: alpha x
  the opacity. Checked in a mounted screenshot (the gate through the glass).
- **vehicles_forge_cannon** (a4937ff): it passed by luck (the gunner on cannon 23 left within 1.6 s on wip's random
  sequence, 1 s later on ours); now the test kills him first.

## Open

- After merging wip 6fbda00 / dbccede: Q's and Jonesy's death trigger went out twice (wip's Companion::death_trigger
  and agent-complete-b's DeathTrigger): one path kept. decals_obj assumed a sticky bomb leaves the house it sticks to
  standing; with the direct hit (8685a2a) it takes it down, as 0x0047eec0 does: the test expects the husk now.
- lev_2_m1 sniper 28: the crosshair test misses him on the bridge over the canyon (CROSSDBG shows the bridge parts
  w_l2_m1_05 / 06 and the level behind him); bombs reach him. A player with the machine gun cannot shoot him.
- Dead army grunts are never removed (lev_2_m4: 123000 entities, 55 ms frames at the end).
- Long runs: the defence completions take 200 to 415 s of game time (20+ minutes of wall time on the loaded
  machine); they are in the SUITE as asked, which makes regress.py much longer than "about 3 minutes".
