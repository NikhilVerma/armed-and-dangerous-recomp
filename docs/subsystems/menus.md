# Front end, pause menu and options

Status: 2026-10-04, static reconnaissance only (no runs). Every function named here is S0. Sources: Ghidra exports
in `work/decomp/`, `tools/recover.py --asm`, raw image reads (`tools/reginputs.py` IMG), `work/options/*.opt`,
and dumps of `lev_1_m1_big12_ui.big` / `lev_1_m1_big01_text.big` made with `tools/formats/bigtool.py`.
Text marked **guess** is not proven.

## 1. Options

### Storage

The options live inside the Game object (`g_game` = `[0x005fe490]`), from +0x174 to +0x4c8 (0xd5 dwords).
`0x00401a80` zeroes that block, then registers each field with a default. Game init `0x004011e0` calls it.

| Offset | Type | Name (registry string) | Default | Used by |
| ---: | --- | --- | --- | --- |
| +0x176 | bool | m_Subtitles (movies) | `0x00447630()` (guess: language-dependent) | sfx code 37 |
| +0x177 | bool | m_SubtitlesGame | `0x00447630()` | sfx code 38 |
| +0x1f8 | blob 0x14 | m_numUnlocked | 0 | |
| +0x20c | blob 0x280 | m_unlocked | 0 | |
| +0x48c | int | m_cheatsKnown | 0 | |
| +0x490 | float | m_soundVolume | 0.8 | sfx code 56 |
| +0x494 | float | m_voiceVolume | 0.8 | sfx code 58 |
| +0x498 | float | m_musicVolume | 0.8 | sfx code 57 |
| +0x49c | int | m_textureQuality | 2 | gfx code 81 |
| +0x4a0 | int | m_textureLod | 4 | gfx code 49 |
| +0x4a4 | int | m_modelLod | 4 | gfx code 50 |
| +0x4a8 | u32 | m_resolution: `(bpp!=16)<<31 \| height<<16 \| width` (`0x00470c90`) | 640x480x32 | gfx code 48 |
| +0x4ac | int | m_maxTextures | 4 | gfx code 80 |
| +0x4b0 | float | m_drawDistance | 1.0 | gfx code 82 |
| +0x4b4 | float | m_brightness | 1.5 | gfx code 47 |
| +0x4b8 | float | m_mouseSensitivity | 0.5 | controls code 68 |
| +0x4bc | bool | m_enablePostFx (label "ui_gfxmenu_blur") | 0 | gfx code 51 |
| +0x4bd | bool | m_enableBumpMapping | 1 | gfx code 53 |
| +0x4be | bool | m_enableShadows | 1 | gfx code 55 |
| +0x4bf | bool | m_enableDecals | 1 | gfx code 59 |
| +0x4c0 | bool | m_enablePixelShaders | 1 | gfx code 60 |
| +0x4c1 | bool | m_skinHQ | 1 | gfx code 77 |
| +0x4c2 | bool | m_autoAim | 0 | controls code 79 |
| +0x4c3 | bool | dirty: save needed (not saved) | | `0x00401e60` |
| +0x4c4 | bool | options.opt was missing or unreadable (not saved) | | `0x00401a80` |

`&m_control->m_inputActions` (blob 0x118 at `[g_game+0xa80]+0x1c`, callback `0x00401a10`) is saved in the same
file. `0x00401a30` runs earlier with only `m_resolution` registered, so the renderer can be created at the saved
size before the full load.

### Registry and options.opt

- Registry `0x0064a330`: u32 count, then at most 32 entries of 28 bytes (`0x00462110` refuses the 33rd). Entry:
  +0 name hash (`0x004620b0`: case-insensitive, `/`→`\`, rotating XOR), +4 name, +8 field pointer, +0xc default,
  +0x10 type (0 int, 1 float, 2 bool, 3 string, 4 blob), +0x14 size, +0x18 callback(mode, name, type, ptr, size),
  mode 0 = default set, 1 = loaded, 2 = before save. Adders: `0x00462160` bool, `0x00462190` int, `0x004621c0`
  float, `0x004621f0` blob; `0x00462250` writes the default. 24 of 32 slots are used.
- Load `0x00462660`: if "options.opt" exists (`0x0048b210`), read it (`0x0048b1a0`) and parse it (`0x00462310`).
- Save `0x00462610` → serialise `0x004625d0`/`0x00462450` into a 64 KB "Save options temp space" → write
  `0x0048b150`. `0x00401e60` saves only when +0x4c3 is set. Callers: `0x00490d80`, `0x004a0850`.
- Format, checked by decoding `work/options/options-after-1080-runs.opt`: u32 `0x73e594e5`, u32 unpacked size,
  then a zlib stream. Unpacked: `"0tpo"`, then records `u32 hash, u16 type, u16 size, data[size]`. That file
  holds 24 records in registration order; its resolution is `0x843805a0` = 1440x1080x32. Unknown hashes are
  skipped. A type mismatch logs "Option is incorrect type".
- Apply `0x00401ce0` (eax low byte = mark dirty, esi = game): pushes every option to the renderer (+0x38
  vtable: gamma +0x40, bump/pixel shaders +0x154, skin +0x9c, LOD +0x118/+0x11c, max textures +0x94), to the
  sound object (+0x88/+0x8c/+0x90), mouse sensitivity, draw distance (`0x0040f8c0`), shadows and decals
  (`[game+0xa7c]+4/+5`). `0x00401010` re-reads the renderer's mode into m_resolution and sets dirty if it changed.

## 2. Menu system

### Screens ("MenuScreen", 0x68 bytes, game heap)

`0x0048ff90` builds the UI manager (0x360 bytes, stored in `g_game->ui` +0xab4) and registers every screen by name.
The screen constructor is `0x004916e0` (ecx = flags, esi = memory, edi = name; stack: list = ui+0xf8, page name,
float; callee pops). `UI_PushScreen 0x00490b00` (eax = argument, ecx = ui, [screen name]) finds a screen with
`0x00490a80`: it walks `*(ui+0xf8)` through screen+4 comparing screen+0xc, and falls back to the ui+0xf4 list. Back
is `0x00490b60` (ecx = ui).

| Off | Meaning |
| ---: | --- |
| +0 | vtable (default `0x0058fce0`) |
| +4/+8 | next / prev-link in the ui+0xf8 list (`0x004929a0`) |
| +0xc | char[32] screen name ("PauseMenu") |
| +0x2c | char[32] page name ("pui_pausemenu"), matched against the UI bank page list by `0x00463c50` |
| +0x4c | flags: 1 active, 2 visible, 0x20 back is ignored, 0x100 and 0x1000 pause the game (**guess**), 0x800 draws the version string |
| +0x50/+0x54 | timers |
| +0x5c | argument passed on enter |
| +0x60 | item that gets input first (an item with flag 0x10000) |
| +0x64 | focused item (`0x00491b20` sets it) |

Default vtable `0x0058fce0` (9 slots). Subclasses replace a few slots: SaveGame, LoadGame and ChooseGame use
`0x58fc2c`; Saving and QuickSave `0x58fc50`; Loading `0x58fc74`; PubMenu `0x58fc98`; Stats `0x58fb14`; Credits
`0x5b9710`.

| Slot | Default | Role |
| ---: | --- | --- |
| 0 | `0x00494030` | destructor (frees to heap `0x0063a210`) |
| 1 | `0x004929e0` | Update(ui, inputFlags*): mouse hover, gives input to item+0x148, select → slot 6, back → `0x00490b60` |
| 2 | `0x004918b0` | Enter/Leave(argument, bool enter). Enter runs `0x004919c0(screen, 1)` |
| 3 | `0x004928f0` | Show(bool, bool) (**guess**) |
| 5 | `0x00491b70` | **InitItem(item, bool)**, thiscall, callee pops 2 |
| 6 | `0x004921a0` | **Activate(ui, item)**, thiscall, callee pops 2 |
| 7 | `0x004927f0` | Draw overlay |

`0x004919c0` walks every world object in `[g_game+0xa84]+0xeb8`. For each object whose +0x46c equals this
screen's page index, it calls slot 5.

### Items

Every menu item is a world object built from the UI bank (`0x004629b0`). Text instances are spawned with
`0x0045f020(eax = class name or 0, ecx = "ui_text", [objMgr = g_game+0xa84, 0, 0x3ff, 0,0,0,0, 0xaa, 1])`. A
text named `ui_checkbox`, `ui_progressbar`, `ui_textoption`, ... selects that class through the name→class table
in `0x0045df60`.

| Class (id) | Size | vtable | GetFlags (+0x150) | HandleInput (+0x148) |
| --- | ---: | --- | --- | --- |
| ui_text (0x66) | 0x4f8 | `0x005a6a10` | 2 | `0x00464a10` (navigation only) |
| ui_checkbox (100) | 0x500 | `0x005a7ac8` | 0x12 | `0x0046cbf0` |
| ui_progressbar (0xa2; slider and list) | 0x5b0 | `0x005a2fc8` | 0x22 | `0x0046d290` |
| ui_slider (99) | 0x50c | `0x005ab290` | | `0x0046ca60` |
| ui_textoption (0xa7) | 0x588 | `0x005af768` | 0x402 | |
| ui_slot (0x80) | 0x4ac | `0x005a22e8` | | |

Common item fields:

| Off | Meaning |
| ---: | --- |
| +0x468 | anchor: index into the UI bank ref list (`ref_options_menu` ...). Its position comes from `0x00464b10` |
| +0x46c | page index |
| +0x470 | bit 0 drawn (**guess**) |
| +0x474 | flags: 1 apply options and save on activate, 0x40 disabled, 0x100 selectable, 0x1000 inert, 0x2000 layout dirty |
| +0x478 | id = code×100 + row (`0x00491ae0`). Code 1..99 selects the handler. When GetFlags has bit 2, row×24 px is subtracted from Y |
| +0x47c/+0x480/+0x484/+0x488 | ids of the left / right / up / down neighbours, −1 = none. Used by `0x00464a10`, which skips unselectable items (up to 16 hops) |
| +0x4a0 | char[32] text id. +0x4c0: wchar_t* looked up from it |
| +0x4e8..+0x4f4 | x, y, sx, sy (`0x004659e0`) |

Input bits: 1 up, 2 down, 4 left, 8 right, 0x10 select, 0x20 back.

Item setup functions (called from InitItem):

- **Label:** `0x00465850` (ecx = text id, edi = item, [1]). It copies the id to +0x4a0, sets +0x4c0 =
  `Text_Lookup 0x00495f00(eax = id, ebx = g_game+0xac0, [nullOnFail])` and sets 0x2000.
- **Checkbox:** `0x0046cb60` (eax = item, esi = bool*, [text id, applyNow]); caller pops. Value at +0x4f8, bound
  pointer at +0x4fc. Select toggles both (`0x0046cbf0`).
- **Slider:** `0x0046cce0` (eax = item, ecx = text id, [steps, byte +0x5ac, callback, ctx, min, max, value]);
  caller pops.
- **List:** `0x0046cd50` (eax = item, ecx = text id, [count, byte, callback, ctx, index]); caller pops.
- Progressbar fields: +0x4f8 wchar_t[64] value text (`0x0046d0e0` ebx = item, esi = wstr), +0x584 last index,
  +0x588/+0x58c min/max, +0x598 value, +0x5a0 index, +0x5a4 `cdecl void cb(item, ctx)`, +0x5a8 ctx. Left and right
  step the value and call the callback (`0x0046cfd0` → `0x0046d030`). The list callbacks (`0x00470f70`,
  `0x00470fc0`) store the index and set the value text from "ui_gfxmenu_lod%d" / "ui_gfxmenu_tq%d" via Text_Lookup.
- **Enable/disable:** `0x004927a0` (eax = item, cl = enabled, [?, hide]).

### Example: Enable Decals

1. UI bank page 11 `pui_gfxopt` has the text instance `ui_checkbox` with id 5912, which gives code 59 (0x3b).
2. The Options item "Graphics" has code 52. Activating it calls `UI_PushScreen("GfxOpt")`.
3. On enter, InitItem `0x00491b70` → default → `0x004710d0`:
   - Code 76, the Apply item, copies g_game+0x174..+0x4c8 into the edit copy at `0x005fe6e8`.
   - Code 59 calls `0x0046cb60(item, &copy.enableDecals = 0x005fea33, "ui_gfxmenu_decals", 0)`.
4. Select toggles the copy. Apply then calls Activate `0x004921a0` → default → `0x004714c0` case 0x4c, which copies
   the copy into g_game, calls `0x00401ce0(dirty = 1)` and goes back.
5. Cancel (code 78) goes back and calls `0x00401ce0(0)`. This undoes the live brightness preview.

The Sound page binds g_game fields directly, with applyNow = 1.

Codes found in the UI bank: 1-3, 5-8, 10-13, 15, 16, 18, 19, 22-25, 27-30, 34-38, 41-60, 64-82, 99. None of the
handlers (`0x00491b70`, `0x004710d0`, `0x004921a0`, `0x004714c0`) has a case for **83-98**. An unknown code reaches
the `0x004710d0` default, which disables and hides the item.

## 3. Adding a "Remaster" page (proposal)

Text banks hold display strings (`ui_gfxmenu_decals` = "Enable Decals"). Spawning an item with an unknown text id
gives the string "Text lookup failure".

1. **Strings.** Hook `Text_Lookup 0x00495f00` (eax = id, ebx = bank, [bool], callee pops 4; ABI confirmed). Return
   our own `wchar_t*` for ids with a private prefix (e.g. `and_`) and forward everything else. A list item's
   value text can be written straight into +0x4f8 with `0x0046d0e0`.
2. **Screen.** After `0x0048ff90` returns (hook it, or hook `0x004011e0`):
   - Allocate 0x68 bytes from the game heap (`0x004e07f0`: ecx = `0x0063a210`, [0x10, size, 2, tag]).
   - Call `0x004916e0(ecx = 0, esi = mem, edi = "Remaster", [ui+0xf8, page name, 0.0f])`.
   - Point +0 at our own 9-slot vtable: a copy of `0x0058fce0` with slots 5 and 6 replaced.
   - Use the page `pui_directorselect`. It is index 19, and no code refers to its name: an image search found no
     xref. Its two bank items (codes 15, 16) can be hidden with `0x004927a0`. `pui_tcrc42` and `pui_tcrc68` are
     also unreferenced.
3. **Items.** After each `0x004629b0` (it runs on every level load, and the items are world objects):
   - Spawn items as `0x004629b0` does: `0x0045f020("ui_checkbox" | "ui_progressbar" | 0, "ui_text", ...)`.
   - Set +0x46c = page index from `0x00463c50` (ecx = name, ebx = name, edi = `[g_game+0xa70]`).
   - Set +0x468 to a ref index (e.g. 71 `ref_text_05` or 44 `ref_text_04`, as the gfx page uses).
   - Set +0x478 = (83..98)×100 + row, and the +0x47c..+0x488 links.
   - Call `0x004659e0(0.5, 0.5, 0.5, 0.5)`, `0x004659b0(scale)` and `0x00465850(id)`.
   - Our InitItem binds each item to the and.ini values with `0x0046cb60` / `0x0046cce0` / `0x0046cd50`. A
     callback, or our Activate, writes and.ini.
   - Add a "Back" item with code 5. Our slots forward other codes to `0x00491b70` / `0x004921a0`.
4. **Entry point.** Add an item to the Options page (index 5, anchor ref 67, row 13, id 8313 = code 83):
   - Set its links to up 6611 (Cheats) and down 509 (Back), and patch Cheats' down link and Back's up link to 8313.
   - Give the Options screen its own vtable copy as well. On code 83, Activate calls
     `UI_PushScreen(eax = 0, ecx = ui, ["Remaster"])`.
   - No code bytes of shared functions are patched (`PatchU32` is not needed if we swap the screen's vtable
     pointer).
5. **Simpler alternative:** append our items to `pui_gfxopt` with codes 83-98. This needs only the item spawning
   and wrappers on `0x00491b70`/`0x004921a0`. The page may be too full (**guess**, not seen on screen).

Functions worth recovering first: `0x00491b70`, `0x004921a0`, `0x004710d0`, `0x004714c0`, `0x004916e0`,
`0x004919c0`, `0x00465850`, `0x0046cb60`, `0x0046cce0`, `0x0046cd50`, `0x00495f00`, `0x004629b0` (large).

## 4. The remaster engine's menus (engine/src/gamemenu.rs, statscreen.rs, pubscreen.rs; 2026-10-14)

Pages (enum `Page`): main, options, extras, graphics, audio, controls, cheats, credits, yes/no, pause, mission
failed, pub (pubscreen.rs), stats (statscreen.rs). Rows are texts at the bank's points (layout.json; the captures
for the main menu and extras), selectable in up / down order; the selected row white with the pulse (0x00463fa0)
and the highlight fade (+0x4e0, 0.2 per frame: 12 / s) of UiText_Draw 0x00464750.

- **Input** (UiScreen_Update 0x004929e0 through 0x00491140): keyboard, mouse / trackpad and pad; key repeat after
  0.25 s, then every 0.1 s (0x00491090, input.rs). The mouse selects the row under it when it moves (with the move
  sound) and a click activates it; a row's hit box is its text bounds plus 32 units in x and 4 in y (UiText_SetBounds
  0x004655b0), joined with a stepper's gauge and value or a checkbox's box; of several, the nearest row (the
  hook/enhance rule). A click (or drag) on a stepper's track sets its value (Stepper_HandleInput 0x0046d290). Pad:
  D-pad / left stick navigate, Cross / A selects, Circle / B and Options go back.
- **Screen fades** (0x00490cb0 / 0x00490e90 / 0x00491670): a page change fades the old page to black over 0.5 s
  (alpha t^3), switches, and fades the new one in over 0.25 s; no input meanwhile. Instant when both the old and the
  new screen (or none) have flag 4 (pause menu, mission failed), and (remaster) every change into or out of play.
- **Steppers** (ui_progressbar, Stepper_Setup 0x0046cce0, gauge 0x0046d130): label centred on the item's point and
  fitted to 144 units, gauge from +77 to +275 (frame 0xff204000, 18 high), fill from +80 up to 192 wide (0xff40c040,
  12 high), a tick above and below the fill at each position, the value from +288. Ranges (0x004710d0,
  0x00491b70): brightness 1.0..2.0 in 11 positions, draw distance 0.25..1.0 in 16, mouse scale 0..1 in 21, the three
  volumes 0..1 in 21; resolution the remaster's window sizes. The remaster's fixed options (texture quality, texture
  LOD, model LOD: no meaning in the new renderer) are drawn greyed at their best value.
- **Checkboxes** (CheckBox_Layout 0x0046cc80): label centred, fitted to 192, the box at -128..-112.
- **Cheats** (pui_cheats): the code box (ui_cheatbox) shows the typed code, then the result (Cheats_Check
  0x004107c0 writes the box's text); the grid of known cheats (ui_cheatdesc, 0x00410be0 / 0x00410c20 / 0x00410b20):
  three columns 160 units apart, rows 40 apart from ref_info - 24, shown when known, active or with List Cheats on,
  greyed unless active; a click on a name enters its code (Cheats_Check state 1). The remaster's earlier "List
  Cheats" text of codes is gone (the grid is the original's list).
- **Controls page** (2026-10-18, bindings.rs): the key binding entries of the device in use (keyboard and mouse:
  Move Forward .. Look Down, Fire, Fire Special, Zoom, Reload, Jump, Next Weapon / Special, Send / Recall Mates,
  Activate, rows 26 apart in two columns; the pad: its button actions, with the sticks' Move / Look fixed), each
  showing "%s or %s" (pms_input_aorb), one input or "Unassigned" (0x00447e70). Choosing one (Enter, a click or tap, the
  pad's A) shows "Press Key To Assign" (UI_ShowAssignKeyPrompt 0x00447f50); once every input is up the next press of
  that device is bound by the original's rule (0x004481b0 / 0x00447fc0: taken off every other action; an action's own
  key unbinds it; an empty slot takes it, else the oldest goes), Escape (the pad's Options) cancels, and the press is
  consumed (0x00414680). Keys keep two slots, pad buttons three. Saved in options.json `bindings`; Reset Controls
  restores the defaults (0x004144a0). The menus keep their fixed keys and Pause stays on Escape / Options, so no
  binding can lock the player out. Prompts name the binding last pressed for the action (input.rs `key`).
  "Invert mouse" has a checkbox (the original's code 13 is a plain toggle text). Test engine/playtests/controls_rebind.json.
- **Pause / yes-no / failed / stats:** as before (statscreen.rs: 0x00494440 / 0x00494050 / 0x004948d0). The rows that
  load the saved game (main menu Resume Game, pause Quick Load, failed Restart from last save) are greyed and skipped
  without one (0x004a2c10 / 0x004a2e90, as orig_ui_menu-800.png and orig_ui_play-720.png show). Save Game stays
  enabled (the remaster saves anywhere; the original only at checkpoints, Game+0x568 bit 8).
- **Save / load screens** (2026-10-15, feedback 26; gamemenu.rs slot pages, saveslots.rs, savegame.rs): pause Save
  Game opens the SaveGame screen, main menu Resume Game the LoadGame screen (pui_savegame: six slots in a 3 x 2 grid,
  0x004a3260; each slot's panel, name, level, difficulty, local file date, tokens, 0x004a3020 / 0x004a33d0; the
  blinking prompt of table 0x005fb920 with the device's Select button; overwrite / delete prompts, 0x004a36f0; the
  nav links of layout.json in four directions; the panel is the mouse's hit box). Saving returns to the pause menu
  (0x004a3a80). Files saves/game1..6.json; Quick Load and Restart from last save load the newest save (quick save or
  slot). Remaster additions: Delete (Delete / Backspace / pad West) on any saved slot, the load screen's Continue
  (newest save). The thumbnail (thumbnail.rs, 0x0044f3a0): the game camera's view at 128 x 76 kept as gameN.png
  beside the save, drawn 70 below the slot's top (0x004a33d0). Not ported: where the screen title is drawn (guess:
  above the slots). Tests engine/playtests/save_thumbnail.json and Test
  engine/playtests/save_slots.json.
- **Main menu:** the version text "v1.00" bottom right (0x004927f0, screen flag 0x800).
- **Extras** (2026-10-16; gamemenu.rs `extras_pages`, extras.rs; campaign.md "Front end"): Play Cutscene and Replay
  Mission are enabled by the token unlockables ui_extrasmenu_cutscene / ui_extrasmenu_replay or the AMATINEE /
  MISSIONS cheats (UiScreen_InitItem cases 11 / 41), else greyed as before. Play Cutscene lists the story movies seen
  (category 0; titles "<name>.vid") and plays one (code 12); Replay Mission lists the levels reached (category 1, at
  the mission start) and the bonus levels bought (category 3), and plays one in replay mode (codes 6 / 42; back to
  the title after its Stats). The three pages are built when they open. Not ported: the profile choice before the
  list ("ChooseGame"), the lists' scrolling (the remaster shows them whole; layout a guess: no capture of these
  pages). Test engine/playtests/campaign_extras.json.
- **Pub screen** (pubscreen.rs): the pointer moves the highlight onto an offered weapon's slot or the exit button
  (ui_pub_cursor's exit point 493, 416), a click takes the weapon or leaves (the pub shows the mouse cursor:
  0x004930c0); the exit prompt names Back on the device in use. Credits: a click leaves too. State log `pub`; test
  engine/playtests/pub.json (keyboard and pad).
- State log `menu` (playtests): page, shown page, selected row, fading, the selected stepper's value, cheats shown.
  Tests: engine/playtests/menus.json (every shell page), gfxopts.json, flow.json (pause, yes / no with its fade, the
  pad, mission failed), stats.json.

Replaced by the engine (no port needed): the screen objects and their manager (UI_Init 0x0048ff90, UiScreen_Construct
0x004916e0, the request queue 0x00490ba0 / 0x00490c20 / 0x00490d80 / 0x00491020 / 0x004929a0, show / focus
0x004918b0 / 0x004928f0, selection 0x004919c0 / 0x00491b20 / 0x00491b60, enabling 0x004927a0, the id split
0x00491ae0) are the `GameMenu` page stack and `MenuRow`s; the widget classes' inits and kind bits (CheckBox_Init,
FitText_*, Stepper_SetSteps, 0x0046ce40, ...) are `Spawner`'s rows and parts; the list boxes (0x0046bdd0..0x0046c980)
belong to the level / movie / director select pages (the remaster's Replay Mission and Play Cutscene pages are rows
built from the unlocks, unscrolled: extras.rs; no director page); the save-slot widgets' init and thumbnail
release (0x004a2f70..0x004a2fe0) are the PNG files' (saveslots.rs draw_thumbs); DisplayMode_Pack / Unpack (0x00470c90 / 0x00470cc0) by the window size in
options.json; Text_Lookup (0x00495f00, 0x00495e50) by `Texts`. The movie screens (0x00493150..0x004933e0) are movie.rs /
cutscenes.rs (docs/subsystems/movie.md). The front end's plain screens are splash.rs's (2026-10-18): the timed splash screen 0x00493520 / 0x00493590
(LegalText: b_legal at its 512 x 256 over black for 4.0 s before the intro movies, Back / Select / a click skip it),
the loading screen (LoadingScreen_Draw 0x00449ac0: the ad image, the bar, the text of UI_GetLoadingText 0x00447600
without an image; from the frame a level change is asked for to the end of the settling) and the exit screen
0x00493770 (the menu fades to it and the game quits); the legal and exit screens hold the music (flag 0x2000,
0x004928f0 -> 0x0044f510). Tests legal_splash, loading_screen, exit_screen.

## Open questions

- Slots 4 and 8 (`0x00433e70`) and the meaning of the slot 3 arguments.
- The +0x5ac byte of the progressbar. The slider's step count is passed but `0x0046cf70` clamps it to at least 2.
- Whether `0x004929e0` needs anything else (the ui+0xf4 list, input capture flag 0x80) for a new screen to receive
  input.
- Whether runtime-spawned items survive a level change. **Guess:** they do not, because the world list is rebuilt,
  so they must be spawned again after every `0x004629b0`.
