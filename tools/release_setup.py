"""First-run setup of a downloaded release (Setup.bat / Setup.command): finds your copy of Armed and Dangerous,
exports its assets, installs the Remaster texture pack and bakes the reflection maps.

    python tools/release_setup.py                      looks in the usual install folders, else asks
    python tools/release_setup.py --game DIR           your copy's folder (the one holding GameData)
    python tools/release_setup.py --no-textures        skip the texture pack download (176 MB)

It runs tools/setup_assets.py and tools/bake_env_maps.py, the same steps a source checkout uses, so a second run
continues where the first stopped. Everything is written under work/ next to this release. Nothing in the game's
folder is changed.
"""
import argparse
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import platform_paths  # noqa: E402

GAME_FOLDER = "Armed and Dangerous"


def steam_libraries():
    """Steam's library folders on Windows: the default one and those libraryfolders.vdf names."""
    roots = [pathlib.Path(os.environ.get(v, "")) / "Steam" for v in ("ProgramFiles(x86)", "ProgramFiles") if os.environ.get(v)]
    found = []
    for root in roots:
        found.append(root)
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            for path in re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(encoding="utf-8", errors="replace")):
                found.append(pathlib.Path(path.replace("\\\\", "\\")))
    return found


def candidates():
    """Folders a copy of the game usually sits in, most likely first."""
    out = []
    if platform_paths.WINDOWS:
        out += [lib / "steamapps" / "common" / GAME_FOLDER for lib in steam_libraries()]
        for v in ("ProgramFiles(x86)", "ProgramFiles"):
            if os.environ.get(v):
                out.append(pathlib.Path(os.environ[v]) / "GOG Galaxy" / "Games" / GAME_FOLDER)
        out.append(pathlib.Path("C:/GOG Games") / GAME_FOLDER)
    out.append(pathlib.Path.home() / "Games" / GAME_FOLDER)
    return out


def has_game(folder):
    return any((c / "game.exe").is_file() for c in (folder / "GameData", folder))


def find_game(given):
    if given:
        return pathlib.Path(given).expanduser()
    if (ROOT / "orig" / "pc" / "GameData" / "game.exe").is_file():
        return None                                         # linked by an earlier run: setup_assets.py carries on
    for folder in candidates():
        if has_game(folder):
            print(f"Found the game in {folder}")
            return folder
    print("Could not find Armed and Dangerous in the usual folders.")
    print("Type or drag in the folder your copy is installed in (the one holding GameData), then press Enter:")
    while True:
        answer = input("> ").strip().strip('"').strip("'")
        if not platform_paths.WINDOWS:
            answer = answer.replace("\\ ", " ")              # a folder dragged into Terminal arrives escaped
        if answer and has_game(pathlib.Path(answer).expanduser()):
            return pathlib.Path(answer).expanduser()
        print("No GameData/game.exe in that folder. Try again (Ctrl+C to stop):")


def run(script, *args):
    return subprocess.run([sys.executable, str(ROOT / "tools" / script), *map(str, args)], cwd=str(ROOT)).returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", help="the game's install folder (the one holding GameData)")
    ap.add_argument("--levels", default="all", help="'all' (default) or a comma-separated list, e.g. lev_1_m1")
    ap.add_argument("--no-textures", action="store_true", help="skip the Remaster texture pack")
    a = ap.parse_args()
    game = find_game(a.game)
    args = ["--levels", a.levels]
    if game:
        args += ["--game", game]
    if not a.no_textures:
        args += ["--remaster-textures"]
    print("Exporting the game's assets. All levels take about half an hour and 12 GB.")
    if run("setup_assets.py", *args):
        sys.exit("Setup stopped: the lines above say why. Run Setup again to continue.")
    if platform_paths.engine_exe().exists():
        print("Baking the reflection maps (about 10 seconds a level).")
        if run("bake_env_maps.py"):
            print("The reflection maps were not baked. The game still runs; ice, water and metal reflect the fog colour.")
    print("\nSetup is done. Start the game with " + ("Play.bat" if platform_paths.WINDOWS else "Play.command") + ".")


if __name__ == "__main__":
    main()
