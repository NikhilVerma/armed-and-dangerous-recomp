# Twiglets

Little wooden creatures of the forest levels that come out of the dense trees, pull a bomb off their back and run at
the player to blow it up at his feet. Recovered code: src/ai/ai_08.cpp (controller, generator, Twiglet_Slot6),
src/ai/ai_09.cpp (Twiglet_Update and the rest of the class), src/ai/ai_06.cpp:2906-2972 (the monk's summon),
src/game/game_07.cpp:2070 (the monk twiglet cluster's hit). Remaster: engine/src/twiglet.rs (TwigletPlugin),
engine/src/ai/monk.rs (the summon).

## 1. Where they are

| Level | Generators (behaviour `twiglet_generator`, p_twiglet_generator) | Activated by |
| --- | --- | --- |
| lev_4_m1 | 93 trees; 13 with a trigger: 200 (64, 75, 78), 202 (68), 204 (14, 19, 79), 205 (171), 207 (530, 531: release_max 20) | 200: volumes 76 / 90 / 91 by the valley at (210, -57); 205: volumes 306 / 307; 207: volumes 422..425 (also captain 421). 202 and 204 have no sender |
| lev_4_m2 | 78 trees; 10 (5) and 6 (2) | nothing sends 10 or 6 in the exported mission: no tree gives twiglets |
| lev_3_m2, lev_4_m3, lev_4_m5, lev_bonus_m3 | 51 / 51 / 9 / 24 trees, all trigger_activate 0 | never (trigger 0 is never sent) |

So in the shipped game the trees give twiglets on lev_4_m1 only; elsewhere twiglets come from the monks (lev_4_m1,
lev_4_m2: section 5). The trees with a trigger name up to three `initial_ic_N` movers (mover_twiglet_tree_a_01 ...
d_03), nodes of the tree's reference anim (l4_tree_a_0 ...: `Mover_twiglet_tree_a_01` at the treetop, ~108 above the
base) and anims of the same name (6.7 s: the twiglet slides down the trunk).

`twiglet_max_distance` (global_settings, Game+0xd60: 1000, lev_4_m1 500).

## 2. The swarm (TwigletController 0x005a7590, one per level)

Made by the first generator's load (0x0049ce00), its spawn timer rolled then (0x0049cdc0: rand * 4.58e-6 + 0.1, 0.1 to
0.25 s). TwigletGenerator_Slot14 0x0049d530: a generator's `trigger_activate` puts it on the active list 0x0063a140,
`trigger_deactivate` takes it off. TwigletController_Slot47 0x0049d080, each frame: the timer counts down; at 0, unless
100 twiglets live (0x0063a148), the active generator with the most left (`release_max`, +0x470) gives one (the last
giver only as a fallback when it has exactly one mover), the timer is rolled again; while any generator has some left
the swarm loop `ctr_twiglet_swarm_1_lp` / `_2_lp` plays at it. With none left the timer is not rolled again (the
oracle's trace shows it running negative).

The release (0x0049d590): with movers, the next one (+0x46c pre-incremented, so the first twiglet takes the second
mover) at the tree's node, the twiglet put in its IC state (0x0049fbe0 / 0x0049fb60); without, 30..100 units round the
tree (x and y each their own distance) at a random turn of its facing, idle. Game+0x2710 counts every twiglet made.

The wake of the closest busy twiglet (0x0049f380 from the controller) needs flag bit 0 of the twiglet's +0x460, which
nothing sets: no effect.

## 3. The twiglet (Twiglet 0x005af020)

p_twiglet: max_health 12, move_speed 40, move_power 100, score_value 20, fx_death fx_exp_twiglet, material wood,
trigger_los 1, activation_range 400. Twiglet_Slot6 0x0049d7a0: with its template's properties it starts idle (state 1,
twiglet_stand), its leash point (+0x56c) where it is made and a 10 s leash delay (+0x578), a call timer 4..10 s.

Twiglet_Update 0x0049dcf0 (state +0x464):

| State | What | Remaster (twiglet.rs `State`) |
| --- | --- | --- |
| 0x01 | idle until the player is within activation_range (2D), then runs (0x0049f2b0) | Idle |
| 0x32 | an IC mover out of the tree; at its end runs (0x0049fab0 with +0x460 bit 5) | Emerging (mover.rs `Mover`, physics off) |
| 0x0a | runs at the player: calls ctr_twiglet_call_1..3 every 4..10 s, repaths every 1..3 s (within 20: to a point 5 to a side of him); within 10 a carried bomb is set off (vt+0xd4 on it), without one: 1 in 5 ctr_twiglet_attack_2, then the bomb out | Run |
| 0x46 | twiglet_get_bomb_out; its note "get_bomb" makes a twiglet_bomb at ref_hand_00 (0x0049f930); at the end runs with it | GetBomb |
| 0x1e / 0x1f | a hit it lives through: at most hit_level_1 (10) twiglet_hit_mgun*, more twiglet_hit_med*; then runs | HitLight / HitHeavy |
| 0x50..0x52 | the Topsy Turvy's stun (flag 0x20: 0..1 s), then hang on (twiglet_hang_into / _hanging, rand(0.25, left - 1) s at a roll >= topsy_turvy_fall_pct with more than 3 s left) or blown into the sky (twiglet_blown_up(_into): gravity -1, at most 120 up) for the effect's rest, then falling back (at most 120 down) to die at the first contact; running twiglets stand while the effect holds (state 0x0a) | ported 2026-10-17: twiglet.rs `topsy_hit` / `blow` / states Hang, BlownUp, Falling; topsy.rs; playtest twiglets_topsy |
| 0x0b, 0x14..0x18, 0x3c..0x3f, 0x5a / 0x5b, 0x64..0x66 | swipe, spear toss, leap / attach, pull / knock-away, the glove's hold / throw / punch | not ported: unreached (no code sets flag bit 0 or enters 0x3c / 0x0b) or the weapon is not in the engine (flag 0x800 the black hole, the knockout bomb's glove: feature knockout-bomb) |

Every frame: after the first 10 s a twiglet farther than twiglet_max_distance (2D) from its leash point dies (the re-roll
0x0046e2c0(3, 7) is dropped: the test runs every frame after); a carried bomb that is no longer alive kills it.

Damage (Twiglet_TakeDamage 0x0049f530): a record with flag 0x40 (area damage) is ignored while it carries a bomb (fx.rs
`BlastProof`): a blast lights the bomb's fuse (fx_explosion, start_delay / min_delay 0.5), the bomb goes off 0.5 s later
and the twiglet dies with it. The chain of a crowd: oracle twiglets_l4m1 (to frame 1800) bombs every ~29 frames, 1300,
1328, 1358, 1386, 1416, 1444. Twiglet_Kill 0x0049f430: a carried bomb is let go and set off; the death fx
fx_exp_twiglet, score_value to the Kills.

The bomb: twiglet_bomb (p_twiglet_bomb, behaviour fx_explosion: max_health 10, area_damage 100, radius 20 / 40,
impact_force 15000, fx_death fx_exp_generic, start_delay / min_delay 0.5): 100 damage to the player at his feet.

### The run's speed

The drive is the AI's (0x00454d80 through the route step 0x00470aa0): a force of mass x 50 x (wanted - current
velocity), the wanted velocity level at move_speed, its vertical part 0 while the body rises. On the flat that is
move_speed less the ground's friction; uphill the drive presses the body into the slope and the wood's friction
(wood_friction 20 in the global settings) holds it back: the oracle's lone runners make 36-38 units / s on the flat,
27 at 6 degrees, 19 at 9, ~10 at 12-15. twiglet.rs `ground_speed` is that force balance in steady state (the 0.02 s time
constant) with a fitted friction 2.0: with it the leading twiglet's distance to the player agrees with the original's
within 10 units every second of its 12 s run (lone runners alone fit 2.5; Havok's combine of 20 and 1 would be 4.5: its
contact solver is not modelled). The slope is the landscape's along the way, 3 units ahead.

## 4. Oracle

Hook (hook/src/testoracle.cpp): "twiglets" (World+0xeec: pos, health, state +0x464, bomb) and "swarm" (active
generators, timer, Game+0x2710 made, +0x270c killed); twiglets are left out of "grunts" (they are on the AI list).
The remaster (engine/src/oracle.rs) writes the same; a bomb gone off is a death with id 0 (FxExplosion_Die).
tools/oracle_diff.py families: twiglets.alive / .emerging / .running / .bombs (the other game's count reaches the
same value within twiglet_frames, 30) and twiglet.nearest (the nearest twiglet's 2D distance to each game's own Roman
within twiglet_dist, 20, within twiglet_frames).

Scenario twiglets_l4m1 (suite): Roman warped into volume 76, traced to frame 1200, twiglet_frames 60 (the releases are
random 0.1..0.25 s steps from each game's own stream: two runs spread by ~20 frames). 2026-10-16: PASS, 22 same, 5 known
(B6: grunts 283 / 284 shoot the original's Roman from frame 58, not the remaster's). Counts reach each value within
14 frames (made: original 1 at 10, 30 at 314; remaster 14, 326; first out of a tree 438 / 450; 30 out 790 / 804);
nearest twiglet within 7.5 units at worst. First bomb: original 1300 (Roman at (221, -69) after the grunts' knocks),
remaster 1240..1258 by its rolls (Roman at (210, -57), on a gentler slope): outside the traced window for that reason.
The numbers above are from the first stream; with twiglet.rs's own stream (`TwigletRng`, so levels without twiglets
draw exactly as before) the nearest is within 12 units at worst.

## 5. The monk's summon (states 0xf2 / 0xf3)

MonkObject vt+0x220 0x00437130: n = rand 500 < 50 (and +0x484 0x4000 clear) rolls the summon. 0xf2: a point 30..40
ahead within +-90 degrees of his facing (+0x934), cast_spell_2 (anim 0x25), state 0xf3. 0xf3: turned to the point;
at the anim's end five twiglets round him: the game's +y turned i x 72 degrees, scaled by two radii 7..20 (x, y), each
where the map has ground (0x0046f280), dropped onto the landscape (Object_ForceGround 1000, 10000), idle (they run once
the player is within 400); then the same end as the basket rain (vt+0x240: the remaster's hiding-spot search).
Remaster: ai.rs Mind::Cast with monk.rs `Spell::Twiglets`, `summon_point`, `twiglet_spots`; playtest monk_summon
(the step `monk_spell` forces the next roll).

## 6. Tests

- engine/playtests/twiglets.json (regress): lev_4_m1, the warp into volume 76: 3 generators, 3..11 made after 1 s, 30
  made and emerging after 6 s, 18..30 running with bombs after 13 s, every twiglet dead and Roman hurt after 25 s.
- engine/playtests/monk_summon.json (regress): lev_4_m2, a monk spawned near Roman, the summon forced: 1..5 made,
  running with bombs, at least one bomb reaches Roman within 12 s.
- twiglet.rs unit tests: the generator's movers (second first, wrapping), the release round a tree without movers.
- Oracle twiglets_l4m1 (oracle_suite).

## 7. Open

- monk_gun2 fires monk_twiglet_cluster (p_monk_gun2), whose hit (0x00482e80) makes five idle twiglets round the impact;
  which monk state fires weapon 0x1a is not found, so the remaster never fires it.
- The monk's basket rain drops its baskets at the cast's end in the original (ai_06.cpp:2881-2903, the anim-done test),
  halfway in the remaster (ai.rs, before this work).
- lev_4_m2's trees (triggers 10 and 6) and lev_4_m1's 202 / 204 have no sender in the exported mission: either a sender
  the export misses (an fx script, a text) or dead data. Not checked in the original beyond lev_4_m1's start.
- Twiglets are not saved (savegame.rs): a game saved mid-swarm restores without them (and the generators keep their
  counts from the level data).
- The twiglet-twiglet and twiglet-body contacts are Jolt's (capsules of radius 1.5, no friction: the ground friction is
  in `ground_speed`); the original's crowd is denser at the hill (x 400-450) where the leaders crawl.
