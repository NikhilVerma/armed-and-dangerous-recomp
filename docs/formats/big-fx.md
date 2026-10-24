# `.big` kind `fx`: effect scripts

Status: 2026-10-03. Files: `<level>_big09_fx.big` (25). Magic `0x00EFFEC5`. Module:
`tools/formats/big_kinds/fx.py`. Roundtrip: `bigtool.py roundtrip "orig/pc/GameData/**/*_fx.big"` gives 25 ok.

Loader: **0x0041c7e0 Fx_LoadChunk** (recovered in `archive/src/loader/auto_00410ec0.cpp`). It reads the hash block size
`t`, then reads the rest of the payload (`size - 4` bytes) into one allocation "Fx Data" (fx +0x0c/+0x18), sets
fx +0x10 = data + t (start of the script area), and fixes up the table in place (0x004f1220, compare
0x0041c7b0). It then binds the particle texture banks `n_fx_bank_1` / `n_fx_bank_2`; those are textures in the
`txtr` bank, not part of this file.

## Layout

| Offset | Type | Meaning | Evidence |
| ---: | --- | --- | --- |
| 0x00 | u32 + block | Name hash table, sized (big-common.md); value = byte offset of the fx in the script area | 0x0041c7e0; 0x0041c7b0 `_stricmp(key, fx+0x10 + value)` |
| after table | bytes | Script area: the fx back to back in offset order, no count, no gaps, nothing after the last | measured in all 25 files |

Every value is distinct, the smallest is 0, and the fx tile the area exactly (measured). The table is the
original tool's layout over (name, offset) in storage order (big-common.md invariants 3 and 4), so it is
recomputed from the names.

### One fx

| Part | Type | Meaning | Evidence |
| --- | --- | --- | --- |
| name | string slot | The fx name (hash key) | 0x0041cb60 returns the script start = name + slot size |
| script | instructions | Run by the interpreter until opcode 0x04 | 0x00420770 |

**String slot**: the text, a NUL, zero padding up to `(strlen + 0x20) & ~0x1f` bytes (32 for names of up to 31
characters, 64 for 32..63). Every string operand and the fx name use this rule (0x0041cb60 and each string case
of 0x00420770). The padding is zero in every file (checked on dump).

**Instruction**: `u32 opcode`, then operands. Dispatch: `movzx eax, byte [opcode - 4 + 0x00423250]`,
`jmp [eax*4 + 0x00423088]` at 0x0042084b; opcodes outside 4..0x104 or mapped to case 0x71 are "Fx System got
unknown opcode". Mapping: opcode 4..0x4f -> case opcode-4; 0x50..0x53 -> case 0x4b; 0x54..0x6f -> case
opcode-8; 0x70, 0x71 -> cases 0x54, 0x55; 0x72..0x79 -> case opcode-10; 0x104 -> case 0x70.

Operand grammar per jump-table case (the `case` numbers of Ghidra's decompilation of 0x00420770; for cases
below 0x4b the opcode is case + 4), as `GRAMMAR` in fx.py holds it: w = 32-bit word, s = string slot, L = u32
count + that many string slots, W = u32 count + that many words. It was read from how each case of 0x00420770 advances the
script pointer, and it is confirmed by the data: all 15,517 fx in the 25 files parse to the exact end of their
slot and end with opcode 0x04.

| Grammar | Cases |
| --- | --- |
| none | 0x00, 0x02, 0x03, 0x10-0x12, 0x2a, 0x2e-0x30, 0x38-0x3a, 0x47-0x4a, 0x58, 0x5e, 0x66, 0x67, 0x6b, 0x6f |
| w | 0x4c, 0x4f, 0x52, 0x54 |
| ww | 0x04, 0x06, 0x07, 0x0c-0x0e, 0x18, 0x1a, 0x1c-0x22, 0x26, 0x44, 0x57, 0x59, 0x5a, 0x68 |
| www | 0x1b, 0x23-0x25, 0x37, 0x42, 0x43, 0x45, 0x46, 0x50, 0x51, 0x56 |
| wwww | 0x05, 0x08-0x0a, 0x27, 0x28, 0x4e, 0x53, 0x55, 0x6c, 0x6d |
| wwwww | 0x3d, 0x41, 0x63-0x65 |
| wwwwww | 0x16, 0x2d, 0x3c, 0x3e-0x40, 0x5c |
| wwwwwww | 0x5f-0x61 |
| wwwwwwwww | 0x29 |
| s | 0x01, 0x0b, 0x0f, 0x19, 0x2b, 0x33, 0x4d, 0x69, 0x6a, 0x6e |
| sww | 0x13-0x15, 0x2c, 0x31 |
| swwww | 0x3b, 0x5b |
| swwwwww | 0x17 |
| swwwwwwww | 0x32 |
| ws | 0x62 |
| wws | 0x5d |
| Lw | 0x34-0x36 (opcodes 0x38-0x3a: pick one name at random, then a word) |
| L | 0x70 (opcode 0x104: name list for the next instruction) |
| W | 0x4b (opcodes 0x4f..0x53: 1, 2 or 4 floats, expanded to 4) |

Opcodes whose meaning is clear from the interpreter. Most others are named only by their helper's address.

| Opcode | Operands | Meaning | Evidence |
| --- | --- | --- | --- |
| 0x04 | | End of script | case 0: sets the done flags |
| 0x05 | s | Create object by name, make it current | 0x004234d0 |
| 0x06 / 0x07 | | Push / pop the current object | 0x00423430 / 0x00423470 |
| 0x08 | min, max | Life span, random in [min, max] | 0x0041d110 "Failed to set lifespan" |
| 0x0f / 0x2f | s | Start another fx by name | cases 0x0b / 0x2b call 0x0041dd50 (looks the name up through 0x0041cb60) |
| 0x12 | min, max | Pause; script resumes here next update | 0x0041dea0 "Invalid Pause location" |
| 0x13 | s | Create object by name (uses a preceding 0x104 list) | 0x00423680 |
| 0x15 | | Wait | 0x0041dee0 "Invalid Wait location" |
| 0x104 | L | Name list (arg7) consumed by the next instruction | case 0x70 |

Words are typed by the instruction, which the grammar does not record. Pairs are usually (min, max) float
ranges fed to 0x0046e2c0, and counts are integers.

## Dump

- `fx.json`: the hash seed and, in storage order, each fx's `name` and `file`.
- `fx/<name>.json`: one fx per file, one instruction per line, for example `["0x08", 0.1, 0.1]` or
  `["0x104", ["null"]]`. The opcode is a hex string. Each word is written as a float when its bits are a
  normal float, else as an integer, and both forms round-trip bit-exactly (`words.py`). Strings are plain JSON
  strings. Counted lists are JSON lists, and their count is recomputed.
- `fx/<name>.bin`: the raw script, used only if an fx does not parse with the grammar. No retail fx needs it.

## Rebuild and safe edits

The rebuild recomputes string slot sizes, list counts, each fx's offset and the hash table, so sizes can change
freely.

Safe:
- Edit operand values, insert or delete instructions, or change strings to any length. Each instruction must
  match its opcode's grammar; the rebuild rejects a wrong operand count.
- Rename, add (point a new entry at a copy of a file), remove or reorder fx. Lookups are by name.

Unsafe or unchecked:
- An integer where the game expects a float, or the reverse, is written as given. Keep the type the dump used.
- Names referenced inside scripts (objects `fxo_*`, sounds, other fx) are resolved at run time against other
  banks. The rebuild does not check them.

## Open questions

- The meaning of most opcodes. Each case's helper address is in the 0x00420770 decompilation.
- Whether anything outside the interpreter keeps byte offsets into the script area. The fx instance stores a
  pointer to the current instruction (+4) only at run time.
