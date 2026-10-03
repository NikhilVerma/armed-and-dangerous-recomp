# `.big` level files

Status: header survey only (2026-10-03). Loader not yet located in Ghidra.

## Layout on disk

Each level folder (`lev_1_m1` ... `lev_5_m5`, `lev_bonus_m1..3`, `lev_shell`: 25 folders) holds one `.jit` file
and 13 `.big` files, numbered in a fixed order:

| # | Suffix | Magic (u32 LE) | Hexspeak | Likely content (guess from name) |
| ---: | --- | --- | --- | --- |
| 01 | `text` | `0xDEADC0DE` | DEAD CODE | Text / strings |
| 02 | `txtr` | `0x0B00B1E5` | | Textures |
| 03 | `anim` | `0x00C0FFEE` | COFFEE | Animation |
| 04 | `land` | `0xFADEBACC` | | Terrain |
| 05 | `skin` | `0x0BADF00D` | BAD FOOD | Skinned meshes |
| 06 | `prop` | `0x00BEADED` | BEADED | Props |
| 07 | `obj` | `0x00BADA55` | BADASS | Objects |
| 08 | `aud` | `0xDEAFDEAF` | DEAF DEAF | Audio |
| 09 | `fx` | `0x00EFFEC5` | EFFECS | Effects |
| 10 | `inst` | `0xBABEBABE` | | Instances / placement |
| 11 | `ff` | `0x00FFDBAC` | | Unknown |
| 12 | `ui` | `0x00B0D1E5` | | User interface |
| 13 | `font` | `0x0ADDBEEF` | | Fonts |

Not related to the Giants `.gzp` archive (`0x6608F101`, `ref/GiantZiP/GZPMain.pas:144`).

## Header

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Magic, per kind (table above) | Same value in all 25 files of each kind |
| 0x04 | u32 | Payload size = file size - 8 | Holds for all 325 files |
| 0x08 | u32 | Unknown (often a small offset or size) | |
| 0x0C | u32 | Unknown, plausibly an entry count (e.g. 0x25F in `lev_1_m1` anim) | guess |
| 0x10 | u32 | Unknown, looks like a hash or checksum | guess |

Many following words are `0xFFFFFFFF`, 0, 1, 2, 3... These look like index lists or null links, which would fit
a load-in-place memory image. That is a guess; confirm against the loader.

## Next

1. Find the loader: references to the magic constants in `.text` (`tools/gh.py`), and callers of
   `filesys\filesys.cpp` functions.
2. Write `tools/formats/big.py` (parse the header, list entries) and `tests/formats/test_big.py` over all 325
   files.
