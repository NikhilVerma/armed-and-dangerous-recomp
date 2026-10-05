You are recovering game functions of Armed and Dangerous (2003, PC, 32-bit x86, MSVC 7.1, link-time code
generation) into compilable C++ in the decompilation project at C:\Users\nikhi\and-decomp. The project goal is a
full decompilation of the game's own code (libraries such as Havok, qhull, D3DX, the CRT and zlib are excluded and
already fenced off).

HARD RULES
- STATIC WORK ONLY. Do not run the game or anything that runs it: no tools/compare.py, tools/bench.py,
  tools/run.py, tools/check.ps1, tools/deploy.ps1, tools/instances.py. Do not run tools/build.ps1 (other agents
  build in parallel). The only check you run is `pwsh tools\check_compile.ps1 <your .cpp>` (compiles one file).
- Write ONLY your own new file(s): BATCH_FILE.cpp (and optionally the same name with .h). Do not edit any existing
  file: no shared headers (game.h, renderer.h, game/*.h ...), no symbols/*.csv, no docs, no src/gen/*. Other agents
  work on other address ranges at the same time. Put any struct or helper you need in your own file.
- Read AGENTS.md, docs/recovery-guide.md (the procedure: follow it exactly) and docs/patterns.md (register ABIs;
  "Pushes that outlive a call") first. Model files: src/game/shadows.cpp, src/renderer/scene_pass.cpp,
  src/renderer/render_r4.cpp (LTCG argument building fixed after a crash), src/renderer/render_r0.cpp.
- Faithful recovery: same behaviour, same order of calls and memory accesses, same float operation order (x87 at
  single precision: compute in float in the disassembly's order), same return values when cheap. The disassembly
  is the truth; Ghidra's decompilation is a draft. Do not fix bugs (report them).
- Names: descriptive only with evidence (strings, verified behaviour, class names: an object vtable's slot 1
  returns a descriptor whose +0x14 is "CLASS(Name)"); otherwise fn_XXXXXXXX. Each function gets a comment:
  address, ABI, what it does, evidence. Label guesses as guess.

YOUR FUNCTIONS: the addresses under key "BATCH_KEY" in symbols/recovery_batches.json (all at stage S0; their
subsystem and classifier evidence are in symbols/functions.csv and work/classify/*.csv). For each one:
1. `.venv\Scripts\python.exe tools\recover.py <addr> --asm` (ABI, callers, callees with their ABIs and whether
   they are already recovered, Ghidra draft, disassembly).
2. Write it per docs/recovery-guide.md: implementation + `_Abi` adapter taking exactly the ABI's inputs (register
   inputs in the order eax ecx edx ebx esi edi, then stack args) + `ABI_ENTRY_<addr>` (src/gen/entry_stubs.h) +
   `HOOK(<addr>, "<Name>", <Name>_Entry, nullptr);` (use plain HOOK lines, not a wrapper macro). Call original
   callees with orig::fn_XXXXXXXX (pass every register input the ABI lists, with the value the original caller
   had in it); call functions recovered in your own file directly. Virtual calls: VCall<R>(object, slot, args...).
   Data: Field<T>(object, offset), GameGlobal<T>(address), g_game (game/game_object.h).
3. Check every callee's `ret` before mapping pushes to arguments: a callee that ends in a plain `ret` with
   unpopped pushes means those pushes are the next call's arguments (docs/patterns.md). symbols/abi.csv often
   overstates stack-argument counts: check the caller's `add esp` / the callee's `ret N` and which stack slots it
   actually reads. x87 results returned in st0: return float only when the callers pop st0. Game matrices are
   0x44 bytes (float m[17]). Size locals from the original stack frame.
4. Recover functions even when "not executed in recorded runs" and without direct callers (virtual methods: find
   the vtable(s) and slot by scanning .rdata of orig/pc/GameData/game.unpacked.exe with pefile, and the class name
   via slot 1); mark them "not executed in recorded runs" in the comment. SKIP (with the reason) only: `unsupported`
   (EBP input), `no ret found` that is not a clean tail jump (a tail jump with the original argument frame intact
   can be reproduced as call-then-return with a hand-written entry stub, see src/renderer/render_r6.cpp), jumps into
   the middle of another function, or anything you cannot express safely. A function that is library code after
   all (Havok/CRT/D3DX) is skipped and reported.
5. Write the file early and append as you go; compile-check every few functions: `pwsh tools\check_compile.ps1
   BATCH_FILE.cpp` must print `ok` with no warnings (the vswhere line is harmless). Never leave the file
   non-compiling when you stop.

Work in address order; large functions are fine (block by block, keep the original stack frame as one array when
it has many slots). Budget your effort so every function is recovered or skipped with a reason.

FINAL ANSWER (your return value, under 400 words): counts recovered / skipped; skipped functions with reasons;
original bugs noticed (address, one line); tracker or ABI errors found; open questions. (The integrator reads the
names from your HOOK lines.)
