# `*_big13_font.big`: bitmap font

Magic `0x0ADDBEEF`. Module: `tools/formats/big_kinds/font.py`. Roundtrip: 25 ok / 25 (2026-10-03).
The file is byte-identical in all 25 levels (MD5 compared).

## How the game reads it

- Loader `0x004888b0`, called by the dispatcher `0x00449e80` with the font record `g_game+0xa78`. It reads
  0x20 bytes (the texture name) onto the stack. It then allocates `size - 0x20` bytes ("Font Data", record +4)
  and reads the rest of the file into it.
- It looks the texture name up in the texture bank with `0x004881c0` (hash lookup, see `big-txtr.md`) and keeps
  the texture in record +8.
- If the texture was found, it calls renderer virtual 1 (`vtable 0x00589990` + 4, `0x00506c90`) with the
  record. Otherwise `0x00488880` calls the same virtual with a file name (`0x0059da34`), which is the fallback
  path. `0x00506c90` is not in the Ghidra function list; I read it with capstone.
- `0x00506c90` checks `data[0] == 4` and keeps pointers and fields in the renderer (+0x5dc to +0x5f0).
- The glyph users are `0x0050705a` (text width: adds `advance + x_offset` per character) and `0x005078c0`
  (glyph quad: draws from `x_offset`, `width + 1` pixels wide, with the uv scaled by `texture_width - 1` and
  `texture_height - 1`).

## Layout (payload offsets)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | char[32] | Texture name (`mainfontid`), NUL-padded | 0x004888b0 reads 0x20 bytes, looks the name up with 0x004881c0 |
| 0x20 | u32 | Version, must be 4 | 0x00506e32 `cmp [esi], 4`; logs `0x5896c0` otherwise |
| 0x24 | u32 | Line height in pixels (24) | renderer +0x5dc; 0x00507033 uses `(h + 1) * scale` |
| 0x28 | u32 | Texture width (256) | renderer +0x5e0; 0x00507ab7 |
| 0x2c | u32 | Texture height (512) | renderer +0x5e4; 0x00507b13 |
| 0x30 | u32 | Unknown (16). Not read by 0x00506c90 | 0x00506e46 to 0x00506e7a skip it |
| 0x34 | u16 | First character (0x20) | renderer +0x5e8; 0x0050791d range check |
| 0x36 | u16 | Last character (0xFC) | renderer +0x5ea; 0x00507964 range check |
| 0x38 | u32 | Glyph count (221 = last - first + 1). Not read by the game | glyphs start at data + 0x1c (0x00506e70) |
| 0x3c | 24 bytes x count | Glyphs, one per character from first to last | 0x005079d3: `(c - first) * 24` |

Glyph (24 bytes):

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | f32 x 4 | u0, v0, u1, v1 in 0..1. Pixel = uv * (texture size - 1); for example 2/255 | 0x00507b13 to 0x00507b5a |
| 0x10 | i16 | x_offset: pixels before the glyph | 0x005079e8, and added into the width at 0x0050707a |
| 0x12 | i16 | width: glyph width in pixels minus 1 (the quad is `width + 1` wide) | 0x005079f0, 0x00507a16 `fild; fadd 1.0` |
| 0x14 | i16 | advance: pixels after the glyph | 0x0050707a, 0x00507a3e |
| 0x16 | u16 | Padding, 0xBAAD in every glyph | constant in the retail file |

## Dump

`font.json` holds:

- **texture:** the texture name. `texture_fill` is written only if the name field had bytes after its NUL;
  the retail file has none.
- **version, line_height, texture_width, texture_height, unknown_10 and first_char:** stored as read.
- **glyphs:** one line per character in order: `code` and `char` (cp1252, informational, ignored on rebuild),
  `uv`, `x_offset`, `width` and `advance`. `pad` is written only when it is not 0xBAAD.

Floats are written as the shortest decimal that packs to the same f32, so an unedited dump rebuilds exactly.

## Safe edits

- **Recomputed on rebuild:** last character and glyph count, from `first_char` and the length of the list.
- **Glyph metrics and uv:** change freely. To use a new atlas, also replace `mainfontid` in the txtr bank
  (`big-txtr.md`). If its size changes, update `texture_width` and `texture_height`: the game uses them only
  to snap uv to pixels.
- **Adding characters:** append glyphs to extend past 0xFC, up to 0xFF for single-byte text. Change
  `first_char` to start earlier, then prepend glyphs.
- **Renaming the texture:** the name must exist in the level's txtr bank. Otherwise the game takes the
  fallback path `0x00488880`.
- **Unsafe:** `version` other than 4. The font is then rejected.

## Open questions

- What does `unknown_10` (16) do? `0x00506c90` does not read it. It may be a point size used by the original
  tool.
- Line height 24 against a glyph cell of 25 pixels (v span 25/511): the `+1` at 0x00507033 suggests the
  stored value is "cell - 1", like `width`.
