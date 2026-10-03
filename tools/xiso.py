"""Minimal XDVDFS (original Xbox disc filesystem) lister/extractor.

Usage:
  python tools/xiso.py list    <image>
  python tools/xiso.py extract <image> <out_dir> [--only PATTERN]

Handles trimmed xiso images and full disc dumps (game partition at a known offset). 2048-byte sectors only
(MODE1/2048 .bin files are plain images).
"""
import argparse
import fnmatch
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game partition offsets: none (xiso), XGD1 full dump, XGD2, XGD3, global (redump variants)
PARTITION_OFFSETS = [0, 0x18300000, 0xFD90000, 0x2080000, 0x89D80000]


def find_partition(f):
    for off in PARTITION_OFFSETS:
        f.seek(off + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return off
    sys.exit("no XDVDFS volume descriptor found")


def walk(f, base, dir_sector, dir_size, prefix=""):
    f.seek(base + dir_sector * SECTOR)
    data = f.read(dir_size)
    seen = set()
    stack = [0]
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, size, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF and right == 0xFFFF:
            continue
        name = data[off + 14:off + 14 + nlen].decode("latin1")
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)
        path = f"{prefix}{name}"
        if attr & 0x10:
            yield path + "/", start, size, True
            if size:
                yield from walk(f, base, start, size, path + "/")
        else:
            yield path, start, size, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["list", "extract"])
    ap.add_argument("image")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--only", default="*")
    a = ap.parse_args()

    with open(a.image, "rb") as f:
        base = find_partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root_sector, root_size = struct.unpack("<II", f.read(8))
        entries = sorted(walk(f, base, root_sector, root_size))
        if a.cmd == "list":
            print(f"partition offset 0x{base:x}, {sum(1 for e in entries if not e[3])} files")
            for path, start, size, is_dir in entries:
                if not is_dir:
                    print(f"{size:>12,}  {path}")
            return
        n = 0
        for path, start, size, is_dir in entries:
            if is_dir or not fnmatch.fnmatch(path.lower(), a.only.lower()):
                continue
            dest = os.path.join(a.out, *path.split("/"))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            f.seek(base + start * SECTOR)
            remaining = size
            with open(dest, "wb") as o:
                while remaining:
                    chunk = f.read(min(remaining, 1 << 22))
                    o.write(chunk)
                    remaining -= len(chunk)
            n += 1
        print(f"extracted {n} files to {a.out}")


if __name__ == "__main__":
    main()
