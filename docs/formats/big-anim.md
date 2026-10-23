# `.big` kind `anim`: animation bank (ANM_Anim chunks)

Status: 2026-10-03; key blocks, node evaluation and the remaster export added 2026-10-13 (sections 3.2, 3.3, 7, 8). Module: `tools/formats/big_kinds/anim.py`, which uses `objchunk.py` (shared with `skin`) and
`common.py` (docs/formats/big-common.md).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_anim.big"` ->
25 ok. Files are `<level>_big03_anim.big`, magic `0x00C0FFEE`. The loader is **0x0045b430
ObjectSystem_LoadAnims** (`archive/src/loader/auto_00410ec0.cpp`). There are 13878 animations over the 25 files
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

### 3.2 Node record. Walk: fn_004ef270 (archive/src/loader/auto_004ef270.cpp); evaluation: fn_004ebf90 (archive/src/anim/anim_02.cpp)

A node record is also the runtime "bone": fn_004ebf90 samples its tracks into the transform at +0x1c in place.

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | u16 | 1 = camera node with the extra block below, 2 = attachment/reference node, 0 = plain | fn_004ef270 `word +4 == 1`, 0x0040a4d0 reads the camera block. Names are survey |
| +0x06 | u16 | Evaluation layout: 0 static (the stored transform is used as is), 1 stepped (copies 11 dwords after the current key of +0x4c; never seen), 2 sampled tracks, 3 aim (look-at +0x14 with a roll track at +0x4c) | fn_004ebf90 switch on word +6; fn_004ee830. Survey of all 25 banks: (kind, layout) = (0,0) 185658, (0,2) 361138, (1,3) 5670, (2,0) 1148, (2,2) 1464, (1,0) 2 |
| +0x08 | u16 | Flags (0x2000, 0x200b, 0xe00b, ...). The low 6 bits end a blend-layer chain pass | fn_004ec530 `byte +8 & 0x3f` |
| +0x0A | u16 | Node index into the skeleton's matrix arrays (0x48 bytes each) | fn_004ec2f0, fn_004ec410 `[+0xa] * 0x48` |
| +0x0C | ptr | Root object; its +0x14/+0x18 (start/end tick) are copied into every sample record | fn_004ebf90 `R[0] = hdr+0x14, R[1] = hdr+0x18` |
| +0x10 | ptr | Parent node, null for a root | fn_004ec410 walks +0x10 up; fn_004ec2f0 multiplies by the parent's world matrix |
| +0x14 | ptr | Aim target node (layout 3; camera nodes point at `<camera>.Target`) | fn_004ebf90 layout 3 calls 0x004ec410 on +0x14 |
| +0x18 | u32 | Stamp of the cached transform, 0x80000000 on disk (never a valid stamp) | fn_004ebf90 `bone+0x18 == stamp` |
| +0x1C | 0x2c bytes | Transform: position vec3, rotation quaternion (x, y, z, w) at +0x28, scale vec3 at +0x38, flags u32 at +0x44. On disk it is the value used for every component the node has no track for | fn_004ebf90; Transform_ToMatrix 0x004e3370 reads +0, +0xc, +0x1c, +0x28 of it |
| +0x44 | u32 | Transform flags: bit 0 rotation present, 0x10 translation present, 0xa scale present, 8 scale negated. Sampling ORs in 0x10 / 1 / 2 | Transform_ToMatrix 0x004e3370; fn_004ebf90 `+0x44 \|= 0x10 / 1 / 2` |
| +0x48 / +0x4C / +0x50 | ptr | Key blocks (3.3): position (types 5, 6), rotation (12, 13, 14; layout 3: roll, scalar types 0, 2, 3), scale (6) | fn_004ebf90 layout 2 samples +0x48 into +0x1c, +0x4c into +0x28, +0x50 into +0x38 |
| +0x54..+0xB0 | | Only when +0x04 = 1 (camera): matrix +0x54, u16 +0x9a FOV kind (0 horizontal, 1 vertical, else pi/2), f32 +0x9c FOV in radians, f32 +0xa0 near, f32 +0xa4 far, key blocks +0xa8 (FOV, scalar) / +0xac / +0xb0 | fn_004ef270, 0x004eec50; 0x004eb710 samples +0xa8 (default +0x9c); 0x0040a4d0 (docs/subsystems/camera.md) |

**Disk quaternions are in the game's matrix convention; key-block quaternions are conjugated.** After sampling a
rotation track, fn_004ebf90 negates x, y, z of the result (`Negate3(bone, 0x28)`); the stored transform at +0x28 is
used without negation when a node has no rotation track. Check: grunt_1's inverse bind matrices reproduce
grunt_ref's stored local rotations (e.g. belly (0.554, 0.439, 0.554, 0.439)) to 1e-6 (tools/remaster_export_anim.py
build_skeleton, 2026-10-13).

### 3.3 Typed key block. Walk: 0x004eeda0 Reloc_FixupTypedBlock; samplers: the table 0x005cabe0

Block header:
- `u8 type`
- `u8 pre mode`, `u8 post mode`, `u8 flags` (1, 1, 0 for most blocks; 3, 3, 1 and 5, 5, 0 also seen)
- `u32 sampler`: stale on disk, replaced at load by 0x004ee830 with the first dword of the 12-byte entry
  0x005cabe0 + type * 12 (`{sampler, item size, ?}`, read from game.unpacked.exe)
- `u32 count`
- `count` x item pointer (each item a separate heap block, sorted by tick)
- types 1-3 only: one dword after the pointers. With count 0 it is the constant value (0x004eb710 returns
  `track+0xc` when `+8 == 0`; the layout 3 roll does the same)

Item layouts. Every item starts with `i32 tick` (4800 per second). "packed" = a 32-bit quaternion (below).

| Type | Size | Item | Sampler (0x005cabe0) | Seen as |
| ---: | ---: | --- | --- | --- |
| 1 | 0x24 | tick, flags, 3 dw, f32 ease-to +0x14 (the next key's +0x10 is its ease-from), f32 out-tangent +0x18, f32 in-tangent +0x1c, f32 value +0x20 | 0x004ebb50 scalar Hermite with ease | camera FOV (+0xa8) |
| 2 | 0x14 | tick, flags, f32 in-tangent +8, f32 out-tangent +0xc, f32 value +0x10 | 0x004ebbe0 -> 0x004eb930 Bezier, n = 1 | roll of 13 camera nodes |
| 3 | 0x08 | tick, f32 value | 0x004ebc30 linear | roll of 74 camera nodes |
| 4 | 0x3c | tick, 5 dw (ease at +0x14 / +0x10 as type 1), vec3 out-tangent +0x18, vec3 in-tangent +0x24, vec3 value +0x30 | 0x004ebc50 vec3 Hermite with ease | not seen |
| 5 | 0x2c | tick, flags +4, vec3 in-tangent +8, vec3 out-tangent +0x14, vec3 value +0x20 | 0x004ebd20 -> 0x004eb930 Bezier, n = 3 | position, 395 nodes |
| 6 | 0x10 | tick, vec3 value | 0x004ebd70 linear | position 13161 nodes, scale 198 |
| 7 / 8 / 9 | 0x48 / 0x28 / 0x14 | float quaternions (7: ease + squad, 8: squad, 9: slerp of +4) | 0x004ebdc0 / 0x004ebe00 / 0x004ebe30 | not seen |
| 10 / 11 | | 10: no sampler. 11: note track (below) | null entries | |
| 12 | 0x24 | tick, 5 dw (unused by the sampler), packed out-tangent +0x18, packed in-tangent +0x1c, packed value +0x20 | 0x004ebe60 squad | rotation, 137 nodes |
| 13 | 0x10 | tick, flags +4, packed tangent +8 (used as both in and out), packed value +0xc | 0x004ebed0 squad | rotation, 9636 nodes |
| 14 | 0x08 | tick, packed value | 0x004ebf40 slerp | rotation, 214493 nodes |

Sizes are the second dword of each 0x005cabe0 entry and match the 0x004eeda0 swap lists. Note track (type 11, root
+0x24): item = `{i32 tick, u32 ?, u32 n, n x {u32, ptr, ptr text}}`; the text is e.g. `FX_Material
FX_fs_grunt_run` (footstep effects).

**Packed quaternion, Quat_Unpack32 0x004f1920** (archive/src/engine/missing_01.cpp, smallest three): bits 0-1 = index i of
the largest component, set to -1.0 when bit 2 is set, else +1.0; the other three components, in index order (table
0x0057e7f8 = {1,2,3}, {0,2,3}, {0,1,3}, {0,1,2}), get the signed fields bits 3-12 * (1/511), bits 13-22 * (1/511)
and bits 23-31 * (1/255) (constants 0x005bcd78, 0x005bcc18); then Quat_Normalize 0x004f12e0. Components are (x, y,
z, w).

**Key search, AnimTrack_FindKeys 0x004eb780** (archive/src/anim/anim_02.cpp): unless header flags bit 1 or a zero key range,
a time before the first key is wrapped by the pre mode and after the last key by the post mode (2 and 3 loop, 4
ping-pong, anything else no wrap). A binary search finds key A <= t < key B and the fraction s = (t - tA) / (tB -
tA). At or after the last key: A = last, B = key 0, s = 0 (the last value). Before the first key: A = last, B =
key 0, s = 1 (the first value). The record holds s at +0x1c and 1 - s at +0x20.

**Interpolation** (s = the fraction, r = 1 - s):
- Linear (3, 6): `A r + B s`.
- Bezier (2, 5), CurveEval_Bezier 0x004eb930 (archive/src/engine/missing_01.cpp): with dt = tB - tA (plus the root's end -
  start when negative, i.e. across the loop) and h = dt / 3 (0x005bcc60 = 1/3):
  `P0 r^3 + 3 s r^2 (P0 + h T0) + 3 s^2 r (P1 + h T1) + P1 s^3`, P0 = A's value, T0 = A's out-tangent, P1 = B's
  value, T1 = B's in-tangent (tangents per tick). Step keys: when `A.flags & 0x1c00 == 0x800` or `B.flags & 0x380 ==
  0x100` the result is A's value (B's when s is exactly 1.0).
- Hermite (1, 4): s is first warped by CurveParam_Ease 0x004eba50 (A's ease-to, B's ease-from), then
  `h00 A + h10 A.out + h01 B + h11 B.in` with the standard cubic Hermite basis.
- Slerp (14), Quat_SlerpRaw 0x004f15a0: **no shortest-arc flip**; d = A.B; d + 1 <= 1e-4 gives B; 1 - d <= 1e-4
  gives linear weights; else the sine weights.
- Squad (12, 13), Quat_Squad 0x004f16b0: `slerp(slerp(A, B, s), slerp(A.out, B.in, s), 2 s (1 - s))`, all with
  Quat_SlerpRaw. Types 12 and 13 use the raw fraction (no ease).

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
- **Not supported:** editing keyframes. The chunk is an in-place object graph. There is no writer for it yet (the
  key encodings themselves are now known, 3.3). See docs/formats/big-skin.md 6 for the reasons, which
  apply here too.

## 6. Open questions

- The 5 unused dwords of type 12 and the ease/TCB parameters of types 1 and 4 (guess: TCB tension, continuity and
  bias, already baked into the tangents by the exporter).
- Node +0x04 = 2 nodes (attachment points such as `Ref_rhand`?) and what reads them.
- How the anim player maps play time to ticks (the record time is the skeleton stamp: is the first frame tick 0 or
  root +0x14?). The remaster export takes time 0 = root +0x14.
- The first pointer in note items (type 11): it points at non-text bytes.
- Are anims referenced by index anywhere (system+0xe2c)?

## 7. Pose evaluation (how a node becomes a matrix)

Evidence: archive/src/anim/anim_02.cpp (fn_004ebf90, fn_004ec2f0, fn_004ec410, fn_004ed5d0), archive/src/engine/engine_01.cpp
(Quat_ToMatrix 0x004e3260, Transform_ToMatrix 0x004e3370, Matrix_Multiply 0x004e2910), archive/src/engine/engine_02.cpp
(Quat_Mul 0x004f1710, Quat_FromAxisAngle 0x004f13d0, 0x004e7830 cross product).

- **Matrices** are 4x4 floats, row-major in memory, for **column vectors**: p' = M p, translation in elements 3, 7
  and 11 (Transform_ToMatrix stores the position at +0xc, +0x1c, +0x2c). A dword at +0x40 holds flags.
- **Quaternions** are (x, y, z, w), w last. Quat_ToMatrix gives the standard right-handed rotation of a unit
  quaternion for column vectors (m1 = 2xy - 2zw, ...). Quat_Mul 0x004f1710(a, b) computes b * a (Hamilton).
- **Local matrix** = T R S (Transform_ToMatrix scales the columns), the flag bits of +0x44 deciding which parts are
  present (3.2).
- **World matrix**: `world(node) = world(parent) * local(node)` (fn_004ec2f0: Matrix_Multiply(B = local, dst, A =
  parent world) computes A * B). Roots: world = local.
- **Layout 3 (aim)**: target = the +0x14 node's world position, taken into the parent's frame; z = normalize(position
  - target); up = column 2 of the parent's inverse (world z without a parent); x = normalize(up x z); y = z x x;
  the basis goes through Matrix_ToQuat 0x004e35a0 and is multiplied by the roll quaternion about local z (axis
  0x005fb89c = (0, 0, 1), angle = the +0x4c scalar track): q = basis * roll. A camera looks down its local -z with
  y up. Check: Camera01 of lev_1_m1 grunt_run evaluates to its stored rotation (0.596, -0.124, -0.162, 0.777).
- **Skinning** (docs/formats/big-skin.md 3.8): the skin matrix of a bone = world(node) * the skin bone's inverse
  bind matrix (fn_004ed5d0).
- **Hierarchies differ per anim.** Each anim carries its own node tree. In lev_1_m1, 116 of the 154 anims that bind
  grunt_1 have `center` as a root, `grunt_ref` parents it to `grunt_instance` (identity), and the other 37 (the `mover_*` anims and grunt_die_3)
  anims parent it to a `mover_*` node that carries the motion of the whole character (root motion). One anim
  (mover_grunt_tree_2) parents `Camera01.Target` to `chest`; one names `tounge1`'s parent `lounge` (a typo in the
  source data).

## 8. Remaster export (tools/remaster_export_anim.py)

`python tools/remaster_export_anim.py <level> [--check]` writes, under work/remaster/<level>/, in game space (Z up,
game units; grunt_1 is 9.8 units tall and faces -Y):
- `anims/<anim>.json` per anim: `fps`, `duration` (seconds, (end - start) / 4800), `tracks` {node: {"t": [[time,
  x, y, z]..], "r": [[time, x, y, z, w]..], "s": ...}}: LOCAL transforms after all the rules above (conjugation,
  flags), sampled with the game's own samplers at every frame tick and every key tick in [start, end]. A constant
  track is one key at time 0; consecutive quaternions are sign-aligned (same rotations); "s" is left out when it is
  1 throughout. Static nodes are included (their stored pose differs per anim). Plus `parents` (this anim's own
  tree), `notes` [[time, text]] and `cameras` {node: {"t", "r" (world), "fov" [[time, vertical radians]], "near",
  "far", "fov_kind"}}; a horizontal FOV is converted with the default aspect 1.702128 (camera +0x154).
- `anims/index.json` {template: {"slots": {slot: anim}, "skeleton": skin}} for templates whose LOD 0 skin is
  skinned (slots from the obj bank, docs/formats/big-obj.md; game lookups are case-insensitive, so names are lower
  case), and `anims/by_prefix.json` {character prefix: [anim names]}.
- `skeletons/<skin>.json`: docs/formats/big-skin.md 3.8.
