# `.big` kind `aud`: sound bank

Status: 2026-10-03. Files: `<level>_big08_aud.big` (25, 11 to 27 MB each). Magic `0xDEAFDEAF`. Module:
`tools/formats/big_kinds/aud.py`. Roundtrip: `bigtool.py roundtrip "orig/pc/GameData/**/*_aud.big"` gives 25 ok
(about 60 s).

Loader: the dispatcher 0x00449e80 calls `renderer_2c` vtable +0x80. The vtable is at 0x0058a230; its
constructors 0x004f9ca0 and 0x004f9d10 store it. Slot +0x80 (0x0058a2b0) holds **0x004fab80**, which is not a
function in the Ghidra export yet; read from the disassembly. It reads the hash block (compare callback
0x004f4cc0, fixup 0x004f1220 at 0x004fac99), the count (+0x78), one more word (+0x8c), then each sound. When
the loop ends it calls 0x004fa7f0 per sound, which creates a DirectSound buffer through 0x004fa6d0 and copies
the PCM in. Name lookups (`Audio : %s not found`) go through 0x004f2470 and similar functions: they hash the
name and index the pointer array at +0x80.

## Layout

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 + block | Name hash table, sized (big-common.md); value = sound index | 0x004fab80 (0x004fac28..0x004fac99) |
| +0 | u32 | Sound count n (audio +0x78) | 0x004fab80 at 0x004faca8 |
| +4 | u32 | Unknown (audio +0x8c); no reader found. Close to, but not equal to, the summed record sizes | 0x004fab80 at 0x004facba |
| +8 | n x sound | Sounds in index order | loop 0x004fad10..0x004faf31 |

### Sound

| Part | Size | Condition | Evidence |
| --- | ---: | --- | --- |
| header | 0xfc | always | read of 0xfc at 0x004fad2b, copied with `rep movsd` x 0x3f |
| PCM data | header.size | flags & 0x40 clear | allocates `[header+0xf0]` bytes, reads them, stores the pointer at header+0xf8 (0x004fae86..0x004faf19) |
| stream path | 0x80 | flags & 0x40 set | read of 0x80 at 0x004fad74 |
| next count k | u32 | flags & 0x40 set | read of 4 at 0x004fadb5; stored at record+0x17c |
| next names | k x 32 | flags & 0x40 set | read of `k << 5` into record+0x180 (0x004fae6b) |

### Header (0xfc bytes)

| Offset | Type | JSON | Meaning | Evidence |
| ---: | --- | --- | --- | --- |
| 0x00 | char[32] | `name` | Sound name, the hash key | 0x004f4cc0 `_stricmp(key, sounds[value])` (record starts with the header) |
| 0x20 | u32 | `flags` | 0x40 = streamed; flags & 0x1010040 = no DirectSound buffer at load | 0x004fad4e; 0x004fa7f0 |
| 0x24..0x78 | 22 x u32 | `params` | Unknown. Non-zero ones are kept by offset. Measured: 0x24 always set (15), 0x30/0x34 floats such as 20.0/500.0 (guess: min/max distance), 0x48/0x4c set on 281 of 1308 | survey |
| 0x7c, 0x9c, 0xbc | char[32] x 3 | `links` | Names of related sounds (for example `alm_airraid_start`, `none`) | survey; no reader checked |
| 0xdc | u16 | (override `format_tag`) | 0 in every file. The game always builds WAVE_FORMAT_PCM | 0x004fa6d0 sets the tag to 1 itself |
| 0xde | u16 | from WAV | Channels | 0x004fa6d0 |
| 0xe0 | u32 | from WAV | Sample rate | 0x004fa6d0 |
| 0xe4 | u32 | derived | Bytes per second = rate x block align (holds in all 29,971 sounds) | survey |
| 0xe8 | u16 | derived | Block align = channels x bits / 8 (holds in all) | survey |
| 0xea | u16 | from WAV | Bits per sample (16 in every file) | 0x004fa6d0 |
| 0xec | u32 | (override) | 0 in every file | survey |
| 0xf0 | u32 | derived / `stream_size` | PCM byte count. For streamed sounds, the size of `GameData\<path>`, which matches all 15,200 streamed sounds | 0x004fae86; survey |
| 0xf4 | u32 | derived | Frames = size / block align (holds in all) | survey |
| 0xf8 | u32 | (override) | 0 in the file; at run time, the PCM pointer | 0x004faecc |

The formats in the retail files are 16-bit PCM, mono at 11025, 22050 or 44100 Hz, or stereo at 22050 or 44100
Hz. No ADPCM or other compressed data is present, and the game only creates PCM buffers.

Streamed sounds (`flags & 0x40`) store no samples. Their path names a raw PCM file under `GameData\streams\`.
The 0x80-byte path field holds stale memory after the NUL (stack addresses, old file names). The `next` names
list other streamed sounds that can follow this one (for example `l1amb` lists `l1amb_002`, `stealth_004`,
...); this is a guess from the data, since the reader was not traced.

## Dump

- `sounds.json`: `hash_seed`, `unknown_8c`, and per sound in index order:
  - In-memory sounds: `name`, `file`, `flags`, `params`, `links`.
  - Streamed sounds: the same, plus `channels`, `rate`, `bits`, `stream_size`, `stream` (the path; when the field
    has stale bytes, `{"s": path, "raw": hex}`) and `next`.
  - `override` appears only if a stored derived value differs from what the rebuild would compute. No retail
    sound has one.
- `sounds/<name>.wav`: each in-memory sound as a standard PCM WAV. The data chunk holds the stored samples byte
  for byte. File names are the sound names; names that are unsafe or duplicate (case-insensitive) get an index
  prefix.

## Rebuild and safe edits

The rebuild recomputes the hash table, the count and, for WAV sounds, channels, rate, bits, block align,
bytes/s, size and frames from the WAV header.

Safe:
- Replace a WAV with any PCM WAV: 8- or 16-bit, mono or stereo, any rate and length. WAVE_FORMAT_EXTENSIBLE
  with the PCM subtype is accepted. Compressed WAVs (ADPCM, MP3) are rejected with a message, because the game
  copies the bytes straight into a PCM buffer.
- Edit `params`, `links` or `flags`, except the 0x40 bit, which must match whether the entry has a `file`.
- Rename, add or remove sounds (names up to 31 bytes). Lookups are by name.

Caveats:
- `unknown_8c` is kept verbatim. If the game uses it as a memory budget, which is not checked, a much larger
  bank may need it raised.
- For streamed sounds, the rebuild trusts `stream_size`. If you replace a `streams\*.raw` file, update
  `stream_size` (frames are derived from it).
- Index-based references to sounds were not checked (0x004f2470 also accepts an index directly), so
  reordering may be unsafe.

## Open questions

- What the word at +0x8c is (audio +0x8c). It is near the total record size (for lev_1_m1, `all records +
  0xfc each` is 19,200 short) but not equal to it.
- The meaning of the 22 parameter words and the three link names. The sound playback functions around
  0x004f32f0..0x004f4e40 were not read.
- What happens to sounds with flags 0x10000 or 0x1000000, which get no DirectSound buffer at load.
- The format of `streams\*.raw` (presumably headerless PCM, since the size matches frames x block align) and the
  streaming code that uses `next`.
