# 2026-10-22: accuracy audit fixes, group "player"

Branch agent-fix-player (from wip/full-decomp 1c265b9). The 39 findings of work/audit/fix-pilot-player.json were
checked against the current code and the recovered C++.

## Done

- Camera 0x0040e56c (camera.rs, camfade.rs): rays reach arm + 10 per carried peasant (Game+0x26f4), so the camera
  stands further back with peasants on the chain; hits on anything parented to Roman do not fade; the fade works on
  the object's own alpha (ObjectAlpha) with the original's order (fade-in of objects not hit the frame before, then
  fade-out of this frame's hits) and ends at 1.0 (the unarmed ticker site quirk: docs/original-bugs.md); the 1 s
  smoothing replaces the zoom blend while it runs.
- Targeting 0x00406f50 (weapons/targeting.rs): the HUD passes' line of sight is blocked only by the landscape and
  the sight list's objects (in_hud_sight); characters pass. Base turret (Game+0xac8 = 1) multi-lock with
  autoaim_base_factor_*; black-hole carried characters stay targetable except by the shark gun. Sticky bomb aim
  point without a target: no 20 ahead while zooming.
- Crosshair (hud.rs crosshair_aim): ray length by mode (1500 base turret, 500 forge cannon, 2000 mounted gun, 5000
  without a weapon); on a gun the ray takes the damage takers only (level.rs DamageTaker); buildings answer by
  takes_bullet_damage for the machine gun / rifle only; the auto-aim target turns the crosshair on; start-in-turret
  levels: on with any hit or lock-on.
- Meters 0x00466e70: a pulse per meter, at or below 20 % including 0, frozen while paused; companions by their own
  max_health; the enemy meter's target (hud+0x2c) computed (HudState::enemy_health), its element still unspawned.

New playtests: camera_carried_peasants, hud_meter_targets.

## Left (deferred)

- Per-object bounding-sphere slack in the target passes (0x00408050 asks each object's bounds; the remaster uses the
  grunt's 4.66 for all): needs each character's model bounds on its entity.
- The vindaloo / base-turret multi-lock over every damage taker (World+0xea4: buildings and props too): non-character
  targets, their points and homing.
- The 20-frame candidate snapshot (0x00408190): a CPU cache; the remaster rebuilds every frame.
- Character body mass (r^3 x density x 4/3 pi or the ragdoll record's sum) and the radius fallbacks (0x004527d0): the
  density +0x38c and the record path are not traced for AIs, and the impulses were tuned with 80.
- The sticky bomb's area-damage radius: vt+0x94's sphere of player_stickybomb_0 (0.57 collision radius or 1.0 half
  diagonal, which one not traced) instead of the guess 1.5.
- Drive / jump / jetpack integration (0x00471810 / 0x00455a30): one force path in the original, two fitted models
  here; needs the force pool decoded.
- Whether ui_health_enemy is drawn at all in normal play (its +0x470 / +0x474 flags).
