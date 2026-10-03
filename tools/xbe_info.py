"""Small structured summary of an original Xbox executable (.xbe), plus a string census.

Usage: python tools/xbe_info.py <file.xbe> [--strings PATTERN]

Reports base address, timestamp, debug path, sections, XDK library versions, and counts of source paths,
assert-like strings and C++ names; with --strings, prints strings matching a regex.
"""
import argparse
import datetime
import hashlib
import re
import struct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--strings", default=None)
    a = ap.parse_args()
    b = open(a.file, "rb").read()
    assert b[:4] == b"XBEH", "not an XBE"
    base, = struct.unpack_from("<I", b, 0x104)

    def at(va):  # header addresses are virtual; headers are mapped at base
        return va - base

    def cstr(va):
        o = at(va)
        return b[o:b.index(b"\0", o)].decode("latin1") if 0 <= o < len(b) else None

    ts, = struct.unpack_from("<I", b, 0x114)
    nsec, sec_va = struct.unpack_from("<II", b, 0x11C)
    dbg_path, dbg_file = struct.unpack_from("<II", b, 0x14C)
    nlib, lib_va = struct.unpack_from("<II", b, 0x160)
    print(f"{a.file}\n  size {len(b):,} sha256 {hashlib.sha256(b).hexdigest()}")
    print(f"  base 0x{base:x} built {datetime.datetime.fromtimestamp(ts, datetime.timezone.utc).isoformat()}")
    print(f"  debug path: {cstr(dbg_path)}  file: {cstr(dbg_file)}")
    for i in range(nsec):
        o = at(sec_va) + i * 56
        flags, va, vsize, raw, rsize, name_va = struct.unpack_from("<IIIIII", b, o)
        print(f"  section {cstr(name_va):<10} va 0x{va:08x} vsize 0x{vsize:06x} raw 0x{raw:06x} rsize 0x{rsize:06x}")
    libs = []
    for i in range(nlib):
        o = at(lib_va) + i * 16
        name = b[o:o + 8].rstrip(b"\0").decode("latin1")
        major, minor, build, flags = struct.unpack_from("<HHHH", b, o + 8)
        libs.append(f"{name} {major}.{minor}.{build}{' debug' if flags & 0x8000 else ''}")
    print("  libraries:", ", ".join(libs))

    strings = [s.decode("latin1") for s in re.findall(rb"[\x20-\x7e]{5,}", b)]
    src = sorted({s for s in strings if re.search(r"\.(cpp|c|h|hpp)\b", s, re.I)})
    asserts = [s for s in strings if re.search(r"assert|ASSERT|failed|!=|==", s)]
    cxx = sorted({s for s in strings if re.fullmatch(r"[A-Za-z_]\w*::~?[A-Za-z_]\w*", s)})
    print(f"  strings {len(strings)}; source paths {len(src)}; assert-like {len(asserts)}; Class::member names {len(cxx)}")
    if a.strings:
        rx = re.compile(a.strings)
        for s in sorted({s for s in strings if rx.search(s)}):
            print("   ", s[:200])


if __name__ == "__main__":
    main()
