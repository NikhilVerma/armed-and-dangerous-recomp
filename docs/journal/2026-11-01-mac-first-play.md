# 2026-11-01: first play on a Mac

## Done

- Exported all 24 levels from the GOG build and played lev_1_m1 on an M5 Pro. The GOG build's table addresses are in
  docs/game-builds.md.
- Test windows on macOS keep an 8-pixel sliver on screen, because a fully hidden window renders nothing and captures
  come out black (`platform::BACKGROUND_WINDOW_NEEDS_VISIBLE_EDGE`).
- Pines drew over the ice in lev_1_m1 (docs/player-feedback.md M1). The exporter now marks DXT2 and DXT3 textures as
  alpha only when a pixel isn't opaque.
- A missing animation frame now draws the stage's own texture (M2, 0x00419940).

## Learned

- `--graphics` is ignored when the script sets the preset, unless `--preset` is also passed. Runs that compare settings
  need both.
- The ice still reflects the level's horizon band and a trunk. That reflection did not cause M1.
- The render scale cuts GPU load far more than a frame cap: see "Heat" in docs/macos.md.

## Tested

- Unit tests 307/307, launch_newgame 42/42 and complete_lev_1_m1_b 17/17 passed after the frame fix and before the
  rebake. The bookmark shot and the flash script ran after the rebake. The full regression suite was not run.

## MetalFX

The render scale now goes through MetalFX's spatial scaler on macOS, as the last step of the 3D view
(`engine/src/platform/metalfx.rs`). The crate `bevy_metalfx` was tried first and removed, because it runs the post
effects at full size. Unit tests (307) and `launch_newgame` (42 checks) passed at that point.

## Player feedback M5 to M14

The player's bugs M5 to M8 and M10 to M14 are fixed, and playtests or screenshots checked each one
(docs/player-feedback.md). M9 is partly fixed and waits for the player's verdict.

The T pose in M7 came from a shortcut in the port. A blast's victim became a ragdoll at death, and the ragdoll's joints
hold their pose, so the figure flew stiff. The original flails until it lands, and `ai/flung.rs` now does the same.

Tiling does not cause M9. `b_dirty_wall` goes from 512 to 1024 px and its marks stay in the same place. I tried a mip
bias for the render scale and removed it, because an edge measure gave 19.09 without it and 19.2 with it. The pack
has too few texels for that wall at close range, so the material now adds the texture's own grain to magnified
surfaces.

The new playtest `player_topsy_throws.json` covers M13.

The fast suite was run on the Mac for the first time: 187 scripts passed and 23 failed. Four of the failures are
scripts missing from the repository (`oilpump`, `topsyhit2`, `turn_in_place`, `turret1`). The other 19 ran on a build of
the last commit too. 18 failed there the same way, and `bodyfall_fx` failed only on the new code and is now fixed. They
need their own look, because they may come from the Mac or the GOG build.

A first triage of those 18 found a stale state is not the cause: regenerating `states/after_pub.json` changed nothing
in the file, and `ticker`, `volumes`, `hud`, `hud_meters`, `save_read`, `save_write`, `taunt1` and `campaign_chain` failed
as before. Several of them expect a different mission state: site 252 enabled, a compass target of 234,252, and 5000 more
money in `campaign_chain`. A guess, not yet tested: the GOG level data differs from the Steam data the expectations
were written against. A Steam export would settle it. `gfxopts` expects a window of at most 1600 wide and got 1920, which
looks like a Mac screen matter. The rest were not looked at.

## Next

- A higher-resolution pack would still help M9, including the textures the pack lacks. The retexture tools run on the
  Windows PC.
- Have the player try MetalFX at 67% render scale and confirm whether the laptop runs cooler (M3). GPU busy is measured;
  temperature is not.
- MetalFX sharpens more than the native image: an edge-strength measure on one still gave 413 at 67% and 220 at native.
  Check in play whether that looks too sharp.

## Open questions

- Existing exports of lev_1_m1 keep the old alpha flag until re-exported. Nothing warns the player.
- Whether the Steam build also lacks fxa_flash frame 14 is unknown.
