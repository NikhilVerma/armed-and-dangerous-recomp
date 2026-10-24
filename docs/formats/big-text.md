# `.big` kind `text`: the string bank

Status: 2026-10-03. Module: `tools/formats/big_kinds/text.py`. Files: `<level>_big01_text.big`, magic
0xDEADC0DE, loader **0x00496060 TextData_Load** (recovered: `archive/src/loader/auto_0048afb0.cpp`).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_text.big"`
gives 25 ok / 25.

## Layout

The loader reads the whole payload into one allocation, "text data" (bank +0xa0), and fixes it up in place.
Offsets are relative to the payload (after the 8-byte `.big` header).

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Hash block size S | 0x00496060 reads `length = D(blob)` and skips it |
| 0x04 | S bytes | Name hash table, V = 36: `char id[32]` + `u32 string index` (big-common.md) | 0x00496060 passes blob+4 to 0x004f1220; bank +0xa4 |
| 4+S | u32 | count: number of strings (= hash entries) | 0x00496060 stores it at bank +0xac |
| 8+S | u32 | Byte size of the string area | Skipped by the loader (`p += 8`); measured: equals the area size in all 25 files |
| 12+S | u32[count] | Offset of string i from the start of the string area | 0x00496060: `offsets[i] = offset + count*4 + &offsets[0]`, i.e. relative to the end of this array; bank +0xb0 |
| 12+S+4·count | ... | count UTF-16LE strings, each ended by a 2-byte NUL, packed in index order with no padding | measured, all 25 files |

The hash lookup compares with **0x00495ee0** (`_stricmp(key, entry+4)`, which is the id stored in the value), so
ids are looked up case-insensitively. The game hashes ids with 0x004f1100 and the table's seed.

## Derived values (all recomputed by `rebuild`)

Measured over all 25 files (`text.dump` asserts the first three on every dump):

- The hash table equals `common.build(ids in index order, id.ljust(32) + u32 index, seed, 36)`: bucket count
  is the next prime above n, chains in bucket order, highest index first in each chain. Only the seed is stored.
- The offsets are the running sum of `len(string) + 2`, starting at 0; the area-size word is their total.
- The id fields contain nothing after their NUL. Ids are unique case-insensitively.
- The hash values form a permutation of 0..count-1.

## Dump

`strings.json`: `hash_seed`, and `strings`, a list in string-index order of `{"id": ..., "text": ...}`. The text is
the UTF-16 string decoded (`surrogatepass`, so any unpaired surrogate survives). Markup such as `<br>`, `<rgb=...>`
and `%s` is part of the text, unchanged. The file is UTF-8 with non-ASCII characters written as-is.

## Safe edits

- **Change any text**, including its length. Only a NUL character is rejected.
- **Add, remove or rename strings.** The hash table, count, area size and offsets are recomputed. Ids must be at most
  31 bytes of Latin-1 and must stay unique when compared case-insensitively. Any `hash_seed` works because the game
  reads it from the file.
- **Reordering, removing or inserting entries changes later strings' indices.** Lookups by id are unaffected.
  Code that uses a string's index directly would break. No such user has been found, but none has been looked for
  (open question).

## Open questions

- Does anything outside the hash lookup index the offset table (bank +0xb0) directly by number? If it does, the
  string order is fixed.
- Whether anything reads the string-area size word (offset 8+S), given that the loader skips it.
- Which code reads bank +0xa8/+0xb0 (the text lookup); not traced in this task.
