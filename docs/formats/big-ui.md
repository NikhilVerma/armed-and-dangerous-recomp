# `*_big12_ui.big`: user interface layout and cutscene subtitles

Magic `0x00B0D1E5`. Module: `tools/formats/big_kinds/ui.py`. Roundtrip: 25 ok / 25 (2026-10-03).
The file is byte-identical in all 25 levels (MD5 compared).

## How the game reads it

- Loader `0x00462950` (dispatcher `0x00449e80`, object `g_game+0xa70`) allocates the payload ("ui data") and
  reads it into ui +0x48. It parses nothing.
- `0x004629b0` builds the UI from that buffer:
  - It keeps four pointer lists of the name tables: "plnames list" +0x84, "refnames list" +0x88,
    "objnames list" +0x8c and "txtnames list" +0x90.
  - It spawns page objects and object instances through `0x0045f020`.
  - It spawns `ui_text` objects for the text instances.
  - It hands the subtitle tables to `0x00495ae0`.
- `0x00495ae0` fixes the subtitle tables up in place, once per buffer:
  - Each movie's `first_line` becomes a pointer.
  - Each line's speaker index becomes an index into the speaker table at `0x005cadd8` (24-byte entries, first
    name "Roman"), matched with `_stricmp`.
  - Each line's text index becomes a pointer to the text name.

## Layout (payload offsets; all integers u32)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 x 4 | Counts of pages (24), refs (77), objects (68) and texts (1107) | 0x004629b0 allocates the four lists |
| 0x10 | u32 | Page instance count (24) | loop at `puVar15[4]` |
| 0x14 | u32 | Object instance count (97) | loop at `puVar15[5]` |
| 0x18 | u32 | Text instance count (203) | loop at `puVar15[6]` |
| 0x1c | u32 | Movie count (22) | passed to 0x00495ae0 |
| 0x20 | u32 | Subtitle line count (958) = sum of the movies' line counts. Not read | 0x004629b0 skips it (`puVar15 + 9`) |
| 0x24 | char[32] x n | pages (`pui_main`, ...) | "plnames list" |
| | char[32] x n | refs (`ref_crosshair`, ...) | "refnames list" |
| | char[32] x n | objects (`ui_mouse_cursor`, ...) | "objnames list" |
| | char[32] x n | texts (`ui_teammate_defend_player`, speaker names, subtitle text ids) | "txtnames list" |
| | u32 x n | page instances: index into pages. Each spawns that object | 0x0045abe0 / 0x0045f020 call in the first loop |
| | 32 bytes x n | object instances, see below | second loop |
| | 40 bytes x n | text instances, see below | third loop |
| | 40 bytes x n | movies: `char name[32]`, `u32 line_count`, `u32 first_line` | 0x00495ae0: +0x20 count, +0x24 first |
| | 16 bytes x n | subtitle lines: `f32 start`, `f32 end`, `u32 speaker` (texts index), `u32 text` (texts index) | 0x00495ae0: `[edi-4]`, `[edi]` at +8/+0xc |

Object instance (8 x u32):

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| 0x00 | object: index into objects | `objnames[r[0]]` passed to 0x0045f020 |
| 0x04 | field_46c, stored to object +0x46c. Values 0..23; guess: page index | 0x004629b0 |
| 0x08 | field_468, stored to object +0x468. Guess: element id within the UI | 0x004629b0 |
| 0x0c | fields_478[5], stored to object +0x478..+0x488. -1 = none. Guess: own id and navigation links (ids such as 10, 11, 12 chain the `ui_slot` items) | 0x004629b0 |

Text instance (10 x u32):

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| 0x00 | text: index into texts. Also the text id passed to `0x00465850` | 0x00463524 |
| 0x04 | field_46c, as for objects | |
| 0x08 | field_468, as for objects | |
| 0x0c | f32 scale (0.9, 1.2, ...), stored to +0x4c8 and +0x4cc by `0x004659b0` | 0x0046351a |
| 0x10 | colour, stored to +0x4e4 by `0x004659d0`. Looks like 0xAARRGGBB with alpha usually 0 (0x00FFFF80, 0x0AFFFFFF) | 0x00463536 |
| 0x14 | fields_478[5], as for objects | |

Name fields are NUL-terminated in 32 bytes. Bytes after the NUL are stale data from the original tool
(`"ObjectRef xx_ui ..."` in 23 of the 24 page names and 3 of the text names). The game never reads them.

## Dump

`ui.json` holds:

- **pages, refs, objects and texts:** the four name lists, in stored order.
- **name_fill:** per list, by index, the full original 32 bytes of each name with stale bytes after its NUL.
  On rebuild the name is written, then a NUL, then the remaining bytes of the fill. An edited name keeps the
  rest of the stale bytes.
- **page_instances:** page names.
- **object_instances:** `object`, `field_46c`, `field_468` and `fields_478`, as signed integers.
- **text_instances:** `text`, `field_46c`, `field_468`, `scale`, `colour` and `fields_478`.
- **subtitles:** one entry per movie (`movie`, optional `name_fill`, `lines`). Each line has `start`, `end`,
  `speaker` and `text`.

Every index into a name list is written as the name, or as an integer index if the name is not unique in its
list. Every name is unique in the retail file.

## Safe edits

- **Recomputed on rebuild:** all nine header counts, every index into a name list (looked up from the names,
  case-insensitively), each movie's `first_line`, and the total line count.
- **Subtitles:** retime them, change their text id or speaker, or add or remove lines and movies.
  - The speaker must be a name in the texts list. To get a voice or colour it must also be in the game's
    speaker table at `0x005cadd8`. An unknown speaker resolves to the index past the end of that table.
  - The text id must exist in the text bank (`text` kind) to show anything.
- **Instances:** change their fields, or add instances that reference existing names. A reference to a name
  that is not in its list is an error that names the list.
- **Adding names:** add to the end of a list. Appending is the safest option, because the ref list and probably
  the others are also indexed by code outside this file (see open questions).
- **Unsafe:** renaming or reordering names. Code looks up UI elements by name (`ui_crosshair`, `pui_main`,
  `ui_mouse_cursor`, `ui_select_*` in 0x004629b0), and objects are found by name in the object bank
  (0x0045abe0). Names are at most 31 bytes.

## Open questions

- What do field_46c, field_468 and fields_478 mean? The names are offsets, not meanings. The readers of object
  +0x468..+0x488 are not traced yet.
- The 77 `ref_*` names are not referenced inside this file. Who reads ui +0x88?
- Are `pages` indices used anywhere other than `page_instances`?
- The speaker table at 0x005cadd8 has 24-byte entries. What are the other fields (colour, voice)?
