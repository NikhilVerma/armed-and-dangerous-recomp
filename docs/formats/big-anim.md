# `.big` kind `anim`: animation bank (ANM_Anim chunks)

Status: 2026-10-03. Module: `tools/formats/big_kinds/anim.py`, which uses `objchunk.py` (shared with `skin`) and
`common.py` (docs/formats/big-common.md).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_anim.big"` ->
25 ok. Files are `<level>_big03_anim.big`, magic `0x00C0FFEE`. The loader is **0x0045b430
ObjectSystem_LoadAnims** (`src/loader/auto_00410ec0.cpp`). There are 13878 animations over the 25 files
(601 in lev_1_m1).

## 1. Payload layout

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0 | u32 + block | Name hash table (big-common.md). V = 4, value = anim index, key = anim name (see below) | 0x0045b430 reads the size and the block, then 0x004f1220. The compare is 0x0045b200 |
| T | u32 | Anim count (equals the hash entry count) | 0x0045b430 -> system+0xe20 |
| T+4 | chunks | count x chunk, **back to back** with no padding, up to the end of the payload | 0x0045b430: `cursor += [anim+8]->size`. 0x004e8430 reads 12 bytes, then size - 12 |

The loader allocates the rest of the payload as "anim image data" (system+0xe30). For each anim, 0x004e8430
copies the chunk from the stream to the cursor and calls 0x004e84e0. That function calls 0x004e1db0
(container), then 0x004ef620 (relocate the anim), then sets root+8 = chunk and u16 root+6 = 0 (1 when the
chunk came from a file). The root pointers go to the "anim list" (system+0xe2c).

**Name and key:** these work exactly as for skins (docs/formats/big-skin.md 2.4). The root holds the source
path at +0x34, e.g. `C:\dev\intermediate\common\anm\captain_run.ANM`; 7442 of the 13878 paths lack `C:`.
0x0045b200 compares `_strnicmp(key, stem, len(stem) - 4)`. Rebuilding the hash table from those names
reproduces all 25 tables exactly.

## 2. Chunk container

An anim chunk has the same layout as a skin chunk; see docs/formats/big-skin.md 2.1-2.3 for the chunk header,
heap blocks, heap header, relocation and dictionaries. Differences measured over all 13878 anim chunks:
- every block is 4-aligned (chunk +4 = 2)
- blocks are back to back, with no gaps

## 3. The anim object graph (as far as the relocation walk shows it)

### 3.1 Root (heap header +0x20). Walk: 0x004ef620

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | u16 | 14 in all anims (guess: version or type) | 0x004ef620 swaps it |
| +0x06 | u16 | 1 on disk. The loader overwrites it | 0x004e84e0 |
| +0x08 | u32 | Stale tool value. Set at load to the chunk address | 0x004e84e0 |
| +0x0C | ptr | Heap header | 0x004ef620 relocates. survey: always the heap header |
| +0x14 | u32 | Start tick (guess). Seen: 160 (9922 anims), 0 (3535), 159, 320, 3200, 9600 | 0x004ef620 swaps it. survey |
| +0x18 | u32 | End tick (guess) | same |
| +0x1C | u32 | Ticks per frame, 160 in all 13878 anims (4800 ticks/s, the 3ds Max tick) | same. survey |
| +0x20 | u32 | Frames per second, 30 in all anims | same. survey |
| +0x24 | ptr | Note track: a typed block of type 11 (3938 anims), or null (9940) | 0x004eeda0 Reloc_FixupTypedBlock. survey |
| +0x28 | ptr | Tree dictionary: name -> node dictionary ("Default Tree" in all anims) | 0x004ef620 relocates, 0x004f0e60. AnimSkinBinding_Create 0x004ed410 looks trees up by name (0x004f0c90) |
| +0x2C | ptr | Default tree (the node dictionary) | 0x004ef620. AnimSkinBinding_Create uses it when no tree name is given |
| +0x30 | u32 | Node count | 0x004ef620 swaps it. survey: equals the node dictionary size |
| +0x34 | char[] | Source path | 0x0045b200 |

The node dictionary maps node (bone) names to node records. Skins bind to these names
(SkinBinding_Create 0x004ed290: skin bone id -> name -> `skeleton+4` dictionary lookup).

### 3.2 Node record. Walk: fn_004ef270 (src/loader/auto_004ef270.cpp)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | u16 | 1 = camera-like node with the extra block below, 2 = attachment/reference node, 0 = plain | fn_004ef270 `word +4 == 1`. Names are survey |
| +0x06 | u16 | Track layout: 0 none, 1 = rotation track only (+0x4c), 2/3 = position, rotation, scale (+0x48/+0x4c/+0x50) | fn_004ef270, fn_004ee830 |
| +0x08 | u16 | Flags (0x2000, 0x200b, 0xe00b, ...) | swapped |
| +0x0C, +0x10, +0x14 | ptr | Root object (+0x0c, survey), and two node links (guess: parent and target) | fn_004ef270 relocates them |
| +0x18 | u32 | 0x80000000 in all nodes | survey |
| +0x1C, +0x28, +0x38 | vec3, vec4, vec3 | Rest transform (guess: position, rotation quaternion, scale) | 0x004ee9c0 / 0x004eea40 swap them |
| +0x48 / +0x4C / +0x50 | ptr | Key blocks: position (type 5 or 6), rotation (12, 13 or 14), scale (6). The roles are inferred from the values | fn_004ef270 -> 0x004eeda0 |
| +0x54..+0xB0 | | Only when +0x04 = 1: matrix +0x54, words +0x98/+0x9a, dwords +0x9c..+0xa4, key blocks +0xa8/+0xac/+0xb0 (camera parameters, guess) | fn_004ef270, 0x004eec50 |

### 3.3 Typed key block. Walk: 0x004eeda0 Reloc_FixupTypedBlock

The block header is:
- `u8 type`
- `u8, u8, u8` (1, 1, 0 for most blocks; 3, 3, 1 and 5, 5, 0 also seen)
- `u32` stale value, replaced at load from the table 0x005cabe0 + type*12 by 0x004ee830
- `u32 count`
- `count` x item pointer

Types 1-3 add one dword after the pointers. Each item is a separate heap block. The item layouts below are
what 0x004eeda0 swaps (dwords, then vectors):

| Type | Item | Seen as |
| ---: | --- | --- |
| 4 / 5 / 6 | 6 dw + 3 vec3 / 2 dw + 3 vec3 / tick + vec3 | position: type 6 in 13161 nodes, type 5 in 395. Scale: type 6 in 198 nodes. Type 6 = `{u32 tick, f32 x, y, z}` |
| 7 / 8 / 9 | 6 dw + 3 vec4 / 2 dw + 2 vec4 / tick + vec4 | not seen at +0x48/+0x4c/+0x50 |
| 12 / 13 / 14 | 6 dw + 3 dw / 2 dw + 2 dw / tick + dw | rotation: type 14 in 214493 nodes, 13 in 9636, 12 in 137. Type 14 = `{u32 tick, u32 packed rotation}`; the packing is not decoded |
| 1 / 2 / 3 | 9 / 5 / 2 dwords | +0x4c of camera nodes: type 3 in 74, type 2 in 13. Type 0 with count 0 also occurs, in 5583 nodes |
| 11 | 3 dwords (tick, ?, count n) + n x {u32, ptr, ptr} | note track at root+0x24. The second pointer is a text note such as `FX_Material FX_fs_captain_run` (footstep effects) |

## 4. Dump

```
anims.json           {"hash_seed", "anims": [{"name", "file", ...informational...}]}   list order = anim index
anims/<name>.anm     the chunk, byte for byte (authoritative)
```

The informational fields per anim are:
- source path, size, block count
- root offset and root words
- `ticks` (start, end, per frame), `fps`, node count
- tree names

Rebuild reads `name` and `file` only. The name must equal the chunk's own name. File names follow the txtr rule.

## 5. Safe edits

- **Replacing an `.anm` with another valid anim chunk of any size** is safe, for example the same anim from
  another level. The hash table, count and offsets are recomputed.
- **Adding, removing and reordering anims** is mechanically consistent. I tested this: I removed 2 anims from
  lev_1_m1, shuffled the rest, added 5 from lev_3_m3, and every lookup still resolved to the right chunk. Whether
  anything stores anim indices (system+0xe2c) is open. ModelSlots_Resolve (docs/formats/big-obj.md) fills
  its matrix from the anim cache.
- **Not supported:** editing keyframes. The chunk is an in-place object graph. There is no writer for it yet,
  and the rotation encoding (types 12-14) is unknown. See docs/formats/big-skin.md 6 for the reasons, which
  apply here too.

## 6. Open questions

- The rotation key encodings (types 12, 13, 14), and the dword fields of types 4, 5, 7, 8, 12 and 13 (likely
  TCB or Bezier parameters).
- Node +0x10/+0x14 links and +0x04 = 2 nodes.
- Root +0x14: start tick, or something else?
- The first pointer in note items (type 11): it points at non-text bytes.
- Are anims referenced by index anywhere (system+0xe2c)?
