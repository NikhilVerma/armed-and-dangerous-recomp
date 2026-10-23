"""Push recovered knowledge into the Ghidra project (the single writer, AGENTS.md), so exports improve.

Applies, idempotently:
  1. function names from symbols/functions.csv (anything not FUN_*)
  2. function signatures from symbols/abi.csv with custom register storage: register inputs become parameters
     named after their register, stack arguments become arg0.., returns EAX / EDX:EAX / ST0 / void
  3. structures for every class in symbols/types/data_members.csv (members named at their offsets)
  4. hand-recovered types and globals (Game, Renderer, AppConfig, timer globals) from TYPES/GLOBALS below
Then re-run archive/tools/gh_export.py to refresh work/decomp/.

Usage: python archive/tools/gh_apply.py [--no-signatures]
"""
import argparse
import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(1, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))  # compare.py, instances.py (shared with the oracle tools)
import gh  # noqa: E402

ROOT = gh.ROOT

# Hand-recovered layouts (offset, name, kind) mirroring archive/src/. kind: u8 u16 u32 f32 ptr:<Type> or bytes:<n>.
TYPES = {
    "Game": (0x2720, [
        (0x000, "vtbl", "ptr:void"), (0x028, "moviePlayer", "ptr:void"), (0x02c, "renderer_2c", "ptr:void"),
        (0x038, "renderer", "ptr:void"), (0x4b0, "field_4b0", "f32"), (0x568, "shellFlags", "u8"),
        (0x748, "field_748", "u32"), (0x74c, "loadingLevel", "u8"), (0x74d, "pendingLevelRequest", "u8"),
        (0x84e, "pendingLevelName", "bytes:520"), (0xa64, "levelManager", "ptr:void"), (0xa68, "field_a68", "u32"),
        (0xa7c, "field_a7c", "ptr:void"), (0xa98, "postProcess", "ptr:void"), (0xab4, "ui", "ptr:void"),
        (0xab8, "saveLoader", "ptr:void"), (0xdce, "cinematicActive", "u8"),
    ]),
    "Renderer": (0x1adc, [
        (0x9b0, "hwnd", "u32"), (0x9e8, "field_9e8", "u32"), (0x9ec, "field_9ec", "u32"),
        (0x9f0, "modeCount", "u32"), (0x9f4, "modes", "bytes:2400"), (0x1354, "desktopMode", "bytes:16"),
        (0x1364, "capsSummary", "bytes:288"), (0x1484, "deviceWindow", "u32"),
        (0x1490, "presentParams", "bytes:56"), (0x14c8, "d3d", "ptr:void"), (0x14cc, "device", "ptr:void"),
        (0x14d0, "modeWidth", "u32"), (0x14d4, "modeHeight", "u32"), (0x14d8, "modeBpp", "u32"),
        (0x14dc, "modeFlags", "u8"), (0x1628, "renderFlags", "u8"), (0x1634, "startWidth", "u32"),
        (0x1638, "startHeight", "u32"), (0x163c, "startBpp", "u32"),
    ]),
    "AppConfig": (0x58, [
        (0x00, "present", "u32"), (0x04, "displayFlags", "u32"), (0x08, "width", "u32"), (0x0c, "height", "u32"),
        (0x10, "bpp", "u32"), (0x1c, "enable_1c", "u8"), (0x20, "field_20", "u32"), (0x24, "field_24", "f32"),
        (0x48, "field_48", "u32"), (0x4c, "enable_4c", "u8"), (0x50, "field_50", "u32"), (0x54, "field_54", "u8"),
    ]),
}
GLOBALS = [
    (0x005fe490, "g_game", "ptr:Game"),
    (0x00604a90, "g_frameDt", "f32"),        # Timer_Tick 0x004e47e0
    (0x00604a94, "g_frameDtUnscaled", "f32"),
    (0x00604a98, "g_gameTime", "f32"),
    (0x00604aa0, "g_timeScale", "f32"),
    (0x00604aac, "g_timerPauseCount", "u32"),
    (0x00604ab8, "g_fixedDtOverride", "f32"),
    (0x00604af8, "g_startTickCount", "u32"),
    (0x0063a0c4, "g_renderer", "ptr:void"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-signatures", action="store_true")
    a = ap.parse_args()

    with gh.open_program() as flat:
        program = flat.getCurrentProgram()
        from ghidra.program.model.data import (CategoryPath, StructureDataType, ByteDataType, WordDataType,
                                               DWordDataType, FloatDataType, PointerDataType, ArrayDataType,
                                               Undefined4DataType, Undefined8DataType, Float10DataType,
                                               VoidDataType, DataTypeConflictHandler)
        from ghidra.program.model.listing import ParameterImpl, ReturnParameterImpl
        from ghidra.program.model.listing import Function as GFunction
        from ghidra.program.model.symbol import SourceType
        from ghidra.program.model.data import DataUtilities

        fm = program.getFunctionManager()
        dtm = program.getDataTypeManager()
        lang = program.getLanguage()
        af = program.getAddressFactory().getDefaultAddressSpace()
        cat = CategoryPath("/and")
        stats = collections.Counter()

        def reg(name):
            return lang.getRegister(name.upper())

        tx = program.startTransaction("and: apply recovered knowledge")
        try:
            # --- structures --------------------------------------------------------------------------------
            def kind_type(kind):
                if kind == "u8":
                    return ByteDataType.dataType
                if kind == "u16":
                    return WordDataType.dataType
                if kind == "f32":
                    return FloatDataType.dataType
                if kind.startswith("bytes:"):
                    return ArrayDataType(ByteDataType.dataType, int(kind[6:]), 1)
                if kind.startswith("ptr:"):
                    target = kind[4:]
                    t = VoidDataType.dataType if target == "void" else dtm.getDataType(cat, target)
                    return PointerDataType(t or VoidDataType.dataType)
                return DWordDataType.dataType

            def make_struct(name, size, members):
                s = StructureDataType(cat, name, size, dtm)
                for off, mname, dt in sorted(members, key=lambda m: m[0]):
                    if off + dt.getLength() > size:
                        continue
                    try:
                        s.replaceAtOffset(off, dt, dt.getLength(), mname, None)
                    except Exception:
                        stats["member conflicts"] += 1
                dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER)
                stats["structs"] += 1

            for name, (size, members) in TYPES.items():
                make_struct(name, size, [(o, n, kind_type(k)) for o, n, k in members])

            by_class = collections.defaultdict(list)
            for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "types", "data_members.csv"))):
                by_class[r["class"]].append(r)
            for cls, rows in by_class.items():
                members = []
                end = 0
                for r in rows:
                    off, size = int(r["offset"], 16), int(r["size"]) * max(int(r["count"]), 1)
                    if size <= 0 or size > 0x10000:
                        continue
                    dt = {1: ByteDataType.dataType, 2: WordDataType.dataType, 4: DWordDataType.dataType}.get(
                        size, ArrayDataType(ByteDataType.dataType, size, 1))
                    members.append((off, r["member"], dt))
                    end = max(end, off + size)
                if members and cls not in TYPES:
                    make_struct(cls, (end + 3) & ~3, members)

            # --- globals -----------------------------------------------------------------------------------
            st = program.getSymbolTable()
            for addr, name, kind in GLOBALS:
                at = af.getAddress(addr)
                st.createLabel(at, name, SourceType.USER_DEFINED)
                try:
                    DataUtilities.createData(program, at, kind_type(kind), -1, False,
                                             DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA)
                    stats["globals"] += 1
                except Exception:
                    stats["global conflicts"] += 1

            # --- function names ------------------------------------------------------------------------------
            for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "functions.csv"))):
                if r["name"].startswith("FUN_") or not r["name"]:
                    continue
                f = fm.getFunctionAt(af.getAddress(int(r["address"], 16)))
                if f and f.getName() != r["name"]:
                    try:
                        f.setName(r["name"], SourceType.USER_DEFINED)
                        stats["names"] += 1
                    except Exception:
                        stats["name conflicts"] += 1

            # --- signatures --------------------------------------------------------------------------------
            if not a.no_signatures:
                for r in csv.DictReader(open(os.path.join(ROOT, "symbols", "abi.csv"))):
                    if r["notes"]:
                        continue
                    f = fm.getFunctionAt(af.getAddress(int(r["address"], 16)))
                    if f is None or f.isThunk():
                        continue
                    params = []
                    for rg in r["inputs"].split():
                        params.append(ParameterImpl(f"{rg}_in", Undefined4DataType.dataType, reg(rg), program))
                    for k in range(int(r["stack_args"])):
                        params.append(ParameterImpl(f"arg{k}", Undefined4DataType.dataType, 4 + 4 * k, program))
                    ret = r["returns"]
                    if ret == "st0":
                        retp = ReturnParameterImpl(Float10DataType.dataType, reg("ST0"), program)
                    elif "edx" in ret:
                        from ghidra.program.model.listing import VariableStorage
                        retp = ReturnParameterImpl(Undefined8DataType.dataType,
                                                   VariableStorage(program, reg("EDX"), reg("EAX")), program)
                    elif ret == "none":
                        retp = ReturnParameterImpl(VoidDataType.dataType, program)
                    else:
                        retp = ReturnParameterImpl(Undefined4DataType.dataType, reg("EAX"), program)
                    from java.util import ArrayList
                    jparams = ArrayList()
                    for p in params:
                        jparams.add(p)
                    try:
                        f.updateFunction(None, retp, jparams, GFunction.FunctionUpdateType.CUSTOM_STORAGE, True,
                                         SourceType.ANALYSIS)
                        if r["cleanup"] == "callee":
                            f.setStackPurgeSize(4 * int(r["stack_args"]))
                        stats["signatures"] += 1
                    except Exception as e:
                        stats["signature errors"] += 1
                        if stats["signature errors"] <= 3:
                            print("signature error", r["address"], e)
        finally:
            program.endTransaction(tx, True)
        print(dict(stats))


if __name__ == "__main__":
    main()
