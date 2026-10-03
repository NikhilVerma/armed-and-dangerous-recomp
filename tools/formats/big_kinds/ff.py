"""Force feedback (rumble) bank: `*_big11_ff.big`, magic 0x00FFDBAC, loader 0x004e4d90 Rumble_Load.
Layout and safe edits: docs/formats/big-ff.md.

Payload:
  u32 count                                    sample count (bank +0x1c)
  name hash table (common.py, sized; value = sample index; key = sample name, compare 0x004e4d60)
  count x 0x840-byte sample:
    +0x000 char name[32]
    +0x020 motor 0 wave descriptor (0x28 bytes)  -> 250 floats generated into +0x048 at load (0x004e52b0)
    +0x048 float wave0[250]                      zero in the file
    +0x430 motor 1 wave descriptor (0x28 bytes)  -> generated into +0x458
    +0x458 float wave1[250]                      zero in the file
  wave descriptor: u32 w00..w14 (unknown, 6 words), f32 length (+0x18; guess: seconds), f32 rate (+0x1c),
                   f32 amplitude (+0x20), u32 shape (+0x24: 0 ramp up, 1 sine, 2 square, 3 noise, 4 ramp down)

Dump: rumble.json (hash seed; per sample in index order: name and both motors' descriptor fields). The wave
buffers are omitted when zero (all retail files); a non-zero buffer would be kept as hex.
Rebuild recomputes the count and the hash table from the names.
"""
import json
import struct

from . import common
from .words import str_from_json, str_to_json, word_from_json, word_to_json

KIND = "ff"
MAGIC = 0x00FFDBAC

SAMPLE = 0x840
MOTORS = (0x20, 0x430)
DESC = 0x28
WAVE = 1000  # 250 floats
FIELDS = ["w00", "w04", "w08", "w0c", "w10", "w14", "length", "rate", "amplitude", "shape"]


def parse(payload):
    count = struct.unpack_from("<I", payload, 0)[0]
    hd, off = common.parse(payload, 4)
    if len(hd) != count:
        raise ValueError(f"count {count} != hash entries {len(hd)}")
    if len(payload) - off != count * SAMPLE:
        raise ValueError(f"{len(payload) - off} bytes of samples for count {count}")
    samples = [payload[off + i * SAMPLE:off + (i + 1) * SAMPLE] for i in range(count)]
    return hd, samples


def sample_to_json(s):
    d = {"name": str_to_json(s[:32]), "motors": []}
    for m in MOTORS:
        words = struct.unpack_from("<10I", s, m)
        mot = {k: word_to_json(v) for k, v in zip(FIELDS, words)}
        mot["shape"] = words[9]
        buf = s[m + DESC:m + DESC + WAVE]
        if any(buf):
            mot["wave_hex"] = buf.hex()
        d["motors"].append(mot)
    return d


def sample_from_json(d):
    out = bytearray(str_from_json(d["name"], 32, "sample name"))
    if len(d["motors"]) != 2:
        raise ValueError(f"{d['name']}: a sample has exactly 2 motors")
    for mot in d["motors"]:
        out += struct.pack("<10I", *(word_from_json(mot.get(k, 0)) for k in FIELDS))
        out += bytes.fromhex(mot["wave_hex"]) if "wave_hex" in mot else b"\0" * WAVE
    assert len(out) == SAMPLE
    return bytes(out)


def _name(s):
    return s[:32].split(b"\0")[0]


def build_payload(seed, samples):
    hd = common.build([_name(s) for s in samples], list(range(len(samples))), seed)
    return struct.pack("<I", len(samples)) + hd.to_bytes(sized=True) + b"".join(samples)


def dump(payload, out):
    hd, samples = parse(payload)
    if build_payload(hd.seed, samples) != payload:
        raise AssertionError("stored hash table differs from the one rebuilt from the names")
    meta = {"kind": KIND, "hash_seed": f"0x{hd.seed:08X}",
            "note": "Order = sample index. shape: 0 ramp up, 1 sine, 2 square, 3 noise, 4 ramp down. "
                    "length <= 0 silences that motor. w00..w14 unknown. See docs/formats/big-ff.md.",
            "samples": [sample_to_json(s) for s in samples]}
    (out / "rumble.json").write_text(json.dumps(meta, indent=1))


def rebuild(src):
    meta = json.loads((src / "rumble.json").read_text())
    return build_payload(int(meta["hash_seed"], 16), [sample_from_json(d) for d in meta["samples"]])
