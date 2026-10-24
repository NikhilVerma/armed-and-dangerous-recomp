"""Export one level's sounds, music, ambience and voice lines for the remaster engine (engine/).

  python tools/remaster_export_audio.py lev_1_m1            -> work/remaster/lev_1_m1/audio/
  python tools/remaster_export_audio.py lev_1_m1 --check 30 -> also prints signal statistics of 30 sounds

Output (game data: lives under work/, never committed):
  wav/<name>.wav     every sound of <level>_big08_aud.big as a standard PCM WAV. In-memory sounds carry their
                     stored samples byte for byte; streamed sounds (flags 0x40) are GameData\\streams\\*.raw,
                     headerless PCM in the format the bank header gives, wrapped in a WAV header (no resampling).
  sounds.json        {"level", "note", "groups", "sounds": name -> {file, category (sfx/voice/music/ambient), loop,
                     loop_evidence, flags, flag_names, priority, volume_db, pitch_variation, rate_range?,
                     min_distance, max_distance, channels, rate, bits, frames, duration, exclusive_channel?,
                     envelope?, fade?, intro?, outro?, follow_on?, source (bank | GameData\\streams\\x.raw),
                     next? (music segment variants), fx? [{fx, looping}], property_refs?}}
  voice.json         {"level", "note", "lines": id (= sound name) -> {sound, file, duration, speaker, subtitle,
                     subtitle_raw, speaker_code, line_number, line_category, exclusive_channel, subtitle_shown,
                     triggers: [{type: sound_ear | code | exe_string | fx | follow_on, ...}]}}
  ambient.json       {"level", "note", "ambients": [{instance, position, world, loops, oneshots, min_distance?,
                     max_distance?, oneshot_delay? (as the game runs), oneshot_delay_authored?, missing_sounds}]}

Formats and rules: docs/formats/big-aud.md, docs/subsystems/audio.md. Every retail sound is 16-bit PCM (mono 11025/
22050/44100 Hz or stereo 22050/44100 Hz); the game only ever creates WAVE_FORMAT_PCM buffers (0x004fa6d0), so no
decoder is needed. The header's +0xec word (cbSize) is 0 in every file; 0x004f49b0 would treat a non-zero one as
IMA ADPCM (guess) and the tool refuses such a sound rather than writing garbage.
"""
import argparse
import json
import math
import pathlib
import re
import struct
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "formats"))
from big_kinds import aud, fx, inst, prop, text  # noqa: E402

GAME = ROOT / "orig" / "pc" / "GameData"
from gamefiles import find_ignoring_case, game_exe  # noqa: E402
EXE = game_exe(GAME)  # tools/gamefiles.py: game.unpacked.exe if present, else game.exe

# Definition flags (+0x20). Readers in archive/src/audio/audio_02.cpp, archive/src/renderer/auto_004fb1f0.cpp, auto_004fb170.cpp.
FLAGS = {
    0x1: "unique_per_object",            # 0x004f32f0 UniqueVoice: one live voice per (definition, object)
    0x2: "unique_restarts",              # with 0x1: the old voice is stopped instead of reused
    0x4: "driver_3d_extra",              # callback +0x60 / virtual +0x44 (EAX-style hook; empty on PC, guess)
    0x20: "always_set",                  # set on every retail definition; no reader found
    0x40: "streamed",                    # data in GameData\streams\*.raw, refilled by 0x00500890
    0x200: "speech",                     # 0x600: speech volume group +0x20 and the subtitle callback 0x00495e00
    0x400: "speech_alt_colour",          # subtitle chat colour 4 instead of 1 (0x00495e00); unused in retail
    0x10000: "sequence_part",            # intro/outro of a loop: no buffer at load (0x004fa7f0 skips 0x1010040)
    0x20000: "voice_flag_0x8000000",     # copied to the voice at start (StartVoice); meaning unknown
    0x200000: "envelope",                # fade envelope +0x38..+0x4c (0x004f2730, 0x004fb360)
    0x400000: "block_0x50",              # +0x50..+0x6c filled by def opcode 10; no reader found
    0x800000: "fade_in_out",             # +0x70..+0x78 (0x004f28d0 / 0x004f2950); ignores the master offset
    0x1000000: "intro_outro_loop",       # +0x7c intro, +0x9c outro around a looping body (0x004fa340, 0x004fb360)
    0x2000000: "music",                  # music volume group +0x1c; segment graph via `next` (0x00500a00)
    0x4000000: "loop_stream",            # guess: set on music and the long ambience streams only; no reader found
    0x8000000: "music_last_segment",     # ResolveSound takes the second variant with play flag 0x800 (0x00500a00)
    0x10000000: "follow_on",             # +0xbc: sound played when this one ends (0x004f2a80)
    0x40000000: "no_group_level",        # group level 0.0 (0x004fb170)
}
CHANNEL_SHIFT, CHANNEL_MASK = 11, 0x1F  # flags bits 11..15: exclusive channel (0x004f35e0)


def payload(level, part):
    return (GAME / level / f"{level}_{part}.big").read_bytes()[8:]


def cstr(b):
    return b.split(b"\0")[0].decode("latin-1")


def f32(h, o):
    return struct.unpack_from("<f", h, o)[0]


def u32(h, o):
    return struct.unpack_from("<I", h, o)[0]


def wav(ch, rate, bits, pcm):
    return aud.wav_bytes(ch, rate, bits, pcm)


# ---------------------------------------------------------------------------------------------------- level data
def load_props(level):
    _, sets = prop.parse(payload(level, "big06_prop"))
    out = {}
    for st in sets:
        d = {}
        for k, v in zip(st["keys"], st["values"]):
            if len(v) == 32 or (b"\0" in v and not len(v) == 4):
                d[k.decode("latin-1").lower()] = cstr(v)
            elif len(v) == 4:
                d[k.decode("latin-1").lower()] = (struct.unpack("<i", v)[0], struct.unpack("<f", v)[0])
        out[st["name"].decode("latin-1").lower()] = d
    return out


def load_instances(level):
    p = payload(level, "big10_inst")
    groups, off, out = struct.unpack_from("<I", p, 0)[0], 4, []
    for _ in range(groups):
        n = struct.unpack_from("<I", p, off)[0]
        off += 4
        for _ in range(n):
            it, size = inst.parse_instance(p, off)
            off += size
            name = it["object"] if isinstance(it["object"], str) else it["object"].decode("latin-1")
            props = []
            for k, v in it["props"]:
                k = k if isinstance(k, str) else k.decode("latin-1")
                if isinstance(v, bytes):
                    v = cstr(v)
                elif isinstance(v, dict) and "str" in v:
                    v = v["str"]
                props.append((k.lower(), v))
            out.append({"id": len(out) + 1, "object": name.lower(), "props": props,
                        "position": [it["transform"][r][3] for r in range(3)]})
    return out


def load_text(level):
    _, strings = text.parse(payload(level, "big01_text"))
    return {n.decode("latin-1").lower(): t.decode("utf-16-le") for n, t in strings}


# ------------------------------------------------------------------------------- string references in game.exe
class Exe:
    """Finds the code that names a sound: the string in the image and the functions that push its address."""

    def __init__(self):
        d = EXE.read_bytes()
        pe = struct.unpack_from("<I", d, 0x3c)[0]
        n = struct.unpack_from("<H", d, pe + 6)[0]
        opt = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.secs = []
        for i in range(n):
            nm, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, pe + 24 + opt + 40 * i)
            self.secs.append((nm.rstrip(b"\0").decode(), va, rs, d[ro:ro + rs]))
        self.text = next(s for s in self.secs if s[0] == ".text")
        self.text_readable = not any(s[0] == ".bind" for s in self.secs)
        self.data = [s for s in self.secs if s[0] in (".rdata", ".data")]
        self.lower = [(nm, va, raw.lower()) for nm, va, _, raw in self.data]
        rows = (ROOT / "symbols" / "functions.csv").read_text().splitlines()[1:]
        self.funcs = sorted((int(r.split(",")[0], 16), int(r.split(",")[1] or 0), r.split(",")[2]) for r in rows)
        self.starts = [f[0] for f in self.funcs]

    def func_at(self, va):
        import bisect
        i = bisect.bisect_right(self.starts, va) - 1
        if i >= 0 and va < self.funcs[i][0] + max(self.funcs[i][1], 1) + 0x40:
            return f"0x{self.funcs[i][0]:08x} {self.funcs[i][2]}"
        return f"0x{va:08x} (no tracked function)"

    def refs(self, va):
        """Code addresses whose instruction bytes hold `va` (push imm32 / mov imm32). None when .text is encrypted
        (the SteamStub-wrapped game.exe, tools/gamefiles.py): the notes then name the tables, not the functions."""
        if not self.text_readable:
            return []
        _, tva, _, raw = self.text
        b, out, i = struct.pack("<I", va), [], raw.find(struct.pack("<I", va))
        while i >= 0:
            out.append(self.base + tva + i)
            i = raw.find(b, i + 1)
        return out

    def data_refs(self, va):
        b, out = struct.pack("<I", va), []
        for _, sva, _, raw in self.data:
            i = raw.find(b)
            while i >= 0:
                out.append(self.base + sva + i)
                i = raw.find(b, i + 1)
        return out

    def string_vas(self, name):
        key, out = name.lower().encode("latin-1") + b"\0", []
        for _, sva, raw in self.lower:
            i = raw.find(key)
            while i >= 0:
                if i == 0 or raw[i - 1] == 0:
                    out.append(self.base + sva + i)
                i = raw.find(key, i + 1)
        return out

    def who_names(self, name):
        funcs, tables = set(), set()
        for va in self.string_vas(name):
            for r in self.refs(va):
                funcs.add(self.func_at(r))
            for t in self.data_refs(va):
                tables.add(f"0x{t:08x}")
                # a name table: code usually points at its first entry; look back a few slots
                for back in range(0, 0x40, 4):
                    for r in self.refs(t - back):
                        funcs.add(self.func_at(r) + f" (table 0x{t - back:08x})")
        return sorted(funcs), sorted(tables)


# ---------------------------------------------------------------------------------------------------- sounds
def decode_header(h):
    flags = u32(h, 0x20)
    _, ch, rate, bps, ba, bits, cb, size, frames, _ = struct.unpack_from("<HHIIHHIIII", h, 0xdc)
    ch_bits = (flags >> CHANNEL_SHIFT) & CHANNEL_MASK
    d = {
        "flags": f"0x{flags:08X}",
        "flag_names": [v for k, v in FLAGS.items() if flags & k] + (["channel_bits"] if ch_bits else []),
        "priority": u32(h, 0x24),
        "volume_db": f32(h, 0x28),
        "pitch_variation": f32(h, 0x2c),
        "min_distance": f32(h, 0x30),
        "max_distance": f32(h, 0x34),
        "channels": ch, "rate": rate, "bits": bits,
        "frames": frames, "duration": round(frames / rate, 4) if rate else 0.0,
    }
    if ch_bits:
        d["exclusive_channel"] = (ch_bits & -ch_bits).bit_length() - 1
    if d["pitch_variation"] > 0:
        v = d["pitch_variation"]
        d["rate_range"] = [round((1 - v * 0.125) * rate), round((1 + v * 0.25) * rate)]
    if flags & 0x200000:
        d["envelope"] = dict(zip(("delay", "attack", "hold", "release", "fade_out", "level"),
                                 (round(f32(h, o), 4) for o in range(0x38, 0x50, 4))))
    if flags & 0x800000:
        d["fade"] = dict(zip(("level", "fade_in", "fade_out"), (round(f32(h, o), 4) for o in (0x70, 0x74, 0x78))))
    links = [cstr(h[o:o + 32]).lower() for o in (0x7c, 0x9c, 0xbc)]
    links = [None if x in ("", "none") else x for x in links]  # "none": no intro/outro (0x004f2470 lookup fails)
    if flags & 0x1000000:
        d["intro"], d["outro"] = links[0], links[1]
    if flags & 0x10000000:
        d["follow_on"] = links[2]
    extra = {f"0x{o:02x}": u32(h, o) for o in range(0x50, 0x7c, 4) if u32(h, o)}
    if extra:
        d["unknown_words"] = extra
    if cb:
        raise ValueError(f"{cstr(h[:32])}: cbSize {cb} (compressed format); not in any retail file")
    if ba != ch * bits // 8 or bps != rate * ba or bits != 16:
        raise ValueError(f"{cstr(h[:32])}: unexpected format fields {ch} ch {rate} Hz {bits} bits")
    return d, (ch, rate, bits, size)


def category(name, flags, ambient_names):
    if flags & 0x2000000:
        return "music"
    if flags & 0x600:
        return "voice"
    if name in ambient_names or name.startswith("amb_"):
        return "ambient"
    return "sfx"


def loop_hint(name, flags):
    """The game decides looping per play call (the `looping` argument; voice flag 0x20, 0x004fa210), not in the
    header. This is the default a caller uses for the sound, from what the original's callers pass."""
    if flags & 0x2000000:
        return True, "music: streamed segments always play DSBPLAY_LOOPING and chain through `next` (0x004fa100)"
    if flags & 0x1000000:
        return True, "intro/outro loop body (flag 0x1000000)"
    if name.endswith("_lp") or "_lp_" in name:
        return True, "name ends in _lp; callers pass looping=1 (e.g. amb_pub_exterior_lp 0x0046a790, ambient loopN)"
    if flags & 0x4000000:
        return True, "flag 0x4000000 (guess: long ambience/music streams)"
    return False, "one-shot unless the caller passes looping=1 (fx opcode 0x39 w, Fx_PlaySound 0x00420080)"


def safe_file(name, used):
    base = name if re.fullmatch(r"[a-z0-9_.\-]+", name) else re.sub(r"[^a-z0-9_.\-]", "_", name)
    while base in used:
        base += "_"
    used.add(base)
    return f"wav/{base}.wav"


def export_sounds(level, out, ambient_names):
    _, _, sounds = aud.parse(payload(level, "big08_aud"))
    (out / "wav").mkdir(parents=True, exist_ok=True)
    result, used, written, missing, stream_files = {}, set(), 0, [], {}
    for s in sounds:
        h = s["header"]
        name = cstr(h[:32]).lower()
        d, (ch, rate, bits, size) = decode_header(h)
        flags = u32(h, 0x20)
        if "pcm" in s:
            f = safe_file(name, used)
            (out / f).write_bytes(wav(ch, rate, bits, s["pcm"]))
            written += len(s["pcm"])
            d["source"] = "bank"
        else:
            path = cstr(s["path"])
            src = find_ignoring_case(GAME, path)
            d["source"] = f"GameData\\{path}"
            d["next"] = [cstr(n).lower() for n in s["next"]]
            key = path.lower()
            if key in stream_files:
                f = stream_files[key]
            elif not src.exists():
                f = None
                missing.append(path)
            else:
                raw = src.read_bytes()
                if len(raw) != size:
                    raise ValueError(f"{name}: {path} is {len(raw)} bytes, header says {size}")
                f = safe_file(name, used)
                (out / f).write_bytes(wav(ch, rate, bits, raw))
                written += len(raw)
                stream_files[key] = f
        loop, why = loop_hint(name, flags)
        result[name] = {"file": f, "category": category(name, flags, ambient_names), "loop": loop,
                        "loop_evidence": why, **d}
    return result, written, missing


# ---------------------------------------------------------------------------------------------------- voice
# Code that plays character lines, by speaker prefix and line category (the suffix after the id). From the
# recovered functions' comments (archive/src/audio/audio_01.cpp, audio_02.cpp); the exact string sets are in the code.
BARKS = {
    ("ca", "fight"): "0x00437940 AIVoice_PlayFight", ("ca", "help"): "0x004375e0 AIVoice_PlayHelp",
    ("ca", "capnbase"): "0x00437510 AIVoice_PlayCapnbase", ("ca", "opengate"): "0x00437760 AIVoice_PlayOpengate",
    ("ca", "general_scream"): "0x00437a20 AIVoice_PlayScream",
    ("ca", "topsy_scream"): "0x00437ac0 AIVoice_PlayTopsyScream",
    ("ca", "sticky_scream"): "0x00437b60 AIVoice_PlayStickyScream",
    ("ca", "hurt"): "0x00437d60 AIVoice_PlayHurt / 0x00437f60 AIVoice_PlayHurtShort",
    ("ca", "dead"): "0x004380e0 AIVoice_PlayDead", ("ca", "taunt"): "0x004382e0 AIVoice_PlayTaunt",
    ("gr", "general_scream"): "0x004383a0 AIVoice_GruntScream",
    ("gr", "topsy_scream"): "0x004385c0 AIVoice_GruntTopsyScream",
    ("gr", "sticky_scream"): "0x00438810 AIVoice_GruntStickyScream",
    ("gr", "opengate"): "0x00438a60 AIVoice_GruntOpengate",
    ("gr", "ladderplaced"): "0x00438c40 AIVoice_PlayLadderplaced",
    ("gr", "climbtaunt"): "0x00438cb0 AIVoice_PlayClimbtaunt", ("gr", "help"): "0x00438eb0 AIVoice_GruntHelp",
    ("gr", "fight"): "0x004390e0 AIVoice_GruntFight",
    ("gr", "hurt"): "0x004392f0 AIVoice_GruntHurt / 0x004394c0 AIVoice_GruntHurtShort",
    ("gr", "dead"): "0x004396a0 / 0x00439890 (death line, once per grunt: state flag 0x400)",
    ("gr", "taunt"): "0x00439ae0", ("gr", "ambient"): "0x00439cb0",
    ("jon", "fight"): "0x0044ea50 / 0x0044eab0", ("jon", "taunt"): "0x0044eab0",
    ("jon", "dead"): "0x0044eb80", ("jon", "hurt"): "0x0044ebf0 / 0x0044ed50", ("jon", "help"): "0x0044ed50",
    ("jon", "friendlyfire"): "0x0044ec60", ("jon", "peasantdropoff"): "0x0044d1f0",
    ("q", "dead"): "0x00487050", ("q", "hurt"): "0x00487150 / 0x004872e0", ("q", "help"): "0x004872e0",
    ("q", "friendlyfire"): "0x004871e0", ("q", "sniper"): "0x004872e0",
    ("q", "fight"): "0x004874c0 / 0x004875d0", ("q", "fightspec"): "0x004874c0",
    ("q", "ambient"): "0x004857e0 (low player health, near Q)", ("q", "ambient_spec"): "0x004857e0",
    ("q", "peasantdropoff"): "0x0044d1f0",
    ("rom", "zeppelin"): "0x00436260", ("rom", "sniper"): "0x0044ed50", ("rom", "peasantdropoff"): "0x0044d1f0",
    ("pe", "peasantdropoff"): "0x0044d1f0", ("pm", "peasantdropoff"): "0x0044d1f0",
    ("pe", "peasantcaptured"): "0x00439f70", ("pe", "pub"): "0x00468dd0 (pub ambience)",
    ("gol", "hurt"): "0x00434180", ("gol", "ambient"): "0x00439f70",
    ("sp", "taunt"): "0x0043a310", ("sp", "friendlyfire"): "0x0043a480",
    ("mo", "taunt"): "0x0043a5e0", ("mo", "ambient"): "0x0043a680", ("mo", "hurt"): "0x0043a6e0",
    ("mo", "dead"): "0x0043a7b0",
}


def split_id(name):
    m = re.match(r"([a-z]+)(\d+)_(.*)", name)
    if not m:
        return None, None, None
    return m.group(1), m.group(2), m.group(3)


def export_voice(sounds, texts, instances, props, exe, fx_users):
    # Sound ears: trigger volumes that play a teammate line (SoundEar 0x00497750 setup, 0x00497900 trigger).
    ear_defaults = props.get("p_sound_ear", {})
    senders = {}
    for i in instances:
        for k, v in i["props"]:
            if isinstance(v, int) and (k.startswith("on_") or k.startswith("send_trigger")):
                senders.setdefault(v, []).append({"instance": i["id"], "object": i["object"], "key": k,
                                                  "position": [round(x, 2) for x in i["position"]]})
    ear_lines = {}
    for i in instances:
        if i["object"] != "sound_ear":
            continue
        p = dict(i["props"])
        tid = p.get("trigger_id", 0)
        md = p.get("max_distance", ear_defaults.get("max_distance", (0, 300.0))[1])
        for role, key in (("roman", "roman_sound"), ("q", "q_sound"), ("jonesy", "jonsey_sound"),
                          ("ai", "ai_sound")):
            if p.get(key, "none").lower() != "none":
                ear_lines.setdefault(p[key].lower(), []).append({
                    "type": "sound_ear", "instance": i["id"], "trigger_id": tid, "speaker_role": role,
                    "max_distance": md, "repeat": bool(p.get("reapeat", 0)),
                    "position": [round(x, 2) for x in i["position"]],
                    "other_lines": [p[k2].lower() for _, k2 in (("", "roman_sound"), ("", "q_sound"),
                                                                ("", "jonsey_sound"), ("", "ai_sound"))
                                    if k2 != key and p.get(k2, "none").lower() != "none"],
                    "trigger_senders": senders.get(tid, [])})
    out = {}
    for name, s in sounds.items():
        if s["category"] != "voice":
            continue
        raw = texts.get(name)
        speaker, line = None, None
        if raw is not None:
            m = re.match(r"\s*([^:<]{1,40}):\s*(.*)", raw, re.S)
            speaker, line = (m.group(1).strip(), m.group(2).strip()) if m else (None, raw.strip())
        pre, num, cat = split_id(name)
        trig = list(ear_lines.get(name, []))
        if pre is not None:
            key = (pre[:3] if pre.startswith("jon") else pre[:2] if pre[:2] in ("ca", "gr", "pe", "pm", "sp",
                                                                              "mo") else pre, cat)
            base = re.sub(r"^l\d_", "", cat)
            for c in (cat, base, base.split("_")[0]):
                fn = BARKS.get((key[0], c))
                if fn:
                    trig.append({"type": "code", "function": fn,
                                 "evidence": "speaker prefix + line category, archive/src/audio/audio_01.cpp/audio_02.cpp"})
                    break
        for prev, ps in sounds.items():
            if ps.get("follow_on") == name:
                trig.append({"type": "follow_on", "after": prev,
                             "evidence": "definition flag 0x10000000, name at +0xbc; replayed by 0x004f2a80 when the "
                                         "previous line ends"})
        for f in s.get("fx", []):
            trig.append({"type": "fx", "fx": f["fx"], "looping": f["looping"], "fx_users": fx_users.get(f["fx"], []),
                         "evidence": "fx opcode 0x39 (random pick) -> Fx_PlaySound 0x00420080, positional"})
        funcs, tables = exe.who_names(name)
        if funcs or tables:
            trig.append({"type": "exe_string", "functions": funcs, "tables": tables,
                         "evidence": "the sound name is a string in game.unpacked.exe referenced by this code"})
        out[name] = {"sound": name, "file": s["file"], "duration": s["duration"], "speaker": speaker,
                     "subtitle": line, "subtitle_raw": raw, "speaker_code": pre, "line_number": num,
                     "line_category": cat, "exclusive_channel": s.get("exclusive_channel"),
                     "subtitle_shown": ("in-game chat feed (0x0046aa60 on Game+0xaac), colour index "
                                        + ("4" if int(s["flags"], 16) & 0x400 else "1")
                                        + ", only when option m_SubtitlesGame (Game+0x177) is on (0x00495e00)")
                     if raw is not None else "nothing: no text bank entry (Text_Lookup 0x00495f00 quiet miss)",
                     "triggers": trig}
    return out


# ---------------------------------------------------------------------------------------------------- ambient
def export_ambient(instances, props, sounds):
    d = props.get("p_ambient", {})

    def num(p, k, default):
        v = p.get(k, d.get(k))
        if v is None:
            return default
        return v[1] if isinstance(v, tuple) else float(v)

    out = []
    for i in instances:
        if i["object"] != "ambient":
            continue
        p = dict(i["props"])
        world = bool(p.get("world", d.get("world", (0, 0))[0] if isinstance(d.get("world"), tuple) else 0))
        loops = [p.get(f"loop{n}", d.get(f"loop{n}", "")) for n in range(1, 11)]
        shots = [p.get(f"oneshot{n}", d.get(f"oneshot{n}", "")) for n in range(1, 11)]
        loops = [x.lower() for x in loops if x and x.lower() != "none"]
        shots = [x.lower() for x in shots if x and x.lower() != "none"]
        md = num(p, "min_delay", 3.0)
        e = {"instance": i["id"], "position": [round(x, 3) for x in i["position"]], "world": world,
             "loops": loops, "oneshots": shots,
             "missing_sounds": [x for x in loops + shots if x not in sounds]}
        if not world:
            e["min_distance"] = num(p, "min_distance", 100.0)
            e["max_distance"] = num(p, "max_distance", 300.0)
        if shots:
            # original bug (0x00493950): the maximum reads the "min_delay" key again (default 5 only if absent)
            e["oneshot_delay"] = [md, md if ("min_delay" in p or "min_delay" in d) else 5.0]
            e["oneshot_delay_authored"] = [md, num(p, "max_delay", 5.0)]
        out.append(e)
    return out


# ---------------------------------------------------------------------------------------------------- check
def stats(path):
    data = path.read_bytes()
    ch, rate, bits, pcm = aud.wav_parse(data, str(path))
    a = np.frombuffer(pcm, "<i2").astype(np.float64) / 32768.0
    a = a.reshape(-1, ch)
    n = len(a)
    rms = math.sqrt(float(np.mean(a ** 2))) if n else 0.0
    peak = float(np.max(np.abs(a))) if n else 0.0
    clip = float(np.mean(np.abs(a) >= 32767 / 32768.0)) if n else 0.0
    dc = float(np.mean(a)) if n else 0.0
    m = a.mean(axis=1)
    zcr = float(np.mean(np.signbit(m[1:]) != np.signbit(m[:-1]))) * rate / 2 if n > 1 else 0.0
    # garbage check: white noise has a flat spectrum; real sounds put most energy low. Ratio of energy above
    # rate/4 to total.
    if n > 256:
        sp = np.abs(np.fft.rfft(m * np.hanning(len(m)))) ** 2
        hf = float(sp[len(sp) // 2:].sum() / max(sp.sum(), 1e-30))
    else:
        hf = 0.0
    return {"ch": ch, "rate": rate, "dur": n / rate, "rms_db": 20 * math.log10(max(rms, 1e-9)),
            "peak_db": 20 * math.log10(max(peak, 1e-9)), "clip": clip, "dc": dc, "zcr_hz": zcr, "hf": hf}


def check(out, sounds, count):
    picks, seen = [], set()
    order = (["voice"] * 10 + ["music"] * 4 + ["ambient"] * 4 + ["sfx"] * 12)
    pools = {c: [n for n, s in sounds.items() if s["category"] == c and s["file"]] for c in set(order)}
    for c in order:
        for n in pools[c][(len([p for p in picks if sounds[p]["category"] == c]) * 37) % max(len(pools[c]), 1):] + \
                pools[c]:
            if n not in seen:
                picks.append(n)
                seen.add(n)
                break
        if len(picks) >= count:
            break
    print(f"{'sound':32} {'cat':7} {'src':6} {'ch':>2} {'rate':>5} {'dur s':>7} {'rms dB':>7} {'peak dB':>7} "
          f"{'clip%':>6} {'dc':>7} {'zcr Hz':>7} {'hf':>5}")
    bad = 0
    for n in picks:
        s = sounds[n]
        st = stats(out / s["file"])
        ok = st["rate"] in (11025, 22050, 44100) and st["dur"] > 0.02 and st["rms_db"] > -60 and st["clip"] < 0.01 \
            and abs(st["dc"]) < 0.05 and st["hf"] < 0.5
        bad += not ok
        print(f"{n[:32]:32} {s['category']:7} {('stream' if s['source'] != 'bank' else 'bank'):6} {st['ch']:>2} "
              f"{st['rate']:>5} {st['dur']:7.2f} {st['rms_db']:7.1f} {st['peak_db']:7.1f} {100 * st['clip']:6.3f} "
              f"{st['dc']:7.4f} {st['zcr_hz']:7.0f} {st['hf']:5.2f}{'' if ok else '  <-- check'}")
    print(f"{len(picks)} checked, {bad} flagged")


# ---------------------------------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out")
    ap.add_argument("--check", type=int, default=0, help="print signal statistics of N exported sounds")
    a = ap.parse_args()
    out = pathlib.Path(a.out) if a.out else ROOT / "work" / "remaster" / a.level / "audio"
    out.mkdir(parents=True, exist_ok=True)

    props = load_props(a.level)
    instances = load_instances(a.level)
    ambient_names = set()
    for i in instances:
        for k, v in i["props"]:
            if isinstance(v, str) and (re.fullmatch(r"(loop|oneshot)\d+", k) or "audio" in k):
                ambient_names.add(v.lower())
    for pset in props.values():
        for k, v in pset.items():
            if isinstance(v, str) and ("audio" in k or re.fullmatch(r"(loop|oneshot)\d+", k)):
                ambient_names.add(v.lower())

    sounds, written, missing = export_sounds(a.level, out, ambient_names)

    # fx scripts that play each sound (opcode 0x39: random pick, Fx_PlaySound 0x00420080)
    _, fxs = fx.parse(payload(a.level, "big09_fx"))
    for e in fxs:
        for ins in e["script"] or []:
            if ins[0] == "0x39":
                for n in ins[1]:
                    s = sounds.get(n.lower())
                    if s is not None:
                        s.setdefault("fx", []).append({"fx": e["name"].decode("latin-1").lower(),
                                                       "looping": bool(ins[2])})
    for pname, pset in props.items():
        for k, v in pset.items():
            if isinstance(v, str) and v.lower() in sounds:
                sounds[v.lower()].setdefault("property_refs", []).append(f"{pname}.{k}")

    meta = {
        "level": a.level,
        "note": "name -> playback parameters from the 0xfc-byte definition header (docs/subsystems/audio.md). "
                "volume_db: dB added to the group level (0.0 = unity); final DirectSound volume = sum of dB * 100, "
                "silent at <= -40 dB (0x004fa1d0). Distances in game units: positional sounds follow the cubic "
                "Bezier dB curve (0, -5, -10, -40) from min_distance to max_distance (0x004f4ec0, 0x004fb170); "
                "pan = clamp(listener-space x / distance, -0.9, 0.9) (0x004fb1f0). priority: higher wins an "
                "exclusive channel (0x004f35e0). rate_range: playback rate is uniform in it (0x004f3a80). "
                "loop is a default: the game passes looping per play call.",
        "groups": {"sfx": "Game+0x490 m_soundVolume -> audio +0x18", "voice": "Game+0x494 m_voiceVolume -> +0x20",
                   "music": "Game+0x498 m_musicVolume -> +0x1c",
                   "slider_to_db": "level_db = bezier(0,-5,-10,-40)(1 - slider); default slider 0.8 = -3.2 dB"},
        "sounds": sounds,
    }
    (out / "sounds.json").write_text(json.dumps(meta, indent=1))

    texts = load_text(a.level)
    exe = Exe()
    # who starts each fx: animation events ("fx <name>" strings in big03_anim) and property values
    fx_users = {}
    anim = payload(a.level, "big03_anim")
    for m in re.finditer(rb"fx ([A-Za-z0-9_]+)\x00", anim, re.I):
        fx_users.setdefault(m.group(1).decode().lower(), set()).add("anim event (big03_anim)")
    for pname, pset in props.items():
        for k, v in pset.items():
            if isinstance(v, str) and v.lower().startswith("fx_"):
                fx_users.setdefault(v.lower(), set()).add(f"property {pname}.{k}")
    fx_users = {k: sorted(v)[:12] for k, v in fx_users.items()}
    voice = export_voice(sounds, texts, instances, props, exe, fx_users)
    (out / "voice.json").write_text(json.dumps({
        "level": a.level,
        "note": "line id = sound name (lower case). Subtitle text is the text bank (big01_text) entry with the same "
                "id, written 'Speaker: line'; the game shows it through the speech start callback 0x00495e00. "
                "triggers: sound_ear instances (with the trigger senders), code by speaker/category, and code that "
                "names the line in game.exe.",
        "lines": voice}, indent=1, ensure_ascii=False))
    amb = export_ambient(instances, props, sounds)
    (out / "ambient.json").write_text(json.dumps({
        "level": a.level,
        "note": "AmbientSounds (0x00500230..0x00500520) as set up by 0x00493950. world: 2D, no distance; else "
                "beyond max_distance both sounds stop, inside the volume offset is bezier(0,-5,-10,-40) over "
                "[min_distance, max_distance]. One loop (random of loops) is kept running; a one-shot (random) "
                "starts when no one-shot plays and the timer (uniform in oneshot_delay, seconds) runs out.",
        "ambients": amb}, indent=1))

    cats = {}
    for s in sounds.values():
        cats[s["category"]] = cats.get(s["category"], 0) + 1
    with_text = sum(1 for v in voice.values() if v["subtitle"] is not None)
    print(f"{len(sounds)} sounds {cats}; {written / 1e6:.1f} MB of PCM written; missing streams: {len(missing)} "
          f"{missing[:5]}")
    print(f"voice lines {len(voice)} ({with_text} with subtitle text); ambients {len(amb)}")
    if a.check:
        check(out, sounds, a.check)


if __name__ == "__main__":
    main()
