"""Where the exporters find the game's executable (read only).

The exporters read a few tables and strings from the game's executable: data in .rdata / .data (property keys,
player tuning floats, the animation mode table, the subtitle speaker table) and, for the sound export's provenance
notes only, references in .text. The Steam build's game.exe is wrapped by SteamStub, which encrypts .text only: its
.rdata, .data and .rsrc are byte-identical to the unwrapped file (checked 2026-10-23 against build 251748), so the
wrapped game.exe serves every exporter. An unwrapped game.unpacked.exe next to it, when present, is preferred; this
project never unwraps or patches the executable (docs/decision.md 2026-10-03).

AND_GAME_EXE overrides the choice (a path to either file).
"""
import os
import pathlib


def game_exe(gamedata):
    """The executable to read: $AND_GAME_EXE, else GameData/game.unpacked.exe if present, else GameData/game.exe."""
    env = os.environ.get("AND_GAME_EXE")
    if env:
        return pathlib.Path(env)
    gamedata = pathlib.Path(gamedata)
    unpacked = gamedata / "game.unpacked.exe"
    return unpacked if unpacked.exists() else gamedata / "game.exe"


def find_ignoring_case(base, rel):
    """base/rel, matching each part of rel without regard to case when the exact spelling is missing (the game's
    banks name files as Windows spelled them; a GameData folder copied to a case-sensitive file system keeps the
    install's spelling). rel may use backslashes. Returns the exact spelling's path when nothing matches."""
    base = pathlib.Path(base)
    parts = [p for p in str(rel).replace("\\", "/").split("/") if p]
    exact = base.joinpath(*parts)
    if exact.exists():
        return exact
    here = base
    for part in parts:
        nxt = here / part
        if not nxt.exists():
            try:
                nxt = next((e for e in here.iterdir() if e.name.lower() == part.lower()), None)
            except OSError:
                nxt = None
            if nxt is None:
                return exact
        here = nxt
    return here

