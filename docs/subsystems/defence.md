# The base-defence levels (lev_1_m4, lev_2_m4, lev_3_m4_bd, lev_4_m5, lev_5_m5)

Static recovery and engine port, 2026-10-15 (branch agent-defence). No run of the original on these levels yet: every
function below is "not executed in recorded runs"; the engine's behaviour follows the recovered code and the level
data. Engine modules: `army.rs`, `defence.rs`, `icobject.rs`, `goliath.rs`, `shrubpatrol.rs`, `mover.rs`, `shell.rs`.

## What the levels are

Roman starts in the base turret on the wall (the mission's `start_in_turret_tag`) and cannot leave it. Army timers
send platoons of grunts marching across the valley along mover paths; at the path's `release_platoon` note each
platoon turns its members into real grunts, who run to marker splines below the wall or carry ladders to it and
climb over. Bombers (shrub_bomber, Roman's allies) fly over and drop kegs, health and turret upgrades to shoot; on the
later levels a goliath walks to the gate and opens it (the army breach), and shrub patrols parachute in to help.
Counters in the level data decide the outcome: trigger 27 (each fake member killed, each released grunt's
`send_trigger_on_death`) counts towards `receive_trigger_pre_success` (456: the army retreats, a timer sends the win
123), trigger 5000 (a grunt over the wall or through the breach) towards the turret's `overrun` (665) and the failure
(666). `grunt_wall_counter_tag` names the 5000 counter the HUD shows (ui_grunt_over_wall_count).

## Army (army.rs)

| What | Original | Engine |
| --- | --- | --- |
| Army timer | ArmyTimerObject vtable 0x005a2800: ReadProps 0x004968b0 (TimerObject 0x00496160 + `create_platoon_N` "time/markers/ladders", `grunt_limits_N` "time/max/unpause"), update 0x00496da0 (Timer_Advance 0x00496690 on both lists), Fire 0x00496c60 | `ArmyTimer`, `timers` |
| Animated timer | TimerObject `anim_name` (+0x4aa): the anim's notes "create_platoon NAME A B" (TimerObject_Command 0x004964d0); ends with the anim (0x00496750) | `ArmyTimer::anim` (lev_2_m4 .. lev_5_m5) |
| Marker / ladder sets | GlobalSettings render_r6.cpp:1001-1024: army_marker{1..3}, army_ladder{1..3}, army_max_grunts 16, army_unpause_count 14; platoon set-up 0x004331f0 | `Army::marker_sets`, `ladder_sets` |
| Platoon | FakeGruntObject 0x005a4da8 (init 0x00432cb0, update 0x00446460, event 0x0043ce30, damage 0x00433000); members at ref_{grunt,regular,captain,torch}_0N of the platoon model (0x00432ef0) | `Platoon`, members drawn as the kind's grunt model running, one baked crowd mesh each (crowd.rs; the original: one pool-2 particle sprite each) |
| Grunt limit | live army grunts (World+0xe84, 0x0043ce10) >= Game+0xbf0: halt, +0xbf0 = unpause; below: go on, +0xbf0 = max | `platoons` |
| Release | "release_platoon": one member every 0.1..0.2 s while under the limit; a ladder (0x00429900, one per army_ladder_delay) or the marker set (0x0042d1b0); the last removes the platoon | `SpawnGrunt::army` (level.rs) |
| Kills | a damage record within kill_range (p_platoon 50) of a member: fx FX_platoon_<kind>_die, trigger 0x1b | `fake_hits` (blasts, the player's bullets through a member) |
| Ladders | ic_plant_ladder / ic_climb_ladder markers (Marker_ProcessArrival 0x0044ba30 -> vt+0x248 0x0043ab40): mover_grunt_ladder_plant, mover_grunt_ladder_climb ("trigger 5000", "deactivate") | `army_grunts` (an anim overlay on slot 4; the grunt held with `Grunt::fixed`) |
| Breach | 0x00429a50: while Game+0xcbe (army_breach_open_trigger) and fewer than army_breach_run_max ran, an AI runs army_breach_marker_tag's spline; the last marker sends 5000 and deactivates | `breach` |
| Pre-success | MissionObject_OnTrigger 0x0044d600: Game+0xbf0 = +0xbf8 = 0, every AI retreats (vt+0x234) | `triggers` (Flee away from Roman) |
| Difficulty | none | `ArmyTuning { wave_scale, limit_scale }` (1.0 = the original) |

## Base turret (defence.rs)

BaseDefendTurret vtable 0x005a9f58 / 0x005a9bb0: load 0x0049a6a0, mount 0x0049bcc0, update 0x0049b030, fire ready
0x0049be70, fire 0x0049bf20, overrun 0x0049bc90, damage 0x0049aeb0. The turret rides the wall's track (the wall's
reference anim base_defend_wall_ref, node ref_base_turret, frames 0..500 at 30 fps, start 250; the node's rotation
dropped): world = wall * T(node) * Rz(yaw) * Rx(pitch). Strafe moves it (side_accel 1000, side_stop_accel 500,
side_max_speed frames / s), the mouse aims it (turn / pitch_max_speed, zoom_* when zoomed; yaw +-180, pitch +-90),
Fire is the machine gun (base_turret_mgun_bullet from ref_shoot_00 / ref_shoot_01 alternately, 0.05 s), Special the
mortar (base_mortar; base_homing during the 10 s ammo upgrade; 4 s / 1 s recycle; lobbed at the crosshair point:
shell.rs), Aim toggles the zoom camera. The intro anim (intro_anim_name) flies the camera in (Camera_normal track),
its notes send triggers 90..94 (the flak on lev_5_m5). The glass window (40) takes hits first (fx::DamageShield);
then Roman takes them, halved while mounted. Overrun (the turret's "overrun" trigger, 0x0049bc90): every 45..75 s a
grunt_regular jumps onto the turret (0x0042d8f0: grunt_turret_jump_on / _1 alternately, AI state 0x67: attached,
off every AI list, removed when the anim ends; `TurretBoarder`).

The failure mob (0x0049ad60, from MissionObject_OnTrigger 0x0044d600): a failure trigger while Roman rides the base
turret (Game+0xac8 = 1, `Mission::base_turret_mounted`) does not fail at once: six grunt_regulars board the turret
(0x0042d780: grunt_turret_attack, _1 .. _4, then _5 by a running count; their note "trigger 150" starts the fade
fx_l5_fadout), Game+0xdcd is set (`Mission::failure_mob`: the success trigger is ignored meanwhile) and
failure_mode_duration (5 s, +0x830) later the turret is removed (vt+0xdc 0x0049ac60): level state 3. Playtest
defence_l1m4_fail.

The fake launchers (0x0049a6a0): fake_base_mortar (shown) and fake_base_homing (hidden) at the turret's origin, and the
trolley base_turret_trolley on the track without the aim (`launchers`). The ammo upgrade swaps them (0x0049b030:
base_homing_reload); each shell replays the shown launcher's reload anim (0x0049bf20); after the upgrade the first
shell with the homing launcher still shown is a homing one, and the mortar comes back. The special recycle time
follows the shown launcher. Playtest defence_launchers.

## Scripted objects (icobject.rs, mover.rs, goliath.rs, shrubpatrol.rs)

- Movers (mover.rs): an object follows its mover anim's root track (the track named as the anim; a character's IC anim
  carries a root track too) from where it started; notes are the script (GenericAIObject_HandleEvent 0x0043d180:
  fx, fxlockedbone, trigger, anim_loop, anim_full, visible / invisible, deactivate).
- ICObject (object_ic, load 0x00436820): bombers, flak, the lev_3 general; hidden when "invisible"; the script starts at
  receive_trigger_activate. Drops: powderkeg_bomber (a keg to shoot), shootable_health (+on_death_player_health),
  shootable_turret_upgrade (the turret's ammo upgrade), shootable_token.
- AnimPlayer (0x0040a700 / 0x0040a7d0): the gates; held at the start, forward on activate, backward on bkwd. The gate
  model has no skeleton: its part rides bone b_gate, moved by that bone's track.
- Goliath (GoliathObject 0x00447040, 0x0043cef0): script to the gate (trigger 8), then the fireball cycle at Roman;
  dies as an object (3000, husk goliath_d, trigger 9 closes the gate and the breach).
- Shrub patrol (ShrubPatrol 0x00436630 / 0x004367c0 / 0x00446440): Roman's allies; parachute script, "ready_guard",
  then bursts at the nearest grunt in range (their enemy list is the AI list World+0xe7c).

## Guesses, debt and open questions

- The platoon's hit test (a bullet within 5 of a member's chest counts as the record at that point): the original's
  collision box (FakeGruntObject_Slot7) is not ported. Members are skinned grunt models (main LOD: grunt_5 lost the GPU
  device at its first draw, unexplained); about 450 at once on lev_1_m4: measure against the 4K 100 fps floor.
- ai.rs stands at ic_* markers; army.rs takes the arrival over before the AI update by position (within 8 of the
  marker). Plan: an ai.rs message for ic_* arrivals (Marker_ProcessArrival's vt+0x248), then army.rs reacts to it.
- The wall assault (2026-10-17, agent-enemyai; engine/src/ai/assault.rs, ai/ladder.rs): Game+0xc54 is the mission's
  army_max_scale (string 0x005b1de8, MissionObject_Load game_02.cpp:2025), 40 on all five defence levels (the note
  here that no level sets it was wrong). An idle army grunt takes a planted ladder (0x00429ce0) or a wall-scale spline
  (0x00429ac0: tags 300..317, single markers with ic_scale_base_wall) within scan_distance while fewer than 40 are on
  their way (World+0xf14), and goes over (army.rs's ic_* scripts, their "deactivate": ai/notes.rs, the death trigger
  sent, no body). The carried ladder (0x004298a0 / 0x0042d650, anim 0x31 grunt_ladder_run) and the planted one
  (mover_ladder_plant from the grunt's matrix + (0.458, 24.015, 12.894)) are drawn. Test: ladders_l1m4 (in 110 s: 8
  ladders carried, 7 planted, 41 scales, over the wall by both). The pre-success retreat is ai/retreat.rs (0x0042db00).
- Not ported: the turret running over grunts on the wall (every third frame, 0x0049b030), the bullseye decal, the base
  mgun's lock-on to target-list candidates (min / max_non_locking): it fires at the crosshair point. The turret's
  removal at the failure mob's end does not despawn the turret (the failure page follows at once).
- The goliath in a base defence (goliath.rs `hurt`, 0x00434180): a hit above hit_level_2 (25) on its scripted walk while
  the breach is shut plays goliath_hit_med (blend 0.25) and GOL50005_Hurt and holds its walk until the anim ends; the
  turret's machine gun keeps stalling it. Playtest goliath_hurt.
- grunt_generator_base (lev_2_m4 .. lev_5_m5): loaded as troop generators (mission.rs), but with release_count 0
  (the reader's default) the recovered release rule (0x0040cb10: batch 0 and live >= release_count -> wait) never
  releases. Open: confirm on the original whether these generators release troops.
- The goliath's fireball flight time (1.5 s) and its range (scan_distance 350) are guesses; the stand time is 1..3 s
  (0x0046e2c0(1, 3)).
- The drops fall at their "gravity" 0.5 (the body's gravity factor) with the speed trap's linear cast (movable.rs);
  token_or_health always drops health.
- Needs captures of the original (the human's OK for screen time): the intro camera, the army's look and pace, the
  turret's aim rates, a full lev_1_m4 to win and to failure.
