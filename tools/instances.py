"""Game instances for running test scenarios in parallel.

Each instance is work/instances/<n>/, a mirror of orig/pc/GameData made of hard links (no copies of the game
data), so several games can run at once: and.ini, and.log, traces, frame dumps and SavedGames are written per
instance. The hook DLL and its symbols are linked afresh every time, so instances always run the deployed build.

Usage (library): instance_dir(n) -> path, ready to run.
       python tools/instances.py [count]   # prepare count instances (default: CPU count / 4, at least 2)
"""
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
BASE = os.path.join(ROOT, "work", "instances")
# Written by the game or the harness: never shared between instances.
PRIVATE = {"and.ini", "and.log", "and-d3d.txt", "and-coverage.txt", "and-profile.txt", "and-input.log",
           "and-vk-scene.txt", "and-vk-scene.obj", "and-vk-draws.txt", "and-vk-targets.txt", "and-crash.dmp",
           "and-rhi-calls.txt"}
PRIVATE_DIRS = {"SavedGames", "shaders"}
RELINK = {"dinput8.dll", "dinput8.pdb"}  # the hook build: copied from build/hook-release (not the game folder,
BUILD = os.path.join(ROOT, "build", "hook-release")  # which the player's running game may hold locked)
# AND_HOOK_BUILD: an agent's own hook build (e.g. build/hook-tower, archive/tools/build.ps1 -BuildDir from its worktree) for
# its own instance, so a hook change is tried without replacing the shared build/hook-release
if os.environ.get("AND_HOOK_BUILD"):
    BUILD = os.path.join(ROOT, os.environ["AND_HOOK_BUILD"])


def _link(src, dst):
    if os.path.lexists(dst):
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)  # another volume: copy


def _replace(src, dst):
    """Copies src over dst; a dst in use (a hard link to a DLL a running game has loaded) is renamed aside first."""
    if os.path.lexists(dst):
        try:
            os.remove(dst)
        except OSError:
            import time
            os.replace(dst, f"{dst}.old-{int(time.time() * 1000)}")
    shutil.copy2(src, dst)


def instance_dir(n):
    """Prepares instance n (idempotent) and returns its directory."""
    dst_root = os.path.join(BASE, str(n))
    for dirpath, dirnames, filenames in os.walk(GAME_DIR):
        rel = os.path.relpath(dirpath, GAME_DIR)
        top = rel.split(os.sep)[0]
        if top in PRIVATE_DIRS:
            dirnames[:] = []
            continue
        out = os.path.join(dst_root, rel) if rel != "." else dst_root
        os.makedirs(out, exist_ok=True)
        for f in filenames:
            if rel == "." and (f in PRIVATE or f.startswith("and-frame-") or f.startswith("and-")):
                continue
            src, dst = os.path.join(dirpath, f), os.path.join(out, f)
            if f in RELINK and os.path.exists(os.path.join(BUILD, f)):
                _replace(os.path.join(BUILD, f), dst)
            elif f in RELINK or not os.path.exists(dst):
                _link(src, dst)
    helper = os.path.join(ROOT, "build", "rthelper-x64", "rt_helper.exe")  # the 64-bit ray-tracing helper
    if os.path.exists(helper):
        _replace(helper, os.path.join(dst_root, "rt_helper.exe"))
    # The helper's AA libraries: FSR 3.1, and the optional DLSS and XeSS (archive/tools/fetch_aa_sdks.ps1).
    for name in ("amd_fidelityfx_vk.dll", "nvngx_dlss.dll", "libxess.dll"):
        lib = os.path.join(ROOT, "build", "rthelper-x64", name)
        if os.path.exists(lib):
            _replace(lib, os.path.join(dst_root, name))
    os.makedirs(os.path.join(dst_root, "SavedGames"), exist_ok=True)
    return dst_root


def default_count():
    return max(2, (os.cpu_count() or 8) // 4)


def _process_paths():
    """Full image paths of the running processes (Win32: EnumProcesses + QueryFullProcessImageNameW)."""
    import ctypes
    import ctypes.wintypes as W
    k32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
    pids = (W.DWORD * 4096)()
    got = W.DWORD()
    psapi.EnumProcesses(pids, ctypes.sizeof(pids), ctypes.byref(got))
    out = []
    for pid in pids[:got.value // 4]:
        h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024)
        n = W.DWORD(1024)
        if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
            out.append(buf.value)
        k32.CloseHandle(h)
    return out


def player_busy():
    """The human is playing: their remaster copy (engine\\target\\release\\and_remaster.exe of any checkout) or the
    original from its own game folder (an .exe in orig\\pc\\GameData, e.g. game.unpacked.exe; not a test instance) is running. Test runs pause
    meanwhile (docs/agent-brief.md: they eat the player's CPU)."""
    for p in _process_paths():
        low = p.lower().replace("/", "\\")
        if low.endswith("\\engine\\target\\release\\and_remaster.exe"):
            return p
        if "\\orig\\pc\\gamedata\\" in low and low.endswith(".exe") and "\\work\\instances\\" not in low:
            return p
    return None


def wait_while_player_plays(out=print, poll=30):
    """Blocks while player_busy(); says so once."""
    import time
    said = False
    while True:
        p = player_busy()
        if not p:
            return
        if not said:
            out(f"  the player's game is running ({p}): waiting")
            said = True
        time.sleep(poll)


if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else default_count()
    for n in range(count):
        print(instance_dir(n))
