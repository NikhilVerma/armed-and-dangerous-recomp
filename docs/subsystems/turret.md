# Mounted guns (StigTurret: lev_1_m1's stig_mgun)

Static recovery 2026-10-14 (no run of the original on the gun yet: every turret function is "not executed in recorded
runs"). Engine port: `engine/src/turret.rs`.

## Instance and data

| What | Value | Evidence |
| --- | --- | --- |
| Instance | id 32, template stig_mgun, model stig_mgun_0, at (71.17, 172.61, 13.64), turned 180 degrees (row 0 = (-0.943, -0.008, 0)): the gun points game +y | work/remaster/lev_1_m1/scene.json |
| Class | StigTurret, vtable 0x005a19d8, behavior `stig_mgun_turret`; load slot 0x00498d60 | src/game/game_08.cpp:1751 |
| Property set p_stig_mgun | player_bone_name ref_player, pivot_bone_name center, camera_bone_name camera01, player_exit_bone_name ref_exit, message_radius 30, turn_max_speed 1.0, pitch_max_speed 0.7, pitch_range -55..15, yaw_range -55..55 (degrees), recycle_time 0.05, auto_aim 1, rapid_fire 1, projectile stig_mgun_bullet, ai_projectile stig_mgun_bullet_ai, emitter / muzzle bone ref_shoot_00, muzzle fx fx_muzzle_flash_stig_mgun, max_health 10000, ai_can_use 1, ai_never_leave 0 | work/remaster/lev_1_m1/properties.json |
| Code defaults | recycle_time 1.0, turn/pitch speed 2.0, zoom turn/pitch 0.025 / 0.1, message_radius 20, ai_kickout_health_pct 50, ai_kickout_stayout_secs 10, ai_kickout_impact_force 6000, ranges -45..45 | 0x00497d00, 0x004980c0, 0x00498d60 (game_08.cpp:885-1135, 1751) |
| p_stig_mgun_bullet | proj_stig_bullet, speed 400, lifetime 2, damage 35, impact_force 4690, damages_buildings 1, trail fx_trail_stig, impact fx_imp_stig_<material> | properties.json |
| p_stig_mgun_bullet_ai (AI gunner) | speed 120, lifetime 4, damage 25 | properties.json |
| Skeleton | base (stand, sandbags) > center (pivot) > gun > ref_shoot_00 (muzzle, 7.16 ahead along -y); root refs ref_player (0, 8.68, 0.06), ref_exit (0, 7.21, 2.53), ref_grunt_00..08, ref_grunt_exit | work/remaster/lev_1_m1/skeletons/stig_mgun_0.json |
| Mesh binding | UBYTE4 blend index without weights (rigid): index 3 base (stand 1867 + sandbags 351 vertices), 6 center (357), 9 gun (2280) | skin chunk stig_mgun_0 mesh streams; tools/remaster_export_anim.py RIGID_SKINNED |
| Camera node Camera01 | reference anim stig_mgun_ref: model (0, 7.00, 9.02), looking along -y tilted 7 degrees down, FOV 0.654 rad vertical (horizontal 60 at aspect 1.702); the corner anims parent it to center | work/remaster/lev_1_m1/anims/stig_mgun_ref.json "cameras" |

## Entering and leaving

- **Prompt** ui_vehicle_entry "Press %s to enter mounted gun" (0x004662a0): when the player may act (0x0047a950) and
  is not mounted, the first gun of list World+0xec4 not occupied by an AI (vt+0x150, flag 4) whose 3D distance to
  the player is within message_radius (0x004983e0) gets flag 1 of +0x530 (0x00498740) and the text shows.
- **Use** (action 0x14) in the player update (0x004788a0, game_06.cpp:2220-2310): mounted -> vt+0x17c (leave);
  otherwise pub doorways first, then the nearest usable of list +0xeb0: a turret qualifies through vt+0x184
  (0x00498760: flag 1 and not AI-occupied) -> 0x00475910 (leave zoom) and vt+0x178.
- **Mount** 0x00498f60 -> 0x00498a80: needs player_bone_name and camera_bone_name; player vt+0xf4(0) (hidden) and
  0x004a0e00(0) (his weapons hidden, game_09.cpp:1092); turret flag 8; the camera target = the turret with the node
  name camera01 (0x0040e200); Object_Attach the player at player_bone_name; "wpn_stig_mount" (2D); Game+0xac8 = 4,
  Game+0xacc = the turret.
- **While mounted** ui_vehicle_exit "Press %s to exit mounted gun" (0x00466210: Game+0xac8 set, not
  start_in_turret Game+0x26cc). The player update takes no input (PlayerCanAct: Game+0xac8). The player's damage is
  halved (PlayerObject_OnHit 0x0047afa0: K(0x005bcb4c) = 0.5). His death leaves the gun (0x0047ae40 -> vt+0x17c).
- **Leave** 0x00498fb0 -> 0x00498bb0: zoom out; flag 8 cleared; player shown (vt+0xf4(1), weapons 0x004a0e00(1)),
  his camera back (0x004757b0), detached; placed at player_exit_bone_name; Object_ForceGround(10, 20); Game+0xac8 =
  +0xacc = 0; the angles +0x61c = +0x620 = 0; the move loop stopped.

## Camera

Camera_Update 0x0040e350 with a camera name and no look-at name (game_01.cpp:3128): the node's matrix directly
(0x0040a4d0, with FOV, near from the node: horizontal 0.654 -> vertical through aspect 1.702 = the follow camera's
0.654 vertical, near 1). camera01 rotates with the pivot (the corner anims' node table parents Camera01 to center;
the reference anim's table is empty: **guess** that the runtime node tree does the same). No zoom node on this gun
(zoom_camera_bone_name unset).

## Aiming

The update 0x004990a0 (game_08.cpp:1878), in camera modes other than 2:
- speeds = zoomed ? (+0x5c4, +0x5c8) : (turn_max_speed +0x5bc, pitch_max_speed +0x5c0);
- +0x620 += Axis(TurnLeft 4) * dt - Axis(TurnRight 5) * dt, +0x61c += Axis(LookDown 7) * dt - Axis(LookUp 6) * dt, the
  axis scale being the speed (Controls_AxisMax 0x00414a30(speed, dead zone 0.21, 0.5); InputPC_AxisValue
  0x004f9560: keys give the scale, mouse sensitivity * delta * scale, sticks scaled past the dead zone);
- +0x61c clamped to pitch_range, +0x620 to yaw_range (each clamp skipped when a limit is 0);
- any change starts the loop "wpn_stig_move_lp" (0x0058f460), none stops it.
Compared with the player's turn (Axis * 0.3 * 10 * dt) and pitch (Axis * 0.95 * dt), the gun turns at 1/3 and
pitches at 0.7 / 0.95 of the player's rate per mouse count: 0.0033 and 0.0042 rad per count at the default
sensitivity (player rates measured: 0.0100 / 0.0057).

The pivot callback 0x00499670 -> 0x00498ff0 rewrites the pivot bone's pose: dest = source * qz(+0x620) * qx(+0x61c)
(Quat_Mul 0x004f1710(a, b) = b * a). The gun points model -y, so +z yaw turns it to the gunner's left (+x) and +x
pitch tips the muzzle down: 55 degrees up, 15 down, 55 each side. (The game_08.cpp comment calls +0x620 "pitch" and
+0x61c "yaw": the clamps and the axes show the reverse.)

## Firing

- Button 8 (Fire) held (0x00414810, threshold 0.5) -> vt+0x138 0x004988e0: +0x530 |= 0x40, &= ~0x80; a player
  occupant fires when time - last fire +0x524 > recycle_time (0.05 s: 20 rounds/s) -> vt+0x13c 0x00498960.
- 0x00498960 without a target: 0x0047dbf0(emitter bone, projectile_name, turret; Game+0xaa8, player, auto_aim): the
  start point is the emitter bone, the target the crosshair's point. In Game+0xac8 = 4 the crosshair ray is 2000
  long and no auto-aim candidates are gathered (Hud_UpdateCrosshair 0x00406f50: other modes release the list), so
  auto_aim finds nothing on this gun. A player shot counts (++Game+0x2718); vt+0x144 the muzzle flash; +0x524 = time.
- Muzzle flash 0x00497fd0 with rapid_fire: one fx_muzzle_flash_stig_mgun held on muzzle_bone_name (desc {0x24, 8});
  each update 0x00497ed0 clears 0x40, else sets 0x80, else releases it: the fx lives until two updates after the
  trigger is let go. The fx script plays wpn_stig_machgun_fire_lp (looped, op 0x39), rumble "playermgunfire" (op
  0x3f), a flash fx_flem_flash and a shell fx_flem_shell every 0.15 s (fx.json).
- No ammo count, no overheating: no such property or code in the turret family.

## AI gunners and damage

- Grunts can take a free gun when they look for cover (0x0042a7b0 -> 0x0042a100: free turrets vt+0x158, within range
  +0x5dc, line of sight), or start in one (start_in_turret_tag, AI_ResolveStartObjects 0x00425e20, Game+0x26cc);
  enter 0x00498820 (flag 4), leave 0x00498890; anims grunt_stig_mgun_idle/fire_tl/tr/bl/br with the gun's
  stig_mgun_idle/fire_* corner poses (blended four ways, 0x00408c70); ref_grunt_00..08 slots. An AI gunner fires
  ai_projectile_name at a target (0x0047de40). lev_1_m1 places no start_in_turret_tag. Ported 2026-10-14 (branch
  agent-gunner, ai.rs `GunPhase`, `pick_gun`, `gun_cycle`, `gun_aim`), except start_in_turret_tag and the kickout.
- Who takes this gun on lev_1_m1 (traced 2026-10-14, grunt-ai.md section 12): grunt_regular 34 at (55, 98), mode
  trigger_marker with marker tag 10, woken by trigger 808, runs to marker 35 at (71, 150) whose action is hide:
  Marker_ProcessArrival 0x0044ba30 -> FindHidingSpot vt+0x240(0, 0, 1) (no target needed) -> 0x0042a100 picks the
  nearest free gun within scan_distance (350): gun 32, 22 units away; no trench is within his range. Any grunt's later
  searches (clip end, reload end) can also pick it when it is nearer than every valid trench.
- The gunner's states (ai_06.cpp:583-797): 0x7b walks to the gun (VC(turret, 0xfc, 0, 2) during the path test,
  turret vt+0x15c(1) reserves it); on arrival attaches to the gun at ref_grunt_08 (0x004539f0, name 0x005b2868), physics
  off, +0x484 0x8000, turret vt+0x160 (enter, flag 4), fire counters from the turret tables (+0x89c.. / +0x8cc..),
  anims grunt_stig_mgun_idle_* (ids 0x87..0x8a) with the gun's stig_mgun_idle_* corner poses (0x00408c70), timer
  +0x494 = rand(+0x8c4, +0x8c8), state 0x7d. 0x7d: target upkeep; vt+0x190(65 degrees) = the target within the gun's
  arc; firing through 0x004292f0 with grunt_stig_mgun_fire_* (0x83..0x86) and the gun's fire corner poses, the aim
  weights +0x580 / +0x584 blending the corners (the AI does not use the pivot callback); target outside the arc with no
  rounds left and no hold (+0x484 0x200): get off at the gun's node ref_grunt_exit (0x005b27f4), walk there (0x7c), then search again
  (vt+0x240(1, 0, 0) with flag 4 cleared).
- Sharing with Roman (one object, one set of flags +0x530): Roman's prompt and Use need flag 4 clear (0x00498760: no AI
  on the gun; a grunt only walking to it, flag 0x10, does not hide the prompt); the AI search needs ai_can_use (flag
  0x100, 0x004993c0), no kickout time left (+0x614), no player (flag 8) and flags 0x14 clear (0x004987a0); a grunt on his
  way (0x7b) gives up when Roman got on first (vt+0x14c = flag 8). ai_never_leave sets bit 9 clear (vt+0x154): the
  gunner never gets off for an out-of-arc target.
- The gunner's counters (Ai_InitGeneric ai_01.cpp:2376-2381, defaults, no lev_1_m1 overrides): stig_fire_num_clips 3..5
  (+0x5b4), _clip_delay 1..1 (+0x5c8), _num_rounds 10 (+0x5bc), _num_bursts 5 (+0x5b8, at most the rounds),
  _burst_delay 0.25..0.5 (+0x5cc), _delay 0.5..0.75 (+0x494). 0x00427600 rolls them (first time: clips too, no pause);
  0x004292f0 (kind 1, vt+0x168 = 1 for StigTurret) plays the fire set while rounds and burst last, re-rolls a spent
  burst with its delay, counts a spent clip down with the clip pause, and clears flag 4 after the last; state 0x7d then
  waits stig_fire_delay and re-rolls (flag 1 of +0x48c when all three ran out). The shots come from the fire set's
  "fire" notes (grunt_stig_mgun_fire_bl: 0.067 and 0.233 of a 0.3 s loop) through GenericAIObject_HandleEvent
  0x0043d180 (anim ids 0x23 / 0x85 / 0x6a / 0x68): rounds and burst > 0 -> vt+0x194 0x004284e0 (the grunt's aim error
  at the target's ref_hit_00) -> the gun's vt+0x13c 0x00498960(0, point): ai_projectile_name from the emitter bone
  (0x0047de40), the muzzle flash; both counters down. In effect: 10 rounds at 6.7 / s in two bursts of 5 with 0.25..0.5
  s between, then (clips - 1) x 1 s and 0.5..0.75 s of pause.
- The aim 0x00428fc0 (vt+0x190, window 65, called twice a frame by 0x7d): weights +0x580 from the angle between the
  gun's heading (0x00453150) and the target (inside +-65: 1 - (angle + 65) / 130; outside 0 / 1 and "off target"), +0x584
  from the elevation (acos(dz / len) * 57.3 - 90; inside +-26.5: 1 - (e + 26.5) / 53), stepped by dt / 0.481 s (set at
  once with flag 1). The gun follows through its corner poses (0x00408c70 with the same weights: stig_mgun_fire_* while
  firing, else stig_mgun_idle_*): their pivot is qz(+-65) * qx(-+25) (stig_mgun_idle_tl center = qz(65) qx(-25)); the
  gunner's own anims grunt_stig_mgun_* blend the same way (vt+0x1a0, ids 0x83..0x8a). Getting off or dying sets the
  gun's corners back to (0.5, 0.5) (0x004266b0).
- No hit reaction on the gun (0x00430340: +0x484 0x8000). His death releases the gun (0x00431720 -> 0x004266b0).
- Damage 0x00498400: ignored at no health or from the same side; with the player on it the player takes the hit;
  with an AI gunner the gun loses health (death vt+0xd4 -> send_trigger_on_death, Object_Kill) and below
  ai_kickout_health_pct the gunner is thrown out (impact force 6000, ai_kickout_fx_name, 10 s stay-out). An empty
  gun takes no damage.

## Engine port (turret.rs) and open points

- Ported: prompt, enter / leave with Use, hidden Roman held at the seat, the camera node, aiming by mouse / stick /
  keys at the gun's rates with the clamps, the pivot turning the gun's mesh (skinned by bone: RIGID_SKINNED export),
  the move loop, 20 rounds/s of p_stig_mgun_bullet to the crosshair point (range 2000), the held muzzle fx, shots
  counted, half damage while mounted, leaving on death.
- Data: stig_mgun_0 / _9 must be re-exported with `RIGID_SKINNED` (tools/remaster_export_anim.py) for the gun to
  turn; with the old export the camera and shots turn but the mesh stays still.
- AI gunner (ai.rs): the search candidate (0x0042a100, compared with the trenches by distance), 0x7b running to
  ref_grunt_exit and attaching (body held at the seat the anims show: 0.3, 7.5 behind the origin), 0x7d aim / counters /
  notes / shots of p_stig_mgun_bullet_ai with the muzzle flash, the gun's pivot from the corner weights (Turret::ai_aim:
  the corners' angles blended linearly, **guess** at the quaternion blend), 0x7c getting off; Roman's prompt hidden while
  a grunt is on it; death releases it. Playtests engine/playtests/gunner1.json (grunt 34 mans and fires, Roman's fight
  kills him, Roman mounts: 7/7) and gunner2.json (target moved out of the arc: off the gun, 4/4).
  Not ported: start_in_turret_tag, the kickout (+0x60c 50 % of 10000 health, impact 6000, 10 s stay-out), flag 1's
  predicted aim point (0x004580a0), the gun's own fire vs idle corner difference (both drive the same pivot here).
  **Guesses**: the path to the gun ends at ref_grunt_exit (4 units) and snaps onto the seat; a grunt beaten to the gun
  by Roman pursues (the original's 0x0042b8e0 would search again).
- Open: the -0.5 * object +0x2dc offset 0x0040a4d0 applies to the camera matrix (zero for a static
  object, **guess**); Roman's pose on the gun (hidden in the original, so none); the gun's corner anims
  (stig_mgun_idle_*) for the player are not played by any traced code; no capture of the original yet.
