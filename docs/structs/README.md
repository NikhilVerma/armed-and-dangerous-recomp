# The struct atlas

What every object offset in the recovered C++ (archive/src/) means, gathered once so that porting agents look a field up
instead of re-deriving `Field<float>(obj, 0x9f0)` from the disassembly. Tool: `tools/struct_atlas.py`; data:
work/struct_atlas.json (not committed, rebuilt in about a minute); committed summaries: one file per main class here.

## Look a field up (porting agents start here)

```
python tools/struct_atlas.py field GenericAIObject 0x490   # one field: names, type, meaning + evidence, functions, sites
python tools/struct_atlas.py class PlayerObject --min-n 3  # one line per field (--all adds the inherited ones)
python tools/struct_atlas.py prop patrol_speed             # where a level / template property is stored (--like: substring)
```

The answers are a few lines each. A field of a base class (Object +0x454 health) is shown for every subclass; the
subclasses' own records are listed with `--all`.

## Rebuild it

```
python tools/struct_atlas.py classes      # class model: descriptors, vtables, factory names (work/struct_atlas_classes.json)
python tools/struct_atlas.py static       # parse archive/src/**/*.cpp (work/struct_atlas_static.json), ~15 s
ORACLE_OUT=work/oracle-structatlas AND_HOOK_BUILD=build/hook-structatlas \
  python tools/struct_atlas.py run walk courtyard ...   # the original off-screen with struct snapshots (needs the hook build)
ORACLE_OUT=work/oracle-structatlas python tools/struct_atlas.py infer   # -> work/struct_atlas.json, coverage, precision
python tools/struct_atlas.py docs         # these files + symbols/types/data_members_proposed.csv
```

## Where each piece of evidence comes from

- **Classes**: the game's own class descriptors (vtable slot 1 returns a descriptor: +8 size, +0x14 `CLASS(Name)` /
  `CLASSV(Name)`, then member-table entries; a `BASECLASS(...)` entry's 4th word is the base class's descriptor, which
  gives the hierarchy). Vtables without a descriptor (slot 1 ReturnZero: the UI objects, ...) are named from the
  factory tables (game_04.cpp kCreateCases + game_03.cpp kBehaviours) as `obj:<behaviour name>`, base Object (assumed).
- **Static accesses**: every `Field<T>(base, 0xOFF)`, file-local accessor (`D(base + 0xOFF)`, `Fl(...)`, `By(...)`, ...;
  detected from each file's helper definitions) and raw cast in archive/src/. The class is the one of the base expression:
  the method's `this` (the function sits in that class's vtable: the lowest common ancestor of every vtable holding
  it; `this` is the parameter the ABI wrapper passes ecx in), a parameter typed by the function's comment (`ecx = AI
  object`, `stack: player, ...`), by the tracker name's class prefix, or by its callers (an argument that is a typed
  `this` elsewhere, propagated four rounds), plus aliases (`const uint32_t o = u(obj);`) and the Game singleton
  (0x005fe490). An offset beyond the class's size is not attributed (`beyond`: usually a too-general comment).
  Per access: type, read / write, constants compared with or stored (float constants resolved from .rdata), masks,
  whether it is used as a pointer (base of another access, virtual call, release).
- **Properties**: calls of the property readers (0x004533a0 string, 0x00453400 / 0x00453450 value, 0x004534a0
  float, 0x004534f0 bool, the template getters) with a `base + 0xOFF` destination and a string constant; parameter
  tables `{'F', 0xOFF, name, default}`; a bool read into a local that then sets a bit (`flag`); weaker: a statement
  that names one string and writes one field (`statement`: often a sound / fx resource name, not a property).
- **Dynamic**: the hook's `[test] struct_snap` (archive/hook/src/teststructsnap.cpp) writes, every N play frames of an oracle
  scenario, the raw bytes of up to K live objects per vtable (World +0xe70 plus the player), each object's position
  from vt+0x54, the frame time, and every few snapshots the first dword of what each pointer-like dword points at.
- **Known names** used for checks: symbols/types/data_members.csv (the game's member tables), the fields the oracle
  trace reads (archive/hook/src/testoracle.cpp), and `+0x...` mentions in docs/subsystems (shown, not trusted).

## Meaning labels (each with its evidence and a confidence 0..1)

| Label | Test |
| --- | --- |
| property "name" | static: the property reader writes it (0.9) |
| bit 0x.. = property "name" | static: a bool property sets that bit (0.8) |
| countdown timer | dynamic: changes equal minus the game clock's step (>= 60% of changes, >= 3) |
| stopwatch / accumulator | dynamic: changes equal plus the clock's step |
| frame counter / countdown | dynamic: integer changes equal the frames between samples |
| position x (+4 y, +8 z) | dynamic: the triple equals the object's position (vt+0x54) in >= 90% of samples |
| world matrix translation | dynamic: (off, off+0x10, off+0x20) equal the position (row-major 4x4, matrix at off-0xc) |
| velocity | dynamic: the triple matches the position's derivative between samples (moving samples, >= 70%) |
| health-like | dynamic: a float that only falls; higher when it is <= 0 at the traced death of that object |
| pointer to Class | dynamic: values are other snapshotted objects or point at that class's vtable; static: dereferenced |
| flags bitfield | static: masked / set / cleared with constants; dynamic: changes flip one or two bits |
| state / enum | static: equality tests against >= 3 constants; dynamic: a small set of values that changes |
| bool | static: only 0 / 1 stored into a byte |
| constant per object | dynamic: never changed while traced (weak: the scenarios are short) |

**Contradictions** are listed per field (e.g. integer masks on a value that is a float at run time, a width that
differs from the game's member table): read them before trusting a field. Types: when the C++ only copies a dword
(`D(a) = D(b)`) and the snapshots show floats, the type is f32 ("type_from dynamic").

Everything here is **inferred** unless it cites the game's member table or the trace. Proposed names for
symbols/types/data_members.csv are in symbols/types/data_members_proposed.csv (status `inferred`, provenance
`struct_atlas:<evidence>`); data_members.csv itself stays the game's own tables (tools/data_tables.py regenerates it).
