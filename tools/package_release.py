"""Build a downloadable release of the remaster for this system: the engine, the asset exporters and a Python to
run them, with Setup and Play launchers. No game data goes in: the player runs Setup on their own copy.

    python tools/package_release.py                       -> work/release/and-remaster-<version>-<system>.zip
    python tools/package_release.py --exe target-wt-NAME  package another cargo target dir's build
    python tools/package_release.py --no-zip              only the folder (for a local test)

The release keeps the checkout's layout, so every tool and script works unchanged:

    Setup.bat | Setup.command        first run: tools/release_setup.py
    Play.bat  | Play.command         engine/play.bat | engine/play.sh
    engine/target/release/           the engine binary (options and saves are written next to it)
    tools/, symbols/functions.csv    the exporters and what they read
    python/                          a standalone CPython with requirements.txt installed
    work/                            written by Setup: the exported assets

Needs `uv` (it downloads the standalone CPython and installs the packages) and a release build of the engine
(cargo build --release in engine/). Systems: Windows x64 and macOS arm64 (docs/decision.md 2026-11-15).
"""
import argparse
import os
import pathlib
import platform
import re
import shutil
import stat
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import platform_paths  # noqa: E402

PYTHON_VERSION = "3.12"
# The tools a release runs, and the modules they import (tools/test_package_release.py checks the list is complete).
TOOLS = """release_setup setup_assets export_level platform_paths gamefiles instances bcn remaster_level
remaster_export remaster_export_anim remaster_export_audio remaster_export_grunt_ai remaster_export_icons
remaster_export_instance_props remaster_export_mission remaster_export_movies remaster_export_player
remaster_export_player_anims remaster_export_ui remaster_textures bake_env_maps""".split()
TOOL_FOLDERS = ["formats", "data"]
FILES = ["requirements.txt", "LICENSE-MIT", "LICENSE-APACHE", "THIRD_PARTY_NOTICES.md", "symbols/functions.csv",
         "docs/play-guide.md", "engine/play.bat", "engine/play.sh"]
# Parts of the standard library no exporter uses.
PYTHON_TRIM = ["test", "idlelib", "tkinter", "turtledemo", "ensurepip", "lib2to3", "pydoc_data"]
VC_RUNTIME = ["vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"]

README = """Armed and Dangerous Remaster {version} ({system})

A rewrite of Armed and Dangerous (2003) in Rust. This download holds the engine only.
You need your own copy of the game (Steam or GOG).

1. {unblock}
2. Run {setup}. It finds your copy of the game, exports its assets (about half an hour, 12 GB)
   and downloads the Remaster texture pack (176 MB). If it cannot find the game, it asks for the folder.
3. Run {play}.

Cut-scenes need ffmpeg at setup time ({ffmpeg}). Without it the game runs without them;
install it and run Setup again to add them.

Controls, the developer menu and the cheat codes: docs/play-guide.md.
Options and saves: engine/target/release/. Session logs: engine/logs/.
Source, issues and the licence: https://github.com/NikhilVerma/armed-and-dangerous-recomp
"""

SETUP_BAT = '@echo off\r\ncd /d "%~dp0"\r\npython\\python.exe tools\\release_setup.py %*\r\npause\r\n'
PLAY_BAT = '@echo off\r\ncall "%~dp0engine\\play.bat" %*\r\n'
SETUP_SH = '#!/bin/sh\ncd "$(dirname "$0")"\nexec python/bin/python3 tools/release_setup.py "$@"\n'
PLAY_SH = '#!/bin/sh\nexec "$(dirname "$0")/engine/play.sh" "$@"\n'


def system_name():
    machine = platform.machine().lower()
    arch = {"amd64": "x64", "x86_64": "x64", "arm64": "arm64", "aarch64": "arm64"}.get(machine, machine)
    return ("windows" if platform_paths.WINDOWS else "macos" if platform_paths.MACOS else "linux") + "-" + arch


def engine_version():
    return re.search(r'^version\s*=\s*"([^"]+)"', (ROOT / "engine" / "Cargo.toml").read_text(encoding="utf-8"), re.M).group(1)


def sh(*argv):
    print("  $ " + " ".join(map(str, argv)), flush=True)
    subprocess.run([*map(str, argv)], check=True)


def copy(rel, out):
    dest = out / rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / rel, dest)


def add_python(out):
    """A standalone CPython (python-build-standalone, fetched by uv) with the exporters' packages installed."""
    store = out.parent / "python-store"
    sh("uv", "python", "install", PYTHON_VERSION, "--install-dir", store)
    found = sorted(p for p in store.iterdir() if p.is_dir() and p.name.startswith(f"cpython-{PYTHON_VERSION}"))
    if not found:
        sys.exit(f"uv installed no cpython-{PYTHON_VERSION} into {store}")
    dest = out / "python"
    shutil.copytree(found[-1], dest, symlinks=True)
    exe = dest / "python.exe" if platform_paths.WINDOWS else dest / "bin" / "python3"
    sh("uv", "pip", "install", "--python", exe, "--break-system-packages", "--no-cache", "-r", ROOT / "requirements.txt")
    lib = dest / "Lib" if platform_paths.WINDOWS else next((dest / "lib").glob("python3.*"))
    for name in PYTHON_TRIM:
        shutil.rmtree(lib / name, ignore_errors=True)
    for cache in list(dest.rglob("__pycache__")):
        shutil.rmtree(cache, ignore_errors=True)
    return exe


def add_vc_runtime(out):
    """Windows: the Visual C++ runtime DLLs next to the engine, so a PC without the redistributable still starts it."""
    dest = out / "engine" / "target" / "release"
    system32 = pathlib.Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"
    missing = []
    for name in VC_RUNTIME:
        if (system32 / name).is_file():
            shutil.copy2(system32 / name, dest / name)
        else:
            missing.append(name)
    if missing:
        print(f"WARNING: {', '.join(missing)} not found in {system32}: players need the Visual C++ redistributable")


def write(path, text, executable=False):
    path.write_bytes(text.encode("utf-8"))
    if executable:
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def add_launchers(out, version):
    if platform_paths.WINDOWS:
        write(out / "Setup.bat", SETUP_BAT)
        write(out / "Play.bat", PLAY_BAT)
        text = README.format(version=version, system=system_name(), setup="Setup.bat", play="Play.bat",
                             ffmpeg="winget install Gyan.FFmpeg",
                             unblock="Unzip the folder. Windows may warn that the files are from an unknown publisher:\n"
                                     "   the build is not code-signed yet. Choose More info > Run anyway.")
        write(out / "README.txt", text.replace("\n", "\r\n"))
    else:
        write(out / "Setup.command", SETUP_SH, executable=True)
        write(out / "Play.command", PLAY_SH, executable=True)
        write(out / "README.txt", README.format(
            version=version, system=system_name(), setup="Setup.command", play="Play.command", ffmpeg="brew install ffmpeg",
            unblock="Unzip the folder. The build is not signed yet, so macOS blocks it. Clear the block once, in Terminal:\n"
                    "       xattr -cr /path/to/this/folder"))


def zip_folder(folder, dest):
    """A zip that keeps the execute bits (macOS) and stores the folder under its own name."""
    with zipfile.ZipFile(dest, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(folder.rglob("*")):
            name = f"{folder.name}/{path.relative_to(folder).as_posix()}"
            if path.is_symlink():
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (stat.S_IFLNK | 0o777) << 16
                z.writestr(info, os.readlink(path))
            elif path.is_file():
                z.write(path, name)
    return dest


def build(exe_target, out_root, make_zip=True):
    exe = platform_paths.engine_exe(exe_target)
    if not exe.exists():
        sys.exit(f"no engine build at {exe}: cd engine && cargo build --release")
    version = engine_version()
    out = out_root / f"and-remaster-{version}-{system_name()}"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    release = out / "engine" / "target" / "release"
    release.mkdir(parents=True)
    shutil.copy2(exe, release / exe.name)
    for name in TOOLS:
        copy(f"tools/{name}.py", out)
    for folder in TOOL_FOLDERS:
        shutil.copytree(ROOT / "tools" / folder, out / "tools" / folder, ignore=shutil.ignore_patterns("__pycache__"))
    for rel in FILES:
        copy(rel, out)
    add_python(out)
    if platform_paths.WINDOWS:
        add_vc_runtime(out)
    add_launchers(out, version)
    size = sum(f.stat().st_size for f in out.rglob("*") if f.is_file())
    print(f"release folder: {out} ({size / 1e6:.0f} MB)")
    if make_zip:
        dest = zip_folder(out, out_root / f"{out.name}.zip")
        print(f"release zip:    {dest} ({dest.stat().st_size / 1e6:.0f} MB)")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default="target", help="cargo target dir under engine/ (default target)")
    ap.add_argument("--out", default=str(ROOT / "work" / "release"), help="where the folder and the zip go")
    ap.add_argument("--no-zip", action="store_true")
    a = ap.parse_args()
    build(a.exe, pathlib.Path(a.out).resolve(), not a.no_zip)


if __name__ == "__main__":
    main()
