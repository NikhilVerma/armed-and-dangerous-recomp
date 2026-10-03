# `*_big10_inst.big`: object instances (level placement)

Magic `0xBABEBABE`. Module: `tools/formats/big_kinds/inst.py`. Roundtrip: 25 ok / 25 (2026-10-03).
The 25 files all differ, from 1 instance (`lev_shell`) to about 1500.

## How the game reads it

- **Load:** `0x0045d510` (dispatcher `0x00449e80`, object system `g_game+0xa84`) allocates the payload
  ("instance data") and reads it into object system +0xe5c. It parses nothing; the buffer stays as stored.
- **Spawn all:** `0x0045d930(objsys, pass)`, called from `0x0044a4b0` with `pass = [esi+8]`, walks the groups.
  For each instance with `pass == -1` or bit `pass` set in `mask`, if no live object already has this id
  (`0x0045d570` compares object +0x396), it calls `0x0045d7a0`.
- **Find by id:** `0x0045d750(objsys, id)` walks the same way. The id is 1-based and counts instances over all
  groups in stored order. `0x0048cb30` calls `0x0045d7a0(objsys, id, NULL)`, which looks the record up by id;
  this is a respawn path.
- **Spawn one:** `0x0045d7a0` copies the record to the stack and builds:
  - a 4x4 matrix from the 12 floats, with element [15] = 1.0 and flags |= 0x13;
  - a hash table object around the property block, fixed up with `0x004f1220` and compare callback
    `0x00495ee0`;
  - a pointer array from the offset table: `ptr[i] = data + offset[i]`.

  It then calls `0x0045f020(name, objsys, matrix, mask, 0, table, ptrs, id, 0xaa, 1)`.

## Layout (payload offsets)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Group count (1 in all 25 files) | 0x0045d930, 0x0045d750 outer loop |
| 0x04 | u32 | Instance count of group 0, then the instances, then the next group | inner loop |

Instance record:

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Record size, including this word. The walkers step by it | 0x0045d996 `add esi, edi` |
| 0x04 | char[32] | Object (template) name: `light`, `marker`, `grunt_regular`, ... | 0x0045d807 copies 8 dwords; passed to 0x0045f020 |
| 0x24 | u32 | Mask: spawned in pass `p` only if bit `p` is set. 0x3FF in the records checked | 0x0045d969 |
| 0x28 | f32 x 12 | Transform: 3 rows of (r0, r1, r2, translation) | 0x0045d82c copies 12 dwords into the matrix |
| 0x58 | u32 | Byte size of the property hash block | 0x0045d856 `lea esi, [esp+esi+...]` |
| 0x5c | block | Property hash table (`big-common.md`, block without its size word). Value = 36 bytes: `char name[32]`, `u32 index`. Key = name | 0x0045d88a 0x004f1220 |
| +0 | u32 | Value count n (= number of properties) | 0x0045d88f |
| +4 | u32 | Data byte count | derived: record size = 0x5c + block + 8 + 4n + data |
| +8 | u32 x n | Offset of value `index` from the start of the data | 0x0045d8b0 loop |
| +8+4n | bytes | Values back to back in index order, each 4 or 32 bytes | all 25 files |

The derived rules hold in all 25 files (checked by the parser on every record):

- The record size equals the sum of its parts.
- The table's n equals the value count, and its indices are a permutation of 0..n-1.
- `hash(name)` equals the bucket.
- The bucket count is the smallest prime greater than n, except **1 for an empty table** (2678 instances).
  This differs from the `next_prime(0) = 2` of `common.next_prime`.
- Entries within a bucket are stored in descending index order, as `common.build` produces them.
- Offsets start at 0 and are contiguous.
- No name has bytes after its NUL.

The **hash seed differs per record**: there are 286 distinct seeds, from 0x0009B69D to 0x03173D03; most records use 0x00A53E4D. The dump keeps it per
instance.

Values carry no type tag; the object's code knows each property's type. In the retail files every value is
4 bytes (integer or f32) or 32 bytes (a NUL-padded string). Property names can repeat within one instance
(54 records). The game's lookup returns one of the duplicates.

## Dump

`instances.json` contains `{"groups": [[instance, ...]]}`. Each instance is written as one header line and
then one line per property:

- **object, mask (hex) and seed (hex):** stored as read.
- **transform:** 3 rows of 4 floats.
- **object_fill:** written only if the name had stale bytes; never in retail.
- **props:** `[name, value]` pairs in index order. The value is one of:
  - integer: 4 bytes. Written when the bits are an integer from -2^20 to 2^24.
  - float: 4-byte f32. Written as the shortest decimal that packs to the same bits. A stored 0 is written as
    `0.0` when the same object's same property is a float elsewhere in the file.
  - string: a 32-byte NUL-padded string.
  - `{"str": s, "fill": hex32}`: a string with stale bytes after its NUL.
  - `{"hex": ...}`: raw bytes of any length. None in retail.

## Safe edits

- **Recomputed on rebuild:** record sizes, group counts, the hash table (buckets, chains, bucket count), the
  value count, the data size and every offset.
- **Values:** change any value, keeping its type. Write `1.0`, not `1`, for a float property. The game reads the
  bytes as whatever type the object expects; `big_kinds/inst.py` cannot check this.
- **Properties:** add or remove them. The object reads only the names it knows, and an object may require some
  properties.
- **Transform, mask and object name:** change freely. The object name must exist in the level's obj bank.
  Names and string values are at most 31 bytes.
- **Instances:** append them at the end of the last group.
- **Unsafe:** deleting or reordering instances. Ids are positions: they are stored in live objects (+0x396) and
  used to respawn (`0x0048cb30`). Scripts or other files may refer to instances by id; not yet checked.

## Open questions

- Which `pass` values does `0x0044a4b0` use (`[esi+8]`)? What do the 10 mask bits mean (difficulty, game
  mode, co-op)?
- Where do the seeds come from? They are shared by runs of records, possibly a tool-side counter or pointer. They do not affect lookups, because the game uses the stored seed.
- Does anything outside this file refer to instances by id (scripts, `text`, `obj`)?
- What do the compare callback `0x00495ee0` and `0x0045f020` do with the property table? Not read yet.
