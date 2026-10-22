# Companions: team orders and barks (Q = Q111Object, Jonesy = MolmerObject)

Traced 2026-10-14 from the recovered code (src/ai/ai_08.cpp, src/audio/audio_01.cpp, src/audio/audio_02.cpp,
src/game/game_06.cpp, src/ui/ui_02.cpp) and constants read from game.unpacked.exe. None of the order code runs in the
recorded sessions ("not executed in recorded runs"), so everything below is static. Movement, targeting, the fire cycle,
hits and death are in player.md 7 and engine/src/ai.rs (Companion). Port: engine/src/ai.rs `team_orders`.

## 1. Order state (+0xbcc flags)

| Flags | Meaning | HUD text (layout string) | Set by |
|---|---|---|---|
| 1 \| 8 | "Defend Me": formation on Roman; flag 8 copies Roman's position / direction into the anchor +0xb70 / +0xb98 every frame (MolmerObject_Slot47 prologue 0x00485910) | `ui_teammate_defend_player` "Defend Me" | spawn (0x004836b0: `|= 9`), return from any other state |
| none of 1 / 4 / 8 | "Return To Me": loose follow | `ui_teammate_return` "Return To Me" | return pressed while 1 \| 8 |
| 1 | "Defend Area": anchored at a point | `ui_teammate_defend_loc` "Defend Area" | send, no enemy under the crosshair |
| 1 \| 4 | "Attack": anchored at the target, which is held in +0xbb0 | `ui_teammate_attack` "Attack" | send with an enemy under the crosshair |

Flag 0x200 marks Q (Q111Object_Slot6 0x00486eb0). The pair is linked through +0xb64 (0x00486ba0).

What the flags change (evidence):

- Formation point (0x00483d60): flag 1 without 8 uses 0x00483c30 on the anchor: the reversed direction -(+0xb98)
  turned about the up axis by formation_angle (+0xbc0, 20) times +-1 degree in radians (0x005bcba0 = +0.01745,
  0x005bcd98 = -0.01745; the sign is the leader flag 0x10), normalised, times formation_distance (+0xbbc), plus the
  anchor. Otherwise Roman's `ref_q_00` / `ref_molmer_00` bone. So an area / attack order puts both behind the point
  as seen from Roman, Q 25 and Jonesy 50 units from it.
- Regroup (mode 1, 0x0048631c): with flag 1 the companion's 2D distance to the anchor is compared with
  formation_distance + 2.5 (K 0x005bcda4); without it, Roman's distance with delay_distance (+0xbb8, 50). With 8 the
  anchor is Roman, so Defend Me is the tight formation and Return To Me the loose one.
- Targets: Q111Object_Slot96 0x004853a0 refuses a target farther (2D) than scan_distance (+0x5dc, key string
  0x005b7060 "scan_distance") from the anchor when flag 1 is set, except the held target; Slot145 0x004852e0 the
  same for flags 1 or 4 without 8. Without flag 1 mode 1 runs the generic GenericAI_Update 0x0043ee90.
- Attack linger: mode 1 with flag 4 and the partner not fighting (0x0042c6f0 of the partner is 0; **guess**: "has a
  target") counts +0xb68 down; at zero the pair returns silently (0x004845b0(0): no line, no HUD text) to Defend Me.
  +0xb68 = rand * 1/8192 + 3 (K 0x005bce94, 0x005bcb7c) = 3..7 s, rolled by the order (0x00484100) and again when a
  target is found in mode 1 (0x00486200).

## 2. Input path

TeammatesSend (action 0x12, key 1) and TeammatesReturn (0x13, key 3), player.md 2. PlayerObject 0x0047a9a0 drops
dead companions (+0xad0 Jonesy, +0xad4 Q), then while Roman may act (0x0047a950: alive, game mode not 2, no
+0xa34 & 0x7f800000 state, not 0x00410150) polls each action (0x00414730, 0.25) and calls 0x004852b0 on both
companions: 0x12 sets +0xbcc flag 0x2000, 0x13 flag 0x4000.

The order handler 0x00485120 (from the per-frame update 0x00485910) does nothing while flags 0x400 / 0x8000 are set,
while dead, or while it or its partner is in mode 5 (carried) or 10 (waiting for an animation, e.g. Q's tea); the
flags then stay pending. Otherwise the first of the pair to update clears both partners' flags (0x004852d0), adds
1.0 (K 0x005bcb2c) to +0xb94 and runs one order for the pair: 0x4000 (return) wins over 0x2000. +0xb94 runs down by
the frame time and clears flag 0x40 below zero (meaning of 0x40 unknown, not ported).

## 3. Orders

**Return** (0x004845b0(1)): 0x00484290 on self and partner: drops the held target +0xbb0, rewrites the flags
(1 \| 8 -> none; anything else -> 1 \| 8 \| 2; flag 4 cleared), re-picks the leader (0x00483810) and the formation
point, drops the current target +0x588 and goes to mode 2 (moving, +0xbac 3..7 s, +0xb68 0.25). The one case that
keeps its place: 1 \| 8 -> none in mode 1 (idle). Then, when both succeeded, the line and the HUD text (below).

**Send** (0x00485120): the object under the crosshair (0x00408020 of the aim Game+0xa6c). When it is in the
companion's enemy list +0x604 (a copy of World list +0xe7c, 0x00483250) or of object type 0x51 / 0x53 (+0x418;
which objects these are is not traced): **Attack** (0x00484830 -> 0x00484100 on both): anchor = the target's
position, direction = Roman's (0x00453150), flags 1 \| 4, target held in +0xbb0 (offered targets are ignored while it
lives, Slot109 0x00485360) and engaged (vt+0x23c; mode 3), +0xb68 = 3..7 s. Else **Defend Area** (0x00484ca0):
anchor = the aim point (0x00408010 + 4), direction = Roman's, flag 1 (0x004844c0: drops the held target and the
route), leader re-pick forced; when Game+0xd34 is set a marker effect is created at the point (Fx_Create 0x0041cc60,
not ported: what sets +0xd34 is not traced).

## 4. Order lines and HUD text

All order lines share vo_delay Game+0xb44: skipped unless `now - Game+0xb44 >= Game+0xb50`, then Game+0xb44 = now
and Game+0xb50 = a new interval in [Game+0xb48, +0xb4c] = vo_delay_min / max, 30 / 45 s (game_02.cpp:2057), set at
level load. The same timer gates the grunts' Spotted / Fight cues (grunt-ai.md, Barks) and the companions'
Damaged / FriendlyFire lines, so an order in the first 30..45 s of a level, or soon after a grunt's cue, is silent.
"r(N)" is `(rand() * N) >> 15`; Roman's lines play without a speaker (audio +0xd0), the others on the speaker (+0xc4).

| Order | Pair alive | Alone |
|---|---|---|
| Return | r(1000) < 800 generic; < 900 `jon25002_B_Return` (Jonesy); else `q1111007_B_Return` (Q) | r(500) < 400 generic, else his own `_B_Return` |
| Attack | r(400) < 200 generic attack; < 300 Jonesy's set; else Q's set | r(1000) < 700 generic, else his own set |
| Defend Area | as Attack with the generic defend lines | as Attack |

- Generic return: r(400) by 100s: `rom50030_Return`, `rom50031_Return`, `rom50057_Return`, `rom50058_Return`.
- Generic attack (0x00484830's helper): r(600), or r(299) when Q (+0xad4) is dead: `rom50027_Attack`,
  `rom50032_Attack`, `rom50034_Attack`, `ROM50117_Attack`, `ROM50118_Attack`, `ROM50119_Attack`. The last three are
  not in lev_1_m1's sound bank (work/remaster/lev_1_m1/audio/sounds.json), so with Q alive half of these picks are
  silent there.
- Generic defend: r(400): `rom50028_Defend`, `rom50029_Defend`, `rom50114_Defend`, `rom50116_Defend`.
- Jonesy's set (same for attack and defend): r(400): `jon50025_Send`, `jon50131_Attack`, `JON50033_Defend`,
  `JON50130_Defend` (strings 0x0059e03c..0x0059e00c).
- Q's set: r(400) < 100 `q1150039_Send`, < 200 `Q1150116_Defend`, else `Q1150118_Defend` (0x0059dffc..0x0059dfdc).
- `q1150117_defend` is never named by the code (audio.md 5 lists it as untriggered).

HUD: after the line, 0x00463890 finds the text by name and sets +0x474 flag 4; its driver 0x00467890 shows it for
3.0 s (0x40400000) and on its first frame hides the other three order texts. All four sit at ref_team_text
(320, 47.5 of 640 x 480), scale 0.9, `#ffff80` (hud.md 4). The return's text is "Defend Me" when the new state has
flag 8, else "Return To Me". The attack / defend texts are set before the line's delay check; the return's after it,
but both always show.

## 5. Barks (voice slots)

The voice methods sit at the same vtable offsets as the grunts' (GruntObject 0x005a9cf0): Q111Object 0x005a6df8 and
MolmerObject 0x005ae5f8 hold, from +0x138 (dump of .rdata at 0x005a6f30 / 0x005ae730):

| Slot | Q | Jonesy | When (grunt-ai.md Barks) |
|---|---|---|---|
| +0x13c Fight cue | 0x004875d0 `Q1150038_Fight` / `Q1150040_Fight` (r(200)) | 0x0044eab0 `JON50026_Fight`, `JON50028_Taunt`, `JON50029_Taunt` (r(300)) | the fire cycle 0x00428980 starts the fire anim with a target (ai_02.cpp:1860); vo_delay |
| +0x140 | 0x004874c0 `Q1150045/46/47/148/149_Fight`, r >= 500 `Q1154007_FightSpec` (range 600 with Jonesy alive, else 499) | 0x0044ea50 `JON50057/58/59_Fight` | **no caller found**: a scan of .text for `call [reg+0x140]` / `mov reg, [reg+0x140]` finds only weapon, cover, turret and Havok uses (the grunts' +0x140, 0x004391d0, is likewise uncalled). Not ported |
| +0x144 Spotted | nop (0x00407ff0) | nop | |
| +0x148 HurtShort | 0x00487150 | 0x0044ebf0 | ported (companion_damage) |
| +0x14c Hurt / Help | 0x004872e0 | 0x0044ed50 | ported, now on vo_delay (was a 4..8 s guess) |
| +0x150 FriendlyFire | 0x004871e0 | 0x0044ec60 | ported, vo_delay |
| +0x154 Dead | 0x00487050 | 0x0044eb80 | ported |
| +0x158..+0x164 (Taunt, Scream, Sticky) | nop | nop | Jonesy's taunts only come through the Fight cue |

Other lines: Q's healthy-tea lines (0x004857e0, ported); the kill-meter dismay table (0x00493b70, engine fn dismay);
sound ears with `q_sound` / `jonsey_sound` (mission.rs). The leader re-pick (0x00483810) and the partner's target
hand-over (Q111Object_Slot139 0x00485490) speak no line. The `Q1150092/93_Sniper` and `ROM50103/104_Sniper` lines need
a damage with flag 0x20000 (sniper; none in lev_1_m1).

## 6. Port notes and open points

- The order is handled once per press for the pair, by the first live companion in query order ("self" for the
  alone / 0x200 choices). Pending orders wait while Q's tea anim plays.
- The attack target is the live grunt whose body the crosshair ray (from the camera, as the aim point) hits first.
  Object types 0x51 / 0x53 are not ported.
- Formation side under an area / attack order: the side of the companion's own bone (**guess** for the leader flag
  0x10 that 0x00483810 sets).
- Support fire (Roman's Fire held) is kept for Defend Me / Return To Me only; 0x00485580's own flag test ("flags
  0x27, or flag 8 without a target") is not fully decoded.
- Test (headless, --play): key 3 toggles Return To Me / Defend Me with the HUD text; key 1 on a spawned grunt gives
  "Attack", the pair kills it and returns to Defend Me 3..7 s later; key 1 on open ground gives Defend Area with Q
  ~25 and Jonesy ~50 units short of the aimed point; a return at 48.6 s played `rom50031_Return`; with --no-ai
  (no grunt cues) Jonesy's Fight cue played `jon50028_taunt` at the start of a burst.

## 7. The leader pick and the regroup (ported 2026-10-14, engine/src/ai.rs `leader_pick`)

Flag 0x10 of +0xbcc marks the leader (Q starts with it: Q111Object_Slot6 0x00486eb0 sets 0x10 | 0x200). The leader
takes Roman's ref_q_00 and the other ref_molmer_00 (0x00483d60); under an area / attack order the leader's formation
point is the anchor's reversed direction turned by +formation_angle, the other's by - (0x00483c30; the rotation's
sense is a guess). The pick 0x00483810 runs when a regroup starts (mode 1 -> 2, forced when Roman faces more than 90
degrees from his direction at the last pick), every 0.25 s on the move and on arrival (both unless the partner idles),
and forced by Defend Area (0x00484ca0): a partner's newer pick is copied; else the one of the pair nearer the anchor
(Roman, or the order's point) is `ebp`, nothing changes while the anchor is within delay_distance of it (unless
forced), and `ebp` leads when it is nearer the leader's point (idle) or, moving, when the other point is the less
aligned with Roman's direction (`|cos b| < |cos a|`); the partner takes the opposite flag. A regroup asks the idle
partner to regroup too (flag 0x80, 0x004837b0). Moving far behind Roman the two run nearly parallel and the flag can
change several times a second (leader1's log), as the compare implies. The playtest log has `q_lead` / `jonesy_lead`.

Corrected 2026-10-17 against the recovered 0x00483810: under an area / attack order (flag 1 without 8) the two points
(0x00483c30, sides 1 and 0) are compared by squared distance whether moving or not (the cosine test is only for
Roman's bones in mode 2); and the partner always takes the opposite flag at the end (0x00483be0 -> 0x004837e0), picked
or not. Playtest companion_leader_swap: Roman turned 180 degrees in place swaps the leader, and back (the pick is
geometry, no random).

Not ported (docs/port/ai.csv): Q's cape cloth, defend_min_distance's engage test.

## 8. The tower task (ported 2026-10-16, engine/src/ai/tower.rs)

Used once in the shipped data: lev_4_m2's Jonesy (molmer 147: receive_trigger_activate 7001, tower_marker_tag 30,
obj_tag -600, send_trigger_on_death 5001; no other Q / Jonesy of the 24 levels has a trigger or a marker tag). The
mission's peasant_success_trigger (7001, the tenth peasant safe) sends him to the Glass Tower; "Get Jonesy (ALIVE) to the
Glass Tower / Guard Jonesy while he blows Tower doors".

| Step | Original | Remaster |
|---|---|---|
| Set-up | 0x00483250: the trigger kept only with a marker tag; 0x004836b0 does not put him on his formation point while +0x52c is set: he starts where the level placed him, then follows Roman as usual | ai.rs `starts_in_formation`; ai/tower.rs `TowerTask::from_props` |
| Trigger | Q111Object_Slot14 0x00483680: +0x52c used up -> 0x004834f0: the head of marker spline 30 (Marker_FindSpline 0x0044c030: marker 356 at (1601, -31, 14)), a route (0x0046f6d0); mode 6, flags 0x8000 / 0x40000, unpaired (0x00486ba0), held target dropped | `receive`: nav.rs route that reaches within 10 of the marker, `OnTask` (ai.rs companions / team orders skip him, so Q is a pair of one) |
| No way | "Molmer_Tower_Failure" (ui text molmer_tower_failure "Jonesy: Get me back to the tower so I can blow it up") as a paused message, 0x00484290 (the return toggle), flag 0x10000: again after +0xbc8 = 15 s | the same (`Mission::message`, `Companion::order_return`, `retry`) |
| Mode 6 | along the route; past its end beyond 10 (100 squared, K 0x005bcb60) a new one, else straight on (0x0046f5b0); within 1 the arrival; speeds the generic 0x004297a0 (move_speed: 39.7 units/s measured) | `walk`: move_speed (`Companion::run_speed`); a stuck walker (under 3 units in 1 s) marks the spot and routes round it (nav.rs can lead over a step the walker cannot take: lev_4_m2 (905, -143)) |
| Arrival | the marker's matrix (0x00457990), physics stopped, health = max (+0x454 = +0x458: oracle, 405 -> 500 on the arrival frame), Marker_ProcessArrival 0x0044ba30 (802, sender 147), the door object (obj_tag -600: ww_glass_tower 110) out of World+0xe9c / +0xe90 / +0xe94 / +0xea0, "mover_molmer_plant_bomb" via 0x0042c410 (state 0x64), mode 7 | the same, the door's collider in physics.no_bullets (the ray / sight lists not modelled) |
| Mode 7 | the IC anim carries him by its own root track (29 units to the doors, then between the bombs), notes: make_bomb (a player_tickerbomb x 0.25, K 0x005bcbe4, taken off the ticker lists +0xeb0 / +0xeb4), plant_bomb Ref_bomb_0N (onto the tower at that bone), fx jones_bomb_plant; at its end (31.3 s) "mover_molmer_bomb_loop" looping (ic_loop), its skeleton root held (0x0042c080), fx fx_jones_bomb_adjust | mover.rs `Mover` (as ai/ic.rs) and its notes; the bomb in "ref_lhand" (guess: the note names no bone; ref_rhand holds the pistol) |
| Hits | none react during the IC anims (0x00430340 skips them while 0x00426630) or on the move (Slot129 0x00486e90: modes 1 / 3 only); attackers not taken as targets (Slot94 0x00486da0); mounted-gun bullets do 0.3 of their damage, forge mortars 0.1 (stig_ / forge_damage_multiplier, record flags 0x80000 / 0x100000) | ai.rs companion_damage, `Companion::damage_scale`, player.rs `DamageKind` |
| Death | the AI death 0x00431720 sends +0x528: 5001, failure 2, "Jonesy just died! Without him you'll never get the book!" (the oracle's level state 9) | companion_damage sends `death_trigger` |

After the arrival the mission does the rest (mission.rs): 802 forces the alarm (15 troops), kills the tower guards
placed for it, shows l4m2_sub2 and starts timer 350 (displayed 120 s countdown; Jonesy's / Roman's lines 823..826 at
3 / 40 / 80 / 110 s); 4000 at 120 s: pre-success, bb_ear 404's fx_exp_glass_tower (the doors), timer 405: 123456 (win)
5 s later. Voice: the tower task itself says nothing (no voice call in 0x004834f0 or modes 6 / 7); the lines are the
sound ears' and the plant fx's (wpn_jones_bomb_attach / _tweak).

Evidence: oracle lev_4_m2_tower (tools/oracle_scenarios; the hook's [test] play_triggers sends 7001 at play frame 60:
the hook trace carries each companion's mode +0xb5c since 2026-10-16). Original, Roman standing at the start: Jonesy
sets off on frame 60, walks 2450 units in 3700 frames (hit by the chasing troops: 500 -> 405), arrives at frame 3800
(mode 7, 802 from 147, health 500), plants, and with nobody guarding him dies at frame 7440 (5001: failure); Q and
Roman fight off the start's troops (Roman 400 -> 157). Playtests tower_l4m2 / tower_l4m2_fail (regress).

## 9. The end of a fight (mode 3; ported 2026-10-17, engine/src/ai.rs `FightEnd`)

Mode 3 runs the generic update (0x0043ee90) and watches its state (MolmerObject_Slot47 0x00486908):

- +0xb68 = 3..7 s (rand * 1/8192 + 3) is rolled when a fight begins (mode 1 -> 3 at 0x00486200, mode 2 -> 3 at
  0x004866da), by an attack order (0x00484100) and on arriving in formation (mode 2 -> 1, 0x00486884): one timer for
  the fight's linger and the attack order's idle time.
- A target lost with nothing new in sight goes through 0x0042bc60. With the fire counters left (+0x484 flag 4) the
  home route (state 0x50): 0x004265f0 is true and mode 3 ends at once, back to the formation point (mode 2). With the
  clip spent the reload in place (state 0x32), then idle: 0x004265c0 (state 1, or 0x79 / 0x7a without a target) is
  true and, when the partner holds a live target other than himself, he takes it (vt+0x23c); else +0xb68 counts down
  and at 0 he goes back (mode 2, 0x00483d60(1, 0, 0)).
- A live target let go of: home (in an area / attack formation by Q111Object_Slot122 0x00486c90 = 0; in Defend Me /
  Return To Me the pursuit Slot122 allows ends in the same anchor refusal 0x004853a0).
- Engine: `Companion::fight_end` (Home / Linger) decided by `FightEnd::on_loss` (reloading or no rounds = the spent
  clip); mode 2 is the regroup (`moving`, the goal the formation point). Log: `q_fight` / `jonesy_fight` ("target",
  "linger", "none"), `*_fight_end` and `*_fight_ends` (how the last ended, how many). Playtest companion_engage_exit.

## 9b. Q's cape (2026-10-18, agent-presentation, engine/src/cloth.rs)

Q111Object's setup 0x00486f40 hangs the template q111_cape (a ClothObject, vtable 0x005aca40: Slot6 0x00487910, Slot7
0x00487990, Slot55 0x00487960) on him (0x00487a40, kept at +0xbd0: 0x004876b0) with the anchor table 0x005fb94c
(0x004879c0): the cape's chest, l_shoulder and r_shoulder held to his bones. The remaster poses the cape model's skeleton
each frame: the anchors on Q's bones, the chains cape_l_2..3, cape_m_1..3, cape_r_2..3 as verlet particles (damping
0.9, gravity 32, pulled 0.12 a frame to the rest shape on the anchors, kept at their bone lengths), each bone turned to
its child. The Havok cloth's parameters are not recovered; the constants are guesses by eye. Test cloth_cape.

## 10. The close kill (ported 2026-10-17, engine/src/ai/close_kill.rs)

Reached in the original on lev_1_m3 (coverage: Q111Object_Slot78, GruntObject_Slot66, 0x0042c670 ran). Q or Jonesy
grabs the grunt he fights and kills him by hand:

- The try 0x00427e80 (from the engage 0x00427910, a burst's end of the fire cycle 0x00428980 and the attack order
  0x00484100): the companion's vt+0x1e4 (only Q111Object / MolmerObject), the target's vt+0x104 (GruntObject /
  BowGruntObject only), not already in one, a coin ((rand * 100) >> 15 >= 50), nearer than 50 (3D), the two headings
  135 degrees or more apart (K 0x005bcea8 / 0x005bceac), the grunt's ref_fight_00 within 0.5 of his feet's height and
  a route to it.
- The grunt's answer (GruntObject_Slot66 0x00434ea0 / BowGruntObject_Slot66 0x004346a0): refused dead, held, on a
  gun or a trench's slot, flying, in an IC move; else state 0xf (`Mind::Held`): standing, his aim set.
- States 0xc..0xe: the companion runs to ref_fight_00 (route, then straight within 1, turning to the grunt).
- 0x0042c670 on both: the IC anims by the companion's vt+0x138 (Q 0x81 / 0x82: mover_q_kill_grunt_1 / _2; Jonesy
  0x7f / 0x80: mover_molmer_kill_grunt_1 / _2) and the same ids of the grunt's table (mover_grunt_qdie_1 / _2,
  mover_grunt_mdie_1 / _2); physics off for both; the grunt's "kill" note kills him (he stays in the anim's last pose).
- Engine: a `Grab` message from ai.rs (`close_kill::attempt`), `Killing` on the companion (the formation update skips
  him), `Victim` on the grunt; a stun or the companion's death lets the grunt go (`orphans`). At the point he turns to
  ref_fight_00's negated y axis; after the anim his reload (state 0x32); the grunt's `kill` note is ai/notes.rs's and his
  pose is kept by ai.rs `DeathAnim`. (Merged 2026-10-18 with agent-enemyai's ai/melee.rs takedown, which built the same
  0x00427e80: one implementation, here.) Engine guards: the last 12 units are driven straight through the held grunt's
  sphere; an approach that has not arrived in 10 s gives up. Playtest
  companion_close_kill (the debug step `close_kill` skips the coin, reach and facing; the answer and the route decide).

## 11. Stunned and carried (modes 5 / 10; ported 2026-10-17, engine/src/ai/carried.rs)

- Flag 0x20 of the damage message: the Topsy Turvy bomb's area damage (0x004a1aa0 -> 0x0047f020, flag 0x20, radius
  topsy_turvy_radius). Gate vt+0x1c0 = Q111Object_Slot112 0x00486c70 (refused on the tower task). A teammate:
  +0x878 = 0.001..0.5 s, +0x87c = topsy_turvy_duration (Game+0xc7c, 5 s). In an IC anim or a close kill (state 0xc)
  0x0042fe50 at once.
- Flag 0x800: a pull by the holder +0x30 (Slot149 0x00486c80, the same gate). A new holder replaces an old one and
  nothing else; a teammate gets +0x878 = 0.5..1.5 s and is not moved. No sender in the remaster yet (the twiglets'
  attach is unreached in the shipped game; feature twiglet-extras): `Knock::Pull` is the API.
- Mode 5: +0x87c counts down (twice a frame while +0x878 does); +0x878's end plays anim 0x71 (state 0xeb), then 0x72
  looped: Q q111_hanging, Jonesy molmer_hang_into / molmer_hanging. +0x87c at -1 or below with no live holder: Jonesy
  plays 0x71 once more (mode 10), Q (the player's 0x00475d40) is up at once; then the stand set and the mode he left,
  his target lost. Orders wait meanwhile.
- Playtest companion_carried: Roman's Topsy bomb hangs both companions and they are up ~6 s later; a pull holds Jonesy
  until his holder dies.

## 12. The banter's speaker tests and the last-shot stamps (ported 2026-10-17, engine/src/ai/dialogue.rs)

- Audio_PlayRandomSound 0x00493b70: a shuffle bag over the table (the entry drawn last swapped back, up to three
  random swaps among the unused, the front drawn; a `used` count per table) and a speaker test per draw, repeated
  until one passes (`LineBag`). The test says who speaks: Roman without a position; a companion at himself, the line
  kept as his (+0xb50, `Companion::speech`); an idle "_Spec" line without a position.
- 0x0044da10 (the kill meter's dismay, table 0x005ce570): 'R' Roman; 'J' Jonesy there and not talking; else Q the
  same. 0x0040d250 (a peasant tossed out of a building, table 0x005fb6d0; its caller is feature peasants): the same,
  and JON50172_Dismay / Q1150162_Dismay only while the level's list +0xf04 is not empty.
- 0x0047b520 (the idle banter, table 0x005ce350; disassembled, no recovered source): a base-defence level takes only
  the _BaseDefend lines; 'r' needs Roman idle 30 s (rom50171_ also Q and Jonesy there); 'q' Q still 30 s (_Spec lines
  also Roman alive and Jonesy there, without a position; world 5 drops q1150025 / q1150035_Ambient); 'j' Jonesy still
  30 s (_Spec and jon50182_Ambient also Roman alive and Q there).
- The idle banter's companion clocks (Game+0xafc Q / +0xb0c Jonesy) restart when he moves (0x0047b730) or fires
  (Q111Object_Slot101 0x004878f0 / 0x0044f160 before each shot's aim error): `Companion::last_shot`.
- Playtests dialogue_dismay (both companions dead: Roman says the dismay), companion_idle_cue (the companions' support
  fire holds the banter back past Roman's 30 s idle).

## 13. Physics off in IC anims (tuning item T1, 2026-10-17)

The IC anims (GenericAI_PlayICAnim 0x0042c410, 0x0042c670) switch the character's physics off (0x004577b0(0)), so
projectiles pass him: the original's Jonesy takes no damage during the 31 s plant on lev_4_m2. `Companion::ic` (set by
ai/tower.rs at the marker and by the close kill) makes player.rs's bullets skip him; hits react not. Playtest
tower_l4m2 checks his health at 500 in the bomb loop.

## 14. Idle fidgets in formation (ported 2026-10-22, engine/src/charanim.rs `CompanionIdle`)

Mode 1 of MolmerObject_Slot47 0x00485910 (ai_08.cpp:1817-1850): the timer +0xbac (random(3, 7) on arriving) counts
down; at its end, when the playing anim ends (the idle set's loop wraps), one of idle_1..3 (ids 0x1e..0x20) plays
once and +0xbcc 0x1000 is set; from then every anim end plays one of ids 0x1b..0x1d looped (Q: q111_kneel_scan 8.2 s,
q111_idle_long_1 21 s, _2 25 s; Jonesy: molmer_idle_long_1 / _2 by a second rand in 0x0044ef20). A move, a fight, a
hit reaction start it over. scan_run (mode 2, id 0xa) is due only when the run's loop outlasts the timer that each
wrap redraws, so it does not show; scan_stand (0x00427330) needs the attention anim 0xf, shown only before the first
formation idle. Playtest companion_fidget.
