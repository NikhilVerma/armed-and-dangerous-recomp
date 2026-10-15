"""Compare captured movie frames (the original's, tools/shot.py dumps, or the engine's playtest shots) with the
exported movie: which movie frame each capture shows, how close the picture is, and whether the subtitle band
shows text when the subtitle table says a line is up.

  python tools/movie_compare.py pre_1_1_opening work/shots/origmovA/*.png [--rect 64,86,576,394] [--band 394,470]
  python tools/movie_compare.py pre_1_1_opening shot.png --rect X0,Y0,X1,Y1 (an engine shot: its picture rect)

For each capture: the best matching frame (mean absolute difference of the picture, 8-bit, on a 128-wide grey
copy; then the full-size RGB difference of that frame), its time (frame / fps; the original's subtitle clock is the
frame decoded next, movie.rs), the subtitle line due then (movie.json), and the band's bright-pixel count. Captures
that match no frame well (mae > 25: the legal screen, other movies, the menu) are listed as "no match".
Frames come from work/remaster/movies/<name>/video.rmjv (tools/remaster_export_movies.py).
"""
import argparse
import glob
import io
import json
import pathlib
import struct
import sys

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOVIES = ROOT / "work" / "remaster" / "movies"


def frames(name, limit):
    data = (MOVIES / name / "video.rmjv").read_bytes()
    _, _, w, h, fn, fd, n, _ = struct.unpack_from("<4sIIIIIII", data, 0)
    idx = [struct.unpack_from("<QI", data, 32 + 12 * k) for k in range(min(n, limit))]
    return fn / fd, [(o, s, data) for o, s in idx]


def jpeg(entry):
    o, s, data = entry
    return Image.open(io.BytesIO(data[o:o + s])).convert("RGB")


def small(im):
    return np.asarray(im.convert("L").resize((128, 77), Image.BILINEAR), dtype=np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("movie")
    ap.add_argument("captures", nargs="+")
    ap.add_argument("--rect", default="64,86,576,394", help="the picture in the capture (x0,y0,x1,y1)")
    ap.add_argument("--band", default="394,470", help="the subtitle band's rows in the capture")
    ap.add_argument("--max-frames", type=int, default=100000)
    a = ap.parse_args()
    meta = json.loads((MOVIES / a.movie / "movie.json").read_text(encoding="utf-8"))
    fps, fr = frames(a.movie, a.max_frames)
    lib = np.stack([small(jpeg(f)) for f in fr])
    rect = tuple(int(v) for v in a.rect.split(","))
    b0, b1 = (int(v) for v in a.band.split(","))
    caps = [p for c in a.captures for p in sorted(glob.glob(c))]
    for path in caps:
        im = Image.open(path).convert("RGB")
        pic = im.crop(rect)
        s = small(pic)
        d = np.abs(lib - s[None]).mean(axis=(1, 2))
        k = int(d.argmin())
        if d[k] > 25:
            print(f"{pathlib.Path(path).name}: no match (best frame {k}, mae {d[k]:.1f})")
            continue
        full = np.abs(np.asarray(jpeg(fr[k]).resize(pic.size), dtype=np.float32) - np.asarray(pic, dtype=np.float32)).mean()
        t = (k + 1) / fps
        due = [l for l in meta["subtitles"] if l["start"] <= t <= l["end"] and l["text"]]
        band = np.asarray(im.convert("L"), dtype=np.int32)[b0:b1]
        lit = int((band > 120).sum())
        line = due[0]["text_id"] + " " + (due[0]["text"] or "")[:40].replace("<BR>", " / ") if due else "-"
        print(f"{pathlib.Path(path).name}: frame {k} t {t:.2f} s mae {full:.1f} | band lit {lit:5d} | due {line}")


if __name__ == "__main__":
    sys.exit(main())
