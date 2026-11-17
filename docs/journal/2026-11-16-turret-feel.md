# 2026-11-16: the base turret after the player's "Hold off the army" session

The player played lev_5_m5 on a Mac trackpad and gave seven items (docs/player-feedback.md T1 to T7).

## What was done

- **Bullets show** (T1). A projectile is drawn with its template's model (player.rs `dress_projectiles`).
- **The mortar kills what it is aimed at** (T2). The blast code matched the original already. A platoon has no
  physics body in the engine, so a shell flew through the soldiers. It now bursts on the platoon's box (army.rs
  `fake_hits`, shell.rs `Shell::strike`).
- **Keg blast** (T3). Logic and effect data match the original. Eight upscaled effect textures of the Remaster pack
  were damaged; the original art shows for them now (graphics/textures.rs `PACK_DAMAGED`).
- **Trackpad judder** (T4). The look reads Apple's `GCMouse` on macOS (platform/rawmouse.rs, crate
  objc2-game-controller, setting `raw_mouse`). docs/macos.md has the evidence and the reasoning.
- **Feel** (T5). The turret camera takes its lens from the camera node (45.9 degrees, was the on-foot 37.5), the zoom
  blends position and lens over 0.15 s, and camera shake works while seated (defence.rs `pose`, turret.rs).
- **Pause blur** (T6). A cubic B-spline upsample behind `blur_smooth` (fxvm/screen_post.wesl).

## Added 2026-11-17

- **A blast throws the critter it kills** (PenguinObject_Damage 0x0048fa20 -> Character_GoRagdoll 0x00431310): an
  impact above 10050 throws it along the blast at impact x 0.15 (a keg: 56 units/s). It flies under gravity, lands
  on the landscape and plays its landing anim (fx.rs `BlastThrow`, charanim.rs `critters`). Test:
  keg_throws_sheep.json.
- **The mortar's muzzle flash follows the gun** as it slides (0x0049c2a0; defence.rs).
- **A shot fired during a hit flinch keeps its fire anim.** The original picks the action state whatever the mode
  (0x00475ff0), and a shot ends the flinch. The engine cleared the flinch one frame late, and that frame dropped
  the fire request (charanim.rs). This was the old weapon_mortar failure: something knocks Roman just before his
  shot there. What knocks him is not found yet (the grunts were held; health stays 400 under SHIELDME).
- The fire recycle is a per-frame test in the original too, so the machine gun's rate is not a difference.
- Smoke playtests on the final build: weapon_mortar (14), keg_throws_sheep (4), turret_mortar_platoon (1),
  defence_l5m5 (9), turret_kickout (12), keg_launch (7) pass.

## The old red tests (2026-11-17)

The Mac journal of 2026-11-01 guessed that GOG data made 18 fast-suite scripts fail. It did not. The causes:

- **A stale shared state.** state_after_pub.json pressed Select to leave the pub. The pub is left on Back only, as
  in the original (pubscreen.rs), so the state was saved inside the pub: no exit trigger 900, site 252 off. The
  script now presses Escape. This alone fixed ticker, save_write, save_read and most of hud and hud_meters.
- **A scripted run had no money.** A script that starts in play now starts a new game, as --level does (main.rs;
  5000, 0x00401e80). campaign_chain passes.
- **Stale expectations**, each checked against the original:
  - hud: the weapon change line has no speaker (WeaponChangeMessage, 0x004a16b0: Chat_AddMessage colour 2).
  - hud_meters: no ticker hint while another bomb ticks (0x00467450, Game+0xdcf).
  - volumes: wall 269 is not crossed in the intro (journal 2026-10-15: the original sends 711 on the walk after).
  - launch_options: the preset reads "Remaster (custom)", because the script itself turns modern_menus off.
  - geom_menu: the detail page gained Ground Detail and Tessellated Scenery.
  - gfxopts: the width limit assumed a 4:3 screen (1920x1200 here).
  - taunt1: the taunt comes after 12 to 18 s in the new state; the wait is 20 s.

After these fixes all 63 fast-suite scripts that load `after_pub` pass, and so do gfxopts, geom_menu,
launch_options and weapon_mortar. The other suite scripts ran after (next section).

## Every level, played by the bot (2026-11-17)

`tools/regress.py --long` on today's build: 23 of 25 passed. The two failures were both base turret levels.

- **complete_lev_1_m4**: the turret shot a token. Its message pauses the game, as in the original (pickup.rs
  `award_token`), and the bot waited 900 s for a trigger a paused game cannot send. The runner now continues past
  a message while it waits for a trigger or a death (playtest.rs). Passes (8 checks, 305 s).
- **complete_lev_3_m4_bd**: a regression from the platoon fix (T2), in the bot and not in the game. The build of
  2026-11-15 wins in 318 s. Today's build stalled at 207 s: the bot fired 175 mortars at a soldier 546 away,
  through a platoon row. Each shell burst on the row's near end and could not reach the row's last survivor
  (kill_range 50, a row 50 wide). The bot now aims at the nearer of the soldiers and the platoon members
  (defence.rs). A player shoots what is in the way; the bot did not.
- Open: does the original's platoon body stop a shell when only one soldier is left in the row? Not checked in
  the original.

The other 50 fast-suite scripts: 49 pass. movie_intro wanted the line `sup_011` half a second into the opening
movie. This game copy's export has no such line (69 lines, the first at 15.2 s, and no `sup_011` text), so the
check was dropped; movie_subs still covers subtitles. Open: whether another build of the game has that line.

## What was tested

- New playtest turret_mortar_platoon.json: one crosshair mortar kills 12.
- Scratch checks by capture: bullets, the lens (cam_fov 45.86), the keg blast, the message screen's blur.
- Unit tests near the changes (cursor, mouselook, shell, gamemenu): 15 pass.
- Smoke playtests on the new build: turret_mortar_platoon (1), defence_l5m5 (9), turret_kickout (12), keg_launch (7),
  modern_menus (29) pass. weapon_mortar failed one step then (anim_state 0, want 2); fixed on 2026-11-17, see above.
  The full suite did not run (the maintainer's
  standing choice).
- **Not tested by a hand:** the raw mouse path. A scripted run cannot move a trackpad. The game logs that it reads
  the device on this Mac; how it feels, and the turn per swipe, wait for the player.

## What was learned

- I first wrote my own pointer reader (an NSEvent monitor, undocumented CGEvent fields, a self-measured gain). The
  maintainer stopped that: search the internet, use what exists. The search took minutes and found the answer: winit
  issue 4581 measures the cause (NSEvent motion comes in one lump per display refresh) and SDL3 moved to `GCMouse`
  in 2026. Search first for anything about platforms, input or rendering.
- "The feel is off" had hard causes. A property-and-code audit by a subagent found the wrong lens in six minutes.
  Ask "which values does the original take from the data that we do not" before asking the player what they mean.
- A subagent's "likely" is a guess. One said the muzzle flashes sat off-screen; the capture shows them on the
  barrels.
- The anim exporter writes a camera's `fov` as vertical radians (it applies 0x0040a4d0's rule). Do not convert twice.
- No maintained Bevy blur crate supports 0.20. Bevy ships a 1D Gaussian shader (bevy_post_process
  `gaussian_blur.wesl`) for depth of field; a pause blur from it needs two more passes.

## Open questions

- Raw trackpad counts: is the turn per swipe right at the original's gains? Read `raw` against `mouse` in the
  player's next session log (inputs.jsonl) and tune, or add a macOS default for Mouse Sensitivity.
- Does `GCMouse` list every mouse (Bluetooth, a streamed session)? A frame without a raw report falls back to the
  window system's motion.
- The fire trail's brightness over dark ground (premultiplied DXT2; journal 2026-10-25-r3). Needs an original
  capture of a keg on a dark level.
- The crosshair ray passes through platoons. Does the original's aim list (+0xea4) hold them?
- The shell's direct-hit damage 300 is not added to the object struck.

## Next

- The player plays the build. Read the session log for the mouse.
- Then the release: push and run the workflow, after asking.
