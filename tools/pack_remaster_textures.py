"""Build the optional Remaster texture pack (the AI retextures, docs/retexture.md) from the per-level folders.

    python tools/pack_remaster_textures.py [--work work/remaster] [--out work/release/remaster_textures]

tools/remaster_textures.py writes each level's retextures to work/remaster/<level>/textures_remaster/<name>.dds; the
same texture appears in many levels (about 460 distinct files over 6,300 copies, 3.2 GB). This tool stores each
distinct file once and writes:

  <out>.zip            textures/<file> for every distinct file, plus manifest.json
  <out>.manifest.json  the same manifest, to commit as tools/data/remaster_textures.manifest.json when a pack is
                       published, so tools/setup_assets.py --remaster-textures can verify a download against it

manifest.json: {"format": 1,
                "files":  {"<file>": "<sha256>"},                    every file in textures/
                "levels": {"<level>": {"<name>": "<file>"}}}         what goes into <level>/textures_remaster/<name>.dds
<file> is <name>.dds; when one name has different contents in different levels, the variants after the first are
<name>@<first 8 hex of their sha256>.dds.

The pack is distributed separately (it is derived from the game's textures; never committed). Nothing is uploaded.
"""
import argparse
import hashlib
import json
import pathlib
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def collect(work):
    """files {file: (sha, source path)}, levels {level: {name: file}}"""
    files, by_name, levels = {}, {}, {}
    for d in sorted(p for p in work.iterdir() if (p / "textures_remaster").is_dir()):
        level = {}
        for src in sorted((d / "textures_remaster").glob("*.dds")):
            name, digest = src.stem, sha256(src)
            variants = by_name.setdefault(name, {})
            if digest not in variants:
                variants[digest] = f"{name}.dds" if not variants else f"{name}@{digest[:8]}.dds"
                files[variants[digest]] = (digest, src)
            level[name] = variants[digest]
        if level:
            levels[d.name] = level
    return files, levels


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--work", default=str(ROOT / "work" / "remaster"), help="the exported levels (default work/remaster)")
    ap.add_argument("--out", default=str(ROOT / "work" / "release" / "remaster_textures"),
                    help="output path without extension (default work/release/remaster_textures)")
    a = ap.parse_args()
    work, out = pathlib.Path(a.work), pathlib.Path(a.out)
    files, levels = collect(work)
    if not files:
        sys.exit(f"no <level>/textures_remaster/*.dds under {work} (run tools/remaster_textures.py first)")
    manifest = {"format": 1, "files": {f: files[f][0] for f in sorted(files)},
                "levels": {lv: dict(sorted(m.items())) for lv, m in sorted(levels.items())}}
    out.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(manifest, indent=1) + "\n"
    with zipfile.ZipFile(out.with_suffix(".zip"), "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for f in sorted(files):
            z.write(files[f][1], f"textures/{f}")
        z.writestr("manifest.json", text)
    out.with_suffix(".manifest.json").write_text(text, encoding="utf-8")
    copies = sum(len(m) for m in levels.values())
    size = sum(files[f][1].stat().st_size for f in files)
    renamed = sum("@" in f for f in files)
    print(f"{len(files)} distinct files ({size / 1e6:.0f} MB, {renamed} name variants) for {copies} copies in "
          f"{len(levels)} levels -> {out.with_suffix('.zip')} ({out.with_suffix('.zip').stat().st_size / 1e6:.0f} MB), "
          f"sha256 {sha256(out.with_suffix('.zip'))}")


if __name__ == "__main__":
    main()
