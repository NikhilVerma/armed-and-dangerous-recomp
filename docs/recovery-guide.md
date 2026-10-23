# Recovering a function: the procedure

How to turn one original function into compilable C++ in `src/` that replaces it in the running game.
Every rule here comes from a bug that actually happened.

## 1. Gather

```
.venv\Scripts\python.exe archive\tools\recover.py 0x004fe3a0 --asm
```

It prints the function's real calling convention (from `symbols/abi.csv`), whether the comparison scenarios
execute it, its callees' conventions, Ghidra's decompilation and the disassembly.

**The disassembly is the truth; the decompilation is a draft.** Ghidra mis-reads reused stack slots, register
arguments and float temporaries (0x004feee0 showed a fake `unaff_EDI`). Check every branch, constant, offset and
call argument against the disassembly.

## 2. Write

One function becomes: an implementation with a readable C++ signature, a small `_Abi` adapter, a generated
entry stub, and a `HOOK()` registration. Copy this shape (from `archive/src/renderer/buffers.cpp`):

```cpp
#include "gen/entry_stubs.h"   // ABI_ENTRY_XXXXXXXX macros (generated)
#include "gen/orig_calls.h"    // orig::fn_XXXXXXXX wrappers (generated)
#include "hooks.h"
#include "renderer.h"          // or game.h

// 0x004fe3a0 (edx = buffer): what it does, in one or two sentences, with evidence (offsets, D3D calls).
uint32_t IndexBuffer_Restore(IndexBuffer* ib) { ... }

static uint32_t IndexBuffer_Restore_Abi(uint32_t edx)          // parameters: register inputs in the order
{                                                              // eax ecx edx ebx esi edi, then stack args
    return IndexBuffer_Restore(reinterpret_cast<IndexBuffer*>(edx));
}
ABI_ENTRY_004fe3a0(IndexBuffer_Restore_Entry, IndexBuffer_Restore_Abi)
HOOK(0x004fe3a0, "IndexBuffer_Restore", IndexBuffer_Restore_Entry, nullptr);
```

The `_Abi` adapter must take exactly the inputs listed by `recover.py` (`abi: inputs [...] stack N`), in that
order, all `uint32_t`, and return `uint32_t` (or `uint64_t` when it returns `eax edx`, `float` for `st0`). An
input you do not use is still a parameter (comment it out by name).

### Calling original code

- `orig::fn_XXXXXXXX(regs..., stack...)` calls any original function with its real convention: parameters are
  its register inputs in the order eax ecx edx ebx esi edi, then its stack arguments. Floats pass as bits
  (`orig::f2u(1.0f)`), pointers as `uint32_t`.
- **Pass every register input the ABI lists, with the value the original caller had in that register.** The
  original caller sometimes loads a register for another reason (a null test) and the callee reads it: 0x0040f8c0
  reads EDX = Game+0xa68 and leaving it out turned the menu black.
- Recovered functions call each other directly as C++ (declare the prototype at the top of your file).
- Virtual methods: `VCall<Ret>(object, slot, args...)`. COM: call the real interface (`r->device->...`).
- Imports the game reaches through a thunk (e.g. `Direct3DCreate9` at 0x00565518) go through that thunk with
  `GameFunction<...>(0x...)`, so the hook DLL's import hooks still apply.
- Variadic C runtime/log functions: `GameLog(fmt, args...)` for 0x004e0b30; others via `GameFunction`.

### Data

- Named members of known structs (`Renderer`, `Game`, ...) where they exist; `Field<T>(object, 0xOFFSET)` for
  anything not yet named. Do not edit shared headers in a parallel run; leave struct naming for later (S3).
- **Pass the game's own string constants by address** (`reinterpret_cast<const char*>(0x005893c0)`), never your
  own copies: the UI keeps the pointers.
- **Use the game's literal constants, not SDK macros**: the game passes 31 to `Direct3DCreate9`, the modern
  SDK's `D3D_SDK_VERSION` is 32.
- **The game's matrix is 0x44 bytes:** 16 floats plus a flags word at +0x40 (0x004e2b40 reads it, 0x004e2eb0
  writes it). A local matrix passed to original code must be `float m[17]` / `uint32_t m[17]`; 16 entries
  corrupts the stack (0x00504270). In general, size every local buffer from the original's stack frame, not
  from what the type "should" be.
- Floats: the game runs x87 at single precision (Direct3D sets it). Compute in `float` in the same order as the
  disassembly (`fild`/`fmul`/`fstp` -> `static_cast<float>(int) * k`). Float-to-int casts truncate (`_ftol`).
- Signed versus unsigned comparisons follow the jump instruction (`jl/jg` signed, `jb/ja` unsigned).
- Return values: when a branch returns whatever happened to be in EAX, return the same value if it is cheap to
  reproduce; callers occasionally use it.

## 3. Check

```
pwsh archive\tools\check_compile.ps1 archive\src\renderer\yourfile.cpp
```

compiles one file (no output, safe to run in parallel). Fix every error and warning.

The behavioural check is `tools\compare.py`: original game versus recovered code in deterministic scenarios,
compared frame by frame on the Direct3D call stream. Only one game instance runs at a time, so in parallel runs
the integrator runs it; agents only compile-check.

## Skip, and say why, when

- `recover.py` notes `unsupported` (EBP input) or `no ret found`.
- The function is not executed in recorded runs (it cannot be verified yet) - unless told otherwise.
- It uses a calling convention or construct you cannot express safely.
