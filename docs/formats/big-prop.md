# `.big` kind `prop`: property sets

Status: 2026-10-03. Module: `tools/formats/big_kinds/prop.py`. Files: `<level>_big06_prop.big`, magic
0x00BEADED, loader **0x0045b7d0 ObjectSystem_LoadProperties** (recovered: `src/loader/auto_00410ec0.cpp`).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_prop.big"`
gives 25 ok / 25.

A property set is a named list of key/value pairs, for example `p_captain_grenade` with keys like `speed`,
`damage` and `fx_end`. Object templates (`obj` kind) name their set, and the game finds it with 0x0045b6c0
ObjectSystem_FindProperty.

## Layout

The loader reads the whole payload into "prop data" (system +0xe54) and fixes it up in place. Offsets are
relative to the payload.

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 + block | Set-name hash table, V = 36: `char name[32]` + `u32 set index` | 0x0045b7d0: block at system +0xe50, compare 0x00495ee0 |
| after | u32 | count: number of sets (= hash entries) | system +0xe48; "property list" of count × 12 bytes (+0xe58) |
| after | count × set | the sets, in index order | 0x0045b7d0 loop |

One set:

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0 | u32 + block | Key hash table, V = 36: `char key[32]` + `u32 value index`, with its own seed | 0x0045b7d0: list entry +0 (table), +4 (block); compare 0x00495ee0 |
| after | u32 | nvalues (= key hash entries) | 0x0045b7d0 `values` |
| after | u32 | data_bytes: byte size of the value data | 0x0045b7d0 `skip` (added to the cursor after the offsets) |
| after | u32[nvalues] | Offset of value j from the start of the value data | 0x0045b7d0: `offset + base`, base = end of this array; list entry +8 |
| after | data_bytes | values in index order, each 4 bytes (a dword) or 32 bytes (a NUL-padded string) | measured: 38961 dwords and 8072 strings over 25 files, no other size |

Getters (each one hashes the key, takes `entry+0x20` as the value index, then reads `values[index]`):
**0x0045dec0** copies a 32-byte string (strncpy 0x20), **0x0045df00** reads a dword (int or float), and
**0x0045df30** reads a bool (`dword != 0`). 0x0045b6c0 returns the 12-byte list entry for a set name. **The value
type is not stored in the file**: the code that reads a key decides how to interpret its value.

## Derived values (all recomputed by `rebuild`)

Measured over all 25 files (2347 sets). `prop.dump` asserts that the rebuild reproduces the payload exactly:

- Both tables equal `common.build(names in index order, name.ljust(32) + u32 index, seed, 36)`. Each key table has
  its own seed (2297 of the 2347 differ from the file's main seed).
- The bucket count is the next prime above n, **except that an empty table has B = 1** (25 empty sets, one per
  file, give `(n=0, B=1)`). `common.next_prime(0)` returns 2, so `prop.bucket_count()` handles this case. See
  "Change for common.py" below.
- Value offsets are the running sum of the value sizes from 0, and data_bytes is their total.
- Names, keys and 32-byte string values contain nothing after their NUL.

## Dump

- `properties.json`: `hash_seed`, plus `sets`, the list of set files in index order.
- `properties/<name>.json`: one file per set, with `name`, `hash_seed` (the key table's seed) and `values`, a list
  of `[key, value]` pairs in index order. The value types are:
  - **JSON string:** a 32-byte NUL-padded string (up to 32 Latin-1 bytes).
  - **JSON float:** a float32, written as the shortest decimal that packs back to the same bits.
  - **JSON int:** a dword (`u32`, or `i32` when negative). JSON `true`/`false` are also accepted.
  - **`{"hex": "..."}`:** raw bytes. No retail value needs this.
- The dump guesses int or float for each dword. A value is an int if it is 0 or its bits are below 0x00800000 or
  at or above 0xFF800000 (small integers and small negative integers). Otherwise it is a float if it decodes to
  a finite value of magnitude 1e-6 to 1e9. A 0 is written as `0.0` when the same key holds a float elsewhere in
  the file. The guess affects only how the value is displayed: the bytes rebuilt depend on the JSON type, so an
  unedited dump rebuilds exactly whatever the guess was. Over the 25 files, 15247 non-zero dwords are shown as
  floats and 12652 as ints. The only key that gets both is `lodcutoff_0`: it is 20.0f in `p_grass`, `p_water`
  and `p_temp_shadow` (9 sets) and an int (for example 1000) elsewhere.
- `values` is a list rather than an object because **keys repeat**. 21 distinct set/key pairs repeat in the retail
  files, for example `p_globalsettings`: `colossus_friction` twice (0 and 0.5) in all 25 files,
  `p_grunt_regular`: `bof_anti_gravity` three times, and `p_colossus`: `material` (`colossus`, then `metal`).
  Lookup 0x004f1160 returns the first match in the chain, and the chain stores the highest index first, so
  **the last occurrence wins**. This is an authoring defect in the original data. It is kept as it is.

## Safe edits

- **Change any value**, including its type. A float written as `1` is packed as the integer 1, so write `1.0` for
  a float key. Strings are limited to 32 bytes.
- **Add, remove or rename keys and sets.** Every table, count, offset and size is recomputed. Names and keys
  are limited to 31 bytes, because the 32-byte field is followed by the index. `hash_seed` may be omitted in a new
  set file (the default is 0; any seed works).
- **Order:** set order and key order only fix indices. Lookups go by name, through the hash tables. Changing the
  order of a repeated key changes which occurrence wins.
- Unsafe: giving a key a type that its reader does not expect. The file has no types. The guessed type of a
  value set to 0 may be wrong.

## Change for common.py (not made; the module belongs to another agent)

`common.next_prime(0)` should return 1 to match the original tool, or `build()` should use 1 for an empty
table: `bucket_count = bucket_count or (1 if n == 0 else next_prime(n))`. `prop.py` and `obj.py` pass the bucket
count explicitly to work around this.

## Open questions

- Which keys are floats and which are ints? This needs the call sites of 0x0045df00 and the type each one uses.
  An exact type table would replace the guess.
- `lodcutoff_<i>` is read as a dword into the template's cutoff array (see big-obj.md). Is the 20.0f in
  `p_grass`, `p_water` and `p_temp_shadow` an authoring slip (it reads as the int 1101004800), or does the
  consumer read the cutoff as a float?
- Do any keys have more than one reader, each using a different getter?
