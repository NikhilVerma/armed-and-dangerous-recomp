"""Dump and rebuild the game's `.big` level files (docs/formats/big.md).

Usage:
  python tools/formats/bigtool.py dump <file.big> <outdir>     split into entries + manifest
  python tools/formats/bigtool.py rebuild <outdir> <file.big>  rebuild from a dump
  python tools/formats/bigtool.py roundtrip [glob ...]         dump + rebuild every file, compare bytes
                                                               (default: every .big under orig/pc/GameData)

A `.big` file is one chunk: u32 magic, u32 payload size, payload. The magic selects the loader in the game
(dispatcher 0x00449e80) and the module here: tools/formats/big_kinds/<kind>.py. Each module provides

  KIND = "txtr"; MAGIC = 0x0B00B1E5
  dump(payload: bytes, out: pathlib.Path) -> None     write the payload's parts as files under `out`
  rebuild(src: pathlib.Path) -> bytes                 the exact payload back from those files

The dump must be editable (one file per asset where the format has assets, JSON for tables) and the rebuild
must reproduce the original bytes exactly when nothing was edited. `big.json` (written here) records the magic.
"""
import argparse
import glob
import importlib
import json
import os
import pathlib
import shutil
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

KINDS = {
    0xDEADC0DE: "text", 0x0B00B1E5: "txtr", 0x00C0FFEE: "anim", 0xFADEBACC: "land", 0x0BADF00D: "skin",
    0x00BEADED: "prop", 0x00BADA55: "obj", 0xDEAFDEAF: "aud", 0x00EFFEC5: "fx", 0xBABEBABE: "inst",
    0x00FFDBAC: "ff", 0x00B0D1E5: "ui", 0x0ADDBEEF: "font",
}


def kind_module(kind):
    try:
        return importlib.import_module(f"big_kinds.{kind}")
    except ModuleNotFoundError as e:
        if e.name != f"big_kinds.{kind}":
            raise
        return importlib.import_module("big_kinds.raw")


def dump(path, out):
    data = pathlib.Path(path).read_bytes()
    magic, size = struct.unpack_from("<II", data)
    if size != len(data) - 8:
        raise ValueError(f"{path}: size field {size} != file size - 8 ({len(data) - 8})")
    kind = KINDS.get(magic)
    if kind is None:
        raise ValueError(f"{path}: unknown magic 0x{magic:08x}")
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    mod = kind_module(kind)
    (out / "big.json").write_text(json.dumps({"magic": f"0x{magic:08X}", "kind": kind,
                                              "module": mod.__name__, "source": pathlib.Path(path).name},
                                             indent=1))
    mod.dump(data[8:], out)
    return kind, mod.__name__


def rebuild(src):
    src = pathlib.Path(src)
    meta = json.loads((src / "big.json").read_text())
    magic = int(meta["magic"], 16)
    payload = importlib.import_module(meta["module"]).rebuild(src)
    return struct.pack("<II", magic, len(payload)) + payload


def roundtrip(files):
    results = {}
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="bigrt-"))
    try:
        for f in files:
            out = tmp / pathlib.Path(f).stem
            try:
                kind, module = dump(f, out)
                ok = rebuild(out) == pathlib.Path(f).read_bytes()
                err = None if ok else "rebuilt bytes differ"
            except Exception as e:  # report and keep going
                kind, module, ok, err = "?", "?", False, f"{type(e).__name__}: {e}"
            results[f] = (kind, module, ok, err)
            shutil.rmtree(out, ignore_errors=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return results


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dump")
    d.add_argument("file")
    d.add_argument("out")
    r = sub.add_parser("rebuild")
    r.add_argument("src")
    r.add_argument("file")
    t = sub.add_parser("roundtrip")
    t.add_argument("patterns", nargs="*")
    a = ap.parse_args()
    if a.cmd == "dump":
        kind, module = dump(a.file, a.out)
        print(f"{a.file}: {kind} ({module}) -> {a.out}")
    elif a.cmd == "rebuild":
        data = rebuild(a.src)
        pathlib.Path(a.file).write_bytes(data)
        print(f"{a.src} -> {a.file} ({len(data)} bytes)")
    else:
        pats = a.patterns or [str(ROOT / "orig" / "pc" / "GameData" / "**" / "*.big")]
        files = sorted(f for p in pats for f in glob.glob(p, recursive=True))
        res = roundtrip(files)
        by_kind = {}
        for f, (kind, module, ok, err) in res.items():
            by_kind.setdefault((kind, module), [0, 0])[0 if ok else 1] += 1
            if not ok:
                print(f"FAIL {os.path.relpath(f, ROOT)}: {err}")
        for (kind, module), (good, bad) in sorted(by_kind.items()):
            print(f"{kind:5} {module:24} {good} ok, {bad} failed")
        return 1 if any(not v[2] for v in res.values()) else 0


if __name__ == "__main__":
    sys.exit(main())
