# `.big` kind `ff`: rumble (force feedback) samples

Status: 2026-10-03. Files: `<level>_big11_ff.big` (25). Magic `0x00FFDBAC`. Module:
`tools/formats/big_kinds/ff.py`. Roundtrip: `bigtool.py roundtrip "orig/pc/GameData/**/*_ff.big"` gives 25 ok.

Loader: **0x004e4d90 Rumble_Load** (recovered in `src/loader/auto_0048afb0.cpp`). It reads the sample count and
the hash block size, builds the hash table (compare callback 0x004e4d60), fixes it up with 0x004f1220, reads
`count * 0x840` bytes of samples into one allocation ("Rumble Samples") and then fills each sample's two wave
buffers with 0x004e52b0 Rumble_GenerateWave.

## Layout

Offsets from the start of the payload (after `u32 magic, u32 size`).

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 | Sample count n (bank +0x1c) | 0x004e4d90 first `Stream_ReadChecked(4)` |
| 0x04 | u32 + block | Name hash table, sized (big-common.md); value = sample index | 0x004e4d90, 0x004f1220 |
| after table | n x 0x840 | Samples | 0x004e4d90 reads `count * 0x840` |

All 25 files hold the same 22 samples (`playerrcktfire`, `areaclick`, ...).

### Sample (0x840 bytes)

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x000 | char[32] | Name, NUL-padded; the hash key | 0x004e4d60: `_stricmp(key, samples + index*0x840)` |
| 0x020 | desc (0x28) | Motor 0 wave descriptor | 0x004e4d90 passes sample+0x20 to 0x004e52b0 |
| 0x048 | f32[250] | Motor 0 wave, generated at load; zero in every file | 0x004e52b0 writes 250 floats |
| 0x430 | desc (0x28) | Motor 1 wave descriptor | 0x004e4d90 passes sample+0x430 |
| 0x458 | f32[250] | Motor 1 wave, generated at load; zero in every file | 0x004e52b0 |

### Wave descriptor (0x28 bytes)

| Offset | Type | JSON key | Meaning | Evidence |
| ---: | --- | --- | --- | --- |
| 0x00..0x14 | 6 x u32 | `w00`..`w14` | Unknown. Measured: w04, w0c, w14 always 0; w08 and w10 small floats (0.1, 0.2, 1.0) | no reader found in 0x004e4d90..0x004e5750 |
| 0x18 | f32 | `length` | Length (guess: seconds). Play time wraps modulo it; <= 0 marks the motor silent | 0x004e5460 loop `t -= length`; 0x004e5020 sets voice flag 1/0x10000 when <= 0; 0x004e5750 ends the voice after both lengths |
| 0x1c | f32 | `rate` | Wave speed | 0x004e52b0: ramp step rate*k, sine step rate*k, square step rate*k |
| 0x20 | f32 | `amplitude` | Amplitude | 0x004e52b0, 0x004e5460 (noise) |
| 0x24 | u32 | `shape` | 0 ramp up, 1 sine, 2 square, 3 noise, 4 ramp down | 0x004e52b0 switch; 0x004e5460 special-cases 3 |

## Dump

`rumble.json`: the hash seed and, per sample in index order, `name` and `motors` (two descriptors with the keys
above). Words are written as floats when their bits are a normal float, else as integers (`words.py`); both
round-trip bit-exactly. A non-zero wave buffer would be kept as `wave_hex` (none in the retail files).

## Rebuild and safe edits

Rebuild recomputes the count and the whole hash table (bucket count, buckets, chains) from the names, with the
stored seed (big-common.md invariants 3 and 4 hold, so unedited dumps are exact).

Safe:
- Change any descriptor value (`length`, `rate`, `amplitude`, `shape`, unknown words).
- Rename, add or remove samples (names up to 31 bytes). Lookup is by name, case-insensitive.

Not checked: whether anything addresses samples by index rather than name (0x004e5020 accepts an index when
no name is given), so reordering samples may change which effect an index-based caller plays.

## Open questions

- Meaning of `w00`..`w14` (guess: an envelope; w08/w10 look like attack/decay times).
- Who calls 0x004e5020 with an index instead of a name.
