# Player character (Roman) in lev_1_m1

Status 2026-10-13: static research for the remaster port (engine/). Nothing here was checked at runtime. The
numbers the port needs are in `work/remaster/lev_1_m1/player.json` (not committed; generated from the retail banks
by `tools/remaster_export_player.py LEVEL`, any level). Per-instance property overrides of the level are in
`work/remaster/lev_1_m1/instance_props.json` (`tools/remaster_export_instance_props.py LEVEL`). All of a level's
exports: `tools/export_level.py LEVEL` (docs/remaster-engine.md "Level export").
Guesses are labelled **guess**. "(exe)" = read from `orig/pc/GameData/game.unpacked.exe` with a read-only script.

## 1. Identity, model, spawn

| Item | Value | Evidence |
| --- | --- | --- |
| Template | `trotter` (the player is Roman; there is no `roman` template) | lev_1_m1_big07_obj; PlayerObject_Load 0x00473da0 plays "Trotter_ref" (src/game/game_05.cpp:544) |
| Property set | `p_trotter` (big06_prop) | templates.json |
| Model / LODs | `trotter_2`, `trotter_2`, `trotter_coll_0` (collision LOD 2) | templates.json, p_trotter collision_lod_index 2 |
| Skeleton | anim `Trotter_ref` (bones Center, belly, chest, neck, head, L/R arm chain, legs; refs ref_hit_00..06, ref_rhand, ref_q_00, ref_molmer_00, camera nodes) | lev_1_m1_big03_anim, node dictionary (docs/formats/big-anim.md 3.1) |
| Spawn | instance 4, pos (-214.578, 1254.768, 185.209), heading row0 (0.99996, 0.0087, 0) | lev_1_m1_big10_inst; no per-instance overrides |
| Grounding at load | Object_ForceGround 0x00455860(player, 1000, 10000) | game_05.cpp:552 |
| Units | game units, Z up; feet (**guess**, shared by docs/subsystems/havok-layouts.md:23) | |

The player is not placed through a marker: the `trotter` instance in big10_inst is the spawn. The mission object
(instance 6) gives the loadout: `player_add_machinegun 0`, `player_add_rifle 1` (overrides; the p_mission defaults
are 1 / 0), `player_set_num_stickybomb 5`, `player_has_jetpack 0`, `player_has_tickerbomb 0`; start movie
`pre_1_1_opening`, next level `lev_1_m1_b`, compass objective tag 50.

## 2. Controls (PC defaults)

Default table 0x00588ed8 (exe; {action, primary, secondary} triples), loaded by Controls_LoadDefaultBindings
0x004144a0 (src/input/input_01.cpp:120). Action names: jump table of 0x004155a0 (exe). Key codes: DIK scan codes;
0x10000.. mouse axes, 0x10004.. mouse buttons (docs/subsystems/qol.md).

| Action (id) | Primary | Secondary |
| --- | --- | --- |
| MoveForward/Backward/Left/Right (0-3) | W / S / A / D | - |
| TurnLeft/Right (4, 5) | mouse X | Left / Right arrow |
| LookUp/Down (6, 7) | mouse Y | Up / Down arrow |
| Fire (8) | Mouse 1 | - |
| FireSpecialWeapon (9) | Mouse 2 | - |
| ToggleZoom (0xa) | unbound | - |
| Zoom, held (0xb) | Z | E |
| Reload (0xc) | R | - |
| TurboJump (0xd): jump, and the jet pack when owned | Space | Mouse 3 |
| IncWeapon / DecWeapon (0xe, 0xf) | Q / unbound | - |
| IncSpecialWeapon / Dec (0x10, 0x11) | Tab / unbound | - |
| TeammatesSend / TeammatesReturn (0x12, 0x13) | 1 / 3 | - |
| Use (0x14) | X | F |
| FastMovement (0x1f) | Backspace | - |
| SpecialEvent (0x22) | H | - |
| Menu up/down/left/right, select, cancel | arrows, Mouse 1 / Enter, Esc / Mouse 2 | |

- No crouch action exists (35 actions 0..0x22).
- Zoom: when ToggleZoom is bound it toggles, otherwise zoom follows the held Zoom key (game_06.cpp:2819-2836).
- Mouse: delta converted to a 30 Hz rate; multiplier = option m_mouseSensitivity + 0.5 (default 0.5 -> 1.0).
  No invert option; inverting = swapping the MOUSE_UP/MOUSE_DOWN bindings (0x00414520; qol.md).
- "Mouse 1" = 0x10004 is taken to be the left button (**guess**: DirectInput button 0).

## 3. Movement

All values: p_trotter (lev_1_m1). Code defaults in parentheses where they differ (PlayerObject_Load 0x00473da0,
src/game/game_05.cpp:557-724).

| Parameter | Value | Use (evidence) |
| --- | --- | --- |
| fwd_max_speed / side_max_speed | 40 / 40 (10) | target speed at full stick (game_06.cpp:2586) |
| fwd/side_max_speed_min | 4 | target speed floor for any non-zero input: v = max*x + (1-x)*min |
| movement_power_min/max | 7 / 10 | drive power, lerp by stick magnitude; velocity time constant about 1/power s (**guess**, 0x00471810) |
| jump_impulse | 70 (10) | upward impulse mass*70 (0x0047aaf0, game_06.cpp:359-476) |
| jump_anti_gravity | 5 (0.25), stored * 0.5 = 2.5 | gravity scale (below) |
| jump_fwd/side_max_speed, jump_movement_power | 6.5 / 6.5, 4 | air control |
| fall_speed / _jump / _turbo | -50 / -70 / -200 | vz below this = falling state |
| fall_land_hard_time | 0.6 | longer falls land with trotter_land_hard |
| land_speed / _jump / _turbo | 30 / 20 / 40 | turbo landing skid threshold |
| turn_max_speed / pitch_max_speed | 0.3 / 0.95 | yaw -= axis * 0.3 * dt * 10 (3 rad/s at full axis); pitch += axis * 0.95 * dt |
| rifle_turn/pitch_max_speed | 0.05 / 0.25 | while zoomed with the rifle (0x004759e0 / 0x00475990) |
| targeting/control exponent, bias | 2 / 0 (zoomed 3 / 0) | stick shaping 0x00414c00 (pow vs fmod unresolved; **guess** pow) |
| targeting_control_drag | 1.0 | multiplies turn speed when g_game+0xa6c byte +0x3ac is set |
| anim_walk_run_switch | 0.3 | run anims above fwd_max_speed * 0.3 = 12 |
| anim_run / walk tables | run high 23, walk high 7, min 2.5, max 32 | anim rate = speed / high_speed (0x00409d90) |
| collision_radius | 2 | |

**Gravity.** World gravity is (0, 0, -64.348) (Havok cinfo; havok-layouts.md). The player update
(PlayerObject_Update 0x004788a0, game_06.cpp:3079-3093) adds an upward force mass * (64.348 - 64.348 * A) each
frame, so the net vertical acceleration is -64.348 * A. A = jump_anti_gravity * 0.5 normally (also on the
ground), turbo_anti_gravity * 0.5 under the jet pack, turbo_glide_anti_gravity * 0.5 while gliding. The 0.5 is the
constant 0x005bcb4c (exe). In lev_1_m1: A = 2.5, so -160.9 units/s^2. Derived: jump apex 70^2 / (2 * 160.9) = 15.2
units after 0.435 s. This assumes the player is not in the gravity-scale list Game+0xa84 +0xf1c (Load does not link
it there).

**Jump.** TurboJump sets flag 0x40. Flag 0x80 is set at once when |v.xy| > 20, otherwise when `trotter_jump_into`
ends. With both set the impulse is applied and "Fx_fs_roman_jump" plays.

**Jet pack (turbo).** It needs the pack object +0xabc (pickup_jumper_pack, or the mission's player_has_jetpack:
lev_3_m1 / lev_3_m2). None in lev_1_m1. Ported 2026-10-16: engine/src/jetpack.rs (its module doc has the mechanics);
the flags: 2 turbo (launched), 4 gliding, 8 glided, 0x10 the wings may open (a launch, or falling with the pack). In
0x0047aaf0 the three polls are: 0x00414730 pressed (the launch, with the grace +0x570 > 0), 0x004148f0 up (spreads the
wings while falling: a tap launch glides from the top of its arc by itself), 0x00414810 held (folds them). Oracle
measurements: work/oracle-blockers1/jet_probe, jet_probe2, tools/oracle_scenarios/jetpack_l3m1.json.

**Ground and landing** (0x00478200, physics_03.cpp:1450-1470): grounded when a contact normal has z > 0.7.
Landing damage 0x00478100 (game_05.cpp:2167): t = (-vz_min - min_speed) / max_speed. It divides by max_speed,
not max - min, which is an original quirk to keep. When t > 0, damage = lerp(min_hurt, max_hurt, min(t, 1)). The
lev_1_m1 values are 150..300 hurt with speeds 150 and 200.

**Instant death by falling:** z < -1000, or a drop from the last ground z greater than death_falling_distance
(110), or a fall time greater than death_falling_time (1.0 s).

## 4. Third-person camera

- The camera is node-driven, not offset constants. 0x004757b0 (game_05.cpp:974) gives Camera_Update 0x0040e350
  (game_01.cpp:2697) the camera name "Camera_start" and the look-at name "Camera_end". These are nodes of the
  player's `Trotter_ref` animation (exe strings 0x0059f024 / 0x0059f034).
- Rest transforms of these nodes come from lev_1_m1_big03_anim and are in player.json. Model space is X right,
  -Y forward (the player's local forward is -Y, game_06.cpp:2630), Z up.

| Node | Position | Target | FOV (horizontal, rad) |
| --- | --- | --- | --- |
| Camera_start | (0, -1.06, 7.75) | (0, -7.19, 6.88) | 1.047 (60 deg) |
| Camera_end | (0, 22.70, 11.13) | (0, -7.19, 6.88) | 1.047 |
| Camera01 | (0, 19.88, 9.99) | (0, -7.04, 5.89) | 1.047 |
| Camera_rifle | (0, 0.13, 7.92) | (0, -7.19, 6.88) | 0.436 (25 deg) |
| Camera_machinegun | (0, 0.13, 7.92) | same | 0.524 (30 deg) |
| Camera_sniper / _2 | (0, -2.05, 7.61) / (0, -2.52, 5.81) | | 0.122 (7 deg) / 0.735 |

- **Guess:** Camera_start (at head height, 7.75) is the pivot and Camera_end (22.7 behind, 11.1 up) is the eye at
  full distance. The update casts rays between them and shortens the distance on a hit. This reading follows the
  ray description below; it was not traced instruction by instruction.
- Node FOVs are horizontal: node word +0x98 = 0 for all of them. They are converted to vertical with
  2 * atan(tan(h/2) / 1.702128) (camera.md).
- Pitch: +0x74 += look axis * pitch rate (pitch_max_speed 0.95) * dt, clamped to [-1.5608, 1.15] rad. The pitch
  rotates the desired matrix around the player (0x0040e350).
- Collision: five rays from the eye, towards the look point and +-5 units along the look matrix axes, shorten the
  distance to the nearest hit. Objects in the way that are not part of the player fade out (rate Game+0xcb4, down
  to Game+0xcac) and fade back in (Game+0xcb0).
- Smoothing: 0x0040fd50 (slerp blend +0x160/+0x164).
- Shake: 0x0040e300.
- Zoom: holding Zoom enters mode +0xacc (0x00475880). The camera then switches to the weapon's node, Camera_rifle
  in lev_1_m1 (weapon table Game+0x2870, stride 0x150). The position and FOV blend over camera_zoom_secs = 0.15 s
  (global settings; code default 0.5) in 0x0040fbb0. Turn and pitch then use the rifle speeds 0.05 / 0.25.
- Letterbox: the original renders into the band y = 0.1125 .. 0.8875 (camera.md); the remaster already lifts it.

### The remaster's camera (engine/src/camera.rs, subsystem port 2026-10-14)

`CameraPlugin`: the follow camera (moved out of player.rs), data from player.json `camera` (`CameraRig`: Camera_end
eye, Camera_start target, the weapons' zoom nodes and FOVs made vertical by 0x0040a4d0's 2 atan(tan(h/2) / 1.702),
the pitch clamp). Ported, with evidence:

| Piece | Original | Remaster |
| --- | --- | --- |
| Pitch limits | +0x74 clamped to [-1.5608, 1.15] (0x005bcebc / 0x005bceb8) | `CameraRig::pitch_limits` in player.rs control (was [-0.95, 0.85] about the rest) |
| Stick pitch | 0.95 x axis x dt added twice a frame (player update game_06.cpp:2379 and Camera_Update game_01.cpp:2895): 1.9 rad/s | player.rs control (was 1.7, guess) |
| Zoom | linear over camera_zoom_secs toward the weapon node's eye and FOV (0x0040fbb0) | the rig's zoom nodes per weapon |
| Death | mode 3 (0x0040fa90 at game_06.cpp:2201): the eye stays, the view turns to the body, smoothed over 1 s (0x0040fd50) | `follow_camera` death branch (looks at the ragdoll's center bone: guess for the node) |
| After a camera path | Camera_Update's tail (+0x17c == 1): the 1 s smoothing back to the follow camera | `CameraSmooth` when campath.rs lets go |
| Shake | 0x0040fac0 (amplitude, time); 0x0040e300: the camera height + amplitude, x -0.75 a frame | `ShakeCamera` message (fxvm.rs sends it), applied after every camera owner (`CameraPlace`) and taken off the next frame (the old fxvm.rs shake subtracted last frame's offset from a camera the follow camera had already re-placed) |

Tests: engine/playtests/camera_follow.json (rest distance and height, zoom FOV and eye, back, the pitch limits with
the arm shortened by the landscape looking up), camera_death.json (shake from a blast, ends; the death camera holds
its eye and ends on the body). Replaced plumbing: docs/port/camera.csv.

## 5. Weapons in lev_1_m1

The loadout is the rifle plus 5 sticky bombs. The machine gun is removed by the mission override.

| Weapon | Numbers | Evidence |
| --- | --- | --- |
| Rifle `player_rifle` (behaviour 0x2d) | recycle 0.5 s, clip 8, max ammo 62, reload 1.0 s (clip_reload_time; section 10), range 500, two-handed, muzzle fx_muzzle_flash_hawkings | p_player_rifle, p_globalsettings |
| Rifle bullet `proj_rifle_bullet` | speed 300 * 3 = 900 units/s, lifetime 4 s, damage 50, head (bones head/neck) x3, impact_force 7370 | p_player_rifle_bullet; Projectile_Load 0x0047e0c0 multiplies speed by 3 (game_06.cpp:959); hit 0x0047eec0 |
| Sticky bomb `proj_stickybomb` | 5 carried, launch angle 10 deg, ballistic arc with g = 64.348, homes, sticks, explodes after linger_lifetime 4 s: damage 200, area 100, radius 30 / 45, impact 15000, range 250 | p_player_stickybomb; game_07.cpp:892-1270 |
| Punch (no ammo) | trotter_punch_1..3; bof_* global factors | p_globalsettings |

- Bullets are projectiles, not hitscan. Each frame the segment a bullet moved is ray-cast (0x0047e580).
- Fire gate (0x004a1e40): a weapon can fire when it never fired or when time - last shot > recycle_time. Each shot
  takes one round from the clip (0x004a2450).
- Auto-aim (0x004a2a40): off when zoomed or on difficulty > 1, otherwise it follows option byte Game+0x4c2. The
  autoaim_* factors (0.04 / 0.12, base 0.065 / 0.075) scale the planes of the target-picking frustum (0x00408390).
- Weapon sounds: wpn_rome_rifle_fire, wpn_rome_rifle_load, wpn_rifle_dry_fire_1/2.

## 6. Health, damage, death

- max_health 400 * difficulty player-health factor (Game+0x554; table 0x00589188 (exe): easy 2, normal 1, hard 1,
  index 3 0.5).
- Hit handler 0x0047afa0 (ai_07.cpp:3842): health -= damage, halved when Game+0xac8 is set. The hit side goes to
  Game+0x26d0 for the ui_hit_* markers; the damage flash lasts 0.1 s. Knockback: direction * impulse * 10, capped
  at 4000.
- Low-health cue below 20 %, every 1 s (game_06.cpp:2144).
- Health pickups: pickup_health_1/2, pickup_healthytea. Amount = pickup +0x49c * Game+0x55c (0x00472800), clamped
  to max health.
- Death: ragdoll and fx_trotter_death, a 5 s timer, then level state 3 = mission failed (0x0044a580,
  game_06.cpp:2154-2168). No checkpoint or respawn was found; the failure screen restarts the mission (**guess**:
  pui_missionfailed).

## 7. Companions in lev_1_m1

| | Q (`q111`, p_q) | Jonesy (`molmer`, p_molmer) |
| --- | --- | --- |
| Spawn | instance 206 (-197.48, 1276.84, 181.13) | instance 207 (-201.66, 1170.04, 127.82) |
| Health | 500 | 500 |
| Formation | angle 20, distance 25, delay 50 | angle 20, distance 50, delay 50 |
| Weapon | q_chaingun: 30-40 rounds in 10-15 bursts, range 100-200 | molmer_pistol (24 rounds in 4 bursts), molmer_dynamite |
| Player anchor bone | ref_q_00 (-11.07, 10.88, 0) | ref_molmer_00 (11.73, 10.68, 0) |

- Following: the formation point is the anchor plus the reversed heading, rotated by +-formation_angle and scaled
  by formation_distance (0x00483c30). A companion does not move while the player is within delay_distance
  (0x00483810). Paths use the nav mesh (0x00483d60). Set-up: 0x00483250 (ai_08.cpp:192).
- Orders: key 1 = attack the object under the crosshair, or defend that point; key 3 = return to the player
  (0x0047a9a0 -> 0x004852b0). Details, the order lines and the HUD texts: companions.md.
- Score: both have score_value -500, so killing a companion costs points.

## 8. Animations

- The animation table at 0x005cb650 (exe) has 7 entries per mode. Mode 0 is aim / fire_sg / fire_rr / reload /
  wchange / throw / scan.
  - 1-5: idle_1..5.
  - 6 / 7: fall_into / fall_loop.
  - 8: land_hard.
  - 9-12: run forward, backward, left, right.
  - 13-16: walk.
  - 17 / 18: jump_into / jump_idle.
  - 19 / 20: jump_land / land_soft.
  - 21-27: turbo.
  - 28 / 29: turn.
  - 30 / 31: hit_small / hit_med.
  - 32-35: topsy.
  - 36-39: windup / punch.
  - 40: blackhole_pull.
- `_b` / `_t` pairs are the lower and upper body (**guess** from the names): legs follow the movement, the torso
  follows the aim.
- 146 trotter anims exist in lev_1_m1. The full list is in player.json.
- Footsteps: fol_rome_walk_1..4 and fol_rome_run_1..4.

## 9. Open questions

- Whether 0x00414c00 is pow or fmod (CRT 0x00566790 is labelled differently in different files).
- What Game+0xd58 is: it scales the move and turn input by (1 - 0.25 * value).
- Where friction +0x5a0 is consumed. The twist limits: normal_twist_limit (+0x560, x 0x005bcba0 to radians) becomes
  the look-at record's limit +0x24 (actor+0x6b0) at each mode change in 0x00475ff0, running_twist_limit (+0x564)
  for kind-4 (walk / run) entries with an aim source; read by the bone callback 0x004735f0 on "center" / "belly"
  (table 0x005fb760). turn_power_min/max (+0x9f0 / +0x9f4): no reader found in src/ (grep of the player code);
  the same offsets are the Character's corpse timer / fade (corpses.cpp, 0x00427100 writes 0.5 at death), so
  **guess**: read at load and never used for turning.
- The exact eye/pivot roles of Camera_start and Camera_end, and whether the camera nodes are animated in other
  anims (trotter_idle_1 carries Camera01/Camera02 tracks).
- p_trotter keys that the exe never reads (no such string in the exe): sniper2_turn/pitch_max_speed and
  zoom_control_bias. The code reads "zoom_control_control_bias" instead (game_05.cpp:583).
- Mouse 1 = left button is assumed.

## Steep slopes (found 2026-10-14)

No character proxy and no max-slope parameter. The player's contact callback 0x00477c90 (reached through the thunk
0x004780f0 from 0x00478200) keeps the contact normal with the largest z as the ground normal (+0x574) and sets each
contact's friction: 0 when normal z <= 0.7 (0x005bcd48, about 45.6 degrees) or the touched object passes
fn_004531e0, else +0x5a0 (blend of friction_min 0.3 / friction_max 2 by pow(1 - min(|v.xy| / maxspeed, 1), 4),
set in 0x00478200). 0x00478200 counts him grounded (+0x570 = 0.5 s timer, +0xa34 |= 0x1000) when a contact's normal
z > 0.7, or when ((vz - prev vz) / dt)^2 < 200 (0x005bcda8) and vz > -5 (0x005bce74), contacts present and normal z
> -0.1 (0x005bcd14). PlayerObject_Update (0x004799d5) releases the drive force action when the timer has run out
while touching something, so on a steep face he slides under gravity with no drive. The drive 0x00471810 weakens an
uphill push by about cos^5 of its angle. The remaster: Jolt walker max slope acos(0.7) (engine/src/player.rs).

## Landing and falling (traced 2026-10-14, static: 0x00478100 / 0x00478200 not executed in recorded runs)

Flags +0xa34: 0x1000 grounded this frame, 0x200 falling, 0x400 fall_into, 0x100 landing, 0x80 / 0x40 jump, 0x20 jump
just fired, 2 turbo, 4 jetpack flight.
- Falling (physics_03.cpp:1602-1622, vz <= 0): vz below fall_speed (-50; jumped -70; turbo / pack -200; never in
  jetpack flight) sets 0x600 and adds dt to +0xa60 (time falling fast); also a foot ray hit below -4 (anim_01.cpp:3029).
  +0x56c keeps the most negative vz; the ground reference z (+0x520) is the position up to 0.5 s after the last contact.
- Death by falling (1633-1655), 0x00478100 with flag 1 (damage = current health, record flag 0x400000 past
  invulnerability): z < -1000, or dropped more than death_falling_distance (110; turbo 600) below the ground reference,
  or +0xa60 > death_falling_time (1.0 s; turbo 6), or a hit by fxo_rocket_turret (bit 31). No scream / camera code.
- Landing (1557-1575): grounded with 0x200 (or 0x80 without 0x40), not 0x20, not already 0x100: Fx_fs_roman_land,
  cue 0x00493b70, landing damage from +0x56c (t = (-vz - landing_damage_min_speed) / landing_damage_max_speed, none
  when t <= 0, clamped to 1, damage = lerp(min_hurt, max_hurt, t): Roman 150..300 from 150 units/s), +0x550 =
  horizontal speed, 0x100 set, landing bob timer 0.25 s.
- Landing anims (game_05.cpp:1855-1878), holding 0x100 until they end: land_hard (mode 8) when +0xa60 >
  fall_land_hard_time (0.6), else jump_land (0x13); in the air fall_into then fall_loop. Moving (> 0.01 a frame with a
  move speed) cancels the landing anim (1721-1726).
- Drive power while landing (game_06.cpp:2506-2547): from landing_power (3 / 2; jump 3 / 2; turbo 2 / 4) back to
  movement_power (7 / 10) over the anim; 0 in the fall state without 0x100 (no air control while falling).
- Landing bob (anim_01.cpp:3035-3049): sin(pi (t / 0.25) + pi), up to 1 unit, only running faster than land_speed (30).
- Grounded grace +0x570 = 0.5 s (1531-1543): coyote time for jumps (game_06.cpp:366, 449), the ground reference.

## 10. The remaster port (subsystem port, 2026-10-14)

(2026-10-17: Roman's weapons are a `weapons::Arsenal` now, with every gun and special of the game: docs/subsystems/
weapons.md. The rifle / machine gun / sticky / topsy rows below still hold.)

Engine files: engine/src/player.rs (movement, jump, falls, camera, weapons, projectiles, death, idle banter),
sticky.rs, topsy.rs, charanim.rs (Roman), turret.rs (the player's side, unchanged). Every ported function is cited by
address in the source; `python tools/port_queue.py game` lists what is left, docs/port/game.csv the functions replaced
or not needed, each with its reason.

What the original does and the remaster now does (static, from src/game/game_05..09.cpp):

| Mechanic | Original | Evidence |
| --- | --- | --- |
| Ground drive and friction (2026-10-15, oracle) | measured every frame (tools/oracle_scenarios/stop.json): full stick from rest moves 36.6 units/s in the first frame and 39.2 after (0.653 a frame, not 40); released, he stops within a frame. The remaster: the drive's exponential pull times DRIVE_GAIN 15, then the ground contacts' friction +0x5a0 (friction_max 2 at rest to friction_min 0.3 at full speed by (1 - v / max)^4) x gravity 160.9 against the horizontal velocity (it sets the 39.2); the slide / stop fx read the speed before the drive (+0x4bc). The force action pool's own integration (0x00455a30 -> 0x00403fb0) is not decoded: the gain is fitted | 0x00478200 (game_05.cpp:573, physics_03.cpp:1455), oracle traces |
| Standing jump | TurboJump sets 0x40; 0x80 (the impulse) only when trotter_jump_into ends (0.133 s), at once when the horizontal speed is above 20 | 0x0047aaf0, game_06.cpp:449-476 |
| Jump / stop / slide fx | Fx_fs_roman_jump at take-off, FX_fs_roman_stop when the horizontal speed drops by more than 20 in a frame (grounded), FX_roman_slide (looping) while touching the ground faster than 10 with no stick input; all with the surface variant | 0x00478800, game_06.cpp:2679-2708 |
| Reload | R (vt+0x15c can reload: spare > 0, clip below size) or the empty clip once +0xc00 (0.5 s after the last shot) has run out; the clip refills when the weapon's timer clip_reload_time (1.0 s) runs out (0x004a2220) or at reload anim frame 43, whichever first: **1.0 s** (the engine refilled at 1.43 s before) | 0x004a2360, 0x004a2220, 0x004a22e0 / 0x004a29d0, game_05.cpp:1601-1628, game_06.cpp:2874-3059 |
| Fire gate | not reloading, clip not empty, never fired or time - last > recycle_time | 0x004a23b0, 0x004a1e40 |
| Weapon switch | Q selects the next usable slot (clip + spare != 0) as pending (0.5 s timer); it completes at the wchange anim's frame 13 (0.4 s); while pending nothing else of the weapon code runs (no fire, reload, special); an empty weapon that cannot reload falls back to the other | 0x004a0bb0 / 0x004a0be0, 0x004a0c10 / 0x004a0c70, 0x004a1690, 0x004a16b0 |
| Specials | blocked while reloading; Topsy Turvy not in a jump (+0xa34 & 0xc0) and with +0x580 clear; the special in hand used up switches to the other only when that one has any left | game_06.cpp:2884-2970 |
| Machine-gun scatter | the target point moves by up to s per axis, s = min(Game+0xd68 x machinegun_spread_pct, pct) x distance x 0.01, Game+0xd68 = seconds Fire held with the machine gun (0 when released): the first shot is exact, full spread after 1 s | 0x0047f4e0, game_06.cpp:2975-3011 |
| Shots fired (Game+0x2718) | each shot, each special use, and the sticky bomb's release once more (a sticky bomb counts two: docs/original-bugs.md) | game_06.cpp:2972, 2999; game_05.cpp:1592 |
| Sticky bomb damage | health 1: a blast (area record) lights a fuse rand() x 2.136e-5 (0..0.7 s), a direct hit (a bullet) sets it off now; bombs are in the bullets' and the blasts' lists | 0x00481b90, 0x00481320, 0x00481520 |
| Projectile lifetime | the property's (4 s; the engine capped it at 3) | 0x0047e4a0 |
| Arrows (grunt bows) | flight time T = 3 sqrt(\|d\|) / (speed x 3), velocity d / T with T x 32.17 added upward: an arc landing on the aim point; a fly-by sound within 70 of Roman | 0x00482510, 0x00482620 |
| Topsy Turvy bomb in the hand | topsy_create (0.467 s into trotter_topsy_into) creates player_topsyturvy in the hand and hides the weapons | 0x0047bbe0, 0x004a0e00 |
| Idle banter | Q or Jonesy still for 30 s, or Roman idle for 30 s: one line of table 0x005ce350 (55 lines) | 0x0047b730 |

Tests (tools/regress.py SUITE): engine/playtests/player_move (speed 40, running jump at once, standing jump after the
wind-up, apex 14.7 at 0.42 s), player_fall (no damage from 60 units, about 170 from 100, death from 400),
player_weapons (machine gun rate and held time, R reload 1.0 s, switch blocks fire, rifle empty-clip reload at 0.5 +
1.0 s), player_special (sticky bomb release, two shots each, a blast sets off a second bomb before its own linger
ends), player_topsy (refused in a jump, the bomb in the hand, weapons back after), player_banter (a line after 30 s
idle); unit tests in player.rs (reload time, scatter, landing damage, blast fuse, arrow arc).

Compared with the original: the jump apex (14.7 against the jumpA capture's 14.6 at 0.42 s), the running speed (40),
the reload's load-sound timing (reloadO: 0.8 s after the last shot, unchanged). Not compared (no capture): the 1.0 s
refill, the switch timing, the machine-gun scatter ramp, the sticky chain, the stop / slide fx, the idle banter; these
follow the code only.

Open questions:
- +0x580 (the Topsy Turvy gate): written by the contact callback 0x00477c90 with the touched body's entity; whether
  the landscape gives 0 is not settled, so the remaster does not test it.
- The idle banter's filter callback 0x0047b520 is not recovered (not in symbols/functions.csv): the remaster plays a
  line only when its speaker is there and leaves the base-defence lines out (guess).
- 0x0047f4e0 sits in a PickupObject vtable by the .rdata scan; that it is the machine-gun bullet's target-point slot is
  inferred from what it reads (machinegun_spread_pct, Game+0xd68).
- The sticky bomb's collision radius (Object_BuildRadiusCollision from its model) is not read: 1.5 (guess).
- When the weapons reappear after the Topsy Turvy effect is not traced (guess: when it ends).
- Left in the queue: the pickups of game_05 (0x004725e0..0x00472aa0, 0x00475a30, 0x00475c70: the pickup agent's,
  pickup.rs), the ticker bombs (PlayerTickerBomb 0x0044de90..0x0044e5e0: mission.rs sites, the mission agent's), the
  penguin restart and the timer objects of game_07 (0x0048f8e0, 0x00496160..0x00496c60: not player code).

## Arrows (2026-10-18, agent-presentation, engine/src/arrows.rs)

The archers' arrows (ArrowProjectile, vtable 0x005a9a40, game_07.cpp:1698-1846) fly with their model (the archer's
projectile template grunt_normal_arrow: grunt_arrow_1, a separate entity following the projectile) and, on a hit
(0x00482780: moved to the hit point, the trail released, stuck to the character hit by 0x0047e9f0, marked stuck), stay
where they hit for linger_lifetime (0x00482460: default 10, p_grunt_normal_arrow 2.5), attached to the character they
hit; an arrow that ends otherwise takes its model with it (0x004824e0). player.rs `Projectile::linger` and the
`ArrowStuck` message; test arrow_models.
