# Quality-of-life features: reconnaissance (2026-10-04)

Static reconnaissance only (no game run). Evidence is from `work/decomp/<addr>.c`, `tools/recover.py --asm`
and raw reads of `game.unpacked.exe` (vtables, constants). **Guess** marks anything not proven. Feature list:
`docs/goals/remaster-basics.md` item 6.

## Game object members used below

| Offset | Object | Evidence |
| --- | --- | --- |
| game+0x2c | PC audio (vtable 0x0058a230, ctor 0x004f9ca0). The Ghidra field name `renderer_2c` is wrong | 0x00401ce0 calls +0x88/+0x8c/+0x90 with the volumes; 0x004e41e0 calls +0x130 (update) |
| game+0x30 | PC input (vtable 0x0058a600, ctor 0x004f7bb0) | 0x004e41e0 calls +0x1c (0x004f8760, polls devices) each frame |
| game+0x38 | Renderer | 0x00401ce0 calls +0x40 (Renderer_SetGamma) |
| game+0xa80 | Control (action bindings) | 0x00401a80 "&m_control->m_inputActions" at control+0x1c |
| game+0x174..0x4c8 | Options block (0xd5 dwords, saved to SavedGames/options.opt) | 0x00401a80 registers each field by name |

Options apply function: **0x00401ce0** (called after option load, from the options menu "accept" 0x004714c0
case 0x4c/0x4e/0x0d). It pushes every option to its subsystem; a good single hook point for remaster overrides.

## 1. Mouse, keys, controllers

**Devices** (0x004f81d0, input vt +0x04): `DirectInput8Create`, then three devices:
- Mouse at input+0x28: GUID_SysMouse (0x00579168), data format at 0x005793b4 = c_dfDIMouse2
  (dwDataSize 0x14, 11 objects). Cooperative level from input+0x218 (HWND) with flags 6 (NONEXCLUSIVE|FOREGROUND).
- Keyboard at input+0x58: GUID_SysKeyboard (0x00579158), 256-byte state.
- Joystick at input+0x160: found by `IDirectInput8::EnumDevices` in 0x004f7e10 (callback, EnumObjects callback
  0x004f7d30), custom 0x34-byte data format. No XInput import (docs/recon.md lists only DINPUT8/DSOUND/D3D9 etc.).

**Polling** (0x004f8760 = input vt +0x1c, per frame from 0x004e41e0): keyboard 0x004f8530, mouse 0x004f83c0,
joystick 0x004f8600. All use **GetDeviceState** (immediate mode), no GetDeviceData buffering.
- Keyboard: per-key state byte at input+0x5c+dik: 0 up, 1 pressed, 2 held, 3 released.
- Mouse 0x004f83c0: `GetDeviceState(0x14)` (DIMOUSESTATE2). Stores, keeping previous values at +0x38..+0x48:
  - input+0x2c = lX * k, input+0x30 = lY * k, with k = (1/30) / g_frameDt * 0.1 (constants 0x005bcc50 = 0.0333,
    0x005bcbfc = 0.1; k = 1.0 (0x005bcb2c) when dt == 0). So deltas are converted to a per-30-Hz-frame rate.
  - input+0x34 = lZ (wheel, raw); buttons 0..7 packed into a bitmask at +0x44 (+0x54 pressed, +0x50 released).
  - DirectInput mouse data is already unaccelerated ("raw"); there is no Win32 cursor path for gameplay
    (0x004f82d0 only clips the cursor, string "Cursor clipped from ...").
- Joystick 0x004f8600: 13 dwords; axes recentred by -0x7dff and scaled by 3.1003e-05 (0x005bcb6c) into
  floats at input+0x1f8..+0x204.

**Sensitivity**: option m_mouseSensitivity (game+0x4b8, default 0.5, slider 0..1, "ui_controls_mousescale" in
0x004710d0 case 0x44). 0x00401ce0 calls input vt +0x24 = 0x004f7cf0 with `sens + 0.5` (0x005bcb4c) which stores
input+0x21c (multiplier 0.5..1.5; ctor default 1.0). Applied only in **0x004f9560** (input vt +0x78, "axis value
for a key code"): for mouse codes it returns `input+0x21c * delta * scale`. One value for both axes.

**Key codes** (names 0x00414c90): DIK scan codes for keys; 0x10000 MOUSE_UP, 0x10001 MOUSE_DOWN, 0x10002
MOUSE_LEFT, 0x10003 MOUSE_RIGHT, 0x10004.. MOUSE_1..8, 0x1000c/0x1000d SCROLL_UP/DOWN; negative codes are
joystick axis directions (0x004f9560 reads input+0x1f8..0x204 with a dead zone argument). Each axis direction
is a separate bindable "key".

**Bindings**: 35 actions (names 0x004155a0, ControlActionMoveForward .. SpecialEvent, 0..0x22). Stored in the
Control object: primary key per action at control+0x1c+4*a, secondary at control+0xa8+4*a (0x00414a30 queries
both and takes the max), 0x118 bytes in total, saved in options.opt as "&m_control->m_inputActions".
**Remapping exists in the original UI**: controls pages (menu ids 0x2c/0x2d, 0x00447d60, 12 rows per page from
table 0x00588dd8), "PMS_INPUT_ASSIGNKEY" prompt (0x00447f50), "ui_yesno_resetcontrols" (0x004714c0 case 0x2e).

**Invert Y**: no option and no "invert" string in the executable. In the original, inversion = binding
LookUp to MOUSE_DOWN and LookDown to MOUSE_UP in the controls menu (guess: works, follows from the code-per-
direction design; not tested).

**Recommended hooks**
- Per-axis sensitivity / invert: hook 0x004f83c0 (or post-process input+0x2c/+0x30 right after it in a hook of
  0x004f8760): multiply X/Y separately, negate Y. Faithful default = unchanged.
- Raw-input alternative (high-DPI, no DI): replace the mouse GetDeviceState result in `hook/src/input_hooks.cpp`
  (already hooks GetDeviceState) with accumulated WM_INPUT deltas.
- XInput: synthesise joystick axes/buttons, either by faking the 0x34-byte joystick state in the GetDeviceState
  hook for input+0x160 (needs a device to exist; guess: none without a DI joystick) or by hooking 0x004f8600 and
  writing input+0x1f8..0x204 plus button state directly. Analogue movement already flows through 0x004f9560.

## 2. Brightness and gamma

m_brightness (game+0x4b4, default 1.5, menu "ui_gfxmenu_brightness" range 1.0..2.0 via temp 0x005fea28) is sent
by 0x00401ce0 as `renderer->vt[+0x40](b, b, b)` = **Renderer_SetGamma 0x005091c0** (recovered:
`src/renderer/auto_00507c10.cpp`). It clamps to 0..2, returns early unless renderer caps byte +0x15fd bit 2 is
set (guess: the FULLSCREENGAMMA cap), builds ramp[i] = pow(i/255, 1/gamma) * 65535 and calls
`IDirect3DDevice9::SetGammaRamp(0, 0, ramp)`. That call has no effect in windowed mode (D3D9 documentation) and
changes the whole desktop ramp in full screen.

**Recommended**: in `hook/enhance/`, wrap Renderer_SetGamma: record the three gammas, skip the device ramp, and
apply `c' = pow(c, 1/gamma)` in a final present pass (Vulkan backend post pass, or a full-screen quad in the
D3D9 facade before Present). Keep the original path when the switch is off.

## 3. Audio

API: **DirectSound 8** (DSOUND ordinal 11), no Miles. Init 0x004fa8f0 (audio vt +0x74):
`DirectSoundCreate8(NULL, ...)` (default device), `SetCooperativeLevel(hwnd, 3)`, primary buffer
(DSBCAPS 0x91 = PRIMARYBUFFER|CTRLVOLUME|CTRL3D) set to 22050 Hz, 2 ch, 16 bit (WAVEFORMATEX on stack).

Volumes (options +0x490 sound, +0x494 voice, +0x498 music, default 0.8, menu 0x00491b70 cases 0x38..0x3a)
reach audio via 0x00401ce0:
- vt +0x88 = 0x004f42e0 (sound) -> audio+0x18; vt +0x8c = 0x004f42a0 (music) -> audio+0x1c;
  vt +0x90 = 0x004f4260 (voice) -> audio+0x20. Each stores `vt+0xac(1 - v, 0, 1)` (an attenuation) and if
  changed calls 0x004f3000, which walks the voice list and calls vt +0x40 = **0x004fa1d0** with vt +0x10
  (0x004fb170) result.
- 0x004fa1d0 is the single `IDirectSoundBuffer::SetVolume` call site found: value in dB, `ftol(dB * 100)`
  (0x005bcb60 = 100), or DSBVOLUME_MIN when below -40 dB (0x005bcb64).

**Recommended**
- Master volume: hook 0x004fa1d0 and add `20*log10(master)` dB before the conversion (guess: every voice volume
  passes here; streamed music path not yet confirmed; alternatively hook IDirectSoundBuffer::SetVolume by
  vtable).
- Device choice: hook the `DirectSoundCreate8` import (call in 0x004fa8f0) and substitute a GUID chosen with
  `DirectSoundEnumerate` from `and.ini`. Optional: 44.1/48 kHz primary format.

## 4. Saves, quicksave, quickload

- "QuickSave" is a UI screen (`pui_saving`, id 0x324, created in 0x0048ff90), not a hotkey. Pushed by:
  - pause menu item 7 (0x004714c0 case 7, only when game shellFlags & 8);
  - automatically by **0x00403560** (if game+0x568 & 8 and the screen is not already up, it queues 0x00403520
    via the deferred-call slot 0x00403370 -> game+0xa58/+0xa5c). Callers: mission start 0x0044daf0 and
    0x004690c0 (guess: checkpoint).
  - 0x00403520 (arg name or "QuickSave") pushes that screen with UI_PushScreen 0x00490b00.
- Quickload: pause menu item 0x22 (0x004921a0). If in a live level (0x00403bb0) it asks "ui_yesno_loadgame"
  (callback 0x0048fee0, guess: leads to the same load), else **0x004a2ef0**: 0x004a2e90(buf, 1) validates the
  quick slot (game+0x56c, saveLoader) and pushes the "Loading" screen, or shows "ui_notify_badQuickload".
- No quicksave/quickload hotkey exists (no ControlAction for it in 0x004155a0).

**Recommended hotkeys**: quicksave = call 0x00403560(game) (esi = g_game; respects the game's own allowed
flag) or 0x00403370(game, 0x00403520, "QuickSave") to defer to the next safe point. Quickload = 0x004a2ef0
when 0x004a2e90(0, 1) returns true. Trigger from the frame (input poll hook), never from the window thread.
Guess: both are safe only when no menu is up; verify in game.

## 5. Frame-rate dependence

docs/original-bugs.md: (a) "game speed and physics may run fast on modern systems" (PCGamingWiki, location
unknown); (b) lev_1_m1 opening stalls at frame steps below ~11.8 ms (above ~85 fps): the starting token is
collected during the opening camera path, so the mission start 0x0044daf0 never runs (reproduced with
`[test] fixed_dt_ms`).

Timer_Tick **0x004e47e0** (from the main frame 0x004e41e0): if pause count 0x00604aac != 0, dt = 0. Else
game time 0x00604a98 from 0x004e4650 (QPC), unscaled dt 0x00604a94 = now - last (0x00604ac4); if the
override **0x00604ab8** != 0 it replaces dt; **g_frameDt 0x00604a90** = timeScale (0x00604aa0) * dt, clamped
to 0.25 s (0x005bcbe4). The frame then runs one variable-step update: game vt +8 (dt), input +0x1c, audio +0x130.
There is no fixed-step loop (the override is a constant step per frame, i.e. game speed follows frame rate).

A "decouple" switch would need: (1) an accumulator in a hook around the update part of 0x004e41e0: run the
game update N times per rendered frame with a fixed dt (e.g. 1/30 or 1/60) written to 0x00604ab8, rendering
once; (2) render interpolation or accept stepped motion; (3) input edge state (keyboard 1/3 states, mouse
deltas from 0x004f83c0, which divides by dt) to be polled once per real frame and consumed by the first
substep only (guess: otherwise presses repeat); (4) audio update once per frame. Simplest faithful fix for
(b): cap simulation dt at >= 1/60 by running at most 60 Hz updates. Needs the update/render split of
0x004e41e0's game vt +8 recovered first.

## 6. Subtitles

- Options: m_Subtitles (game+0x176, movies) and m_SubtitlesGame (game+0x177, in game), menu 0x00491b70 cases
  0x25/0x26. Only +0x176 has a reader (0x00493440, byte read at 0x004934fb); a scan of all functions found no
  read of +0x177 (guess: the in-game option has no effect on PC; open question).
- Drawing: 0x00493440 (called from 0x00415bb0, a movie/cutscene screen) -> **0x00495cb0**(subs, movieTime,
  enabled): binary search 0x00495c50 over 16-byte entries {start, end, style, textId}; text from TextData
  0x00495f00; style table **0x005cadd8**, stride 0x18: {name, RGB colour, flags (bit 0 = shown even when
  subtitles are off), size 1.1, x 0.5, y 0.15}; styles are per speaker ("Roman", "Rome", "Q", "Rexus", ...).
  Size goes through 0x00465720 (fits the text to width * size / 640 by bisection), then 0x00495e80 draws with
  renderer vt +0x1c8 (text draw), centred (0.5, 0.5), flag 2.
- **Recommended**: size option = scale the style table size field (+0xc) at start-up, or hook 0x00465720's
  result. Background = hook 0x00495e80: draw a translucent screen quad behind the text bounds before the
  renderer +0x1c8 call (bounds from 0x00495fa0, guess: the text measure function).
