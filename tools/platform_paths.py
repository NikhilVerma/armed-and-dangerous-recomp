"""Where things are on this machine, for the tools that build, run or feed the remaster engine on Windows, macOS
and Linux (the engine's own counterpart is engine/src/platform/mod.rs).

    engine_exe(target)      engine/<target>/release/and_remaster(.exe)
    app_data_dir()          the engine's per-user data folder (session recordings)
    find_ffmpeg()           ffmpeg on PATH, else where winget / Homebrew put it; None if not installed
    FFMPEG_HINT             how to install it on this system
    PLAY_COMMAND            how to start the game (engine\\play.bat, engine/play.sh)
"""
import os
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WINDOWS = os.name == "nt"
MACOS = sys.platform == "darwin"

EXE_SUFFIX = ".exe" if WINDOWS else ""
PLAY_COMMAND = "engine\\play.bat" if WINDOWS else "engine/play.sh"
FFMPEG_HINT = ("winget install Gyan.FFmpeg" if WINDOWS else "brew install ffmpeg" if MACOS
               else "install ffmpeg with your package manager")


def engine_exe(target="target"):
    """The release build of the engine in cargo target dir engine/<target>."""
    return ROOT / "engine" / target / "release" / f"and_remaster{EXE_SUFFIX}"


def app_data_dir():
    """The engine's per-user data folder, as engine/src/platform/mod.rs app_data_dir names it."""
    if WINDOWS:
        base = os.environ.get("LOCALAPPDATA")
        return pathlib.Path(base) / "AndRemaster" if base else None
    if MACOS:
        return pathlib.Path.home() / "Library" / "Application Support" / "AndRemaster"
    return pathlib.Path(os.environ.get("XDG_DATA_HOME") or pathlib.Path.home() / ".local" / "share") / "AndRemaster"


def find_ffmpeg():
    """ffmpeg's path: on PATH, else winget's package folder (Windows) or Homebrew's bin (macOS, when the shell that
    started Python has no Homebrew PATH); None when it is not installed."""
    found = shutil.which("ffmpeg")
    if found:
        return pathlib.Path(found)
    if WINDOWS:
        winget = pathlib.Path(os.environ.get("LOCALAPPDATA", "")) / "Microsoft" / "WinGet" / "Packages"
        for c in sorted(winget.glob("Gyan.FFmpeg*/ffmpeg-*/bin/ffmpeg.exe")):
            return c
    if MACOS:
        for c in (pathlib.Path("/opt/homebrew/bin/ffmpeg"), pathlib.Path("/usr/local/bin/ffmpeg")):
            if c.is_file():
                return c
    return None
