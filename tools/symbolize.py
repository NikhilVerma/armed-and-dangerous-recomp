"""Map hook-DLL offsets (as printed in and.log crash reports: "hook dll +b642f") to function + source line, using
the PDB next to build/hook-release/dinput8.dll (dbghelp through ctypes; 32-bit DLL loaded as a virtual module).

Usage: python tools/symbolize.py 0x434c6 0xb642f ...      (offsets from the DLL base)
       python tools/symbolize.py --log work/instances/main/and.log   (every "hook dll +X" of the first crash)
"""
import argparse
import ctypes
import os
import re
from ctypes import wintypes

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "build", "hook-release", "dinput8.dll")
BASE = 0x10000000


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wintypes.ULONG), ("TypeIndex", wintypes.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wintypes.ULONG), ("Size", wintypes.ULONG), ("ModBase", ctypes.c_uint64),
                ("Flags", wintypes.ULONG), ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64),
                ("Register", wintypes.ULONG), ("Scope", wintypes.ULONG), ("Tag", wintypes.ULONG),
                ("NameLen", wintypes.ULONG), ("MaxNameLen", wintypes.ULONG), ("Name", ctypes.c_char * 512)]


class IMAGEHLP_LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wintypes.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wintypes.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("offsets", nargs="*")
    ap.add_argument("--log")
    a = ap.parse_args()
    offsets = [int(o, 16) for o in a.offsets]
    if a.log:
        text = open(a.log, encoding="utf-8", errors="ignore").read()
        crash = text[text.find("CRASH code"):]
        m = re.search(r"CRASH code \w+ at ([0-9A-Fa-f]+)", crash)
        frames = re.findall(r"\((?:hook dll) \+([0-9a-f]+)\)", crash.split("wrote and-crash.dmp")[0])
        base_guess = None
        first = re.search(r"([0-9a-f]{8}) \(hook dll \+([0-9a-f]+)\)", crash)
        if m and first:
            base_guess = int(first.group(1), 16) - int(first.group(2), 16)
            offsets.append(int(m.group(1), 16) - base_guess)
        offsets += [int(f, 16) for f in frames]
    dbghelp = ctypes.WinDLL("dbghelp")
    proc = ctypes.c_void_p(0x1234)  # a pseudo handle: dbghelp only needs a unique value without a process
    dbghelp.SymSetOptions(0x2 | 0x10)  # UNDNAME | LOAD_LINES
    if not dbghelp.SymInitialize(proc, os.path.dirname(DLL).encode(), False):
        raise SystemExit("SymInitialize failed")
    dbghelp.SymLoadModuleEx.restype = ctypes.c_uint64
    dbghelp.SymLoadModuleEx.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p,
                                        ctypes.c_uint64, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
    if not dbghelp.SymLoadModuleEx(proc, None, DLL.encode(), None, BASE, os.path.getsize(DLL), None, 0):
        raise SystemExit("SymLoadModuleEx failed")
    for off in offsets:
        addr = BASE + off
        sym = SYMBOL_INFO()
        sym.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 512
        sym.MaxNameLen = 511
        disp = ctypes.c_uint64()
        name = "?"
        if dbghelp.SymFromAddr(proc, ctypes.c_uint64(addr), ctypes.byref(disp), ctypes.byref(sym)):
            name = f"{sym.Name.decode()}+{disp.value:#x}"
        line = IMAGEHLP_LINE64()
        line.SizeOfStruct = ctypes.sizeof(IMAGEHLP_LINE64)
        d32 = wintypes.DWORD()
        where = ""
        if dbghelp.SymGetLineFromAddr64(proc, ctypes.c_uint64(addr), ctypes.byref(d32), ctypes.byref(line)):
            path = line.FileName.decode()
            try:
                path = os.path.relpath(path, ROOT)
            except ValueError:  # another drive (e.g. the build's FetchContent sources)
                pass
            where = f"{path}:{line.LineNumber}"
        print(f"+{off:x}  {name}  {where}")
    dbghelp.SymCleanup(proc)


if __name__ == "__main__":
    main()
