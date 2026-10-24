"""Rebuild the audio mix of a sound log (the original's [test] log_sounds and-sounds.txt, or the engine's
AND_SOUND_LOG file) from the exported wav files, and compare two mixes: loudness per half second, peaks, and the
share of each sound family. A stand-in for listening: it shows level and balance differences, not quality.

  python tools/sound_mix_compare.py ORIG_LOG ENGINE_LOG [--level lev_1_m1] [--seconds 10] [--out DIR]

The original's log: "<frame since load> <name> <driver volume dB>" (the test runs' master is -40 dB: only the
difference to the master is used); the engine's: "<seconds> <name> <volume_db> <2d|3d>". Both are aligned at their
first footstep. Positional attenuation is not in either log (sounds are mixed at their own volume)."""
import argparse
import collections
import json
import os
import re
import wave

import numpy as np

RATE = 22050


def load_wav(path):
    with wave.open(path) as w:
        n, ch, rate, width = w.getnframes(), w.getnchannels(), w.getframerate(), w.getsampwidth()
        a = np.frombuffer(w.readframes(n), dtype=np.int16 if width == 2 else np.uint8).astype(np.float32)
    if width == 1:
        a = (a - 128) * 256
    a = a.reshape(-1, ch).mean(axis=1) / 32768
    if rate != RATE:
        idx = np.arange(0, len(a), rate / RATE)
        a = np.interp(idx, np.arange(len(a)), a)
    return a


def parse(path, orig):
    out = []
    for line in open(path):
        p = line.split()
        if len(p) < 3:
            continue
        if orig:
            f = int(p[0])
            if f < 0:
                f = 0
            out.append((f / 60.0, p[1].lower(), float(p[2])))
        else:
            out.append((float(p[0]), p[1].lower(), float(p[2])))
    return out


def family(name):
    return re.sub(r"(_\d+)+$", "", re.sub(r"_lp$", "", name))


def mix(events, sounds, wavdir, secs, orig):
    buf = np.zeros(int(secs * RATE))
    fam = collections.Counter()
    t0 = next((t for t, n, _ in events if n.startswith("fs_")), 0.0)
    for t, n, v in events:
        d = sounds.get(n)
        if not d:
            continue
        # each sound at its bank volume (volume_db) in both mixes: the logs' own volumes differ in meaning (the
        # original's test master is -40 dB for effects but not for voice); positional loops (amb_*_lp) are left out,
        # neither log carries the distance they are heard from
        if d.get("loop") and n.startswith("amb_") and n != "amb_snow_wind_fields_lp":
            continue
        g = 10 ** (float(d.get("volume_db", 0.0)) / 20)
        a = load_wav(os.path.join(wavdir, os.path.basename(d["file"]))) * g
        start = int((t - t0 + 1.0) * RATE)
        if start >= len(buf) or start + len(a) <= 0:
            continue
        if d.get("loop"):
            reps = int(np.ceil((len(buf) - max(start, 0)) / max(len(a), 1))) + 1
            a = np.tile(a, reps)
        s = max(start, 0)
        seg = a[s - start: s - start + len(buf) - s]
        buf[s: s + len(seg)] += seg
        fam[family(n)] += float(np.sum(seg ** 2))
    return buf, fam


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("orig")
    ap.add_argument("engine")
    ap.add_argument("--level", default="lev_1_m1")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--out", default="work/remaster/render")
    a = ap.parse_args()
    base = os.path.join("work", "remaster", a.level, "audio")
    sounds = {k.lower(): v for k, v in json.load(open(os.path.join(base, "sounds.json")))["sounds"].items()}
    wavdir = os.path.join(base, "wav")
    o, of = mix(parse(a.orig, True), sounds, wavdir, a.seconds, True)
    e, ef = mix(parse(a.engine, False), sounds, wavdir, a.seconds, False)
    w = RATE // 2
    rms = lambda x: [20 * np.log10(np.sqrt(np.mean(x[i:i + w] ** 2)) + 1e-9) for i in range(0, len(x) - w + 1, w)]
    ro, re_ = rms(o), rms(e)
    print("loudness per 0.5 s (dBFS)  original | engine")
    for i, (x, y) in enumerate(zip(ro, re_)):
        print(f"  {i * 0.5:4.1f}s  {x:6.1f} | {y:6.1f}  diff {y - x:+5.1f}")
    print(f"overall: original {20 * np.log10(np.sqrt(np.mean(o ** 2)) + 1e-9):.1f} dBFS, engine "
          f"{20 * np.log10(np.sqrt(np.mean(e ** 2)) + 1e-9):.1f} dBFS; peaks {np.max(np.abs(o)):.2f} / {np.max(np.abs(e)):.2f}")
    to, te = sum(of.values()) or 1, sum(ef.values()) or 1
    print("share of energy by sound family (original | engine)")
    for k in sorted(set(of) | set(ef), key=lambda k: -(of.get(k, 0) / to + ef.get(k, 0) / te))[:14]:
        print(f"  {k:32s} {100 * of.get(k, 0) / to:5.1f}% | {100 * ef.get(k, 0) / te:5.1f}%")
    os.makedirs(a.out, exist_ok=True)
    for name, buf in (("mix_original.wav", o), ("mix_engine.wav", e)):
        with wave.open(os.path.join(a.out, name), "w") as f:
            f.setnchannels(1)
            f.setsampwidth(2)
            f.setframerate(RATE)
            f.writeframes((np.clip(buf, -1, 1) * 32767).astype(np.int16).tobytes())


if __name__ == "__main__":
    main()
