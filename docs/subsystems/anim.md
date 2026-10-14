# Animation (playback, blending, notes, the player's anim table)

The original's animation layer is src/anim/anim_01.cpp (slots, blending, notes, the player's helpers, the leg IK) and
anim_02.cpp (ANM loading, key tracks, samplers, pose evaluation). The remaster's is engine/src/skin.rs (the player for
any skinned character) and engine/src/charanim.rs (which anim each character plays: the per-character state choices
belong to the player and AI code and only call into it). `python tools/port_queue.py anim` lists what is left;
docs/port/anim.csv what the engine replaces.

## 1. What the original does

### Slots (actor +0x248, four 0x20-byte slots)

`+0` anim entry (-1 none), `+4` time (seconds), `+8` previous time, `+0xc` overshoot after a wrap, `+0x10` user value
(a rate block, below), `+0x14` blend time, `+0x18` blend weight, `+0x1c` flags (1 loop, 2 wrapped this frame, 0x40
backwards, 0x80 / 0x100 restart at start / end, 0x200 / 0x800 set the time as a fraction / value, 0x1000 changed,
0x4000 four-corner set, 0x8000 pair, 0x10000 silent copy, 0x20000 no blend).

- **AnimSlot_Set 0x00408a70** sets a slot. A plain change (Anim_Set 0x00408d90: slot 1 cleared, slot 0 the anim) with
  a blend time b > 0 copies slots 0 / 1 into 2 / 3 (flag 0x10000: the old anim keeps playing, silent) and sets slot 1's
  weight to 1 with blend time b; when a blend is already running the current pose is cached instead (0x00408820:
  skeleton +0x20, blended from by actor +0x2c8 over b, in 0x004ec2f0). b = 0 cuts (and drops a running blend).
- **Anim_Update 0x00409ec0**: each slot's time += rate x dt (rate 0x00409d90), wrapping when looping (flag 2, overshoot
  kept) or holding at the ends; then each blend weight drops by dt / blend time. The pose records (0x00408ee0) weigh
  the new anim against the old by **(1 - cos(w pi)) / 2** of the remaining weight w (0x005bcb70 = pi): a cosine ease.
- **Rate 0x00409d90**: 1, or a rate block's: the body velocity along the block's direction (model space), clamped to
  [min, max], mapped from [low_speed, high_speed] onto [low_factor, high_factor]; negated with flag 0x40.
- **Notes 0x0040a200 / AnimTrack_FireEvents 0x004eb480**: each frame every non-silent slot fires the note keys in
  [previous time, time) (backwards: (time, previous]), split at the wrap; ticks are 4800 per second. A note exactly at
  the anim's end never fires (the interval stops short of it), a note at 0 fires on the first frame and on each wrap.
  0x0040a3f0 fires the notes from the start for a slot reset by request.
- Pose blends: 0x004ec600 (two layers: new over old), 0x004ec800 (pairs blended by the aim weight, then by the fade),
  0x004ec2f0 (towards the cached pose), dispatched by 0x004eccc0.

### The player's anim table (0x005cb650, 0x00475ff0)

Index mode x 7 + state, 0x28-byte entries: +0 type (1 one anim, 2 a `_b` / `_t` pair blended by the aim pitch, 3 four
corners; 0 = link to state +4 of the same mode), +4 name prefix, +8 slot flags (1 loop, 0x40 backwards), +0xc rate
block, +0x10 kind. Modes (+0x554): 0 stance, 1..5 idle fidgets, 6 / 7 fall, 8 land hard, 9 / 10 run forwards /
backwards (the same anim backwards), 11 / 12 run left / right, 13..16 walk, 17 / 18 jump, 19 / 20 land, 21..27 turbo,
28 / 29 turn in place, 30 / 31 hit, 32..35 topsy (35 the world turn backwards), 36..40 punches, black hole. States
(+0x558): 0 none, 1 / 2 firing (weapons 0..2 / others), 3 reload, 4 weapon change, 5 throw, 6 scan.

- Many states link to state 0: running or walking while firing plays the run / walk anim (only the stance, the jump
  and the turns have fire anims), walking while throwing too (only standing, running and jumping throw).
- 0x00475ff0 starts an entry when the mode or the state changed and its anims differ, with the **blend time
  0x005cb3a8[previous kind][kind]** (13 x 13: mostly 0.25 s, 0.125 within the stance's kind 2, 0.5 into kind 12, the
  fidgets). A mode change within the same state other than 0 keeps the **fraction** of the anim (flags 0xa00 with
  0x00475fc0); two movement entries (kind 4) of one type in state 0 / 6 carry the **time** on; else the anim restarts (at
  its end when it plays backwards).
- Rate blocks (PlayerObject_Load 0x00473da0 from p_trotter's anim_run_* / anim_walk_* / anim_side_* keys): forward
  (0, -1, 0), backward (0, 1, 0), left (1, -0.5, 0) / sqrt(1.25), right (-1, -0.5, 0) / sqrt(1.25); lev_1_m1: run high
  speed 23, walk 7, min 2.5, max 32: at 40 units/s the run plays at 32 / 23 = 1.39.
- The walk / run choice (0x00476a10): run above fwd_max_speed x anim_walk_run_switch (12); the side modes when the
  sideways speed beats the forward speed x 1.5 (forwards) or x 1 (backwards).

### The AI's anims

GenericAI_PlayAnim 0x004262d0 / PlayAnim4 0x00426480 pass a 0.25 s blend, except a character's first anim (0). The
anim names per id are the classes' vt+0x1a4 tables (ai.rs anim_name).

### Bone callbacks (PlayerObject_Load binds table 0x005fb760)

- 0x004735f0 on "center" / "belly": the torso twist and the turn in place (ported: charanim.rs `Twist`).
- 0x00472cc0 on the legs, with the foot placement 0x00477570: section 3.

## 2. The remaster (engine/src/skin.rs, charanim.rs)

- `Animator` layers (anim, time in [0, length], speed, loop, mask, weight, pair). `play` / `play_synced` (fraction
  kept) / `play_continued` (time kept) / `restart` / `stop`; `blend` is the cross-fade time for the changes made from
  now on. A change starts a `Fade` from the old layers (they keep playing, silent) or, while a fade runs, from the
  frozen pose; the old pose weighs `cos_blend(left / secs)`.
- `RateBlock::rate` is 0x00409d90; a negative speed plays backwards.
- Notes: `step_time` / `note_in` give the original's intervals; `AnimNote` messages carry them (gameaudio.rs plays the
  fx notes: footsteps, clothing, voices).
- charanim.rs `PlayerAnimTable` reads work/remaster/<level>/player_anims.json (tools/remaster_export_player_anims.py:
  the mode table, the blend table, the rate directions) and player.json's anim_speed_tables; `play_mode` starts
  (mode, state) as 0x00475ff0 does; `roman` maps the engine's player state to modes and states as 0x00476a10 does.
- AI characters, Q / Jonesy and the penguins cross-fade over 0.25 s (`AI_BLEND`).

Tests (tools/regress.py): engine/playtests/anim_blend.json (the stance -> fire cross-fade of 0.125 s eases and ends;
the run's rate 1.39 at 40 units/s, backwards -1.39, the side run's block), anim_notes.json (footsteps: 15 in 4 s of
running at the rate's 3.6 a second; a slow stick walks), unit tests in skin.rs (note intervals forwards / backwards /
at the ends, the cosine ease, the rate block).

## 3. Leg IK and foot placement (engine/src/footik.rs)

0x00477570 after the anims each frame, the leg callback 0x00472cc0 (records found by 0x00472c30, lifts read by
0x00472c90): a ray down from 6 above each ankle (12 long, doubling after a hit; a miss gives 0 and the next foot no
ray) finds the foot's ground height (hit + 0.1 relative to the base) and normal, eased by min(dt x 10, 1); the lowest
foot (>= -2.5) drops the body by lowest x 0.75 - 0.1 (only downwards); each leg's ankle is lifted by its height above
the lowest with a law-of-cosines two-bone solve (hip, knee), the ankle keeps its orientation, turned onto the ground
normal. Not ported: the landing bob, the fall flags (the player's fall code decides), the body's pull onto the ground.
`FootPlacement` is game-neutral (any skeleton with hip -> knee -> ankle legs); the player gets one.

Test: engine/playtests/foot_ik.json (flat ground: feet at 0.1, body -0.1, no lift; the slope at 150, 140: lift
0.4..2.5, body down; back to flat after walking); unit tests (the drop / lift rule, the two-bone solve).

## 4. Open questions

- Whether 0x00476a10's direction choice reads the local or the world velocity (+0x494 / +0x498): the remaster uses the
  local (guess: the left / right modes only make sense in the player's frame).
- The cross-fade from a frozen pose uses the pose shown (after the torso twist); the original caches the pose before
  its bone callbacks? (0x00408820 evaluates with flags 5: the callbacks run.) Not compared.
