# 2026-10-15: remaster bugs found by the oracle harness (agent-oraclefix)

Branch agent-oraclefix from wip/full-decomp 40ba479. The harness was the test loop: `tools/oracle_diff.py SCENARIO
--exe target-wt-oraclefix` (both games), `--only remaster` after each engine change. Two extra scenario files written
for the work: tools/oracle_scenarios/stop.json (walk 1 s, release, strafe, back: every frame; in the suite) and
intro.json (the intro from the load, trace_from -900; exploratory).

Done (cause, fix; before -> after in the scenario reports):
- B1 Roman sliding after the briefing. Causes: the ground drive was ~15x too soft and nothing but the drive slowed him
  (the original: full speed in one frame, dead stop on release, 39.2 units/s, measured every frame in stop.json); and
  the briefing came one frame after the intro path's end, so the remaster's Roman lost the stick a frame before the
  pause. Fix (player.rs): DRIVE_GAIN on the drive's power, the ground contacts' friction +0x5a0 (friction_max ->
  friction_min blend x gravity), the slide / stop fx on the speed before the drive (+0x4bc); the mission's systems
  ordered before `control`, which does not run while the game is paused. walk roman.pos: KNOWN at 0 (1057.8 vs
  1059.7, 4.8 units of slide) -> same; intro roman.pos same over the whole intro; stop roman.pos same (start / stop
  within a frame).
- B2 trigger volume 269 as a world AABB. Fix: oriented boxes (mission.rs Volume::Box with the placement's inverse).
  walk events.triggers: 711 at 4.32 s in the intro -> 711 at frame 44 vs 47 (the original's), same.
- B3 grunt 181 on the archway (z 108). Cause: Object_ForceGround's ray sees only bodies without an owner object (the
  landscape): Ray_CastQuery 0x00414110 called by 0x004137e0 with no include list and the "ownerless" flag; the
  collector 0x00413a90 rejects owned bodies without the query's stamp. Fix: physics.rs raycast_landscape for
  force_ground, Roman's and the companions' grounding (ai.rs: one line). grunt.pos at -30: 94 units -> same (all 27
  grunts within 0.6, the usual capsule offset).
- B4 Roman's heading 0 while turning: a trace bug (oracle.rs facing took game_to_engine twice: the up axis). walk_turn
  roman.heading: KNOWN at 84 -> same.
- B5 volumes while paused. Fix: mission.rs volumes return while Time<Virtual> is paused. courtyard 98 at -9 -> 2 (the
  original's 2); pub 810 at 2 in both.
- B7 the pub: the keys' Select no longer leaves with nothing left (a click does: feedback 17); the page fades out
  0.5 s as in the original with the game paused, then the exit trigger and the unpause (pubscreen.rs leave_pub);
  Roman stays at the door (0x004690c0 computes Ref_exit and drops it); PlayerStats published every frame. pub: 900 at
  382 (5th ENTER) -> 470 (original 472); weapon / clip / ammo / paused / timing / roman.pos same.
- Harness: sound_key compares a sound ear's voice line by kind (the speaker is a roll: jon50001 vs rom50001); an
  unlimited reserve near a weapon change agrees; remaster deaths include placed objects removed by
  receive_trigger_kill (bigredarrow 258).

Learned:
- The original's movement is stiff: velocity follows the stick within a frame; its 39.2 units/s is the friction's
  doing (0.3 x 160.9 / the drive). Exact Havok integration of the force action pool not decoded (gain fitted).
- Object_ForceGround never lands on objects, only the landscape.
- The pub's exit bone position is computed and unused in the original.

Open / next:
- B9 (new): the companions do not idle at their slot while Roman walks off (ai.rs companions: arrival within 4 units
  of the slot; the original stops ~20 behind him). For the AI agent.
- B10 (new): footstep notes after a start / stop at other frames (charanim.rs notes during the run / stop blend).
- pub: barracks troops after 900 come 30 frames earlier in the remaster (release delay roll), grunt 34 ToMarker (AI).
- B6, B8: the AI agent.
- Open question: the partial-stick walk speed. With the friction model a half stick (LeftStickY 0.35) walks at ~7.8
  units/s instead of the drive's 10.5 target (playtest input_pad range moved to [7, 9.5]); the oracle's scripted input
  has no pad axes, so this is the model's, not measured. A hook pad-axis input (keys_frames) would settle it.
