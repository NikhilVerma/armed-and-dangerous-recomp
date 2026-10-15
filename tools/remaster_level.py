"""Shared loaders for the level data generators (remaster_export_instance_props.py, remaster_export_player.py,
remaster_export_grunt_ai.py). Game data only: everything read here lives under orig/ and work/, never in git.

A Level is one level's view of the retail data, as the game resolves it:
  - instances   the instance bank (big10_inst) in bank order; id = 1-based position (the numbering the game, the
                mission export and the engine use); props = the instance's own overrides, keys as stored
  - props       every property set (work/remaster/<level>/properties.json, written by tools/remaster_export.py)
  - templates   every object template (templates.json, same exporter)
  - resolved()  a property set with an instance's overrides on top (Object_LoadProperties reads the instance value
                first, then the template's set, then the code default: 0x004534a0 and siblings)
  - exe_reads_key()  whether game.unpacked.exe has a NUL-terminated string for a property key (or a "%d" format
                that makes it): a key with none is never read (data-only)
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT if (ROOT / "orig").exists() else ROOT.parents[2]   # a worktree reads the main checkout's orig/ and work/
GAME = MAIN / "orig" / "pc" / "GameData"
WORK = MAIN / "work" / "remaster"
EXE = GAME / "game.unpacked.exe"

sys.path.insert(0, str(ROOT / "tools"))


def _text(v):
    return v.decode("latin1") if isinstance(v, bytes) else v


def round_vec(v, digits):
    """Rounded floats with -0.0 made 0.0 (JSON stays stable across platforms)."""
    return [round(x, digits) + 0.0 for x in v]


_EXE_STRINGS = None


def exe_has(s):
    """True when `s` is a whole NUL-terminated string of game.unpacked.exe (read only)."""
    global _EXE_STRINGS
    if _EXE_STRINGS is None:
        data = EXE.read_bytes()
        _EXE_STRINGS = {m.group(0).decode("latin1") for m in re.finditer(rb"(?<![\x20-\x7e])[\x20-\x7e]{2,}(?=\x00)", data)}
    return s in _EXE_STRINGS


_EXE_FORMATS = None


def exe_reads_key(key):
    """True when the exe has the key's string, or a "%d" format string that makes it (e.g. "lodcutoff_%d")."""
    global _EXE_FORMATS
    if exe_has(key):
        return True
    if _EXE_FORMATS is None:
        _EXE_FORMATS = [re.compile(re.escape(s).replace("%d", r"\d+") + "$") for s in _EXE_STRINGS
                        if "%d" in s and "%" not in s.replace("%d", "") and " " not in s]
    return any(f.match(key) for f in _EXE_FORMATS)


_EXE_IMAGE = None


def exe_f32(va, n):
    """n floats of game.unpacked.exe's mapped image at virtual address va (read only)."""
    global _EXE_IMAGE
    if _EXE_IMAGE is None:
        import pefile
        _EXE_IMAGE = pefile.PE(str(EXE)).get_memory_mapped_image()
    import struct
    return list(struct.unpack_from(f"<{n}f", _EXE_IMAGE, va - 0x400000))


def plain_number(x, digits=6):
    """A float32 table value as written by hand: integral -> int, else rounded."""
    r = round(x, digits)
    return int(r) if r == int(r) else r


class Level:
    def __init__(self, level, out=None):
        import remaster_export as rx   # noqa: E402 (tools/ on the path above)
        self.name = level
        self.out = pathlib.Path(out) if out else WORK / level
        for need in ("properties.json", "templates.json"):
            if not (self.out / need).exists():
                raise FileNotFoundError(f"{self.out / need} missing: run tools/remaster_export.py {level} first")
        self.props = json.loads((self.out / "properties.json").read_text())
        self.templates = json.loads((self.out / "templates.json").read_text())
        self.instances = []
        for i, it in enumerate(rx.load_instances(level)):
            props = {}
            for k, v in it["props"]:
                props[_text(k)] = _text(v)
            self.instances.append({"id": i + 1, "template": _text(it["object"]).lower(),
                                   "transform": it["transform"], "pos": [r[3] for r in it["transform"]],
                                   "props": props})

    def property_set(self, template):
        """The property set name of a template (lower case), or None."""
        t = self.templates.get(template)
        return t["property"] if t else None

    def pset(self, name):
        return self.props.get(name, {})

    def template_props(self, template):
        return self.pset(self.property_set(template))

    def resolved(self, inst):
        """The instance's template set with the instance's overrides on top (keys lower case)."""
        p = dict(self.template_props(inst["template"]))
        p.update({k.lower(): v for k, v in inst["props"].items()})
        return p

    def first(self, template):
        return next((i for i in self.instances if i["template"] == template), None)

    def all(self, template):
        return [i for i in self.instances if i["template"] == template]

    def anim_bank(self):
        """The level's anim bank parsed by tools/remaster_export_anim.py: {name lower: Anim}."""
        import remaster_export_anim as ra
        anims, _, _ = ra.load_level(self.name)
        return anims
