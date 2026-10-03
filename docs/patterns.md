# Patterns

Recurring compiler idioms, data idioms and how to recognise them. Each entry: the pattern, an example
address, and how to use it.

## Member-description table entries (data)

Six u32 words: `offset, size, count, 0, name_ptr, type_code`, with `name_ptr` pointing at a
`"DATA(Class::member)"` string. Consecutive entries describe consecutive members of one class.

- Example: 0x0058423c `{0x554, 4, 1, 0, -> "DATA(PlayerWeapon::m_weaponDescriptor)", 2}`.
- Use: real member names and offsets for struct recovery. `tools/data_tables.py` regenerates
  `symbols/types/data_members.csv`. Treat as provenance `string`.

## VC7.x CRT entry

`WinMainCRTStartup` at 0x539238: `push 0x60; push <SEH table>; call __SEH_prolog; mov edi, 0x94;
call _alloca_probe; ... call [GetVersionExA]`. A detector heuristic flagged this as "PELock"; it is not.
