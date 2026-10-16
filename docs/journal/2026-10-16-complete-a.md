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
| lev_1_m1_b | yes (complete_lev_1_m1_b) | placed grunts never sent send_trigger_on_death (forge gunners -> counter 416); a gunner dead in a loaded save kept his gun held (could never be entered) | e0dc1ec (complete-b); 943b4ae | forge cannon view: its glass sight drawn opaque (below) |
| lev_1_m3 | yes (complete_lev_1_m3) | two pubs: the second (283) replaced 119 and its ticker bombs; sticky bombs did no damage to the building they stuck to | 63d393d; 8685a2a | window grunts stand inside the wall middle (ref_front_2f_m), only a blast reaches them (the original's trace: same spot, they shoot Roman) |
| lev_1_m4 | yes (complete_lev_1_m4, 207 s of defence) | platoon members only died to a bullet within 5 units of a member (10 kills a minute, 300 needed) | 96c9ec2 | |
| lev_2_m1 | in progress | snipers on ledges: the bot needs spots in sight (land_sight) | | |
| lev_2_m2 | in progress | sticky bombs vs buildings (above); the machine gun's muzzle fx leak made the Q1-12 fight crawl | 8685a2a; 260b758 | |
| lev_2_m3 | in progress | peasants (agent-peasants, merged); rocket tower turrets | | |
| lev_2_m4 | not run yet | | | |
| lev_3_m1 | waits on agent-blockers1 (ticker sites on the goliaths, jet pack) | | | |
| lev_3_m2 | not run yet | | | |
| lev_3_m3 | yes (complete_lev_3_m3, 35 s) | none (peasants merged) | | |
| lev_3_m4_bd | not run yet | | | |

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

## Open

- Forge cannon view (lev_1_m1_b, lev_2_m1): the glass sight k_glass_warm is drawn opaque, so the mounted view
  is a dark or orange plate. Cause found: material kind bit 0x10 is translucency (byte +0x34 bit 4: blend mode from
  byte +0x37, 2 = SRCALPHA / INVSRCALPHA; alpha = material +0x48 times the frame's alpha, the texture factor:
  fn_004897d0); the forge cannon's glass records are 0x02000111 with +0x48 = 0.35 and 0.1. Not done: the export
  (opacity +0x48 and blend +0x37 per material) and the GameMaterial blend. Aiming still works (the shell flies to the
  crosshair's point).
