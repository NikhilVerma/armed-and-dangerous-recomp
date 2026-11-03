"""Where the exporters find the game's executable (read only).

The exporters read a few tables and strings from the game's executable: data in .rdata / .data (property keys,
player tuning floats, the animation mode table, the subtitle speaker table) and, for the sound export's provenance
notes only, references in .text. The Steam build's game.exe is wrapped by SteamStub, which encrypts .text only: its
.rdata, .data and .rsrc are byte-identical to the unwrapped file (checked 2026-10-23 against build 251748), so the
wrapped game.exe serves every exporter. An unwrapped game.unpacked.exe next to it, when present, is preferred; this
project never unwraps or patches the executable (docs/decision.md 2026-10-03).

AND_GAME_EXE overrides the choice (a path to either file).
"""
import hashlib
import os
import pathlib

GOG_2_0_0_3 = "d59366423d97583d791505baaaeeccc821192611ed48d54bddc7318ea5543a14"

# Where the tables the exporters read sit in each build, by the address they have in the Steam build.
#
# The GOG release was linked differently, so its tables moved, and not all by the same amount. The project's notes
# and exporters all use Steam addresses. A build missing from this list keeps those addresses.
#
# Each GOG address was found by the table's contents and checked against docs/subsystems (2026-11-01).
_TABLE_ADDRESSES = {
    GOG_2_0_0_3: {
        0x00589188: 0x00589190,  # difficulty factors
        0x005cadd8: 0x005c9dd8,  # subtitle speakers
        0x005cb3a8: 0x005ca390,  # player cross-fade times
        0x005cb650: 0x005ca638,  # player animation modes
        # the player's rate blocks, which the animation modes point to
        0x005fea3c: 0x005dca1c,
        0x005fea64: 0x005dca44,
        0x005fea8c: 0x005dca6c,
        0x005feab4: 0x005dca94,
        0x005feadc: 0x005dcabc,
        0x005feb04: 0x005dcae4,
        0x005feb2c: 0x005dcb0c,
        0x005feb54: 0x005dcb34,
    },
}


def table_va(exe, steam_va):
    """Where the table that the Steam build keeps at steam_va sits in the executable exe."""
    return _TABLE_ADDRESSES.get(hashlib.sha256(pathlib.Path(exe).read_bytes()).hexdigest(), {}).get(steam_va, steam_va)


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

