"""The Remaster preset's textures: the AI retextures (GameData/overrides/textures/<name>.png, docs/retexture.md) for
one exported level, made ready for the engine (engine/src/graphics/textures.rs).

  python tools/remaster_textures.py lev_1_m1                 -> work/remaster/lev_1_m1/textures_remaster/<name>.dds

For every texture of the level (work/remaster/<level>/textures/<name>.dds) with an override PNG (names compared
ignoring case):
  - colour lock: the override's broad colour is pulled back to the original's, out = cand + up(orig - down(cand))
    (down: box to the original's size, up: bicubic; in linear light, as the GPU filters sRGB textures), so shrunk
    back to the original's size it is the original. The AI's detail finer than one original texel stays; its colour
    and brightness drifts (fx flashes up to +50 levels, measured 2026-10-15) do not.
  - alpha: kept from the override where the original has alpha (cut-outs, blends), forced opaque where the original
    is opaque, so a material's alpha behaviour never changes (scene.json `alpha` / `premultiplied` stay valid: the
    lock is against the stored, possibly premultiplied, texels).
  - a full mip chain (box filter in linear light), BC1 (opaque) or BC3 (alpha), DDS, so the engine loads it as fast
    as the originals.
Kept original: data textures (docs/retexture.md "Data textures": terrain blend masks, lightmaps), normal maps (the
AI's relief changed the lighting: n_b_t_snow made the snow 7 levels brighter), overrides no larger than the original,
and those failing the fidelity gate (shrunk back, more than GATE levels from the original after the lock: the
override's shapes differ, e.g. fx_snow_flakes' large white flakes for the original's dim dots, 41.9).
Game-derived output: never committed (work/ is ignored).
"""
import argparse
import concurrent.futures
import io
import json
import os
import pathlib
import re
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main_checkout():
    """The main checkout (orig/ and work/ live there; a git worktree has neither)."""
    git = ROOT / ".git"
    if git.is_file():  # a worktree: "gitdir: <main>/.git/worktrees/<name>"
        gitdir = pathlib.Path(git.read_text().split(":", 1)[1].strip())
        return gitdir.parents[2]
    return ROOT


DEFAULT_OVERRIDES = main_checkout() / "orig" / "pc" / "GameData" / "overrides" / "textures"
# data, not pictures (docs/retexture.md 2026-10-12): upscaled blend masks smeared the dirt layer into the snow
# the fidelity gate: mean byte difference (alpha-weighted) between the locked result shrunk back and the original;
# DXT and edge residue stay under ~10 (2026-10-15, lev_1_m1: median 2.4)
GATE = 12.0
DATA = re.compile(r"^(b_a_blend|.*_ground_light|.*_lightmap_|.*_mask)")


def resize_f(a, size, method):
    """Resize a float array (H, W) with Pillow's float mode."""
    return np.asarray(Image.fromarray(a.astype(np.float32), "F").resize(size, method), dtype=np.float64)


def lock(cand, orig):
    """cand (H, W) float, orig (h, w) float: cand + up(orig - down(cand))."""
    h, w = orig.shape
    H, W = cand.shape
    err = orig - resize_f(cand, (w, h), Image.BOX)
    return cand + resize_f(err, (W, H), Image.BICUBIC)


def dds(levels, fmt):
    """One DDS file from per-level images, each encoded by Pillow (DXT1 / DXT5), with the mip count set."""
    header, data = None, []
    for im in levels:
        b = io.BytesIO()
        im.save(b, "DDS", pixel_format=fmt)
        raw = b.getvalue()
        if header is None:
            header = bytearray(raw[:128])
        data.append(raw[128:])
    flags = struct.unpack_from("<I", header, 8)[0] | 0x20000  # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", header, 8, flags)
    struct.pack_into("<I", header, 28, len(levels))
    caps = struct.unpack_from("<I", header, 108)[0] | 0x400008  # DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    struct.pack_into("<I", header, 108, caps)
    return bytes(header) + b"".join(data)


def to_linear(c):
    c = np.clip(c / 255.0, 0.0, 1.0)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return 255.0 * np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def convert(job):
    name, orig_path, png_path, out_path, srgb = job
    try:
        orig_im = Image.open(orig_path)
        orig = np.asarray(orig_im.convert("RGBA"), dtype=np.float64)
        cand_im = Image.open(png_path).convert("RGBA")
    except Exception as e:  # noqa: BLE001 (a texture Pillow cannot read stays original)
        return name, f"skipped: {e}"
    W, H = cand_im.size
    h, w = orig.shape[:2]
    if W <= w and H <= h:
        return name, "skipped: not larger than the original"
    cand = np.asarray(cand_im, dtype=np.float64)
    # the lock in the space the GPU filters in: linear light for sRGB textures (the sampler averages the finer
    # detail in linear light; locked in sRGB bytes the result came out brighter than the original, +6 red on the
    # snow at frame 520), raw values for data (normal maps)
    enc, dec = (to_linear, to_srgb) if srgb else (lambda c: c / 255.0, lambda c: np.clip(c, 0.0, 1.0) * 255.0)
    rgb_l = np.stack([lock(enc(cand[..., c]), enc(orig[..., c])) for c in range(3)], axis=-1)
    rgb_l = np.clip(rgb_l, 0.0, 1.0)
    opaque = orig[..., 3].min() >= 250
    if opaque:
        alpha = np.full((H, W), 255.0)
    elif cand[..., 3].min() >= 250:
        # the override lost the alpha: the original's, enlarged
        alpha = resize_f(orig[..., 3], (W, H), Image.BICUBIC)
    else:
        alpha = cand[..., 3]
    # mips: box filter in the filtering space
    levels_l = [(rgb_l, alpha)]
    while levels_l[-1][0].shape[:2] != (1, 1):
        c, a = levels_l[-1]
        lh, lw = c.shape[:2]
        size = (max(1, lw // 2), max(1, lh // 2))
        levels_l.append((np.stack([resize_f(c[..., i], size, Image.BOX) for i in range(3)], axis=-1),
                         resize_f(a, size, Image.BOX)))
    levels = []
    for c, a in levels_l:
        px = np.clip(np.concatenate([dec(c), a[..., None]], axis=-1) + 0.5, 0, 255).astype(np.uint8)
        levels.append(Image.fromarray(px, "RGBA"))
    top = levels[0]
    fmt = "DXT1" if opaque else "DXT5"
    if opaque:
        levels = [im.convert("RGB") for im in levels]
    out_path.write_bytes(dds(levels, fmt))
    # fidelity: the result shrunk back (in the filtering space) vs the original: mean absolute difference in bytes,
    # weighted by the original's alpha (the colour under alpha 0 never shows)
    topf = np.asarray(top, dtype=np.float64)
    back = np.stack([dec(resize_f(enc(topf[..., i]), (w, h), Image.BOX)) for i in range(3)], axis=-1)
    wgt = orig[..., 3:] / 255.0
    shift = float((np.abs(back - orig[..., :3]) * wgt).sum() / max(wgt.sum() * 3, 1e-6))
    if shift > GATE:
        out_path.unlink(missing_ok=True)
        return name, f"kept original: fidelity gate (shift {shift:.2f} > {GATE})"
    return name, f"{W}x{H} {fmt}{'' if srgb else ' linear'} (orig {w}x{h}, shift {shift:.2f})"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("level")
    ap.add_argument("--overrides", default=str(DEFAULT_OVERRIDES))
    ap.add_argument("--work", default=str(ROOT / "work" / "remaster"))
    ap.add_argument("--force", action="store_true", help="convert again even when the output is newer")
    ap.add_argument("--only", help="a name pattern (regex) to convert")
    a = ap.parse_args()
    level_dir = pathlib.Path(a.work) / a.level
    src = level_dir / "textures"
    dst = level_dir / "textures_remaster"
    dst.mkdir(exist_ok=True)
    overrides = {p.stem.lower(): p for p in pathlib.Path(a.overrides).iterdir() if p.suffix.lower() == ".png"}
    # the engine samples normal maps as data (gamematerial.rs load_tex srgb false); everything else as sRGB colour
    normal_maps = set()
    for m in (level_dir / "models").glob("*.json"):
        try:
            for sub in json.loads(m.read_text()).get("submeshes", []):
                if (sub.get("material") or {}).get("normal_map"):
                    normal_maps.add(sub["material"]["normal_map"].lower())
        except (ValueError, AttributeError):
            pass
    jobs, data = [], []
    for t in sorted(src.glob("*.dds")):
        name = t.stem.lower()
        if a.only and not re.search(a.only, name):
            continue
        if name not in overrides:
            continue
        # normal maps stay original: the AI's added relief changed the lighting itself (the snow's n_b_t_snow,
        # locked: the ground at frame 520 came out 7 levels brighter, the sun term is not linear in the normal)
        if DATA.match(name) or name in normal_maps:
            data.append(name)
            (dst / f"{name}.dds").unlink(missing_ok=True)
            continue
        out = dst / f"{name}.dds"
        if not a.force and out.exists() and out.stat().st_mtime > max(overrides[name].stat().st_mtime, t.stat().st_mtime):
            continue
        jobs.append((name, t, overrides[name], out, name not in normal_maps))
    print(f"{a.level}: {len(jobs)} to convert, {len(data)} data textures kept original ({', '.join(data)})")
    report = {}
    with concurrent.futures.ProcessPoolExecutor(max_workers=max(1, (os.cpu_count() or 4) - 2)) as ex:
        for name, msg in ex.map(convert, jobs):
            report[name] = msg
            print(f"  {name}: {msg}")
    log = dst / "report.json"
    old = json.loads(log.read_text()) if log.exists() else {}
    old.update(report)
    log.write_text(json.dumps(old, indent=1, sort_keys=True))
    print(f"{sum(1 for p in dst.glob('*.dds'))} remaster textures in {dst}")


if __name__ == "__main__":
    sys.exit(main())
