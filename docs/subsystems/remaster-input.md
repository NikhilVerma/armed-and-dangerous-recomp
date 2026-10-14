# Remaster engine input (keyboard / mouse / gamepad)

`engine/src/input.rs`, on `bevy_enhanced_input` (git master pinned at cb50e45: the first release for Bevy 0.20 was
not out on 2026-10-14). A remaster improvement: the original PC build reads one DirectInput joystick with
Xbox-era names (qol.md section 1).

## Contexts and the Controls resource

- Three contexts, one active: `Gameplay` (in play), `Menus` (front end, pause, pub screen), `Message` (a paused
  mission message). `contexts` picks it each frame from AppState, the menu page and `Mission::showing`.
- `collect` copies the evaluated actions into `Controls` (`down`, `pressed`, `released`, `axis`; `step` for menu
  navigation with key repeat 0.4 s / 0.12 s). Game systems read `Controls`, never raw devices, except mouse look
  (player.rs `LookDelta`: the cursor / raw / absolute-raw handling for trackpads and Moonlight), mouse hover in
  menus, cheat typing, and raw left clicks where a trackpad tap (press and release in one frame) must count.
- When the active context changes, buttons still held count only after a release (Options opens the pause menu
  without also being the menu's Back; the R2 that dismisses a message does not also fire).
- Pads count only while the window has focus (the player may be in another program); scripted runs (`--play`)
  read only their virtual pad (`{"pad": "..."}` steps; "PlayStation" in the first one makes it a DualSense).

## Layout

Keyboard and mouse: the original's defaults (player.md 2). Pad: a modern twin-stick layout agreed with the player
(2026-10-14):

| Pad (DualSense / Xbox) | In play | In menus |
|---|---|---|
| Left stick | move (analog: speed max x a + (1 - a) x 4) | navigate |
| Right stick | camera (3 rad/s turn, 1.7 rad/s pitch, shaping ^2, zoomed ^3) | |
| R2 / RT | fire | |
| L2 / LT | aim / zoom (held) | |
| R1 / RB | throw special | |
| L1 / LB, D-pad left / right | next special weapon | navigate |
| Cross / A | jump | confirm, continue |
| Square / X | use (hold for ticker bombs) | |
| Triangle / Y | switch weapon | |
| Circle / B | reload | back |
| D-pad up / down | send / recall teammates (not implemented yet) | navigate |
| Options / Menu | pause | back |

Messages are dismissed by Cross / A, Circle / B, R2 / RT (MenuSelect, MenuCancel or Fire: 0x00462790), Enter or
Mouse 1.

## Prompts

`input::key(Act)` gives a prompt's %s for the device in use: `<icon=name>` from the Kenney Input Prompts pack
(CC0; `tools/remaster_export_icons.py` packs 51 icons, 128 px, into `ui/icons.png` + `icons.json`), drawn inline by
`hud.rs` (`text_mesh`, 1.3 line heights). Keyboard / mouse show the key last used for the action (Use X / F,
continue Mouse 1 / Enter / Space); pads show their own buttons (Sony vendor id 0x054c -> PlayStation icons). The
paused message's prompt follows a device change while it is shown.

## Rumble

On the pad in use, only while focused: a light pulse per shot, a heavier one when Roman is hit (by the damage),
explosions within 300 units by distance. Not traced from the original (Rumble_Update drives DirectInput force
feedback there).

## Checked

Headless playtests: pad.json / padps.json (walk, turn, fire, Xbox A and DualSense Cross icons), padmenu.json (title
-> New Game -> briefing -> pause with Options -> stick navigation -> Circle resumes). Not yet tried on the player's
DualSense.
