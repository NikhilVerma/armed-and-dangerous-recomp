"""Comparison test: does recovered code behave exactly like the original?

Runs the game twice under identical deterministic conditions (virtual clock, scripted level load, fixed exit
frame, no enhancements) and compares the per-frame Direct3D call-stream hashes (hook/src/d3d_trace.cpp):
  baseline   every recovered function switched off ([hooks] X=0): the original game
  candidate  recovered functions switched on (all, or --only NAMES)
PASS when every frame's call count and hash match. On failure, prints the first divergent frame.
The baseline is cached per game hash + scenario in work/compare/ (use --fresh to redo it).

Usage: python tools/compare.py [--only Game_StartLevel,App_Init] [--scenario menu,level] [--fresh]
Exit code 0 = PASS, 1 = FAIL, 2 = harness error.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_DIR = os.path.join(ROOT, "orig", "pc", "GameData")
EXE = os.path.join(GAME_DIR, "game.unpacked.exe")
OUT = os.path.join(ROOT, "work", "compare")


def hook_names():
    names = []
    for dirpath, _, files in os.walk(os.path.join(ROOT, "src")):
        for f in files:
            if f.endswith(".cpp"):
                text = open(os.path.join(dirpath, f), encoding="utf-8").read()
                names += re.findall(r'HOOK\(0x[0-9a-fA-F]+,\s*"([^"]+)"', text)
                # File-local wrapper macros (R2A_HOOK, R7_PIXEL_HOOK, ...): NAME_HOOK(004f0390, Name, ...) with the
                # address without 0x and the name unquoted (stringised by the macro). Without these the comparison
                # left ~120 hooks switched on in the baseline too, so they were never compared.
                names += [n for _, n in re.findall(r'^\s*[A-Z0-9_]+_HOOK\(([0-9a-fA-F]{8}),\s*(\w+)', text, re.M)]
    return sorted(set(names))


SCENARIOS = {
    # boot with movies disabled (the legal text times out by itself), then sit on the main menu
    "menu": {"level": "", "keys": "", "frames": 900},
    # boot, then the harness requests lev_1_m1 once the front end has loaded
    "level": {"level": "lev_1_m1", "keys": "", "frames": 900},
    # the level, with the device reported lost at frame 600: the game releases and recreates its resources
    "reset": {"level": "lev_1_m1", "keys": "", "frames": 900, "test": {"device_lost_frame": 600}},
    # gameplay: dismiss the briefing (mouse 1), fire three shots, pause (Esc) and resume (Up, Up, Enter)
    "play": {"level": "lev_1_m1", "frames": 1100,
             "keys": "500:MOUSE1,570:MOUSE1,610:MOUSE1,650:MOUSE1,700:ESC,740:UP,770:UP,800:ENTER"},
    # (resize_frame exists in the harness, but the game only stretches the image on resize: no new code runs)
    # the menu, then a normal shutdown (WM_CLOSE at frame 400): destructors and device release run
    "quit": {"level": "", "keys": "", "frames": 400, "test": {"close_frame": 400, "exit_frame": 0}},
}


RENDER = {}  # [render] settings for the candidate run only (--rhi)
DUMP = ""  # "frame" or "first:every:count": also save those frames as and-frame-*.bmp


def write_ini(hooks_on, level, frames, keys="", test=None, render=None, extra_sections=None, game_dir=None):
    lines = ["[hooks]"]
    lines += [f"{name}={1 if on else 0}" for name, on in hooks_on.items()]
    extra = {k: dict(v) for k, v in (extra_sections or {}).items()}
    display = {"windowed": 1, "width": 0, "height": 0, "all_modes": 0, "vsync": -1, "fps_limit": 0}
    display.update(extra.pop("display", {}))
    lines += ["", "[startup]", "skip_intro=0", "skip_legal=0", "load=", "", "[display]"]
    lines += [f"{k}={v}" for k, v in display.items()]
    # Remaster defaults are on (docs/decision.md); comparisons run the original game's defaults.
    remaster = {"original_defaults": 1}
    remaster.update(extra.pop("remaster", {}))
    extra["remaster"] = remaster
    for section, values in extra.items():  # enhancement sections for this run ([camera], [qol], ...)
        lines += ["", f"[{section}]"] + [f"{k}={v}" for k, v in values.items()]
    lines += ["", "[test]"]
    values = {"clock_step_ms": "16.6667", "d3d_trace": 1, "scenario_load": level, "exit_frame": frames, "shots": "",
              "keys": "", "keys_frames": keys,
              "hash_frames": (DUMP + ':1:1') if DUMP and ':' not in DUMP else DUMP, "dump_hashed": 1 if DUMP else 0,
              "record_input": 0, "fixed_dt_ms": 0, "no_movies": 1,
              "parallel": 1,
              # never a window on the desk, no focus, no sound (hook/src/testoffscreen.cpp); a run may pass offscreen=0
              "offscreen": 1}
    values.update(test or {})  # scenario settings replace the defaults (an ini key must appear once)
    lines += [f"{k}={v}" for k, v in values.items()]
    if render:
        lines += ["", "[render]"] + [f"{k}={v}" for k, v in render.items()]
    with open(os.path.join(game_dir or GAME_DIR, "and.ini"), "w") as f:
        f.write("\n".join(lines) + "\n")


def launch(game_dir):
    """Starts the game in an instance folder. Shown without activation (SW_SHOWNOACTIVATE): with [test] offscreen=1 the
    hook also keeps its window off every monitor, so a test run never takes the focus or appears on the desk."""
    # a hook DLL from before [test] offscreen would show the window: refuse to start the game with it
    dll = os.path.join(game_dir, "dinput8.dll")
    if not os.path.exists(dll) or b"test: offscreen v2" not in open(dll, "rb").read():
        raise RuntimeError(f"{dll}: no [test] offscreen support (rebuild: pwsh tools/build.ps1); not starting a visible game")
    # no error dialogs on the desk ("Bad Image", crash boxes): the child inherits this process's error mode
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)  # FAILCRITICALERRORS | NOGPFAULTERRORBOX | NOOPENFILEERRORBOX
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 4  # SW_SHOWNOACTIVATE
    proc = subprocess.Popen([os.path.join(game_dir, os.path.basename(EXE))], cwd=game_dir, startupinfo=si)
    guard_offscreen(proc)
    return proc


def guard_offscreen(proc):
    """Safety net for [test] offscreen: a thread that kills the game the moment one of its visible windows overlaps
    the desktop (a code path the hook does not hold off-screen), and says so."""
    import ctypes
    import ctypes.wintypes as W
    import threading
    u = ctypes.windll.user32
    desk = W.RECT(u.GetSystemMetrics(76), u.GetSystemMetrics(77), 0, 0)  # SM_X/YVIRTUALSCREEN
    desk.right, desk.bottom = desk.left + u.GetSystemMetrics(78), desk.top + u.GetSystemMetrics(79)

    def watch():
        found = []

        @ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        def each(h, _):
            pid = W.DWORD()
            u.GetWindowThreadProcessId(h, ctypes.byref(pid))
            if pid.value == proc.pid and u.IsWindowVisible(h):
                r = W.RECT()
                u.GetWindowRect(h, ctypes.byref(r))
                if r.right > desk.left and r.left < desk.right and r.bottom > desk.top and r.top < desk.bottom:
                    found.append((r.left, r.top, r.right, r.bottom))
            return True
        while proc.poll() is None:
            u.EnumWindows(each, 0)
            if found:
                proc.kill()
                print(f"  guard: the game showed a window on the desktop at {found[0]}: killed (fix hook/src/testoffscreen.cpp)")
                return
            time.sleep(0.01)
    threading.Thread(target=watch, daemon=True).start()


def run_once(label, hooks_on, level, frames, timeout, keys="", test=None, render=None, extra_sections=None,
             game_dir=None, out=print):
    """One game run. game_dir: an instance folder (tools/instances.py) to run in parallel with others; default the
    game folder itself. out: where progress lines go (parallel runs collect them)."""
    # Never the player's game folder: a run rewrites and.ini and deletes SavedGames/options.opt. Without an instance,
    # the "main" instance (tools/instances.py).
    if game_dir is None:
        import instances
        game_dir = instances.instance_dir("main")
    gd = game_dir
    write_ini(hooks_on, level, frames, keys, test, render, extra_sections, gd)
    for name in ("and-d3d.txt", os.path.join("SavedGames", "options.opt")):
        p = os.path.join(gd, name)
        if os.path.exists(p):
            os.remove(p)
    t = time.time()
    proc = launch(gd)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        out(f"  {label}: timed out after {timeout}s")
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read()
    if "CRASH" in log:
        out(f"  {label}: CRASH")
        out("\n".join(l for l in log.splitlines() if "CRASH" in l or "active:" in l))
    rows = [l.split() for l in open(os.path.join(gd, "and-d3d.txt"))] if os.path.exists(
        os.path.join(gd, "and-d3d.txt")) else []
    out(f"  {label}: {len(rows)} frames in {time.time() - t:.0f}s")
    if DUMP:
        import shutil
        for name in os.listdir(gd):
            if name.startswith("and-frame-"):
                shutil.move(os.path.join(gd, name), os.path.join(OUT, f"{label}-{name}"))
    return rows, "CRASH" in log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--scenario", default="all", help="all, or comma-separated: " + ", ".join(SCENARIOS))
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--fresh", action="store_true")
    ap.add_argument("--rhi", action="store_true", help="candidate runs through the renderer interface ([render] rhi=1)")
    ap.add_argument("--self", action="store_true", help="candidate = original too: checks the harness is deterministic")
    ap.add_argument("--dump", default="", help="frame number (or first:every:count) to save as images in work/compare")
    ap.add_argument("--jobs", type=int, default=0,
                    help="scenarios run at once, each in its own game instance (tools/instances.py); 1 = in the "
                         "game folder, one after another; default: one per scenario up to the CPU count / 4")
    a = ap.parse_args()

    global DUMP
    DUMP = a.dump
    names = hook_names()
    spec = a.only
    from_file = spec.startswith("@")
    if from_file:  # --only @file: names in a file (lists too long for a command line)
        with open(spec[1:]) as f:
            spec = f.read().strip()
    only = [n for n in spec.split(",") if n]
    unknown = [n for n in only if n not in names]
    if unknown and from_file:
        # Generated lists (tools/bisect_hooks.py) name every HOOK in src/, including files the built DLL does not
        # contain yet (build.ps1 -Exclude): ignore those instead of failing every run.
        print(f"note: {len(unknown)} hook(s) not in the built DLL ignored, e.g. {unknown[:3]}")
        only = [n for n in only if n in names]
    elif unknown:
        print("unknown hooks:", unknown, "known:", names)
        return 2
    candidate = {n: (n in only if only else True) for n in names}
    if a.self:
        candidate = {n: False for n in names}
    baseline = {n: False for n in names}

    os.makedirs(OUT, exist_ok=True)
    exe_hash = hashlib.sha256(open(EXE, "rb").read()).hexdigest()[:12]
    dll = os.path.join(GAME_DIR, "dinput8.dll")
    harness_hash = hashlib.sha256(open(dll, "rb").read()).hexdigest()[:12]
    on = [n for n, v in candidate.items() if v]
    print(f"candidate: {len(on)} recovered functions")
    scenarios = list(SCENARIOS) if a.scenario == "all" else a.scenario.split(",")
    import instances
    jobs = a.jobs or min(len(scenarios), instances.default_count())

    import queue
    free = queue.Queue()
    for n in range(jobs):
        free.put(n)

    def run_scenario(index, name):
        slot = free.get() if jobs > 1 else None
        try:
            return scenario_body(name, instances.instance_dir(slot) if slot is not None else None)
        finally:
            if slot is not None:
                free.put(slot)

    def scenario_body(name, gd):
        lines = []
        out = lines.append
        sc = SCENARIOS[name]
        out(f"[{name}]")
        # Baseline variants: original-code runs for this game, scenario and harness build (recovered code is off
        # in the baseline, but the harness lives in the same DLL, so any DLL change invalidates them). Rare
        # nondeterministic runs exist, so every distinct original run is kept and a candidate passes if it
        # matches any of them. On a mismatch one more original run is taken before declaring failure.
        cache = os.path.join(OUT, f"baseline-{exe_hash}-{name}.json")
        variants = []
        if not a.fresh and os.path.exists(cache):
            saved = json.load(open(cache))
            if saved.get("harness") == harness_hash:
                variants = saved["variants"]

        def record_baseline():
            base, crashed = run_once(f"{name} baseline", baseline, sc["level"], sc["frames"], a.timeout, sc["keys"],
                                     sc.get("test"), game_dir=gd, out=out)
            if crashed or len(base) < sc["frames"] - 5:
                return False
            if base not in variants:
                variants.append(base)
                json.dump({"harness": harness_hash, "variants": variants}, open(cache, "w"))
            return True

        if not variants and not record_baseline():
            out("  baseline run failed")
            return lines, True
        if variants:
            out(f"  baseline: {len(variants)} known variant(s)")
        cand, crashed = run_once(f"{name} candidate", candidate, sc["level"], sc["frames"], a.timeout, sc["keys"],
                                 {**(sc.get("test") or {}), **({"rhi_calls": 1} if a.rhi else {})},
                                 {"rhi": 1} if a.rhi else None, game_dir=gd, out=out)

        def first_divergence(base):
            n = min(len(base), len(cand))
            first = next((i for i in range(n) if base[i] != cand[i]), None)
            if first is None and len(cand) < len(base):
                first = len(cand)
            return first

        if not crashed and any(first_divergence(b) is None for b in variants):
            out(f"  PASS: {len(cand)} frames identical")
            return lines, False
        if not crashed:
            out("  mismatch: taking one more original run to rule out a rare baseline variant")
            record_baseline()
            if any(first_divergence(b) is None for b in variants):
                out(f"  PASS: {len(cand)} frames identical (matches a newly seen baseline variant)")
                return lines, False
            # Loading progress depends on real time (streaming thread), more so with several games running at
            # once: one more candidate run. A real difference fails every time.
            out("  still different: one more candidate run")
            cand, crashed = run_once(f"{name} candidate", candidate, sc["level"], sc["frames"], a.timeout, sc["keys"],
                                     {**(sc.get("test") or {}), **({"rhi_calls": 1} if a.rhi else {})},
                                     {"rhi": 1} if a.rhi else None, game_dir=gd, out=out)
            if not crashed and any(first_divergence(b) is None for b in variants):
                out(f"  PASS: {len(cand)} frames identical (second candidate run)")
                return lines, False
        best = max(variants, key=lambda b: first_divergence(b) or 0)
        first = first_divergence(best)
        if crashed:
            out("  FAIL: candidate crashed")
        elif first is not None and first < len(cand) and first < len(best):
            out(f"  FAIL: first divergent frame {first}: baseline {best[first]} candidate {cand[first]}")
        else:
            out(f"  FAIL: candidate stopped at frame {len(cand)} of {len(best)}")
        return lines, True

    failed = False
    if jobs > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(jobs) as pool:
            results = list(pool.map(lambda iv: run_scenario(*iv), enumerate(scenarios)))
    else:
        results = [run_scenario(i, n) for i, n in enumerate(scenarios)]
    for lines, bad in results:
        print("\n".join(lines))
        failed |= bad
    # Several games at once make loading timing vary more (measured: +-1 draw in menu and level frames, either
    # run). A scenario that failed in parallel gets one more try alone in the game folder; a real difference
    # fails there too.
    retry = [n for n, (_, bad) in zip(scenarios, results) if bad] if jobs > 1 else []
    if retry:
        failed = False
        for name in retry:
            print(f"{name}: failed with {jobs} games at once; once more alone")
            lines, bad = scenario_body(name, None)
            print("\n".join(lines))
            failed |= bad
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
