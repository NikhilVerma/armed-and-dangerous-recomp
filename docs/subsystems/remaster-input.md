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
| Left stick | move (analog: the original's response, below) | navigate |
| Right stick | camera (the original's: 3 rad/s turn, 1.9 rad/s pitch at full deflection, linear) | |
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

## The original's input semantics (subsystem port, 2026-10-14)

Ported from archive/src/input/input_01.cpp and the readers in game_06.cpp / game_01.cpp; `python tools/port_queue.py input`
is empty, docs/port/input.csv lists what the remaster replaces.

| Original | Remaster |
| --- | --- |
| Actions held / pressed this frame / released on either of two bindings (Controls_IsHeld 0x00414810, _IsPressed 0x00414730, _IsReleased 0x004148f0); axes the larger of the bindings' (Controls_AxisMax 0x00414a30) | `Controls::down / pressed / released / axis` from bevy_enhanced_input |
| An action that did its job is consumed until every binding is up (Controls_Consume 0x00414680, freed by 0x004146c0 each poll) | buttons held across a context change count only after a release (`Controls` blocked) |
| One active device: the last that gave input; only it is read unless locked (Controls_Poll 0x004145c0, Controls_Lock 0x00414590) | every device is read; the active one picks the prompts and the rumble |
| Analog axes: dead zone 0.21 per axis, rescaled linearly (InputPC_AxisValue 0x004f9560 with the callers' 0x3e570a3d); digital codes give full deflection | `AXIS_DEAD_ZONE` (Axial, 0.21) on Move and Look |
| Movement: the axes' vector cut to length 1 (two keys: a diagonal at full speed, not faster), its length through the control curve 0x00414c00 (fmod by control_exponent 2 with bias 0: the identity) and above 0.225 raised by 0.4 (game_06.cpp:2479-2501) | player.rs `move_curve`: a half-pushed stick (0.5) runs at 31.6, 0.35 walks at 10.4 |
| Turn: turn_max_speed 0.3 x axis x dt x 10 (3 rad/s; zoomed 0.05), linear | right stick and the arrow keys (the original's secondary TurnLeft / Right bindings) |
| Pitch: pitch_max_speed 0.95 x axis x dt added twice a frame, by the player update (game_06.cpp:2379) and by Camera_Update (game_01.cpp:2895): 1.9 rad/s (zoomed 0.5), clamped to [-1.5608, 1.15] about the rest | right stick and Up / Down arrows; `CameraRig::pitch_limits` |
| Mouse: counts x sensitivity (option + 0.5; InputPC_SetMouseSensitivity 0x004f7cf0) | player.rs control: the turn and pitch per count measured against the original |
| Force feedback: rumble samples with envelopes (Rumble_Update 0x004e5750) sent to vt+0x88, which on PC is 0x00477c10 `ret 0xc`: the PC build never rumbles | the remaster's own rumble (above): an improvement, not a port |

The stick's response changed with this port: before it was shaped |a|^2 (zoomed |a|^3) with a guessed 1.7 rad/s pitch
(the shaping read 0x00414c00 as pow; the recovered code calls the CRT's fmod, and the turn does not go through it). If
the player prefers the old curve it is a remaster option to add, off by default.

Tests: engine/playtests/input_pad.json (pad move speeds through the curve, walk under 12, right-stick turn and pitch
rates, R2 fires, the arrow keys turn), camera_follow.json (mouse pitch limits).
