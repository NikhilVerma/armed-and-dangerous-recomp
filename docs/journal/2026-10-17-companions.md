# 2026-10-17: companions and the Q112 (agent-companions)

Branch `agent-companions` (from wip/full-decomp 4509be5), target dir engine/target-wt-companions. The features of
docs/port/features.csv owned here, each from the recovered C++ (src/ai/ai_02..08.cpp, audio_01.cpp, game_02 / 06.cpp)
and, where no source was recovered (0x0044da10, 0x0047b520), from the disassembly.

## Done

| Feature | Mechanics (addresses) | Engine | Playtest |
| --- | --- | --- | --- |
| companion-leader-swap | 0x00483810: an area / attack order's points compared by distance even on the move; the partner always given the opposite flag | ai.rs leader_pick | companion_leader_swap (Roman turned 180 degrees swaps the leader, and back) |
| companion-engage-exit | mode 3 of 0x00485910: 0x004265f0 home at once with the fire counters left, 0x004265c0 after a spent clip: the partner's target or the +0xb68 linger (rolled at the fight's start, on arrival, by an attack order); Slot122 | ai.rs `FightEnd` | companion_engage_exit |
| companion-carried | flags 0x20 / 0x800 of 0x00430340 gated by Slot112 / Slot149; modes 5 / 10: the hang anims, up after +0x87c < -1 with no live holder | ai/carried.rs (the Topsy bomb knocks companions in its radius) | companion_carried |
| player-grab | not the player's: the companions' close kill 0x00427e80 + GruntObject_Slot66 / BowGruntObject_Slot66 + states 0xc..0xf + 0x0042c670 (mover_q_kill_grunt_* / mover_molmer_kill_grunt_* with mover_grunt_qdie_* / mdie_*) | ai/close_kill.rs | companion_close_kill |
| companion-idle-cue | the last-shot stamps Game+0xafc / +0xb0c (0x004878f0, 0x0044f160) restart the banter's 30 s | ai.rs `Companion::last_shot`, player.rs idle_banter | companion_idle_cue |
| level-end-dialogue | not a level-end conversation: the speaker tests 0x0040d250 / 0x0044da10 / 0x0047b520 with 0x00493b70's bag | ai/dialogue.rs, ai.rs dismay, player.rs idle_banter | dialogue_dismay |
| q112-extras | swing 0x00428160 (state 0xb, 100 within 35, at a burst's end within 25), trail +0x958, eye loop 0x00435170 / 0x00435220, voice slots | ai/q112.rs, ai.rs think hooks | q112_extras (lev_2_m2) |
| q112-boss | dead in the shipped game (only the test movers' q112_marker note makes one) | - | - (docs/port/ai.csv) |
| T1 (Jonesy hurt while planting) | IC anims switch physics off (0x0042c410 -> 0x004577b0(0)): bullets pass | `Companion::ic`, player.rs | tower_l4m2 (health 500 in the bomb loop) |

Re-tagged: GruntObject_Slot147 is the armed jaws' target test (feature armed-jaws); 0x0042f190 / 0x0042f1d0 /
0x0042f2f0 (states 0x19a / 0x19b) are the player's punching glove's (feature knockout-bomb, special 7).

## Learned

- Two feature names were wrong: "player-grab" is the companions' close kill (vt+0x1e4 is set for Q111Object and
  MolmerObject only, vt+0x104 for the grunt and archer); "level-end-dialogue" is the banter's speaker test.
- The 0x20 damage flag is the Topsy Turvy bomb's (Game+0xc7c is topsy_turvy_duration): teammates hang (anim 0x71 /
  0x72) instead of the grunts' fall roll.
- The Q112's melee numbers in infantry.md were swapped: +0x5fc 25 is the start range, +0x600 35 the hit's reach; the
  damage is 100 (0x0043eae0 for an AI named "q112").
- nav.rs gives no route for a goal within its own start cell's reach (`best == s`): a short straight walk needs the
  line-of-sight test first.

## Debt and guesses

- The Q112 swings only at a burst's end (faithful), but its fire rate is still the engine's one round per fire anim
  (infantry.md debt): a burst of 30..40 rounds takes ~50 s, so the swing comes late (q112_extras waits 55 s).
- The trail is unit-tested only: with a 150..700 fire range the Q112 rarely chases.
- Close kill: the facing at the fight point (toward the grunt), the last 12 units driven through the grunt's sphere,
  the 10 s give-up (engine guard).
- Carried: the companion stands still before the hang (the original runs his generic state for up to 1.5 s).
- The pull (0x800) has no sender yet (twiglet-extras).

## Tests

See the end of this file (fast regress, level completions, level_smoke).
