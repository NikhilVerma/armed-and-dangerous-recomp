# Patterns

Recurring compiler idioms, data idioms and how to recognise them. Each entry: the pattern, an example
address, and how to use it.

## Member-description table entries (data)

Six u32 words: `offset, size, count, 0, name_ptr, type_code`, with `name_ptr` pointing at a
`"DATA(Class::member)"` string. Consecutive entries describe consecutive members of one class.

- Example: 0x0058423c `{0x554, 4, 1, 0, -> "DATA(PlayerWeapon::m_weaponDescriptor)", 2}`.
- Use: real member names and offsets for struct recovery. `tools/data_tables.py` regenerates
  `symbols/types/data_members.csv`. Treat as provenance `string`.

## Link-time code generation: arguments in any register (code)

The game was built with whole-program optimisation. Many functions take arguments in EAX, EDX, ESI, EDI or
EBX, and callers can rely on a callee leaving a register untouched. Consequences for recovered code:

- Calling an original function from C needs an inline-asm shim that loads exactly its register inputs.
- **Hidden inputs:** a callee may read a register its caller happened to hold, e.g. `0x0040f8c0` reads
  EDX = `Game+0xa68`, which the original caller loaded only for a null test. Missing it turned the front-end
  black (brightness never set). Check every callee with `archive/tools/reginputs.py` and confirm at runtime.
- Do not use EDX/ECX as a scratch call register in a shim unless the callee is known not to read it.
- A recovered function entered from original code needs a naked entry stub matching its register ABI
  (example: `Game_StartLevel_Entry` in `archive/src/game/start_level.cpp`).

## Game string pointers (data)

Pass the game's own string constants (e.g. `0x005bc12c "MainMenu"`) where the original does, not copies.
Not proven necessary yet, but systems may keep or compare the pointers.

## VC7.x CRT entry

`WinMainCRTStartup` at 0x539238: `push 0x60; push <SEH table>; call __SEH_prolog; mov edi, 0x94;
call _alloca_probe; ... call [GetVersionExA]`. A detector heuristic flagged this as "PELock"; it is not.

## Pushes that outlive a call: argument lists built across several calls (code)

LTCG lets the compiler push arguments for a later call before earlier calls, when it knows those earlier callees
neither read nor pop them. Example (0x00488b00 and MeshGroup_Draw 0x0048a290): renderer virtuals +0x68 and +0x64
are one-instruction getters (`lea eax, [ecx+0x6c]` / `lea eax, [ecx+0xb0]`, plain `ret`), yet the caller does
`push n; push &palette; call [vt+0x68]; push eax; call [vt+0x64]; push eax; call [vt+0x5c]`. Nothing is popped
until +0x5c (`ret 0x10`), whose four arguments are (view = +0x64's result, world = +0x68's result, palette, n).

- Recovering it as `vt68(palette, n)` breaks the stack (VCall assumes the callee pops what it was given): it
  crashed the comparison. Recover it as `w = vt68(); v = vt64(); vt5c(v, w, palette, n)`.
- How to spot it: a `call` whose target ends in a plain `ret` (or `ret N` smaller than what was pushed) but whose
  caller does not `add esp` afterwards; check every callee's `ret` before mapping pushes to arguments.
