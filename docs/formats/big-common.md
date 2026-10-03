# `.big` shared container: the name hash table

Status: 2026-10-03. Module: `tools/formats/big_kinds/common.py`. Check:
`.venv\Scripts\python.exe tools\formats\big_kinds\common.py check` (header-level parse of every file).

Nine kinds (`text`, `txtr`, `anim`, `skin`, `prop`, `obj`, `aud`, `fx`, `ff`) store a name hash table near
the start of the payload. It is the on-disk form of the game's generic hash table: a 0x20-byte table object
built by the loader, plus an "entries" block read from the file and fixed up in place by
**0x004f1220** (unnamed in Ghidra; "HashTable_FixupInPlace" would describe it). The loaders that call 0x004f1220:
0x0041c7e0 Fx_LoadChunk, 0x0045af30 ObjectSystem_LoadSkins, 0x0045b430 ObjectSystem_LoadAnims, 0x0045b7d0
ObjectSystem_LoadProperties, 0x0045bf70 (obj), 0x0045d7a0, 0x00488400 RenderSystem_LoadTextures, 0x00496060
TextData_Load, 0x004e4d90 Rumble_Load (`tools/recover.py 0x004f1220` caller list).

## Layout

Offsets relative to the size word; it sits at payload +0 except in `ff` (+4, see below).

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Block size in bytes (everything below) | Loaders read it, allocate that much, read the block (0x00488400, 0x0045af30, 0x004e4d90) |
| 0x04 | u32 | Bucket count B | 0x004f1220 copies it to table+0; 0x004f1100 takes the hash modulo it |
| 0x08 | u32 | Hash seed | 0x004f1220 copies it to table+4; 0x004f1100 XORs it into every character |
| 0x0C | u32 | Value size V (bytes per entry after the link) | 0x004f1220 copies it to table+8 and steps entries by V+4 |
| 0x10 | i32[B] | Bucket heads: index of the bucket's first entry, -1 = empty | 0x004f1220: head = entries + index*(V+4), -1 becomes null |
| 0x10+4B | n x (4+V) | Entries: u32 next, then V bytes of value | 0x004f1220 second loop; lookups return entry+4 (0x004f1160) |

`next` is 0xFFFFFFFF at the end of a chain. Any other value means "continue": 0x004f1220 replaces it with a
pointer to the physically following entry (`sete; dec; and` at 0x004f12ab..0x004f12b4), so chains must be
contiguous runs of entries, stored in bucket order. The original tool writes `next = index + 1`.
n follows from the size: n = (size - 12 - 4B) / (4 + V).

### The hash and lookup

- **0x004f1100** (edx = key, edi = table): `sum += seed ^ c` over the key's chars, c sign-extended (`movsx`)
  with 'A'..'Z' lowered by +0x20; result `sum % B` (`div dword [edi]`). So lookups are case-insensitive.
- **0x004f1160** (eax = table; [key]): hash, then walk the chain calling the table's compare callback (table
  +0x10, context table +0x14) with (context, key, entry+4); returns entry+4 or null.
- Keys are not in the table. The value tells the callback where the name is:

| Kind | V | Value | Compare callback | Key |
| --- | ---: | --- | --- | --- |
| txtr | 4 | texture index | 0x00488150: `_stricmp(key, rs->records[v].name)` | texture name |
| anim, skin, obj, aud, ff | 4 | index (a permutation of 0..n-1 in every file) | 0x0045b200 (anim), 0x0045ad40 (skin), 0x004e4d60 (ff); others not checked | guess: name |
| fx | 4 | byte offset into the Fx data after the table, where the entry's name string sits | 0x0041c7b0: `_stricmp(key, fx->dataEnd(+0x10) + v)` | fx name |
| text, prop | 36 | char name[32] + u32 index | 0x00495ee0 | the name in the value; hash of it matches the bucket in all files |

## Invariants measured over all 225 files (9 kinds x 25)

All of these hold in every file (scratch survey scripts, 2026-10-03; `common.py check` re-verifies the first
three on every run):

1. The block parses exactly: heads are contiguous and ascending, next = index + 1 or END, every entry reached.
2. Re-serialising from (bucket of each entry, values) reproduces the block byte for byte, so heads and next
   are derived data.
3. B = the smallest prime strictly greater than n (e.g. n=310 -> 311, n=601 -> 607, n=8 -> 11).
4. Within a bucket, entries are in descending index order (the tool inserted at the chain head, in index
   order). For txtr, recomputing bucket = hash(name) with the stored seed gives the stored bucket for all
   4764 non-empty buckets; `common.build()` therefore regenerates the txtr table exactly from the names.
5. The seed differs per file with no visible rule (e.g. 0x01345C58, 0x0C800405); guess: random or time-based in
   the original tool. Any seed works because the game reads it from the file; keep it for exact rebuilds.

## What follows the table, per kind

| Kind | Before the table | After the table | Evidence |
| --- | --- | --- | --- |
| text | nothing (the whole payload is read as one blob "text data") | u32 count (= n), offset table, strings | 0x00496060 |
| txtr | nothing | u32 count (= n), texture blob | 0x00488400; docs/formats/big-txtr.md |
| anim | nothing | u32 count (= n), anim image data | 0x0045b430 |
| skin | nothing | u32 count (= n), skins (32-byte aligned) | 0x0045af30 |
| prop | nothing (whole payload read as "prop data") | u32 count (= n), property list | 0x0045b7d0 |
| obj | nothing | u32 = n (measured; loader 0x0045bf70 not recovered) | survey |
| aud | nothing | u32 = n (measured; loader is renderer/sound vtable +0x80, not read) | survey |
| fx | nothing | **no count**: the Fx data follows directly, in the same allocation (size - 4 bytes) | 0x0041c7e0 |
| **ff** | **u32 sample count (= n)** | count x 0x840-byte rumble samples | 0x004e4d90 Rumble_Load |

Deviations: `ff` puts its count before the size word (`DIR_OFFSET["ff"] = 4` in common.py); `fx` has no count
word after the table and its values are offsets, not indices. All other kinds share the "table, u32 count"
shape. `land`, `inst`, `ui` and `font` do not start with this table (not part of this survey).

## Using the module

```python
from big_kinds import common
hd, end = common.parse(payload, common.DIR_OFFSET[kind])  # HashDir: seed, value_size, bucket_count,
                                                           #          buckets[i], values[i] (stored order)
hd.to_bytes()                                              # exact bytes back, heads/next recomputed
common.build(names, values, seed)                          # a fresh table in the original tool's order
hd.to_json() / common.HashDir.from_json(d)                 # generic JSON when keys are unknown
```

Safe edits for a kind that uses `build()` from names: adding, removing, renaming entries (B, buckets, heads and
next are recomputed). A kind that keeps the generic JSON (`to_json`) must keep each entry's bucket consistent
with its key by hand; prefer recomputing from names where the kind knows them.

## Open questions

- The compare callbacks of obj and aud (and whether their keys are names) are not read yet.
- 0x0045d7a0 also calls 0x004f1220; which chunk it loads is not checked (possibly `inst`).
