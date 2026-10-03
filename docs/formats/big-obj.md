# `.big` kind `obj`: object templates

Status: 2026-10-03. Module: `tools/formats/big_kinds/obj.py`. Files: `<level>_big07_obj.big`, magic 0x00BADA55,
loader **0x0045bf70** (5380 bytes, not recovered; "ObjectSystem_LoadTemplates" would describe it; strings
"objdef hashtable entries", "objdef data", "obj template list"). Roundtrip:
`.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_obj.big"` gives 25 ok / 25.

A template names an object type (for example `captain` or `v_rockettower`), its model slots (slot 0 is
`staticmodel`, most of the rest are animation names), its LOD model names and its property set (`prop` kind).

## Layout

Unlike text and prop, the loader reads this payload piece by piece from the stream. Offsets are relative to the
payload.

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 + block | Template-name hash table, V = 4: template index | 0x0045bf70: "objdef hashtable entries" (system +0xe3c), table +0xe38 with compare **0x0045b5f0**: `_stricmp(key, system[+0xe40][v*0x50 + 0x10] + 4)`, the template's name |
| after | u32 | count: number of templates (= hash entries) | read into system +0xe34; "obj template list" = count × 0x50 bytes (+0xe40) |
| after | count × record | Template records, in index order | the rest is "objdef data" (+0xe44) |

Template record. Offsets are relative to the record. The loader reads the size word into a local, then
`size - 4` bytes to record+4:

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x00 | u32 | Record size in bytes | 0x0045bf70 reads 4, then `size - 4`; then overwrites +0 with the new slot hash table object |
| +0x04 | char[32] | Template name | compare 0x0045b5f0 (`record + 4`) |
| +0x24 | u32 | A: slot count | "template Animation data" = A × 12 bytes; ModelSlots_Resolve 0x0045bbe0 loops over +0x24 |
| +0x28 | u32 | L: LOD count | lodcutoff loop bound in 0x0045bf70 |
| +0x2c | u32 | 0x3C: offset of the slot table's size word | loader adds `record + 4`, so the block sits at record + 0x40; always 0x3C (7584 templates) |
| +0x30 | u32 | Offset of the slot names | loader adds `record`; compare **0x0045b630** reads `[+0x30] + v*32` |
| +0x34 | u32 | Offset of the LOD names | loader adds `record`; ModelSlots_Resolve reads `[+0x34] + i*32` |
| +0x38 | u32 | Offset of the slot × LOD matrix | loader adds `record`; ModelSlots_Resolve memsets `L*A*4` bytes there |
| +0x3c | u32 + block | Slot hash table, V = 4: slot index; own seed | 0x0045bf70 calls 0x004f1220 on it with compare 0x0045b630 |
| [+0x30] | char[A][32] | Slot names | 0x0045b630 |
| [+0x34] | char[L][32] | LOD model names | 0x0045bbe0 (guess "LOD model": names like `captain_1`, `captain_9`, `captain_coll_0`) |
| [+0x38] | u32[L·A] | Runtime matrix, **zero in every file** | ModelSlots_Resolve 0x0045bbe0 memsets and fills it |
| after | char[32] | Property set name | 0x0045bf70: `FindProperty([+0x38] + L*A*4)` → template +0x3c |
| after | u32[L] | lodcutoff, **zero in every file** | 0x0045bf70 sets template +0x40 to this address. The last LOD and the collision/shadow LODs get 0xFFFFFFFF, the others 0. Each is then overwritten from property `lodcutoff_<i>` (getter 0x0045df00) |

The in-memory template entry (0x50 bytes at system +0xe40) holds collision_lod_index (+0x00) and
shadow_lod_index (+0x04). Both are read from the template's property set and default to -1. It also holds
the anim data (+0x0c) and the record pointer (+0x10).

## Derived values (all recomputed by `rebuild`)

Measured over all 25 files (7584 templates). `obj.dump` asserts that the rebuild reproduces the payload exactly:

- Both tables equal `common.build(names in index order, index, seed, 4)`, with B = the next prime above n. Each
  template's slot table has its own seed, which is stored.
- The four offsets follow from A and L: `0x3c`, `0x40 + slot block`, then `+32A`, then `+32L`. The size word is
  `matrix offset + 4·L·A + 32 + 4·L`.
- The matrix and lodcutoff are all zero. They are only written to the JSON if non-zero.
- Every hash value set is a permutation of the indices.

## Dump

- `objects.json`: `hash_seed`, plus `objects`, the list of template files in index order.
- `objects/<name>.json`: one file per template, with `name`, `hash_seed` (the slot table's seed), `property`,
  `lods` and `slots` (lists of names in index order). `matrix` and `lodcutoff` appear only when non-zero.

## Safe edits

- **Add, remove, rename or reorder slots and LODs**, and rename the property set. All offsets, sizes and the
  slot hash table are recomputed. Slot 0 is `staticmodel` in all 7584 templates; keep it first (guess:
  ModelSlots_Resolve treats row 0 specially, because `nameOffset != 0` skips its filter).
- **Add, remove or rename templates.** The template table and count are recomputed. `hash_seed` may be omitted
  in a new file (the default is 0).
- Names may be up to 32 bytes. A 32-byte name has no NUL, and the retail files contain one:
  `captain_jumper_hit_ground_get_up` (a slot of `captain`, not the last one, in 20 files). The compare
  0x0045b630 uses `_stricmp` on the unterminated field, which runs on into the next slot name, so a lookup of
  that slot by name presumably fails. This is an original defect, reproduced here and not fixed. Keep new names
  at 31 bytes or fewer.
- **Template order is risky.** The template index is what the name table maps to. Whether other chunks (for
  example `inst`) refer to templates by index is not known.
- The LOD count changes the meaning of `lodcutoff_<i>`, `collision_lod_index` and `shadow_lod_index` in the
  template's property set. Keep them consistent.

## Open questions

- Loader 0x0045bf70 is not recovered. The layout above comes from its decompilation and disassembly, and was
  checked against all 7584 records.
- Do `inst` or other chunks refer to templates by index? This decides whether reordering templates is safe.
- What do the rows and columns of the matrix mean? ModelSlots_Resolve fills it from the anim cache; L×A
  suggests one entry per LOD per slot. It does not matter for the file, because it is always zero there.
- The meaning of the LOD names (guess: model names per LOD; `_coll` names are collision meshes).
