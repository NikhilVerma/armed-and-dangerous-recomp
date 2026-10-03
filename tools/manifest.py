"""Hash every file under a directory into docs/manifest.md (handover s4).

Usage: python tools/manifest.py orig/pc [--label "PC Steam build 251748"]

Prints a short summary; the full table goes to docs/manifest.md (appended as a new section).
"""
import argparse
import datetime
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    ap.add_argument("--label", default=None)
    args = ap.parse_args()

    base = os.path.abspath(args.directory)
    rows = []
    for dirpath, _, files in os.walk(base):
        for name in files:
            p = os.path.join(dirpath, name)
            rows.append((os.path.relpath(p, base).replace("\\", "/"), os.path.getsize(p), sha256(p)))
    rows.sort()

    label = args.label or os.path.relpath(base, ROOT).replace("\\", "/")
    today = datetime.date.today().isoformat()
    total = sum(r[1] for r in rows)
    out = os.path.join(ROOT, "docs", "manifest.md")
    new = not os.path.exists(out)
    with open(out, "a", encoding="utf-8", newline="\n") as f:
        if new:
            f.write("# Manifest\n\nSHA-256 of every original file. Addresses in notes are valid only for the "
                    "executable hash they name.\n")
        f.write(f"\n## {label} ({today})\n\n")
        f.write(f"Root: `{os.path.relpath(base, ROOT)}` - {len(rows)} files, {total:,} bytes.\n\n")
        f.write("| File | Bytes | SHA-256 |\n| --- | ---: | --- |\n")
        for rel, size, digest in rows:
            f.write(f"| `{rel}` | {size:,} | `{digest}` |\n")
    print(f"{len(rows)} files, {total:,} bytes -> docs/manifest.md")


if __name__ == "__main__":
    sys.exit(main())
