"""Ghidra query tool over pyghidra (handover s13: tools, not dumps).

Every subcommand prints a small structured result. The Ghidra project lives in work/ghidra/ (ignored).
There is one writer for the project: only `import` and `rename` modify it.

Usage (from the repo root, with the venv active):
  python tools/gh.py import                 # first import + auto-analysis of the unpacked game.exe
  python tools/gh.py stats                  # function counts: total, thunks, library (FID), named, rest
  python tools/gh.py func 0x401000          # one function: name, size, callers, callees
  python tools/gh.py decomp 0x401000        # Ghidra decompilation of one function
  python tools/gh.py xrefs 0x5a1234         # references to an address
  python tools/gh.py strrefs "filesys"      # functions referencing strings that contain a substring
  python tools/gh.py rename 0x401000 Name   # set a function name (writer only)
  python tools/gh.py define list.json       # create functions at addresses only data refers to (writer only)

GHIDRA_INSTALL_DIR must point at the Ghidra install (see docs/environment.md).
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "orig", "pc", "GameData", "game.unpacked.exe")
PROJECT_DIR = os.path.join(ROOT, "work", "ghidra")
PROJECT_NAME = "and"
PROGRAM_NAME = "game.unpacked.exe"


def out(obj):
    print(json.dumps(obj, indent=1, default=str))


def open_program(analyze=False):
    import pyghidra
    pyghidra.start()
    os.makedirs(PROJECT_DIR, exist_ok=True)
    # open_program imports the binary on first use and reuses the project afterwards.
    return pyghidra.open_program(BINARY, project_location=PROJECT_DIR, project_name=PROJECT_NAME,
                                 analyze=analyze)


def addr(program, text):
    return program.getAddressFactory().getDefaultAddressSpace().getAddress(int(text, 16))


def func_at(program, text):
    fm = program.getFunctionManager()
    a = addr(program, text)
    f = fm.getFunctionContaining(a)
    if f is None:
        sys.exit(f"no function contains {text}")
    return f


def describe(f, monitor):
    return {
        "address": f"0x{f.getEntryPoint().getOffset():08x}",
        "name": f.getName(),
        "size": f.getBody().getNumAddresses(),
        "signature": str(f.getSignature()),
        "calling_convention": f.getCallingConventionName(),
        "thunk": f.isThunk(),
        "callers": sorted(f"0x{c.getEntryPoint().getOffset():08x} {c.getName()}"
                          for c in f.getCallingFunctions(monitor))[:40],
        "callees": sorted(f"0x{c.getEntryPoint().getOffset():08x} {c.getName()}"
                          for c in f.getCalledFunctions(monitor))[:40],
    }


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("import")
    sub.add_parser("stats")
    for name in ("func", "decomp", "xrefs"):
        p = sub.add_parser(name)
        p.add_argument("address")
    p = sub.add_parser("strrefs")
    p.add_argument("text")
    p = sub.add_parser("rename")
    p.add_argument("address")
    p.add_argument("name")
    p = sub.add_parser("define")
    p.add_argument("file", help="JSON list of addresses (writer only)")
    args = ap.parse_args()

    with open_program(analyze=(args.cmd == "import")) as flat:
        program = flat.getCurrentProgram()
        from ghidra.util.task import ConsoleTaskMonitor
        monitor = ConsoleTaskMonitor()
        fm = program.getFunctionManager()

        if args.cmd == "import":
            out({"program": program.getName(), "functions": fm.getFunctionCount(),
                 "image_base": str(program.getImageBase())})

        elif args.cmd == "stats":
            total = thunks = named = default = 0
            sizes = []
            for f in fm.getFunctions(True):
                total += 1
                if f.isThunk():
                    thunks += 1
                    continue
                n = f.getName()
                if n.startswith("FUN_"):
                    default += 1
                    sizes.append(f.getBody().getNumAddresses())
                else:
                    named += 1
            sizes.sort()
            out({"total": total, "thunks": thunks, "named_by_analysis_or_fid": named,
                 "unnamed": default,
                 "unnamed_size_median": sizes[len(sizes) // 2] if sizes else 0,
                 "unnamed_size_p90": sizes[int(len(sizes) * 0.9)] if sizes else 0,
                 "unnamed_bytes": sum(sizes)})

        elif args.cmd == "func":
            out(describe(func_at(program, args.address), monitor))

        elif args.cmd == "decomp":
            from ghidra.app.decompiler import DecompInterface
            f = func_at(program, args.address)
            d = DecompInterface()
            d.openProgram(program)
            r = d.decompileFunction(f, 60, monitor)
            print(r.getDecompiledFunction().getC() if r.decompileCompleted() else r.getErrorMessage())

        elif args.cmd == "xrefs":
            a = addr(program, args.address)
            refs = []
            for r in program.getReferenceManager().getReferencesTo(a):
                src = r.getFromAddress()
                f = fm.getFunctionContaining(src)
                refs.append({"from": f"0x{src.getOffset():08x}", "type": str(r.getReferenceType()),
                             "function": f.getName() if f else None})
                if len(refs) >= 100:
                    break
            out({"address": args.address, "count": len(refs), "refs": refs})

        elif args.cmd == "strrefs":
            from ghidra.program.util import DefinedStringIterator
            hits = []
            for data in DefinedStringIterator.forProgram(program):
                s = str(data.getValue())
                if args.text.lower() not in s.lower():
                    continue
                users = sorted({fm.getFunctionContaining(r.getFromAddress()).getName()
                                for r in program.getReferenceManager().getReferencesTo(data.getAddress())
                                if fm.getFunctionContaining(r.getFromAddress())})
                hits.append({"string": s[:120], "at": f"0x{data.getAddress().getOffset():08x}",
                             "functions": users[:10]})
                if len(hits) >= 50:
                    break
            out(hits)

        elif args.cmd == "define":
            # Functions reached only through data (vtables, callbacks) that auto-analysis left undefined.
            from ghidra.app.cmd.disassemble import DisassembleCommand
            from ghidra.app.cmd.function import CreateFunctionCmd
            wanted = json.load(open(args.file))
            made, skipped = [], []
            tx = program.startTransaction("define functions")
            try:
                for s in wanted:
                    a = addr(program, s)
                    if fm.getFunctionAt(a) or fm.getFunctionContaining(a):
                        skipped.append(s)
                        continue
                    DisassembleCommand(a, None, True).applyTo(program, monitor)
                    if CreateFunctionCmd(a).applyTo(program, monitor):
                        made.append(s)
                    else:
                        skipped.append(s)
            finally:
                program.endTransaction(tx, True)
            out({"defined": len(made), "skipped": len(skipped), "skipped_sample": skipped[:20]})

        elif args.cmd == "rename":
            from ghidra.program.model.symbol import SourceType
            f = func_at(program, args.address)
            tx = program.startTransaction("rename")
            try:
                f.setName(args.name, SourceType.USER_DEFINED)
            finally:
                program.endTransaction(tx, True)
            out({"address": args.address, "name": args.name})


if __name__ == "__main__":
    main()
