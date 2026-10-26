# 2026-10-26: the wall ghost on lev_1_m1_b (branch fix/wall-ghost)

## Bug

Walk video of lev_1_m1_b, frames 104-144: Roman runs into the rusty wall. A whitish haze covers his feet and legs,
and a pale grey translucent copy of his left leg shows on the wall to his left. The original has neither. Frame 180
is clean.

## Cause

It was the snow stop puff, `fx_fs_roman_stop_snow`: pool-1 particles, size 2-3 growing 1-1.5/s, life 2-3 s, alpha
fade ramp 0x49. The "ghost leg" is Roman's own left leg in the aim pose behind the puff. The right leg was in front of
the puff and stayed dark. The run alternates the run and aim anims at the wall.

How I narrowed it down (scratch captures in work/ghost/, script work/ghost/run.py, the walk's oracle keys up to play
frame 300):
- Turning off AO, snowfall or modern fog, or switching TAA to MSAA, changed nothing.
- With soft_particles=0 the haze became an opaque white cloud with the leg inside it, so the haze is a particle
  effect.
- The state log shows one `fx_fs_roman_stop_snow` start at t ~4 s, just as Roman reaches the wall.

The trigger was wrong. The remaster fired the stop fx when the body's measured speed (+0x4bc) fell by more than 20 in
a frame. That happens when Roman runs into a wall. The original (PlayerObject_Update 0x004788a0, at 0x004799d5,
game_06.cpp:2678-2685) compares the WANTED speeds instead: |+0x470.xy| (m_targetLocalLinearVelocityNext) minus
|+0x47c.xy| (m_targetLocalLinearVelocity) < -20 (0x005bcd44). 0x00476a10 (game_05.cpp:1670-1680) sets both from the
drive's input at the end of each update. So the fx fires one frame after the stick is released, and never while the
stick is held into a wall.

## Fix

engine/src/player.rs:
- The new field `Roman::target_h` holds the wanted horizontal speeds of the last two frames. It replaces
  `prev_hspeed`.
- `ground_effects` compares those two speeds. Up to 60 fps the threshold is 20 per frame, as the original. Above
  60 fps it shrinks with dt. The old code scaled it up below 60 fps, which made a 39-unit key release miss at 30 fps.

## Tests

- Fresh capture of frames 100-152 (play frames 210-314): work/review/ghost_fixed.jpg. No haze and no ghost leg; both
  legs are dark. No stop fx started in the whole run.
- New playtest stop_fx, added to the regress SUITE: hold W for 1 s, then no stop puff; release, then exactly one.
  2 passed.
- gfx_effects_remaster 16/16, geom_remaster 6/6 and camera_follow 15/15 passed.

## Next / open

- At the wall the remaster still alternates trotter_run_aim and trotter_aim every few frames (hspeed 39 / 0.7). The
  original keeps running in place. Roman also gets a little closer to the wall than in the original. Both are
  separate items.
