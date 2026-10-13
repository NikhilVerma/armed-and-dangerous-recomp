# HUD: how the original draws it, and what the remaster must match

Status 2026-10-13. Static reading of the recovered sources (`src/`), the retail banks of lev_1_m1 and the
`game.unpacked.exe` image; checked against captures (section 9). Nothing here was traced at run time unless a
capture is named. **Guess** marks what is not proven. Companion docs: `ui.md` (coordinate system, draw paths,
anchoring design), `menus.md` (screens, items, options), `docs/formats/big-ui.md`, `big-font.md`, `big-anim.md`.

Export for the remaster engine: `tools/remaster_export_ui.py <level>` writes `work/remaster/<level>/ui/`
(schemas in section 10).

## 1. Where the layout lives

| Piece | Source | Evidence |
| --- | --- | --- |
| Elements (which object/text, page, layout point, nav ids, text scale and colour) | UI bank `big12_ui` | Hud_CreateLevelUi 0x004629b0, `src/ui/ui_01.cpp:2344` |
| Layout points `ref_*` | **Nodes of the page anchor anim** in `big03_anim` (`pui_main`, `pui_pub`, `pui_pressstart`, `pui_general`). Rest position node +0x1c, in 640x480 pixels, origin bottom left, y up | 0x00464b10 `ui_01.cpp:3386`: anchor = page object (list g+0xa84 +0xec0, 0x00463c90), matrix of the ref "bone" from 0x00453c50 (`src/anim/anim_01.cpp:2241`), x = m[3] / 640, y = m[7] / 480 |
| Element graphics | Skin model (LOD 0 of the object's template, `big07_obj`) in `big05_skin`: quads in 640x480 pixels around the object origin | UiMesh_Draw 0x00464e20 `src/renderer/render_r3.cpp:320`: world = rotation * x-aspect * translation * scale(+0x37c/640, +0x380/480, +0x384/480) |
| Textures | `big02_txtr` (stored bottom-up; the export flips them upright) | decoded `b_ad_image` is upside down; meshes carry v = 1 on their top edge |
| Font | `big13_font` metrics + txtr `mainfontid` (256x512) | docs/formats/big-font.md |
| Strings | `big01_text` (`Text_Lookup` 0x00495f00) | UiText_SetName 0x00465850 `src/ui/ui_02.cpp:254` |

All 24 pages, 77 refs, 97 object instances and 203 text instances are identical in every level (big-ui.md).
`ui_text` instances on menu pages get their row subtracted: `y -= (id % 100) * 24` px (UiScreen_InitItem
0x00491b70, `src/ui/ui_03.cpp:1306`).

Which anim a page object poses with is not traced (open question 1). The export uses the anim named as the page
when it exists (`pui_main`, `pui_pub`, `pui_pressstart`), else `pui_general`. With that rule the pause menu items
land exactly where `work/visual/pause_m/00750.bmp` shows them ("Continue" centre at 640x480 y = 300 from the top,
24 px rows), and the HUD matches `work/visual/diag_lev_1_m1/00690.bmp` (a 640x480 capture) to a pixel or two.

## 2. Frame order

1. `0x00402e80` (game update, `src/game/game_01.cpp:640`), when 0x00462790 allows (pause input):
   - `Hud_UpdateCrosshair` 0x00406f50 (`ui_01.cpp:297`): target under the crosshair, crosshair state, marker slots;
   - `0x004915b0`: menu screen stack update;
   - `0x00463fa0` (`ui_01.cpp:3035`): HUD widgets (weapon icons, arrows, pulse phase), section 4;
   - every object's update virtual (slot +0xbc, e.g. 0x00466e70) runs with the other objects (**guess**: in the
     object system update 0x0045da90): the per-element drivers of section 4.
2. `Game_RenderFrame` 0x00403050 (`src/renderer/render_r0.cpp:38`): the 3D scene (ScenePass_Render, which also runs
   the chat update 0x0046a820), then the viewport is restored and **`UiPass_Draw` 0x00464460**
   (`src/game/ui_pass.cpp:53`), then 0x00491670 (menu overlay), then present.
3. `UiPass_Draw`:
   - saves world/view/projection, sets the screen transform 0x004f01c0 (normalised 0..1 space, ui.md section 1);
   - world-tracking markers (list g+0xa84 +0xebc, 0x004190a0): projected 3D positions, no anchoring;
   - every UI object of list g+0xa84 +0xeb8 in list order (creation order = UI bank order: page objects, object
     instances, text instances). Drawn when shown: `+0x474 & 2`, or `+0x470 & 1` (page shown) and
     `+0x474 & 4` (element shown). Hidden while paused (g+0x2684) when `+0x474 & 0x20` (`ui_pass.cpp:98`).
     After each object the screen transform is reset unless its kind bits (virtual +0x150) have bit 1;
   - the list at ui +0x9c (draw virtual +0x138, no visibility test), then the mouse cursor ui +0x54 when byte
     ui +0x50 is set.

Pages: Hud_CreateLevelUi hides every page (0x00463f30) and shows `pui_main` (0x00463e80 with "pui_main", 1).
Menu screens show their own page on enter (0x004918b0 pushes the shown bits, 0x00463df0 restores them). The
HUD page stays shown under the pause menu (capture `pause_m`).

## 3. Drawing primitives

### 3.1 Meshes (UiMesh_Draw 0x00464e20)

- One draw per model part, through the skin batch (0x0048a290), so material/texture/alpha are the skin's.
- Position: the object origin = the ref point. Vertices are 640x480 pixels relative to it (x right, y up).
- X is multiplied by `(GetAspect * 4/3) / (viewport w / h)` unless `+0x474 & 0x10`: 1.0 for a full-screen
  viewport, so the original **stretches the 4:3 layout across a wide screen**.
- z: parts have small negative z (compass -6.6, health -0.2); with the perspective screen projection (w = -z)
  they shrink a little towards the screen centre (ui.md section 1). Visually negligible.
- Animated parts: crosshairs have `<name>_in` / `<name>_out` anims (0x00466650 / 0x004666a0,
  `ui_02.cpp:1119`); meters use "anim 4" (0x00408800) as a 0..1 frame: compass heading, objective arrow
  distance, ticker bomb lock, text box height.

### 3.2 Meters: texture scroll (health, ticker bomb arming)

`DrawSlidePanel` (`src/renderer/render_r6.cpp:1116`), used by 0x00466ce0 / 0x004672d0 / 0x00467dd0: for the part
whose texture slot is `b_health_level1` (health) the texture matrix is translated by `(x, t)` with
`t = clamp((1 - value) * 0.5, 0, 0.5)`; `x` picks the colour band. So the bar empties by **scrolling the texture**,
not by scaling geometry. Health driver 0x00466e70 (`src/game/game_04.cpp:839`):

| `+0x4a8` | Target | Name |
| --- | --- | --- |
| 0 | player (g+0x748) | `ui_player_health` |
| 1 | `*(0x00408020(g+0xa6c)) + 0x2c` (boss/enemy) | `ui_health_enemy` |
| 2 | player +0xad4 (Q) | `ui_health_q` |
| 3 | player +0xad0 (Jonesy) | `ui_health_jonesy` |

value = health / max (+0x454 / +0x458). Colour band x: 0.5 below 25 %, 0.25 below 50 %, else 0. At or below
20 % the element's alpha (+0x378, vt+0x100) pulses: up at 8/s, down at 2/s. Hidden with no target or in a cut
scene (g+0xdce).

### 3.3 Text (UiText_Render 0x00465360 -> UiText_Draw 0x00464750 -> Text_Draw 0x00495e80)

`src/renderer/render_r3.cpp:274` and `:381`, `src/renderer/render_r4.cpp:1244`,
`src/renderer/auto_00507c10.cpp:82`, `src/renderer/auto_00503900.cpp:439`.

- **Position:** the ref point (+0x4e8, +0x4ec, normalised), **alignment 0.5, 0.5** (centred both ways) unless the
  caller passes a non-zero offset.
- **Colour:** `+0x474 & 0x40` disabled `#404040`; `& 0x800` `#7f0000`; `& 0x80` (selected/highlighted) white;
  else the bank colour. Alpha is always forced to 0xff (`colour | 0xff000000`); the bank's alpha byte is unused.
- **Size:** `size = ((pulse - 1) * fade + 1) * scale`, where
  - `scale` = bank text scale (+0x4c8, 0x004659b0), or a fit result (below);
  - `pulse` = ui +0x94 = `|sin(phase) * 0.15| + 1`, `phase += dt * 5` wrapped to (-pi, pi] (0x00463fa0);
  - `fade` (+0x4e0) moves by 0.2 per drawn frame towards 1 while `+0x474 & 0x80` (highlight) and not `& 0x400`,
    else towards 0. So **the selected menu item breathes between 1.00x and 1.15x**; other text does not pulse.
- **Glyph size:** `size * one screen pixel`: pixelW = 1 / viewport width (0x00506f90). The original draws
  text in **physical pixels**; a scale-1.0 line is 25 px tall at every resolution. The 640x480 look is therefore
  `25 * scale` px per line at 480 lines; at 4K the original text is a quarter of that relative size.
- **Shadow:** every `ui_text` has `+0x474 |= 0x200` (init 0x004651a0, `ui_01.cpp:3597`) -> flag 2 -> a black
  copy drawn first, offset +2 px right and 2 px down (screen pixels).
- **Glyph model** (0x005078c0): `left = pen + x_offset * s`, quad `(width + 1) * s` wide and
  `(line_height + 1) * s = 25 s` tall from the pen line top, `pen = left + advance * s`; quads snap to the pixel
  grid. Line advance = 25 s (0x00506ff0).
- **Markup** (renderer virtuals 102-112, `src/renderer/auto_00504b90.cpp:367-470`): `<br>` line break,
  `<rgb=RRGGBB>` ... `</rgb>` colour (e.g. "You need `<rgb=ffff00>`Ticker Bombs!`</rgb>`"), `<tab=N>` (N widths of
  'W'), `<ll=N>` line width; other `<...>` tags are skipped. `\n` also breaks. Multi-line text is aligned per
  line in x and as a block in y.
- **Fitting:** `UiText_FitToBox` 0x004656f0 / `UiText_FitScale` 0x00465720 (`ui_02.cpp:194`, `:210`) shrink the
  scale until the text fits a box given in 640-wide units (converted with the real back-buffer size). Prompts use
  576 x unlimited; subtitles use 576.
- **Background box** (`+0x474 & 0x4000`, 0x00465430): fill ARGB `0xc04080c0`, outline `0xff4080c0`, over the text
  bounds. The mission briefing box is the separate mesh `ui_text_bkd` (texture `b_text_bkd`, 856 x 19.6 px,
  stretched vertically to the text by anim 4 = min(text height / 480, 1), 0x00466470 `ui_02.cpp:1031`).

## 4. HUD elements (page 0 `pui_main`, lev_1_m1)

Positions are the ref points in 640x480, **y up from the bottom** (top-left y = 480 - y). Mesh size = the model's
rectangle around that point. "Anchor" is the remaster's widescreen rule (hook/enhance/ui_aspect.cpp, section 7).

| Element | Kind | Ref | x, y (y up) | Size / look | Shows; driver | Anchor |
| --- | --- | --- | --- | --- | --- | --- |
| `ui_player_health` | mesh `ui_health` | ref_health | 68.4, 90.9 | 45 x 93 px figure `b_health` (scrolling fill) + `b_health_outline` | player health, 3.2; 0x00466e70 | left |
| `ui_health_q`, `ui_health_jonesy` | mesh | ref_health_q / _jonesy | 143.3, 101.4 / 143.5, 68.0 | face + bar | teammate health; 0x00466e70 | left |
| `ui_health_enemy` | mesh | ref_health_enemy | 538.8, 132.1 | bar | boss health | left (ui_aspect lists it left although x is right: check) |
| `ui_weapon_bkd` | mesh | ref_weapon_bkd | 503.5, 83.3 | 163 x 57 px, both panels in one texture | shown with a player; 0x00463fa0 | right |
| `ui_player_rifle` ... `_rlvindaloo` (6) | mesh | ref_weapon | 539.3, 83.3 | 77 x 20 px icon | the current primary weapon (0x004a1660 == index); 0x00463fa0 | right |
| `ui_player_grenades`, `_guyfawkes`, `_bunchoffives`, `_blackhole`, `_topsy` | mesh | ref_grenade | 455.5, 83.3 | 64 x 32 px icon | current special (0x004a1670 == 6 + i); special 10 drives the `on01` lock frame | right |
| `ui_ammo_count` | text 0.8 `#a0ffa0` | ref_ammo_count | 539.3, 68.4 | 20 px line | `"%d/%d"` ammo of the current primary; `"%d/"` when its type is 2; 0x00465a20 `ui_02.cpp:418` | right |
| `ui_ammo_infinity` | mesh | ref_ammo_count + 23 x | 562.3, 68.4 | 18 x 9 px `k_ammo_infinity` | shown for weapon type 2 (rifle in lev_1_m1: "8/ oo") | right |
| `ui_grenade_count` | text 0.8 `#a0ffa0` | ref_grenade_count | 456.1, 68.4 | | `"%d"` special count; 0x00465ad0 | right |
| `ui_select_{up,down,left,right}_{on,off}` | mesh `b_arrow` | ref_weapon_select_* | up 456.1, 106.2; down 456.1, 60.1; left 497.1, 83.3; right 580.3, 83.3 | 41 x 10 / 10 x 41 px | "off" shown with more than one weapon (left/right) or special (up/down); "on" while the cycle button (0x10/0x11, 0xf/0xe) is held; all off in cut scenes; 0x00463fa0 | right |
| `ui_compass` | mesh | ref_compass | 554.0, 384.5 | 74 px dial `b_compass` + face + needle | anim 4 = 1 - (pi - atan2(dir.y, dir.x)) / 2pi (camera heading); 0x00466700 `ui_02.cpp:1150` | right |
| `ui_compass_objective` | mesh | ref_compass_objective | 554.0, 384.5 | 69 px arrow | nearest objective: rotated by target bearing minus camera bearing, anim 4 = 1 - min(dist * 0.002, 1); 0x00466790 | right |
| `ui_crosshair` and 10 per-weapon variants, `ui_crosshair_friendly`, `ui_autoaim` | mesh | ref_crosshair / ref_autoaim | 320, 240 | 14.5 px `b_crosshair` (4 animated corner bones) | chosen by weapon and target (Hud_UpdateCrosshair); `_in`/`_out` anims | centre |
| `ui_hit_top/bottom/left/right` | mesh `b_hit` | ref_hit_* | top 320, 430.4; bottom 320, 50; left 52, 240; right 587.8, 240 | 658 x 43 / 32 x 458 px | shown for g+0xcb8 s when its bit is in the hit mask g+0x26d0; 0x004670f0 `ui_02.cpp:1392` | stretch |
| `ui_multi_lock` x4 | mesh | ref_chat_01..04 (re-placed at run time) | | lock-on marker | world-tracked lock targets; 0x00407c20 | none (screen space) |
| `ui_clock`, `ui_tickerbomb`, `ui_tickerbomb_set` | mesh | ref_clock / ref_tickerbomb / ref_tickerbomb_text | 320, 384.5 / 464.3, 384.5 / 321.6, 329.8 | clock face 70 px; arming bar 351 x 26 px | ticker bomb (`ui_01.cpp:2089`, `:2151`; 0x00467450) | centre / right / centre |
| `ui_text_bkd` + `ui_mission_briefing` (0.9 `#d0d0ff`) + `ui_pause_text` (0.9 `#7070ff`) | mesh + text | ref_mission_hint | 320, 281.1 | 856 px band | objectives on the pause screen (capture `pause_m`) | centre |
| `ui_mission_timer` | text 1.1 `#ffff80` | ref_message_01 | 320, 77.2 | | `"%d:%02d"` of `seconds + 0.9` (0x00465b70; driver `src/game/game_07.cpp:3099`) | centre |
| `ui_bof_hint` | text 1.2 `#a0ffa0` | ref_message_01 | 320, 77.2 | | "Press %s to punch!" | centre |
| Prompts: `ui_tickerbomb_hint/_patience/_none` (1.0 white), `ui_vehicle_entry/_exit`, `ui_pub_entry_hint`, `ui_pub_fail` (1.0 `#a0ffa0`) | text | ref_tickerbomb_text | 321.6, 329.8 | fit to 576 units | context prompts; `%s` = key name (0x00465280); 0x00467450, 0x004662a0, 0x00466210, 0x00467b60 | centre |
| `ui_teammate_*` (4) | text 0.9 `#ffff80` | ref_team_text | 320, 47.5 | | order feedback, 3.0 s, each hides the others; 0x00467890 | left (x is 320: check) |
| `ui_chat_01..05` | text 0.8 | ref_chat_01..05 | 320, 437.3 / 419.9 / 396.9 / 373.9 / 352.1 | | messages (section 5) | left (x is 320: check) |
| `ui_captured_count`, `ui_return_count`, `ui_safe_count`, `ui_grunt_over_wall_count` | text 0.8 (1.5) `#a0ffa0` | ref_captured / return / safe / count | 72.9, 309.4 / 121.4, 309.4 / 167.6, 309.1 / 93.3, 338.6 | | mode counters (g+0x26e8, g+0x26f4; **guess** which is which); 0x00465be0..0x00465ce0 | left |
| `ui_peasant`, `ui_flag_base` | mesh | ref_peasant / ref_base_flag | 121.7, 349.9 / 93.3, 393.8 | icons | peasant carried / flag mode (**guess**) | left |
| `ui_memory_warning` | text 2.0 `#ff4040` | ref_chat_05 | 320, 352.1 | | debug | |
| `ui_mouse_cursor` | mesh `ui_multi_lock` | ref_crosshair | | | menu cursor, hidden in play | |

Weapon icons are found by name through the weapon table g+0x2810 (6 x 0x150) and g+0x2ff0 (5 x 0x150); the
names above are the UI bank objects those tables name (Hud_CreateLevelUi tail, `ui_01.cpp:2525`).

## 5. Messages, prompts and subtitles

- **In-level messages (Chat, `ui_02.cpp:1995-2270`):** `Chat_AddMessage` 0x0046aa60 word-wraps at **490 units**
  (break at the last space, else mid-word, dropping that character: original bug), appends up to 3 lines; the oldest
  line scrolls off after **6.0 s**. Lines are shown in `ui_chat_01..03` (centred at x 320, scale 0.8) with the
  colour table (0x0046a7e0): index 0 black, 1 `#1fffff`, 2 `#f0f01f`, 3 `#ff1fff`, 4 `#1fffff`, 5 `#ff0000`. Text
  bank lines go through it with colour 2 (`src/game/game_09.cpp:1730`).
- **Prompts:** one text at ref_tickerbomb_text, fitted to 576 units, `%s` replaced by the bound key name;
  the per-class drivers re-evaluate visibility each frame (ticker bomb every 6th call).
- **Timed messages:** 0x004677c0 shows for 2.0 s, teammate orders 3.0 s (0x00467890).
- **Message box** (0x00466060): `"%s<br><br>%s"` = message + "press OK" text; `+0x474 & 0x8000` adds draw flag 8.
- **Movie / caption subtitles** (0x00495cb0, `render_r4.cpp:1211`): line by time from the UI bank tracks
  (0x00495c50 binary search); style from the speaker table 0x005cadd8 (24-byte entries, exported as
  `layout.json` `captions`): colour per speaker (Roman `#c0c040`, Q `#40c0c0`, ...), size 1.1 fitted to 576 units,
  centred at **x 0.5, y 0.15** of the screen (y up: the lower part), shadowed. `Info`, `InfoHigh`, `InfoLow`,
  `SmallInfo` (size 1.3 / 1.1 at y 0.5 / 0.7 / 0.3 / 0.5) are always shown; the others only with the subtitles
  option (Game +0x176). The in-game subtitles option +0x177 has no reader in the recovered code yet (open question).

## 6. Pause menu (page 15 `pui_pausemenu`)

Items are `ui_text` at `ref_info` (pui_general: 320, 276.4) minus `row * 24`, scale 1.0, colour `#a0ffa0`,
selected white with the pulse, disabled `#404040`:

| Row | Text | Action (id / 100) | y (y up) |
| ---: | --- | ---: | ---: |
| 4 | Continue | 5 | 180.4 |
| 5 | Save Game | 7 | 156.4 |
| 6 | Quick Load | 34 | 132.4 |
| 7 | Restart the level | 69 | 108.4 |
| 8 | Options | 3 | 84.4 |
| 9 | Abandon Mission | 8 | 60.4 |

Above them the HUD page shows `ui_text_bkd` and `ui_mission_briefing` (title, "OBJECTIVES:", objective lines with
"Not Complete"/"Complete" in red/green, laid out with `<tab>`/`<rgb>` markup, **guess**). Screens, input and
item handlers: menus.md. Yes/No (page 22): message 1.4 white at ref_info (y 276.4), "Yes" 1.1 `#a0ffa0` (row 5,
y 156.4), "No" 1.1 `#ff6060` (row 6, y 132.4).

## 7. The "better" target: the remaster's UI enhancements (hook/enhance)

`[ui]` and `[camera]` in and.ini, all off by default:

- `ui anchor=1` (hook/enhance/ui_aspect.cpp): keeps the 4:3 proportions on wide screens. Per object, the view
  matrix gets `x' = k x + c` with `k = (4/3) / aspect`: HUD elements pinned left (`a = 0`), right (`a = 1 - k`)
  or centred (`(1 - k) / 2`) by their ref (HudAnchor table; unlisted refs by x thirds), `ui_hit_*` stretched,
  menu pages pillarboxed; mouse hits mapped back (g_uiCursorToLayout, g_uiHitScale). The exporter writes this
  choice as each element's `anchor`.
- `ui text_scale=1.0`, `text_relative=1`: text size relative to the 640x480 layout (`height / 480` per glyph
  pixel) instead of physical pixels; fixed-layout text (menus, objectives) is capped at text_scale 1 so lines do
  not leave the 4:3 frame.
- `camera hor_plus=1` (hook/enhance/camera.cpp): Hor+ field of view, so the 3D view widens while the anchored HUD
  keeps its 4:3 shapes.

For the remaster engine: lay out in 640x480 units, scale uniformly by `height / 480`, place each HUD element by
its anchor, keep menus centred at 4:3, size text as `25 * scale * height / 480` px per line, and keep the
shadow at 2 px of 480 (scaled). Fixes worth keeping off by default: the anchors flagged "check" in section 4
(`ref_health_enemy`, chat and team text are centred at x 320 but listed left).

## 8. When elements show, hide and animate (summary)

- No player (g+0x748) or a cut scene/flag g+0x26cc: weapon icons, arrows, panel and infinity hidden (0x00463fa0).
- Cinematic g+0xdce: health meters and progress meters hidden.
- Paused (g+0x2684): objects with `+0x474 & 0x20` hidden (prompts created with 0x00465230 and the classes using
  0x00466580); the weapon-cycle arrows are forced off.
- Crosshair: `_in` / `_out` anims on target acquisition (Hud_UpdateCrosshair); friendly variant over allies.
- Hit flashes: per-side timer g+0xcb8.
- Health at or below 20 %: alpha pulse; colour band changes at 50 % and 25 %.
- Selected menu item: white, size pulse 1.0-1.15 at 5 rad/s with a 0.2-per-frame fade.

## 9. Reference captures (original look)

| Capture | What it shows |
| --- | --- |
| `work/visual/diag_lev_1_m1/00690.bmp` | **640x480, original layout** (anchor is identity at 4:3): health, compass, crosshair, weapon panel "5" / "8/ oo" |
| `work/visual/hud_tex0/00650.bmp` (and `hud_tex1`) | 3840x2160 with `ui anchor=1` (the "better" target): same HUD pinned to the edges |
| `work/visual/pause_m/00750.bmp` | 1280x720 pause menu: mission briefing box, objectives with red "Not Complete", menu items (selected white, disabled grey) |
| `work/visual/pause_click/00700.bmp` .. `01540.bmp` | pause menu sequence (selection changes) |
| `work/visual/menu_m/00690.bmp`, `menu_click*/` | shell main menu (lev_shell), font and highlight colours |

## 10. Export (`tools/remaster_export_ui.py`)

`work/remaster/<level>/ui/`:

- `layout.json`
  - `space`: `{width 640, height 480, origin "bottom-left", y "up"}`.
  - `colours`, `pulse`: the constants of section 3.3.
  - `pages[]`: `{index, name, kind "hud"|"menu", anchor_anims[], refs{name: {x, y, z, source, keyed?}}}`.
  - `models{name: [part]}`, part = `{texture, colour "#rrggbb", rect [x0, y0, x1, y1], uv_rect, z,
    vertices [[x, y, u, v]], triangles [[i, j, k]]}`: pixels around the element origin; uv OpenGL-style
    against the upright PNGs (v = 0 bottom row).
  - `textures{name: {file, width, height, format, alpha, premultiplied_in_bank}}` (DXT2 is un-premultiplied).
  - `elements[]`: common `{id, kind "mesh"|"text", name, page, page_name, ref, ref_source, pos [x, y] (y up),
    pos_top_left, anchor "left"|"right"|"centre"|"stretch", nav {id, left, right, up, down}, shows, driver}`;
    mesh adds `{z, model, anims[]}`; text adds `{align [0.5, 0.5], text_id, string, scale, colour, colour_raw,
    glyph_height_px_at_480, shadow, menu_action, menu_row}` (menu rows already subtracted from `pos`).
  - `captions[]`: `{speaker, colour, always_shown, size, x, y}` (from the exe; null without it).
  - `subtitles[]`: `{movie, lines [{start, end, speaker, text_id, string}]}`.
- `font.json`: `{texture, image, common {line_height 25, line_height_raw 24, base, scale_w 256, scale_h 512,
  first_char 32}, shadow, chars {"<code>": {char, x, y, width, height, xoffset, yoffset, xadvance,
  advance_after_glyph, atlas_width, atlas_height, uv}}}`. `x, y, width, height` are pixels in `font.png` (top-left
  origin, upright); `xadvance = x_offset + advance` (pen advance). Checked by rendering "Storm the Base!" from it.
- `font.png`, `textures/<name>.png`, `ui_bank.json` (raw bank dump).

## Open questions

1. Which anim poses each page object: `pui_mainmenu` and other menu pages name model `pui_pub` in big07_obj,
   yet their refs (`ref_info`, `ref_main_menu`, `ref_options_menu`) exist only in `pui_general` (and
   `ref_main_menu` also in `pui_pressstart`, at a different y). 0x00453c50's fallback through the object system
   bone table (0x0045ace0 / 0x004eb5d0) is the likely path; not traced.
2. Ref nodes all have rest transforms and no keys in lev_1_m1 (`keyed` absent); `ui_*_in/_out` anims animate
   the crosshair bones (not exported as curves; rotation keys are undecoded, big-anim.md).
3. The in-game subtitles option (Game +0x177) has no reader in `src/`; how conversation lines are captioned in
   play is not traced (they may go through Chat).
4. Blend state of UI meshes (alpha test vs blend, additive for `b_hit`?) comes from skin materials and the batch
   drawer; not traced. The export only flags textures with alpha.
5. Mode counters: which of g+0x26e8 / g+0x26f4 / the list sum feeds captured, safe and return.
6. `ui_health_enemy` and the chat/team texts are listed "left" by ui_aspect.cpp but sit at x 539 / 320.
7. The class-to-vtable mapping (name -> driver) is inferred from strings and behaviour, not from the class
   factory (0x0045df60 ids); drivers marked guess in `layout.json` need that check.
