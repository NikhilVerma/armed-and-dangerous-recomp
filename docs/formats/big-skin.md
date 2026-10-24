# `.big` kind `skin`: model bank (ANM_Skin chunks)

Status: 2026-10-03; bones, blend data and skinning (3.3, 3.7, 3.8) added 2026-10-13. Module: `tools/formats/big_kinds/skin.py`, which uses `objchunk.py` (the chunk container,
shared with `anim`) and `common.py` (docs/formats/big-common.md).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_skin.big"` ->
25 ok. Files are `<level>_big05_skin.big`, magic `0x0BADF00D`. The loader is **0x0045af30
ObjectSystem_LoadSkins** (`archive/src/loader/auto_00410ec0.cpp`). There are 8535 skins and 11176 meshes over the 25
files. The `land` kind embeds one chunk of the same format (docs/formats/big-land.md); everything below
section 2 applies to it as well.

## 1. Payload layout

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0 | u32 + block | Name hash table (big-common.md). V = 4, value = skin index, key = skin name (see 2.4) | 0x0045af30 reads the size and the block, then 0x004f1220. The compare is 0x0045ad40 |
| T | u32 | Skin count (equals the hash entry count) | 0x0045af30 -> system+0xe0c |
| T+4 | chunks | count x {chunk, zero padding}. The next chunk starts at `(size & ~31) + 32`, so there is always 1..32 bytes of padding, never 0 | 0x0045af30: `cursor = (size - (size & 0x1f)) + cursor + 0x20`. All padding is zero in all 25 files |

The loader reads everything after the count into one allocation ("ObjectSystem::LoadSkins", system+0xe1c).
For each chunk it calls 0x004e1db0, then 0x004eae90, which relocates the chunk in place. It stores the root
object in the "skin list" (system+0xe18) and sets root+8 = chunk address and u16 root+6 = 0. Finally it
registers the skin through 0x0045a860 with 1.0f. **0x0045ad10 ObjectSystem_FindSkin** looks a skin up by name.

## 2. The chunk container (shared with `anim` and `land`)

### 2.1 Chunk header and heap blocks

A chunk is a memory image of a small heap. Every pointer inside it is a chunk-relative offset.

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Chunk size in bytes | 0x0045af30 steps by it. The anim path 0x004e84e0 reads 12 bytes, then size - 12 |
| 0x04 | u32 | log2 of the largest block alignment (2 or 4; it equals the block maximum in all 22413 anim+skin chunks) | survey. No reader found |
| 0x08 | u32 | Offset of the heap header, which is always the last 0x30 bytes | 0x004e1db0: `header = chunk + [chunk+8]` |
| 0x0C | blocks | Heap blocks, linked in a ring | 0x004e1ad0 walks the ring |

Each block starts with a 12-byte header `{u32 next, u32 size, u16 flags (0), u16 align_log2}` followed by its
data. `align_log2` is 2 or 4, so data is 4- or 16-aligned. 0x004e1ad0 relocates `next` and swaps +4/+8/+0xa
as a dword and two words. In anim chunks, blocks are back to back. In skin chunks, 16-aligned blocks state 4
bytes more than the distance to the next block, and the next block may start 4..16 bytes later for alignment
(survey). The rebuild never relies on these sizes.

Heap header (0x30 bytes, the last block). 0x004e1db0 builds the same header for a new heap:

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| +0x00 | `next` = header+0x10 (it is itself a ring node: size 0x30, flags 0, align 2) | 0x004e1db0 new-heap path |
| +0x0C | Heap/allocator pointer, 0 on disk. The loader stores its argument here | 0x004e1db0 `mov [esi+0xc], ebx` |
| +0x10 | Ring head, which points at the first block (0x0C) | 0x004e1ad0 |
| +0x14 | Sum of all block sizes | survey: all chunks. 0x004e1ad0 swaps it |
| +0x18 | Block count | survey: all chunks |
| +0x1C | Always 2 | 0x004e1db0 writes max(arg, 2) |
| +0x20 | Root object | 0x004e1db0 caller: `skin = [header+0x20]`. 0x004e1ad0 relocates it |

### 2.2 Relocation

0x004e1db0 relocates through the context `{chunk, 1, 0, 0}`. The second field is "relocate only": in that
mode, **0x004e2040 Reloc_FixupPointer** adds the chunk address to each non-null pointer it is given, and all
byte-swap calls are skipped. Which fields are pointers is decided only by the type-directed walk: 0x004eae90
(skin root), 0x004e8620 (mesh), 0x004fbae0 (stream block), 0x004eaaa0 (element list), 0x004f0e60 and
0x004e1320 (dictionaries). Nothing in the chunk marks pointer fields. For that reason the dump keeps each
chunk whole (see 6).

Some fields hold values left over from the authoring tool. The game never relocates them and overwrites them
at load:
- root+8 (e.g. 0x485080)
- the compare pointers in dictionary index rows (0x0040e070, 0x0040e010; 0x004e0f80 sets the real ones)
- typed-block +4 in anims (0x004ee830 sets it)

### 2.3 Dictionaries

Name lists inside the object graph use one generic container. It is fixed up by 0x004f0e60 and
0x004e1320 Reloc_FixupIndexedTable.

| Object | Offset | Meaning | Evidence |
| --- | ---: | --- | --- |
| container | +0x00 | Heap header pointer | 0x004f0e60 relocates |
| | +0x04 | Table pointer | 0x004f0e60 relocates, then calls 0x004e1320 on it |
| | +0x08 | Flags. Bit 0: second index by id. Bit 1: name compare is case-insensitive (0x004f0c40 instead of 0x004f0c00) | 0x004f0e60 `test byte [ebp+8], 2` / `, 1` |
| table | +0x1C | Entry count | Container_At (archive/src/loader/auto_004ef270.cpp) |
| | +0x20 | Index count (1, or 2 with flags bit 0) | same |
| | +0x24 | Index rows, 0x1c bytes each. Row +0x10 is the sorted entry-pointer array | same. Compare table 0x005caa58 = {4, name compare}, {2, 0x004f0be0 id compare} |
| entry | +0x00 | u16 entry size | survey |
| | +0x02 | u16 v: the value dword is at +v and the name string at +v+4 | 0x004f0c00/0x004f0c40 compare against `entry + [entry+2] + 4`. SkinBinding_Create (0x004ed290) reads names there |
| | +0x04 | (id rows only) u16 w: refcount at +w, u16 id at +w+2 | 0x004f0be0: `[e + [e+4] + 2]`. NameDictionary_Add 0x004f0cd0 increments the refcount at +w |

### 2.4 Name and lookup key

The root object holds the source path at +0x34, for example `C:\dev\intermediate\common\skn\captain_1.SKN`.
2819 of the 8535 paths lack the `C:` prefix. The compare callback 0x0045ad40 takes `strrchr(path, '\\') + 1`
and calls `_strnicmp(key, that, strlen(that) - 4)`. The key is therefore the file name without its last four
characters, and the comparison is case-insensitive. Rebuilding the hash table from these names (seed kept)
reproduces all 25 tables exactly. **Quirk:** the compare is a prefix compare of length len - 4, so a key that
extends the stored name also matches (`captain_1x` finds `captain_1` when they share a chain). This is
original behaviour.

## 3. The skin object graph

All offsets are relative to the object. Offsets the loader neither visits nor reads are marked as survey or guess.

### 3.1 Root (heap header +0x20)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | u16 | 9 in all skins (version or type, guess) | 0x004eae90 swaps it |
| +0x06 | u16 | 0 or 1 on disk. The loader overwrites it with 0 | 0x004eae90, 0x0045af30 |
| +0x08 | u32 | Stale tool value. Set at load to the chunk address. The loader reads the chunk size through it | 0x0045af30 |
| +0x0C | ptr | Heap header | 0x004eae90 relocates. survey: the heap header in all 8535 skins |
| +0x14 | ptr | Element list or null; non-null in 143 skins. 0x1c-byte header, count +0x14, array +0x18 of elements typed by +4: 0 (3 vec3, 3 vec2), 1 (2 matrices, 3 vec2), 2 (2 vec3, 2 dwords, vec2). Meaning unknown | 0x004eaaa0 Reloc_FixupElementList |
| +0x18 | ptr | Bone-name dictionary (flags 3: name + id). Bone records refer to it by id | 0x004eae90. SkinBinding_Create 0x004ed290 uses `mesh+4 -> +0x18 -> +4` |
| +0x1C | ptr | Tree dictionary: name -> mesh dictionary. Always exactly one tree, "Default Tree" | 0x004eae90 walks +0x1c -> each value -> each value -> 0x004e8620 |
| +0x20 | ptr | The default tree's mesh dictionary: name -> mesh object, e.g. "MergedMesh" | 0x004eae90 relocates. survey: equals the single tree value |
| +0x24 | u32 | Tree count (1) | survey |
| +0x28 | u32 | Mesh count (1..13) | survey: equals the mesh dictionary size in all skins. big-land.md reads it |
| +0x2C | u32 | Usually the total material count (7431 of 8535) | survey |
| +0x30 | u32 | 0 | survey |
| +0x34 | char[] | Source path | 0x0045ad40 |

### 3.2 Mesh object (0x98 bytes)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | ptr | Root object | 0x004e8620 relocates. SkinBinding_Create reads +4 -> +0x18 |
| +0x10 | u32 | Source position count. The array is not stored | 0x004e8620 loop count for +0x28 |
| +0x14 | u32 | Source face count, for +0x2c (12-byte records). Not stored | 0x004e8620 |
| +0x18 | u32 | Count for +0x30 (u32 each). Not stored | 0x004e8620 |
| +0x1C | u32 | Source uv count, for +0x34 (8-byte pairs). Not stored | 0x004e8620, fn_004ef9e0 |
| +0x20 | u32 | Triangle count. For captain_1: 2220, which equals the non-degenerate strip triangles | survey |
| +0x24 | u32 | Source position format (switch with strides 0x005fb8ac..: 12, 16, 24, 32, 40, 48) | 0x004e8620 |
| +0x28..+0x3C | ptr | Source arrays (positions, faces, ...). Null in all retail skins | 0x004e8620. survey: all 11176 meshes |
| +0x40 | u32 | Render vertex count. Equals stream block +0x0c | 0x004e8620. survey |
| +0x44 | ptr | **Stream block** (3.3). Present in all meshes | 0x004fbae0. Mesh_GetVertex 0x00449480 |
| +0x48..+0x6C | ptr | Per-vertex index tables (+0x48 position, +0x60.. uv sets). Null in retail | 0x004e8620. fn_004ef9e0 |
| +0x70 | ptr | Material-name dictionary (flags 3) | 0x004e8620, 0x004f0e60 |
| +0x74 | ptr | Texture-name dictionary (flags 3). The names are source `.tga` paths | same |
| +0x78 / +0x7C | u32 / ptr | Material count and array (0x64 bytes each, 3.5) | 0x004e8620 stride 100 |
| +0x80 / +0x84 | u32 / ptr | Draw-group count and array (0x2c bytes each, 3.6) | 0x004e8620 stride 0x2c |
| +0x88 / +0x8C | u32 / ptr | Bone count and array (0x58 bytes each, 3.7) | 0x004e8620 stride 0x58. SkinBinding_Create |
| +0x90 / +0x94 | u32 / ptr | Bone palettes, 0x18 bytes each: +0x10 count, +0x14 pointer to u16 bone indices | 0x004e8620 stride 0x18 |

### 3.3 Stream block (the vertex buffer description)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| +0x04 | ptr | Mesh | 0x004fbae0 relocates |
| +0x0C | u32 | Vertex count | survey (= mesh+0x40) |
| +0x10 | f32[3] (+pad) | Position scale | 0x004effa0 `arg2` = block+0x10 (Mesh_GetVertex) |
| +0x20 | f32[3] (+1.0) | Position offset | 0x004effa0 `edi` = block+0x20 |
| +0x30 + 16c | f32[2], f32[2] | uv set c (0..3): scale, then offset | VertexStream_ReadVec2 0x004efec0 via fn_004ef9e0: scale (c+3)*16, offset +8 |
| +0x70 + 20s | {ptr data, u32 stride, 12 bytes} | Vertex streams s = 0, 1. Stream 1 exists only with tangent/binormal | 0x004fbae0 relocates +0x70 and +0x84. 0x004effa0 `stream = streams + desc[0]*20` |
| +0x98 | 0x154 bytes | Vertex format record: +0 checksum, +4 vertex size, +8 FVF-like word (e.g. 0x8000011A), +0xc slot mask, +0x10/+0x11 bytes, +0x14 16 x 4-byte slots | 0x004fbc30 builds it |
| +0xAC + 4k | {u8 stream, u8 offset, u8 type, u8 bytes} | Attribute slot k. Slots seen: 0 position, 1 blend weights, 2 normal, 9..12 uv0..3, 13 blend indices, 14 binormal, 15 tangent | 0x004effa0 reads slot 0, fn_004ef9e0 reads slots 9+c. The names come from matching each slot to the D3D declaration |
| +0xF0 | D3DVERTEXELEMENT9[] | Vertex declaration ending with D3DDECL_END, e.g. `POSITION SHORT4 @0, BLENDINDICES SHORT4 @8, BLENDWEIGHT SHORT4 @16, NORMAL SHORT4 @24, TEXCOORD SHORT2 @32` | survey: decoded for all meshes |

Slot types (component readers): 6/7/8 = 2/3/4 shorts, 10/11/12 = shorts x 1/32767 (0x005bcb78),
14/15/16 = floats (0x004efec0, 0x004effa0). Position = component x scale + offset. In retail, positions are
type 7 (SHORT4, w unused), uvs type 6, normals type 7 or 5 (4 bytes, decoding not read). Blend indices hold
**(palette slot + 1) x 3** and blend weights are shorts / 32767 (3.8). 521 meshes (reference and dummy objects) have stride 0 and no vertex data.

### 3.4 Drawing

**0x004fe01f** calls `DrawIndexedPrimitive(D3DPT_TRIANGLESTRIP, 0, group+0x14, group+0x18, 0, n - 2)`, which
makes every index buffer a triangle strip. Degenerate triangles join the runs. Indices are absolute vertex
numbers within `[first, first + count)` (survey: all 20261 groups).

### 3.5 Material (0x64 bytes)

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| +0x04 | Mesh pointer | 0x004e8620 |
| +0x14 | u16 material-name id (mesh+0x70 dictionary) | survey: ids match one-to-one |
| +0x16 | u16 texture id (mesh+0x74 dictionary). 0 = untextured | survey. The texture refcount equals the number of materials using it |
| +0x18 | u32 0, 2 or 6 (guess: extra map flags) | survey |
| +0x34 | u32 0x02000100 (untextured) / 0x02000101 / 0x02000103 | survey |
| +0x38..+0x4C | floats (1.0 when textured, then e.g. 0.3) | survey |
| +0x50..+0x5C | Four 0x00RRGGBB colours (guess: emissive, ambient, diffuse, specular, in the 3ds Max standard material order) | survey |
| +0x60 | f32 specular power | survey |

Texture names are full source paths, for example
`C:\dev\assets\global\tga\characters\bad\captains\captain_body_front.tga`. The level's txtr bank holds a texture
with the stem as its name (`captain_body_front` in lev_1_m1), and the decoded uvs of captain_1 line up with
that image. How the renderer turns the path into the bank name was not traced (guess: the stem).

### 3.6 Draw group (0x2c bytes) and index buffer

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| +0x04 | Mesh pointer | 0x004e8620 |
| +0x10 | Material index (into mesh+0x7c) | survey: grunt_1 has 5 groups and 3 materials, with +0x10 = 0,1,2,2,2 |
| +0x14 | First vertex (MinIndex) | 0x004fe013 |
| +0x18 | Vertex count (NumVertices) | 0x004fe010 |
| +0x1C | i32 bone palette index (mesh+0x94), -1 = unskinned | survey |
| +0x20 | Usually 3 x the strip's triangles (19926 of 20261 groups) | survey |
| +0x24 | Index buffer record. +0x04 group pointer, +0x10 index count, +0x24 f32[6] bounds (min xyz, max xyz), +0x3c pointer to u16 indices | 0x004e8620 relocates +0x24, record +4 and +0x3c |
| +0x28 | Pointer to +0x20 dwords, null in retail | 0x004e8620 |

### 3.7 Bone (0x58 bytes)

| Offset | Meaning | Evidence |
| ---: | --- | --- |
| +0x00 | Not visited by the walk. Values vary at random (0 in most bones, -1 in 1700, the rest look like leftover tool memory) | survey, 0x004e8620 |
| +0x04 | Mesh pointer | 0x004e8620 |
| +0x08 | u16 id in the bone-name dictionary (root+0x18) | SkinBinding_Create 0x004ed290 looks it up by id, then by name in the anim's node tree |
| +0x0C | f32[16] **inverse bind matrix**, row-major for column vectors (translation in elements 3, 7, 11): maps a model-space bind-pose vertex into the bone's space | fn_004ed5d0 (archive/src/anim/anim_02.cpp) multiplies the node's world matrix by it; inverting it reproduces the grunt_ref node transforms (3.8) |
| +0x4C | u32 flags (0x11, 0x17, ...) | 0x004e8620 swaps it |

Palette entries (mesh+0x94) index this bone array (u16 each).

### 3.8 Skinning

Evidence: SkinBinding_Create 0x004ed290 and AnimSkinBinding_Create 0x004ed410 (archive/src/loader/auto_004e6570.cpp),
fn_004ed5d0 (archive/src/anim/anim_02.cpp), Skin_BuildPalette 0x004edbe0 (archive/src/renderer/render_r4.cpp), 0x004feb90
(archive/src/renderer/auto_004fde30.cpp), docs/subsystems/camera.md (c42..c44).

- **There is no skeleton asset.** The skeleton is an anim's node tree (docs/formats/big-anim.md 3.2, 7):
  AnimSkinBinding_Create binds every skin of a model to the anim's tree (default +0x2c, or one named), and
  SkinBinding_Create resolves each skin bone by name (bone id -> root+0x18 name -> tree lookup). A missing name logs
  "Error binding" and leaves the bone null.
- **Skin matrix** per bone: `node world * inverse bind` (fn_004ed5d0: Matrix_Multiply(B = definition+0x8c + index *
  0x58 + 0xc, dst = skeleton+0x1c + node index * 0x48, A = pose)). The vertex positions are therefore in model space,
  bind pose. When two skins share a node, the first one to stamp the slot wins (the slot is per node, not per skin
  bone); no lev_1_m1 skin has differing matrices for one bone name across its meshes.
- **Palette** per draw group (Skin_BuildPalette): group +0x1c >= 0 selects mesh+0x94 [group+0x1c] (u16 mesh bone
  indices); -1 uses all mesh bones in order. 0x004feb90 uploads the palette matrices as 3 registers each from c45,
  after the unskinned world*view base at c42..c44.
- **Blend index** = (palette slot + 1) * 3, so index 0 is the base matrix (no bone) and slot s is at c45 + 3s.
  Survey of all 25 skin banks, SHORT4 indices with SHORT4 weights (375 meshes): 1124588 weighted influences, every
  one a multiple of 3, none at index 0, none past its palette; reading slot = index / 3 instead overruns the palette
  (grunt_1 group 1 has a 2-bone palette and indices 3 and 6).
- **Blend weights**: 4 shorts / 32767; they sum to 32765..32767 (rounding).
- Other blend formats: UBYTE4 indices without a weight slot (3451 meshes, collision skins such as captain_coll_0:
  one rigid bone, 1756 vertices at index 0 = no bone) and SHORT2 indices with SHORT2 weights (132 UI meshes).
  Some UBYTE4 meshes carry rigid parts on several bones (lev_1_m1 oilpump_1: first index byte 3..18, multiples of 3,
  decoded like SHORT4 through the group palette; also guard_alarm_*, stig_mgun_*, the weapons' LODs). The remaster
  export (rigid_vertex_bones) splits those per bone, each part with its "bone", and gives the model "skeleton".
- Skinned position = sum_k w_k * world(node(palette[slot_k])) * inverse_bind(palette[slot_k]) * v.

Remaster export (tools/remaster_export_anim.py build_skeleton and skin_vertex_weights; tools/remaster_export.py
writes the joints and weights): `skeletons/<skin>.json` = {"bones": [{"name", "parent" (-1 root), "t", "r", "s",
"skinned"}], "inverse_bind": [16 floats column-major per bone], "reference_anim", "inverse_bind_conflicts",
"max_shear"}. The hierarchy and the unskinned nodes come from a reference anim that binds the skin (a `*_ref` slot of
a template using the skin first); bind world = inverse(inverse bind) for skinned bones, parent bind world * the
reference anim's stored local transform for the others; local rest = inverse(parent bind world) * bind world. A
synthetic identity bone `__model` is added when blend index 0 is weighted. Each model submesh gets "joints" (u16x4
skeleton bone indices) and "weights" (f32x4, sum 1) byte offsets in its .bin, in the same vertex order as the
other attributes, and the model json gets "skeleton".

Verification (2026-10-13, `python tools/remaster_export_anim.py lev_1_m1 --check`, PNGs in
work/remaster/anim_check/): grunt_1's rest skeleton reproduces the bind vertices to 2.6e-6; skinned with grunt_ref
frame 0 it moves at most 0.08 units (grunt_ref's `center` is 0.02 off the bind pose); with grunt_run frame 0 the
bounding box is (-2.55, -4.97, -0.11)..(2.74, 3.14, 8.74) against the bind (-4.82, -2.53, -0.04)..(4.86, 1.66, 9.82)
(arms forward, legs in stride, feet on z = 0). Over 154 binding anims sampled at 7 times each the largest extent is
14.8 units (grunt_crash_land, lying down); nothing explodes.

## 4. Dump

```
skins.json            {"hash_seed", "skins": [{"name", "file", ...informational...}]}   list order = skin index
skins/<name>.skn      the chunk, byte for byte (authoritative)
skins/<name>.obj      view-only: positions and uv0 decoded as in 3.3, triangles from the strips, `g groupN` +
                      `usemtl <material name>` per draw group. Raw game coordinates (z up), vt = (u, 1 - v)
skins/<name>.json     view-only: vertex format (slots, decl, scales), materials (name, texture, colours),
                      draw groups (material, vertex range, palette bones, bounds), bones (id, name, matrix)
```

Rebuild reads `name` and `file` only. The name must equal the chunk's own name (2.4). The `.obj` and `.json`
files are ignored. File names follow the txtr rule: an index prefix is added for unsafe characters or a
case-insensitive clash. No clashes occur in retail.

I checked the decode on lev_1_m1: captain_1 and trotter_2 render as clean figures from the OBJ, and
captain_1's uvs line up with `captain_body_front`.

## 5. Safe edits

- **Replacing a skin with another valid chunk of any size** is safe. Examples: the same skin from another
  level, or a chunk from a future writer. Rebuild recomputes the hash table, the count, the offsets and the padding.
- **Adding, removing and reordering skins** is mechanically consistent. I tested this: I removed 2 skins from
  lev_1_m1, shuffled the rest, added 5 from lev_3_m3, and every name lookup still resolved to the right
  chunk. Whether anything stores skin indices is an open question, as it is for txtr. Lookups by name go
  through ObjectSystem_FindSkin.
- **Not supported:** editing the mesh through the `.obj`/`.json`, or patching bytes inside a `.skn` that change
  a block's size.

## 6. Why the meshes are not editable losslessly (yet)

The `.skn` is an in-place object graph. No relocation table records which dwords are pointers; the type walk in
2.2 is the only place that knows. Writing a changed mesh back means writing all of the following:

- the heap blocks
- every pointer
- the dictionaries (sorted index rows, ids, refcounts)
- the vertex-format record (checksum and mask from 0x004fbc30)
- the stale fields

Without that writer, a mesh can only be edited in place without changing any size. That is too narrow to
call the format "editable", so the `.skn` stays binary and the OBJ is view-only.

The decode in 3.x is enough to write a minimal chunk. A minimal chunk has:

- one root
- the three dictionaries
- one mesh with a stream block (SHORT4 position, SHORT2 uv, optional skinning)
- materials, groups with strips, bones and palettes

Everything in 3.x marked "null in retail" can stay null.

For load-time replacement there are two hook points:
- **0x0045af30**: substitute the chunk at `cursor` before 0x004e1db0. The size word drives the cursor step,
  so a hook must keep the bank's own step or replace the bank as a whole.
- **0x0045ad10 ObjectSystem_FindSkin**: return a different root object.

## 7. Open questions

- Does anything refer to skins by index (system+0xe18) rather than by name? This decides whether reordering
  is safe.
- Material +0x18, +0x34 and +0x38..+0x4c: what do they mean, and how are untextured materials drawn?
- How are type-5 normals (4 bytes) and the stream-1 tangent/binormal decoded? This needs the vertex shaders.
- The vertex shader itself was not read: the (slot + 1) * 3 rule rests on 0x004feb90's register layout and the survey
  in 3.8.
- Root element list (+0x14, 143 skins): what are the three element types?
- Mesh +0x20 vs group +0x20: 335 groups do not equal 3 x the strip triangles.
- Texture path -> txtr bank name resolution: not traced.
- Chunk +4 and block flags: no reader found. The sizes of 16-aligned blocks are 4 more than their spacing.
