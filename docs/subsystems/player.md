# Player character (Roman) in lev_1_m1

Status 2026-10-13: static research for the remaster port (engine/). Nothing here was checked at runtime. The
numbers the port needs are in `work/remaster/lev_1_m1/player.json` (not committed; generated from the retail banks).
Per-instance property overrides of the level are in `work/remaster/lev_1_m1/instance_props.json`.
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

**Jet pack (turbo).** It needs the pack object +0xabc (pickup_jumper_pack). There is none in lev_1_m1
(player_has_jetpack 0). Numbers are in player.json for later levels.

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

## 5. Weapons in lev_1_m1

The loadout is the rifle plus 5 sticky bombs. The machine gun is removed by the mission override.

| Weapon | Numbers | Evidence |
| --- | --- | --- |
| Rifle `player_rifle` (behaviour 0x2d) | recycle 0.5 s, clip 8, max ammo 62, reload 1.0 s, range 500, two-handed, muzzle fx_muzzle_flash_hawkings | p_player_rifle, p_globalsettings |
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
  (0x0047a9a0 -> 0x004852b0).
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
- Where friction +0x5a0 is consumed, and who uses turn_power_min/max and the twist limits.
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
