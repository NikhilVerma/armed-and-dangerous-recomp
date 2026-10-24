# `.big` kind `txtr`: texture bank

Status: 2026-10-03. Module: `tools/formats/big_kinds/txtr.py` (uses `common.py`, docs/formats/big-common.md).
Roundtrip: `.venv\Scripts\python.exe tools\formats\bigtool.py roundtrip "orig/pc/GameData/**/*_txtr.big"` ->
25 ok. File `lev_NN_big02_txtr.big`, magic `0x0B00B1E5`, loader **0x00488400 RenderSystem_LoadTextures**
(`archive/src/loader/auto_0045bbe0.cpp`), called from the dispatcher at 0x0044a060. 7287 textures over 25 files.

## Payload layout

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0 | u32 + block | Name hash table (big-common.md), V = 4, value = texture index, key = name | 0x00488400 reads size + block, 0x004f1220; compare 0x00488150 |
| T | u32 | Texture count (= hash entry count) | 0x00488400 -> rs+0x20 |
| T+4 | blob | count entries, back to back, up to the end of the payload | 0x00488400: blob = chunk size - table size - 8, rs+0x24 |

Texture entry (offsets relative to the entry):

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | char[32] | Name, NUL-padded (max 31 chars; bytes after the NUL are zero in all files) | record+0 = entry; 0x00488150 `_stricmp` on it; "Failed to create texture %s" (0x0050bb30) |
| 0x20 | u32 | Entry size = 0x28 + pad + image chain | 0x00488400: `data += [data+0x20]` |
| 0x24 | u32 | pad: 128 - (blob offset of entry + 0x28) % 128, so 1..128 | 0x00488400: record+4 = entry + 0x28 + [entry+0x24]; rule holds for all 7287 |
| 0x28 | u8[pad] | Zero | survey |
| 0x28+pad | chain | Image chain, 128-aligned within the blob (the blob allocation is aligned, 0x004e07f0 arg 7) | record+4 |

Image chain (read by **0x0050c360**, renderer vtable 0x00589990 slot 84 (+0x150); not yet recovered; working
name "Renderer_CreateTextureFromChain"):

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Level count | 0x0050c36b `mov ebp,[edi]`, passed as max levels to 0x00509eb0 |
| 0x04 | 16 bytes x levels, each followed by its pixels | Level image header | |
| +0x0 | u16 | Width | 0x0050c1b0 source rect; 0x0050c402 compare with D3D level width |
| +0x2 | u16 | Height | same |
| +0x4 | u32 | D3DFORMAT (21 A8R8G8B8, 23 R5G6B5, 24 X1R5G5B5, 'DXT1', 'DXT2' in retail) | 0x0050c1b0 passes it to D3DXLoadSurfaceFromMemory; level 0's goes to 0x00509eb0 |
| +0x8 | u16 | Rows: height, or block rows for DXT (unused by the game: 0x0050c360 saves it and never reads it) | survey |
| +0xA | u16 | Pitch: width * bytes per pixel, or block columns * 8/16 | 0x0050c1b0 pitch argument |
| +0xC | u32 | Pixel bytes = rows * pitch; next level follows the pixels | 0x0050c419 `add edi, ebx` |
| +0x10 | bytes | Pixels, top row first | 0x0050c1b0 `image + 0x10` |

Level sizes: level l is max(4, w0 >> l) x max(4, h0 >> l) for DXT and max(1, ...) otherwise (all 7287
textures). Every texture is a power of two; level counts are 1..11 and often stop short of a full chain
(e.g. `fire_test` 128x256, 8 levels, the last two stored as 4x4).

## Dump

```
textures.json         {"hash_seed": "0x01345C58", "textures": [{"name", "file", "format", "width",
                       "height", "levels"}, ...]}   (list order = texture index)
textures/<name>.dds   one DDS per texture: stored format, all stored levels as the mip chain
```

- DDS: legacy 124-byte header; FourCC for DXT, RGB masks for the others (A8R8G8B8, R5G6B5, X1R5G5B5; also
  read back: R8G8B8, X8R8G8B8, A1R5G5B5, A4R4G4B4, DXT3/4/5). Pillow opens all of `lev_1_m1`'s files.
- **DXT2** textures (3094 of 7287) are written with the **DXT3** FourCC: same block encoding, and most tools
  cannot open DXT2. Rebuild turns a DXT3 DDS back into DXT2 when textures.json says `"format": "DXT2"`. The
  alpha is probably premultiplied (that is what DXT2 means), so an editor shows those textures as stored.
- File names are the texture names; a name with characters outside `[A-Za-z0-9_.-]`, or one that collides
  case-insensitively with an earlier one, gets an index prefix (`0042_name.dds`). The `file` field is what links.
- The game stores no per-texture flags: everything in the record is set by the loader (below).

## Rebuild and safe edits

Rebuild reads only `name` and `file` (and `format` for the DXT2 case) from the JSON, and size, levels and
pixel format from each DDS header. It recomputes the hash table (B = next prime > n, buckets from the
names and the stored seed, chain order), the count, every entry size and pad, and every level header.

Safe:
- Replace a DDS with another size, level count or supported format. Keep sizes powers of two (all retail
  textures are; non-power-of-two support depends on the device caps). Save DXT1/DXT3/DXT5 or 32-bit
  A8R8G8B8 legacy DDS, not DX10-header DDS (rejected with a message).
- Add a texture (new JSON entry + DDS), remove one, rename one, reorder. **Caution:** the order is the
  texture index; whether anything else in the level refers to textures by index instead of by name is not
  known yet. Names are the safe link; keep order unless you know.
- Names longer than 31 bytes are rejected.

Unsafe/unknown: deleting or renaming a texture that models, effects or UI look up by name (they get null:
`n_fx_bank_1`/`n_fx_bank_2` missing makes Fx_LoadChunk log an error).

## How the game loads a texture (for load-time replacement)

1. **0x00488400 RenderSystem_LoadTextures** (eax = stream, esi = render system `rs`, [chunk size]). In the
   level, `rs` is Game+0xa78 (Fx_LoadChunk passes `[Game+0xa78]` to the lookup). It reads the table into
   rs+0x10 with the table object at rs+0xc, the count into rs+0x20, allocates 0x24-byte records (rs+0x18),
   a pointer per record (rs+0x1c, initially &records[i]) and reads the blob to rs+0x24. Per texture i:
   - record+0x00 = entry (its name), record+0x04 = entry + 0x28 + pad (the image chain), +0x08 = 0;
   - record word +0x22 |= 0x108: 0x100 = mipmapped, 0x008 = "data is an image chain" (use virtual 84);
   - record+0x18 = record+0x1c = 0xF (format class unknown until created);
   - record byte +0x23 |= 2 when the name starts with `n_fx_bank_` (`_strnicmp` 0x00544957, 10 chars): such
     textures keep LOD 0 (0x0050a510 Renderer_UpdateTextureLod);
   - immediately registers it: renderer virtual 73 (+0x124) = **0x0050bb30 Renderer_RegisterTexture**.
2. **0x0050bb30** sees flag 8 and calls virtual 84 (+0x150) = **0x0050c360** (texture, chain, 0).
3. **0x0050c360**: unless created (+0x23 bit 0x80), calls **0x00509eb0 Renderer_CreateTexture**(texture,
   level0 width, level0 height, level count, level0 D3DFORMAT). That clamps the size to renderer
   +0x14e4..+0x14f0, maps the format to a class (0x0050b1f0) and falls back to a supported class
   (0x0050ab40), and creates the D3D texture (D3DPOOL_MANAGED unless a render target).
   Then for each D3D level it picks the first stored level whose width and height are both >= the D3D
   level's (skipping larger stored levels; it stops advancing at the last one) and copies it with
   **0x0050c1b0 TextureImage_LoadIntoSurface** = D3DXLoadSurfaceFromMemory (statically linked 0x00545c10,
   source format = stored D3DFORMAT, pitch +0xA, rect = width x height, D3DX_FILTER_LINEAR). D3DX converts
   when the device format differs. (`Texture_FillLevelsFromMemory` 0x0050b040 is the other path, for records
   without flag 8; bank textures do not use it.)

The chain's memory (the blob, rs+0x24) stays allocated for the level's lifetime; the record keeps pointing
into it.

### Identification: by name

A texture is identified by its **name string** (record+0, max 31 chars), **case-insensitively**. There is no
stored hash per texture; the bank's table hashes the name at lookup time with **0x004f1100** (seed from the
file, see big-common.md) and confirms with `_stricmp` in **0x00488150**.
- **0x004881c0** (ecx = name, esi = render system) -> texture record pointer (rs+0x1c[index]) or null.
  15 call sites (0x004172a8, 0x004198fd, 0x0041c8f8, 0x0041c944, 0x0041f5c4, 0x00422bfe, 0x004262ab,
  0x00488914, 0x004896be, 0x004896f3, 0x00489728, 0x0048975d, 0x00493616, 0x004947cf, 0x00494b9f;
  E8-call scan of .text).
- **0x00488180** (ecx = name, esi = render system, edi = record or 0): repoints rs+0x1c[index of name] at
  another record (edi) or back at its own (edi = 0). One caller, 0x00494bc2 (not read). The game itself
  already has a by-name texture redirect.

So an override folder can be matched by `<texture name>.dds`, compared case-insensitively.

### Where to hook a replacement

- Simplest and earliest: in RenderSystem_LoadTextures (recovered, `archive/src/loader/auto_0045bbe0.cpp`), after
  record+0/+4 are set and before the virtual 73 call: if `override/<name>.dds` exists, build an image chain
  from it (u32 levels, then per level the 16-byte header + pixels, exactly as `txtr.chain_bytes()` does) in a
  heap block that lives as long as the level, and set record+4 to it. Everything downstream (size, format,
  level count) is read from the chain, so a larger or differently formatted DDS works, within the device's
  size limits and supported formats.
- Alternative: hook 0x0050c360 and substitute its chain argument by the record's name (record+0). Covers
  textures registered by other paths that also use flag 8.
- Keep in mind: `n_fx_bank_*` textures get a forced LOD 0; the renderer LOD bias (0x0050a510) drops top levels
  of other textures, so a high-resolution replacement may not show its top level unless the LOD rules are
  also changed (enhancement work, `archive/hook/enhance/`, off by default).

## Open questions

- Whether anything refers to textures by index (record order) rather than by name.
- The caller of 0x00488180 (0x00494bc2) and when it redirects textures.
- record+0x10/+0x14 are read as width/height by 0x0050a510 and as a size log by 0x0050b040; the loader does not
  set them; who does (probably 0x00509eb0) is not checked.
- Whether DXT2 alpha is really premultiplied in the art (it changes nothing for the game, only for editing).
