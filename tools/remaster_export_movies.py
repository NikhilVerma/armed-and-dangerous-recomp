"""Export the game's cut-scene movies for the remaster engine (engine/src/movie.rs), so the engine plays them
without shipping ffmpeg or a DivX / MPEG-4 decoder.

  python tools/remaster_export_movies.py                    -> every GameData/movies/*.pcv
  python tools/remaster_export_movies.py pre_1_1_opening    -> one (names case-insensitive, without .pcv)
  python tools/remaster_export_movies.py --check            -> re-reads each exported movie and prints its numbers

Input: orig/pc/GameData/movies/*.pcv (AVI: MPEG-4 Part 2 video 'DXGM' 512x308 at 30 fps, PCM s16le audio; the
original plays them through DivxMediaLib.dll, archive/src/movie/movie_01.cpp) and the UI bank's subtitle tables
(work/remaster/<level>/ui/layout.json `subtitles` + `captions`, made by tools/remaster_export_ui.py: the bank is
identical in every level). Needs ffmpeg (winget Gyan.FFmpeg); --ffmpeg overrides the path.

Output (game data: lives under work/, never committed), one folder per movie, its name lower case
(work/remaster/movies/<name>/):
  video.rmjv   the frames as baseline JPEGs in one file the engine streams (format below)
  audio.wav    the soundtrack as stored (PCM s16le, its own rate and channels); absent for a silent movie
  movie.json   {name, source, width, height, fps, frames, duration, audio: {rate, channels} | null,
               subtitles: [{start, end, speaker, text_id, text, colour, always_shown, size, x, y}]}

Why per-frame JPEG (a Motion-JPEG stream) and not a modern codec: no maintained Bevy 0.20 video crate decodes
in pure Rust (bevy_av1: AV1 in IVF; bevy_movie_player: FFmpeg; bevy_video / bevy_h264: openh264, a C library,
Bevy 0.9 / 0.13), while zune-jpeg (pure Rust, already in the engine's dependency tree through `image`) decodes a
512x308 frame in about a millisecond. Every frame is a key frame, so seeking, skipping and looping are exact;
the size (~20-35 KB a frame at -q:v 3) is near the original's (pre_1_1_Opening.pcv 120 MB).

video.rmjv: little-endian
  0   b"RMJV"   magic
  4   u32       version (1)
  8   u32       width
  12  u32       height
  16  u32       fps numerator
  20  u32       fps denominator
  24  u32       frame count n
  28  u32       reserved (0)
  32  n x (u64 offset, u32 size)   each frame's JPEG, offsets from the start of the file
  ..  the JPEGs

Subtitle times are seconds of movie time: the original looks the line up with the video time (frame / fps,
0x0050e740) through 0x00495c50 (start <= t <= end) and draws it with the speaker's style (table 0x005cadd8:
colour, always shown, size, x, y; 0x00495cb0). A line whose text id is not in the text bank draws nothing there
(Text_Lookup 0x00495f00 quiet), so it gets text null here.
"""
import argparse
import json
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOVIES = ROOT / "orig" / "pc" / "GameData" / "movies"
OUT = ROOT / "work" / "remaster" / "movies"
FFMPEG_WINGET = pathlib.Path.home() / "AppData/Local/Microsoft/WinGet/Packages"


def find_ffmpeg(given):
    if given:
        return pathlib.Path(given)
    p = shutil.which("ffmpeg")
    if p:
        return pathlib.Path(p)
    for c in sorted(FFMPEG_WINGET.glob("Gyan.FFmpeg*/ffmpeg-*/bin/ffmpeg.exe")):
        return c
    sys.exit("ffmpeg not found: install it (winget install Gyan.FFmpeg) or pass --ffmpeg")


def probe(ffmpeg, src):
    ffprobe = ffmpeg.with_name("ffprobe" + ffmpeg.suffix)
    r = subprocess.run([str(ffprobe), "-v", "error", "-show_entries",
                        "stream=codec_type,width,height,r_frame_rate,sample_rate,channels:format=duration",
                        "-of", "json", str(src)], capture_output=True, text=True, check=True)
    d = json.loads(r.stdout)
    video = next(s for s in d["streams"] if s["codec_type"] == "video")
    audio = next((s for s in d["streams"] if s["codec_type"] == "audio"), None)
    num, den = (int(x) for x in video["r_frame_rate"].split("/"))
    return {"width": video["width"], "height": video["height"], "fps": (num, den),
            "duration": float(d["format"]["duration"]),
            "audio": {"rate": int(audio["sample_rate"]), "channels": int(audio["channels"])} if audio else None}


def split_jpegs(data):
    """Splits a Motion-JPEG byte stream into its images: walks each image's marker segments to the scan, then the
    entropy-coded data to the EOI marker (inside it 0xFF is followed by 0x00 or a restart marker only)."""
    out, i, n = [], 0, len(data)
    while i < n:
        if data[i:i + 2] != b"\xff\xd8":
            raise ValueError(f"no SOI at {i}")
        j = i + 2
        while True:
            if data[j] != 0xFF:
                raise ValueError(f"bad marker at {j}")
            m = data[j + 1]
            if m == 0xFF:
                j += 1
                continue
            seg = struct.unpack_from(">H", data, j + 2)[0]
            j += 2 + seg
            if m == 0xDA:  # start of scan: entropy-coded data follows
                break
        while True:
            k = data.index(b"\xff", j)
            m = data[k + 1]
            if m == 0x00 or 0xD0 <= m <= 0xD7:
                j = k + 2
                continue
            if m == 0xD9:
                j = k + 2
                break
            j = k + 1  # fill byte or another marker: keep scanning
        out.append(data[i:j])
        i = j
    return out


def write_rmjv(path, width, height, fps, frames):
    head = struct.pack("<4sIIIIIII", b"RMJV", 1, width, height, fps[0], fps[1], len(frames), 0)
    offset = len(head) + 12 * len(frames)
    index = bytearray()
    for f in frames:
        index += struct.pack("<QI", offset, len(f))
        offset += len(f)
    with open(path, "wb") as o:
        o.write(head)
        o.write(index)
        for f in frames:
            o.write(f)


def read_rmjv(path):
    data = path.read_bytes()
    magic, version, w, h, fn, fd, n, _ = struct.unpack_from("<4sIIIIIII", data, 0)
    assert magic == b"RMJV" and version == 1, path
    frames = [struct.unpack_from("<QI", data, 32 + 12 * k) for k in range(n)]
    return {"width": w, "height": h, "fps": fn / fd, "frames": n, "bytes": len(data),
            "first": data[frames[0][0]:frames[0][0] + frames[0][1]] if n else b""}


def subtitles_by_movie(ui_dir):
    """movie name (lower case) -> its subtitle lines with the speaker's style resolved."""
    layout = json.loads((ui_dir / "layout.json").read_text(encoding="utf-8"))
    styles = {c["speaker"].lower(): c for c in layout.get("captions", [])}
    out = {}
    for m in layout.get("subtitles", []):
        lines = []
        for l in m["lines"]:
            st = styles.get(l["speaker"].lower())
            lines.append({
                "start": l["start"], "end": l["end"], "speaker": l["speaker"], "text_id": l["text_id"],
                "text": l.get("string"),
                # a speaker missing from the table resolves past its end (big-ui.md): no style; white, subtitle-gated
                "colour": st["colour"] if st else "#ffffff",
                "always_shown": st["always_shown"] if st else False,
                "size": st["size"] if st else 1.1,
                "x": st["x"] if st else 0.5,
                "y": st["y"] if st else 0.15,
            })
        out[m["movie"].lower()] = lines
    return out


def export(ffmpeg, src, subs, quality):
    name = src.stem.lower()
    info = probe(ffmpeg, src)
    out = OUT / name
    out.mkdir(parents=True, exist_ok=True)
    fn, fd = info["fps"]
    # constant frame rate: frame k is shown from k / fps (the original decodes one frame per 1/fps of play,
    # Movie_Update 0x0050e110), so dropped AVI frames are repeated, never skipped
    r = subprocess.run([str(ffmpeg), "-v", "error", "-i", str(src), "-map", "0:v:0", "-fps_mode", "cfr",
                        "-r", f"{fn}/{fd}", "-c:v", "mjpeg", "-pix_fmt", "yuvj420p", "-q:v", str(quality),
                        "-f", "image2pipe", "-"], capture_output=True, check=True)
    frames = split_jpegs(r.stdout)
    write_rmjv(out / "video.rmjv", info["width"], info["height"], info["fps"], frames)
    wav = out / "audio.wav"
    if info["audio"]:
        subprocess.run([str(ffmpeg), "-v", "error", "-y", "-i", str(src), "-map", "0:a:0", "-c:a", "pcm_s16le",
                        str(wav)], check=True)
    elif wav.exists():
        wav.unlink()
    meta = {
        "name": name, "source": f"GameData/movies/{src.name}", "width": info["width"], "height": info["height"],
        "fps": fn / fd, "frames": len(frames), "duration": info["duration"], "audio": info["audio"],
        "subtitles": subs.get(name, []),
    }
    (out / "movie.json").write_text(json.dumps(meta, indent=1), encoding="utf-8")
    size = (out / "video.rmjv").stat().st_size
    print(f"{name}: {len(frames)} frames ({len(frames) * fd / fn:.2f} s, source {info['duration']:.2f} s), "
          f"{size / 1e6:.1f} MB video, audio {info['audio']}, {len(meta['subtitles'])} subtitle lines")


def check():
    for d in sorted(p for p in OUT.iterdir() if (p / "movie.json").exists()):
        meta = json.loads((d / "movie.json").read_text(encoding="utf-8"))
        v = read_rmjv(d / "video.rmjv")
        ok = v["frames"] == meta["frames"] and v["first"][:2] == b"\xff\xd8"
        print(f"{d.name}: {v['frames']} frames {v['width']}x{v['height']} {v['fps']} fps, {v['bytes'] / 1e6:.1f} MB, "
              f"wav {(d / 'audio.wav').exists()}, {len(meta['subtitles'])} subtitles {'ok' if ok else 'BAD'}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*", help="movie names (default: all)")
    ap.add_argument("--ffmpeg")
    ap.add_argument("--ui", default=str(ROOT / "work" / "remaster" / "lev_1_m1" / "ui"),
                    help="a level's exported ui folder (layout.json with the subtitle tables)")
    ap.add_argument("--quality", type=int, default=3, help="ffmpeg -q:v for the JPEGs (2 best .. 31)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.check:
        check()
        return
    ffmpeg = find_ffmpeg(a.ffmpeg)
    subs = subtitles_by_movie(pathlib.Path(a.ui))
    want = {n.lower() for n in a.names}
    sources = [p for p in sorted(MOVIES.glob("*.pcv")) if not want or p.stem.lower() in want]
    if want and len(sources) != len(want):
        sys.exit(f"not found: {sorted(want - {p.stem.lower() for p in sources})}")
    for src in sources:
        export(ffmpeg, src, subs, a.quality)
    unmatched = sorted(set(subs) - {p.stem.lower() for p in MOVIES.glob("*.pcv")})
    if unmatched and not want:
        print(f"subtitle tables without a movie file: {unmatched}")


if __name__ == "__main__":
    main()
