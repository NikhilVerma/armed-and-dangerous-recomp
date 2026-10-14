# Level 1 (lev_1_m1) mechanics checklist

The player's direction (2026-10-14): every mechanic of the level 1:1 with the original **or better**, before going
further; checked against the oracle (the original under the hook: logs, dumps, captures), not by the player's
playthrough. Built from four inventories (player, AI, mission scripting, world objects) of the decompiled code
(`src/`), the data export (`work/remaster/lev_1_m1/`) and the engine (`engine/src/`).

Status: **V** verified against the original (evidence in the journal) · **P** ported, not yet compared · **S**
stand-in / guess · **M** missing · **B** better than the original (a remaster improvement, gameplay unchanged).
Priority: 1 = the player meets it in the first minutes, 2 = changes how the level plays, 3 = detail.
Update this table as items land (commit hash in the last column).

## Player

| Mechanic | Original | Status | Pri | Notes / done |
|---|---|---|---|---|
| Walk / run / strafe, drive | 0x004788a0, 0x00471810 | V | | stick shaping 0x00414c00 and the min speeds not ported |
| Walk back factor 0.7 | (none found) | S | 3 | trace the back speed |
| Jump, air control | 0x0047aaf0 | V / S | 3 | jump_into stage not modelled; air control fitted |
| Slopes | 0x00477c90, 0x00478200 | V | | friction blend not ported |
| Landing anims, landing damage, death by falling | 0x00478100, 0x00478200 (player.md) | P | 2 | fall_loop, land_hard / jump_land (move cancels), damage 150..300 from 150 u/s, death past 110 units or 1 s falling, no drive while falling, land fx by material (fall.json) |
| Mouse look rates | 0x00475990.. | V | | pitch clamp narrower than the original (±1.56 / 1.15) |
| Look from trackpad / Moonlight | — | B | 1 | cursor + raw + absolute raw (98e7700); unconfirmed |
| Controller support | 0x004f8600 joystick | B | 1 | input.rs on bevy_enhanced_input: modern twin-stick layout, contexts, rumble, focus-gated pads (remaster-input.md) |
| Follow camera, collision, fade | 0x0040e350, 0x0040e56c | V | | smoothing slerp 0x0040fd50 not ported |
| Zoom | 0x0040fbb0 | P | 3 | |
| Intro path, briefing camera hold | 0x0040ff50, 0x0044daf0 | V | | Roman off-screen report: not reproduced (session log next) |
| HUD layout | UI bank | V | | B: letterbox band mapped to the window, edge-anchored (this change) |
| Prompts name the binding | "Press %s" 0x00465280 | B | 1 | Kenney icons inline (keys, mouse, Xbox, DualSense) for the device in use; Controls page too |
| Hit-direction markers ui_hit_* | 0x0047afa0 side mask, 0x004670f0 | P | 1 | hit_flash set from the shot direction, damage_flash_duration |
| Pain sounds | table 0x0064a300, rom50069..73_Hurt | P | 1 | at most 1/s; heavy breathing below 20 % |
| Low-health pulse + sound cue | 0x00466e70, game_06.cpp:2155 | P | 1 | alpha pulse up 8/s, down 2/s |
| Crosshair states (_in/_out, friendly) | 0x00406f50, 0x00406840 (hud.md 11) | P | 2 | aim ray over list +0xe9c, on-target classes, friendly crosshair, _in / _out anims, special crosshair; shots aim at its point (crossG / crossF) |
| Gun auto-aim | 0x004a2a40 (option, default off) | M | 3 | off by default in the original; B: optional |
| Rifle: fire gate, recycle, clip | 0x004a1e40, 0x004a23e0 | P | | taps fire (227163b) |
| Shots from the gun to the crosshair point | 0x0047dbf0 (P = ref_shoot_00, Q = hud+0x5c) | V | | 227163b (spawn offset measured) |
| Reload (anim-driven refill) | game_05.cpp:1601, tables 0x0059eef4 / 0x0059eedc / 0x005fb9dc | V | 2 | original (reloadO capture): load sound 48 frames (0.8 s) after the 8th shot, engine 0.80 s; load sound at frame 10 (rifle) / 15 (MG), refill at frame 43 (1.43 s); reload.json |
| Weapon switch (wchange anim, frame 13) | 0x004a16b0, 0x004a17d0 | P | 2 | trotter_*_wchange_t, swap at frame 13 (0.4 s), chat "ROME: I've switched to the %s", ui_weapon_put_away; special switch line too (wchange.json) |
| Machine gun (pub) | p_player_machinegun | P | | spread distribution guessed |
| Rifle fire anim | game_05.cpp:1380: fire_sg for rifle / MG | P | 1 | trotter_fire_sg / jump_fire_sg |
| Aim-pose blend | 0x00475ff0: w = (clamp(2·pitch,-1,1)+1)/2 | P | 1 | traced formula |
| Sticky: target pick | 0x00406f50 sticky pass | V | | a161975 |
| Sticky: release at throw-anim frame 9 (~0.3 s), ammo at release | game_05.cpp:1579, game_09.cpp:1813 | P | 1 | release at the anim's fire note (0.267 s), ammo at release |
| Sticky: flight, homing, stick, blast | 0x004813b0.. | V | | |
| Ticker bombs (pub award, hold Use, fuse) | 0x0044e3a0 / 0x0044e4a0 | P | | count 4 has no source |
| Topsy Turvy | game_05.cpp:1790 | P | 2 | the view follows the anims' Camera_topsy track in Roman's frame (back = world_turn reversed: guess); hits during topsy not ignored yet (topsy.json) |
| Hit reaction, knockback | 0x0047afa0 | V | | |
| Death presentation (ragdoll, fx_trotter_death, death lines) | vt+0xd4, player.md 6 | P | 2 | ragdoll from his pose (ragdoll.rs; Roman's head end from the neck), fx_trotter_death, death line, failed page after 5 s (dead.json) |
| Mounted gun stig_mgun 32 | game_08.cpp turret | M | 2 | |
| Teammate orders (keys 1 / 3) | 0x004852b0, 0x00485120 (companions.md) | P | 2 | Defend Me / Return To Me / Defend Area / Attack, ui_teammate_* text, order lines under vo_delay (team1..5) |
| Idle fidgets, turn-in-place, land anims | 0x00476a10 | M | 3 | |
| Grunt drops (type rule) | cleanup_01.cpp:96, 0x00475a90 | P | 3 | the original's weighted pick (health 0.5 / 0.25, current weapon 0.5 not the rifle, other weapon 0.25, sticky 0.25); MG ammo pickups collected |
| Pickup generators 219 / 220 | 0x00471b70 | M | 3 | |

## AI

| Mechanic | Original | Status | Pri | Notes / done |
|---|---|---|---|---|
| Guards (attention / ready / window) | 0x0042d0f0 | P / V | | fire cadence, dry cycle, turns verified |
| Target search, sight, companions as targets | 0x00426920 | V | | |
| Fire cycle, aim error, muzzle | 0x00428980, 0x004284e0, 0x004a2010 | V | | second encounter 40 vs 42 shots, 24 vs 22 hits |
| Retaliation when hit (adopt attacker) | 0x00430340 end, 0x0042c820 | P | 1 | offer-target rule (10 s, nearer); playtest retal.json |
| Alert group on death / hit | 0x00431720, 0x0042cc70, 0x0042c700 | P | 2 | spotting and death hand the killer / target to the group after 0.25..1 s (group.json) |
| Chase with nav routing, wait, go home | 0x00427910, ai_06.cpp:1420, 0x0046f6d0 | P / S | 2 | moving grunts route around obstacles (nav.rs A*, replanned every 1 s) when the straight line is blocked; wait / go home not ported |
| Marker paths, hide, cover (FindHidingSpot) | 0x0044ba30, 0x0042a7b0 | P / M | 2 | trigger_marker runs to the marker (straight line), actions hide (fallback wander 60..100), trigger_charge, scatter; no cover slots, splines, nav routing; barracks troops not on markers yet |
| Alarm chain (grunt 67 → marker 151 → siren → generators, captains) | 0x00404e50, 0x0044ba30 (mission.md) | P | 2 | alarm.rs: runner, siren fx + Help, controller 2..4 s up to 10, siren destroyed stops it (alarm.json / alarm2.json) |
| Barracks release (ref_exit, markers, release_max) | ai_01.cpp:900 | S | 2 | spawns ±12; keeps releasing after destruction |
| Hit reactions (explosion variants) | 0x00430340 | P | 3 | |
| Death variants (blown up, ledge, in back, window) | 0x004319e0, 0x00427100 | P / M | 2 | random grunt_die_1..6 (0x50 + rand*6), blown up / ragdoll by hit_ragdoll; not yet: the 0x56 / 0x57 flag variants, ledge_death, the launch velocity of record flag 2 |
| Barks: Taunt, General_Scream, Help, shared delays | grunt-ai.md "Barks" | P | 2 | voice sets fixed (GR1 / GR3 / GR2), death / vo / taunt delays, Spotted, Fight, Taunt, Hurt two-stage, Dead vs DeadAtPosition, Sticky only on the bomb's grunt; Help (alarm) waits for the alarm chain |
| Fidget shiver on level 1 | Game+0x2708 == 1 | P | 3 | fidget 0x12 is grunt_idle_shiver on world 1 |
| Companions: formation, targeting, fire, TNT | 0x00485910.. | V | | |
| Companions: hit reactions, death, friendly fire | 0x00430340, 0x004319e0, 0x00486df0 | P | 2 | friendly fire: no damage, FriendlyFire line; hits: Damaged / Hurt lines, hit_mgun / small / med anims; death: Dead line, *_die, -500 accidents, body removed; they take Roman's attacker |
| Companions: barks, orders, leader swap, partner target | audio_02.cpp:195.., companions.md | P | 2 | Fight cue (Q fight, Jonesy fight / taunt) under vo_delay; the other slots have no caller or are empty |
| **Kill-streak "dismay" banter** (the remarks after blasts) | MissionObject_Update game_02.cpp:2476, table 0x005ce570, kill meter Game+0xbb8 ≥ 3 | P | 1 | meter +1 per grunt death, drain 0.5/s, 30-45 s cooldown |
| Sound-ear lines: alive / in range, on the speaker | 0x00497900 | S | 2 | 2D, random |
| Penguins: physics, hits, death, screams, score, sticky | 0x0048f690.., 0x0048fa20 | P / M | 2 | any hit kills (1 health, no filter), pngn_blown_up (scream note) -> land_dead, gone after 5 s, -10 accidents; not yet: launch physics, sticky salute-and-run (penguin.json) |

## Mission scripting

| Mechanic | Original | Status | Pri | Notes / done |
|---|---|---|---|---|
| Edge details (texts, compass, fx ears) | mission.json `to.detail` | P | 1 | read from the wrong place: sub2 / sub3 briefings, compass after the pub, fx ears (this change) |
| Start trigger on the first frame | 0x0044db80 | S | 3 | sent after the path |
| Triggers 808 / 810 / 98 / 99 / 713 / 711 (volumes, voice) | TriggerObject, SoundEar | V | | |
| send_trigger_on_death (pipe 96 → 711, boom_island → 301 penguin debris) | 0x00450ee0 | P | 1 | fx::Died → SendTrigger |
| Gates blown (556 / 901) | 0x0044e150 | V | | |
| Win sequence (score, sounds stop, stats, next level lev_1_m1_b) | 0x0044a390 / 0x0044a230 / 0x00494440 (win-sequence.md) | P | 1 | statscreen.rs: counters (stats.rs), the Stats screen with its phases and poster, money kept; lev_1_m1_b not built: back to the title. Sounds not stopped yet |
| Objective jingle | 0x0044f560 | M | 3 | |
| Opening movie + 71 subtitles | pre_1_1_opening | M | 3 | |
| Pub re-entry, barkeep lines, amb_pub_busy | 0x00468dd0, 0x00467fe0 | P | 3 | amb_pub_busy loop while inside, the intro line once then one of 15 general lines; extras / once-weapon lines not set up |
| Music states | 0x0044f580 | S | 3 | |
| Score | various | P | 3 | stats.rs: kills / accidents by score_value, loot (win 1000, tokens 2500) |

## World objects

| Mechanic | Original | Status | Pri | Notes / done |
|---|---|---|---|---|
| Powder kegs, pipes: one bullet kills, chains | FxExplosion 0x0041c180.. | P | | |
| Damage filters: takes_bullet_damage, player / teammate / badguy_damage, takes_damage | 0x00450da0, 0x0040ce40, 0x0041c4e0 (docs/subsystems/damage.md) | P | 1 | fx.rs `Destructible::accepts`: buildings ignore bullets and grunts, kegs / pipes ignore grunts and companions |
| Bullet-transparent objects (invisible walls, arrow) | list +0xe90, filter 0x00413a90 | P | 1 | physics.no_bullets (trees do stop bullets); a trigger-killed object's collider is removed |
| Houses: breakable windows, door anim | 0x0040ad80 | M | 3 | |
| Barracks stop releasing when destroyed | | P | 2 | mission.rs deaths(); barracks.json (destroyed 25 releases nothing on 810) |
| Oil pump animation (oilpump_cycle) | | M | 3 | needs the export: oilpump_1.json has no skeleton / bone binding (3 submeshes); oilpump_cycle (4 s) animates bone_bigarm / bone_pump / bone_gyro |
| Fx opcodes used by L1 deaths and muzzle flashes | render_r2b.cpp:270-1117 (fx-vm.md) | P | 2 | 0x79, 0x10, 0x14, 0x31, 0x32, 0x3f, 0x4a, 0x55, 0x56 ported (ccd9aec, 6f474db); impulse masses guessed; lights stored, not drawn |
| Trenches (barricade collision, cover) | p_barracade | M | 3 | |
| Guard alarm, grunt generators | | P | 2 | (alarm chain); the siren can be shot (100 health: guess) |
| Decals on objects | 0x00416f50 | M | 3 | |
| Shadows | projected silhouettes | B? | | cascaded shadow maps (different technique) |
| Ambient sounds, snow, sky, fog, lights | | V | | |
