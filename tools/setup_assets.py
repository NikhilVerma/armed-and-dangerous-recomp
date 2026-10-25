"""From your own copy of Armed and Dangerous to everything the remaster engine loads, in one command.

    python tools/setup_assets.py --game "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Armed and Dangerous"
    python3 tools/setup_assets.py --game ~/Games/ArmedAndDangerous     macOS / Linux: a copy of the game's folder
    python tools/setup_assets.py --game DIR --levels lev_1_m1            just the first level (minutes, not an hour)
    python tools/setup_assets.py --remaster-textures remaster_textures.zip   add the optional texture pack later
    python tools/setup_assets.py --check                                  what is set up, what is missing

Steps (each skipped when its output is already there, unless --force):
  1. game     checks the install (GameData/game.exe, the level banks) and links it in as orig/pc (a directory
              junction on Windows, a symbolic link elsewhere; nothing is copied, nothing in the install is written).
              A plain copy of the install folder serves as well as the install (nothing is read from the registry
              or Steam): on macOS or Linux, copy it from a Windows PC (docs/macos.md). The Steam build (app 6090,
              build 251748) is the tested one; another build (GOG) is reported as untested and tried anyway.
  2. export   tools/export_level.py for each level -> work/remaster/<level>/ (scene, animations, audio, mission,
              UI, prompt icons, player, AI and instance data). Reads the banks and, read only, game.exe's data
              sections (tools/gamefiles.py): the Steam executable is used as installed, it is never unwrapped.
  3. movies   tools/remaster_export_movies.py -> work/remaster/movies/ (needs ffmpeg; skipped with a note if it
              is not installed: the game then runs without its cut-scenes).
  4. textures (optional, --remaster-textures ZIP_OR_URL) the AI retexture pack: verified file by file against the
              manifest (tools/data/remaster_textures.manifest.json once a pack is published, else the one inside
              the archive) and installed as work/remaster/<level>/textures_remaster/<name>.dds, hard-linked to one
              shared copy (work/remaster/textures_pack/) where the file system allows. The Remaster preset uses
              them; without them it uses the original textures.

Everything written goes under work/ (never committed). Afterwards: cargo build --release in engine/, then
engine\\play.bat (Windows) or engine/play.sh (macOS, Linux). Python 3.12 with requirements.txt.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import platform_paths  # noqa: E402

ORIG = ROOT / "orig" / "pc"
GAME = ORIG / "GameData"
WORK = ROOT / "work" / "remaster"
PACK_STORE = WORK / "textures_pack"
TRUSTED_MANIFEST = ROOT / "tools" / "data" / "remaster_textures.manifest.json"
# Where the texture pack will be published: not decided yet (README "Getting started"). Until then pass a local
# archive. When it is published, put its URL here and commit its manifest as TRUSTED_MANIFEST.
PACK_URL = "https://pub-7801d845a6624a4881145d2e393c182c.r2.dev/textures/v2/remaster_textures.zip"

KNOWN_EXES = {
    "960df24c70d788a34946fa5984685ef69bf2c0cacf04a275769472ce302f5b45": "Steam, app 6090, build 251748 (tested)",
}
EXPORT_STEPS = "scene,anims,audio,mission,ui,icons,player,instance_props,grunt_ai,player_anims"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def find_gamedata(given):
    """The install folder may be given as the game's root (which holds GameData) or as GameData itself."""
    p = pathlib.Path(given).expanduser().resolve()
    for cand in (p / "GameData", p):
        if (cand / "game.exe").is_file():
            return cand
    sys.exit(f"no GameData/game.exe under {p}: give the folder Steam installed the game into "
             "(Steam: right-click the game > Manage > Browse local files), or a copy of it (docs/macos.md)")


def levels_in(gamedata):
    return sorted(d.name for d in gamedata.iterdir() if d.is_dir() and d.name.startswith("lev_")
                  and d.name != "lev_shell" and (d / f"{d.name}_big10_inst.big").exists())


def link_game(gamedata, force):
    """orig/pc -> the install's root (the folder holding GameData)."""
    target = gamedata.parent
    if GAME.exists():
        if GAME.resolve() == gamedata.resolve():
            print(f"game: orig/pc already points at {target}")
            return
        if not force:
            sys.exit(f"orig/pc already holds a game ({GAME.resolve()}); pass --force to point it at {target}")
        if ORIG.is_junction() or ORIG.is_symlink():
            ORIG.unlink()
        else:
            sys.exit(f"orig/pc is a real folder, not a link; move it away yourself to use {target}")
    ORIG.parent.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        r = subprocess.run(["cmd", "/c", "mklink", "/J", str(ORIG), str(target)], capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"could not create the junction orig/pc -> {target}: {r.stderr.strip() or r.stdout.strip()}")
    else:
        ORIG.symlink_to(target, target_is_directory=True)
    print(f"game: orig/pc -> {target}")


def check_game(gamedata):
    exe = gamedata / "game.exe"
    digest = sha256(exe)
    build = KNOWN_EXES.get(digest)
    print(f"game: {exe} sha256 {digest[:16]}... {build or 'UNKNOWN build (GOG or another release: untested)'}")
    lv = levels_in(gamedata)
    print(f"game: {len(lv)} levels: {' '.join(lv)}")
    if not lv:
        sys.exit("no level banks (lev_*/lev_*_big10_inst.big) in GameData: is the install complete?")
    return lv


def run(argv):
    print("  $ python " + " ".join(str(x) for x in argv), flush=True)
    return subprocess.run([sys.executable, *map(str, argv)], cwd=str(ROOT)).returncode


def export(levels, force):
    bad = []
    for level in levels:
        if (WORK / level / "scene.json").exists() and (WORK / level / "player_anims.json").exists() and not force:
            print(f"export: {level} already exported (--force to redo)")
            continue
        if run([ROOT / "tools" / "export_level.py", level, "--steps", EXPORT_STEPS]):
            bad.append(level)
    if bad:
        sys.exit(f"export failed for {', '.join(bad)} (the lines above name the step and its error)")


def movies(force):
    if (WORK / "movies").is_dir() and any((WORK / "movies").iterdir()) and not force:
        print("movies: already exported")
        return
    if not platform_paths.find_ffmpeg():
        print(f"movies: SKIPPED, ffmpeg not found ({platform_paths.FFMPEG_HINT}, then run this again); "
              "the game runs without its cut-scenes")
        return
    if run([ROOT / "tools" / "remaster_export_movies.py"]):
        sys.exit("movies: export failed (see above)")


def fetch(source):
    """A local archive path, or an http(s) URL downloaded into work/downloads/."""
    if source.startswith(("http://", "https://")):
        dest = ROOT / "work" / "downloads" / pathlib.Path(source.split("?")[0]).name
        dest.parent.mkdir(parents=True, exist_ok=True)
        print(f"textures: downloading {source} -> {dest}")
        # Cloudflare (r2.dev) answers 403 to urllib's default "Python-urllib" agent
        req = urllib.request.Request(source, headers={"User-Agent": "and-remaster-setup/1"})
        with urllib.request.urlopen(req) as r, tempfile.NamedTemporaryFile(dir=dest.parent, delete=False) as t:
            shutil.copyfileobj(r, t)
        os.replace(t.name, dest)
        return dest
    p = pathlib.Path(source).expanduser()
    if not p.is_file():
        sys.exit(f"textures: no such archive {p}")
    return p


def install_textures(source):
    archive = fetch(source)
    with zipfile.ZipFile(archive) as z:
        inner = json.loads(z.read("manifest.json"))
        if TRUSTED_MANIFEST.exists():
            manifest = json.loads(TRUSTED_MANIFEST.read_text(encoding="utf-8"))
            print(f"textures: verifying against {TRUSTED_MANIFEST.relative_to(ROOT)}")
        else:
            manifest = inner
            print("textures: no published manifest in tools/data yet: checking the archive against its own "
                  "manifest (integrity only, not origin)")
        if inner.get("format") != 1 or manifest.get("format") != 1:
            sys.exit("textures: unknown manifest format")
        PACK_STORE.mkdir(parents=True, exist_ok=True)
        bad = []
        for name, digest in manifest["files"].items():
            dest = PACK_STORE / name
            if dest.exists() and sha256(dest) == digest:
                continue
            try:
                data = z.read(f"textures/{name}")
            except KeyError:
                bad.append(f"{name}: missing from the archive")
                continue
            if hashlib.sha256(data).hexdigest() != digest:
                bad.append(f"{name}: sha256 mismatch")
                continue
            dest.write_bytes(data)
    if bad:
        sys.exit("textures: the archive does not match the manifest:\n  " + "\n  ".join(bad[:20]))
    placed = levels = 0
    for level, names in manifest["levels"].items():
        if not (WORK / level).is_dir():
            continue
        out = WORK / level / "textures_remaster"
        out.mkdir(exist_ok=True)
        levels += 1
        for name, file in names.items():
            dest = out / f"{name}.dds"
            if dest.exists():
                dest.unlink()
            try:
                os.link(PACK_STORE / file, dest)
            except OSError:
                shutil.copyfile(PACK_STORE / file, dest)
            placed += 1
    print(f"textures: {len(manifest['files'])} files verified; {placed} placed in {levels} exported levels "
          f"(levels exported later: run this again)")


def check():
    ok = GAME.joinpath("game.exe").exists()
    print(f"game:     {'orig/pc -> ' + str(GAME.resolve().parent) if ok else 'MISSING (--game DIR)'}")
    if not ok:
        return
    lv = levels_in(GAME)
    done = [l for l in lv if (WORK / l / "player_anims.json").exists()]
    print(f"export:   {len(done)}/{len(lv)} levels" + (f" (missing: {' '.join(sorted(set(lv) - set(done)))})"
                                                       if len(done) < len(lv) else ""))
    mv = WORK / "movies"
    print(f"movies:   {len(list(mv.iterdir())) if mv.is_dir() else 0} exported")
    tex = [l for l in done if (WORK / l / "textures_remaster").is_dir()]
    print(f"textures: {len(tex)}/{len(done)} exported levels have the Remaster pack (optional)")
    exe = platform_paths.engine_exe()
    print(f"engine:   {'built' if exe.exists() else 'not built (cd engine && cargo build --release)'}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--game", help="the game's install folder (the one holding GameData)")
    ap.add_argument("--levels", default="all", help="'all' (default) or a comma-separated list, e.g. lev_1_m1")
    ap.add_argument("--no-movies", action="store_true", help="skip the cut-scene export")
    ap.add_argument("--remaster-textures", metavar="ZIP_OR_URL", nargs="?", const=PACK_URL,
                    help="install the optional AI retexture pack: the published one (no value), a local .zip or an http(s) URL")
    ap.add_argument("--force", action="store_true", help="redo steps whose output exists; repoint orig/pc")
    ap.add_argument("--check", action="store_true", help="report what is set up and stop")
    a = ap.parse_args()
    if a.check:
        check()
        return
    if a.remaster_textures and not a.game:
        # the pack alone, into the levels already exported
        install_textures(a.remaster_textures)
        return
    if a.game:
        gamedata = find_gamedata(a.game)
        link_game(gamedata, a.force)
    elif not GAME.joinpath("game.exe").exists():
        ap.error("--game DIR is needed the first time (the folder your copy of the game is installed in)")
    lv = check_game(GAME)
    todo = lv if a.levels == "all" else [x.strip() for x in a.levels.split(",") if x.strip()]
    unknown = sorted(set(todo) - set(lv))
    if unknown:
        sys.exit(f"unknown levels: {', '.join(unknown)}")
    export(todo, a.force)
    if not a.no_movies:
        movies(a.force)
    if a.remaster_textures:
        install_textures(a.remaster_textures)
    print(f"done. Next: cd engine && cargo build --release, then {platform_paths.PLAY_COMMAND} (or python "
          "tools/setup_assets.py --check)")


if __name__ == "__main__":
    main()
