# `land` big file (magic `0xFADEBACC`)

One file per level (`<level>_big04_land.big`): the landscape mesh and its spatial cell grid. Tool:
`tools/formats/big_kinds/land.py`. Roundtrip: 25 of 25 files byte-identical
(`bigtool.py roundtrip "orig/pc/GameData/**/*_land.big"`).

## How the game reads it

The dispatcher 0x00449e80 calls 0x00448470 `Landscape_Load` (recovered in `archive/src/loader/auto_00410ec0.cpp`) with
the chunk size. It does not use the shared hash-table container (`big-common.md`).

1. It reads a `u32` (the grid header offset), then reads the remaining `size - 4` bytes into one allocation
   ("LandscapeSystem::Load", landscape +0x04 and +0xf00).
2. 0x004eb140 opens the start of that data as an in-place ANM_Skin chunk. 0x004e1db0 finds the chunk header at
   data + `u32 [data+8]`, and 0x004eae90 relocates the object graph with base-relative pointers. The object is
   stored at landscape +0xefc. 0x004eb140 can also load the same layout from a standalone ANM_Skin file, with
   `u32 size` first, so `landscape.anm` has the format of a skin file.
3. It registers the skin (0x0045a860, 50.0f) and builds the model (+0xf4c, 0x004ed410) and the render object (+0x00).
4. It copies header words 1..10 into the landscape. For each mesh it points "Grid Array" (+0xef4) and
   "Grid Indices Array" (+0xef8) into the data. The mesh count comes from skin object +0x28.
5. It calls Grid_Init 0x0048e870 with the bounds and the cell size, and then calls `Landscape_BuildRegions`
   0x00448870.

## Layout

The payload follows the 8-byte `.big` header. Offsets in the "grid" rows are relative to `G` = `4 + skin_size`.

| Offset | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| 0 | u32 | `skin_size`: offset of the grid header within the data (data starts at payload +4) | 0x00448470 reads it as the header offset |
| 4 | bytes[skin_size] | ANM_Skin chunk, loaded in place | 0x004eb140, 0x004e1db0, 0x004eae90 |
| 4 + 0 | u32 | skin size (equals `skin_size` in all 25 files) | the file path of 0x004eb140 reads 12 bytes and allocates this size |
| 4 + 8 | u32 | offset of the skin's 0x30-byte chunk header (always `skin_size - 0x30`) | 0x004e1db0: `header = base + [base+8]` |
| header +0x20 | u32 | offset of the skin object | 0x004eb140: `*(chunk+0x20)` |
| object +0x28 | u32 | mesh count (1 to 3), which is also the number of grids below | 0x00448470 loop bound; matches in all 25 files |
| object +0x34 | char[] | name, `***landscape***` in all 25 files | observed |
| G+0x00 | u32 | bytes of the grid section after this word. The loader does not read it; rebuild derives it | observed in all 25 files |
| G+0x04 | u32 | `cells_x` (landscape +0x08) | 0x00448470 |
| G+0x08 | u32 | `cells_y` (landscape +0x0c) | 0x00448470 |
| G+0x0c | u32 | `cells_z` (landscape +0x10), 1 in all files | 0x00448470 |
| G+0x10 | f32 x3 | min x, min y, min z (landscape +0x14, +0x1c, +0x24) | 0x00448470 and the region init in 0x00448790 (min x minus margin) |
| G+0x1c | f32 x3 | max x, max y, max z (landscape +0x18, +0x20, +0x28) | same |
| G+0x28 | f32 | `cell_size` (landscape +0x2c), from 150 to 400 | passed as arg1 to Grid_Init 0x0048e870, which stores it and `k / cell_size` |
| G+0x2c | per mesh: `{u32 first, u32 count}[cx*cy*cz]` | cell table. Cell (x,y,z) is at index `(cy*z + y)*cx + x` | 0x00448870 |
| | `u32[sum of counts]` | primitive indices into the mesh's primitive array (mesh +0x84, stride 0x2c) | 0x00448870; the next mesh starts at `indices + last.first + last.count` (0x00448470) |

Observations from all 25 files:
- `first` is always the running sum of the earlier counts.
- The grid ends exactly at the end of the payload.
- `cells_x = floor((max_x - min_x) / cell_size) + 1`, and the same holds for y. This is observed, not confirmed in
  code. The tool stores the values rather than deriving them.

## Dump contents

- **`landscape.anm`:** the ANM_Skin chunk, byte for byte, containing the meshes, materials and texture paths. It
  stays opaque in this tool. Its serialisation is a relocatable object graph (0x004eae90, `Reloc_*`). It is
  shared with skin entries, so it should be decoded once for all kinds.
- **`grid.json`:** contains:
  - `skin_name` (information only, ignored on rebuild)
  - `cells_x`, `cells_y`, `cells_z`
  - `min` and `max` (`[x, y, z]`)
  - `cell_size`
  - `meshes[m].cells[z][y][x]`: the list of primitive indices for that cell. Each row of x cells is on one line.

Floats are written as the exact decimal value of the f32, so they round-trip bit-exactly.

## Rebuild

Rebuild recomputes the leading `skin_size`, the grid byte-size word, and every cell's `first` and `count`. Rebuild
checks that:
- the skin's own size word equals the size of `landscape.anm`
- the number of meshes in `grid.json` equals the skin's mesh count (object +0x28)
- every `cells` array has the shape `[cells_z][cells_y][cells_x]`

## Safe edits

**Safe:**
- Add, remove or reorder primitive indices in any cell. The lists can change length.
- Change `min`, `max` and `cell_size`, together with `cells_x/y/z` and a matching cells array. Keep
  `cells_x = floor(extent / cell_size) + 1` so that the runtime Grid_Init grid and this cell table agree. The tool
  does not enforce that relation.
- Replace `landscape.anm` with another ANM_Skin whose size word is correct. The mesh count must match `grid.json`.

**Not safe, and not checked by the tool:**
- Indices that are out of range for the mesh's primitive count. The primitive count lives inside the skin and is
  not decoded here.
- Editing bytes inside `landscape.anm` by hand, apart from replacing strings in place without changing their
  length.

## Open questions

- The ANM_Skin object layout beyond +0x28 and +0x34, and the type and element tables walked by 0x004eae90 and
  0x004eaaa0. Decode this once for the skin, obj and land kinds.
- Is `cells_x = floor(extent / cell_size) + 1` the original tool's rule, or a coincidence of the data?
- What does 0x0048e870 Grid_Init build from the bounds and the cell size? It seems to be a second, runtime grid
  (`k` = float 0x005bcb2c).
- The grid byte-size word (G+0x00) is never read by `Landscape_Load`. Does anything else read it?
