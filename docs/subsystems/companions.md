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
