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
    return sorted(set(names))


SCENARIOS = {
    # boot with movies disabled (the legal text times out by itself), then sit on the main menu
    "menu": {"level": "", "keys": "", "frames": 900},
    # boot, then the harness requests lev_1_m1 once the front end has loaded
    "level": {"level": "lev_1_m1", "keys": "", "frames": 900},
    # the level, with the device reported lost at frame 600: the game releases and recreates its resources
    "reset": {"level": "lev_1_m1", "keys": "", "frames": 900, "test": {"device_lost_frame": 600}},
}


DUMP = ""  # "frame" or "first:every:count": also save those frames as and-frame-*.bmp


def write_ini(hooks_on, level, frames, keys="", test=None):
    lines = ["[hooks]"]
    lines += [f"{name}={1 if on else 0}" for name, on in hooks_on.items()]
    lines += ["", "[startup]", "skip_intro=0", "skip_legal=0", "load=",
              "", "[display]", "windowed=1", "width=0", "height=0", "all_modes=0", "vsync=-1", "fps_limit=0",
              "", "[test]", "clock_step_ms=16.6667", "d3d_trace=1", f"scenario_load={level}", f"exit_frame={frames}",
              "shots=", "keys=", f"keys_frames={keys}", f"hash_frames={(DUMP + ':1:1') if DUMP and ':' not in DUMP else DUMP}", f"dump_hashed={1 if DUMP else 0}", "record_input=0", "fixed_dt_ms=0", "no_movies=1"]
    lines += [f"{k}={v}" for k, v in (test or {}).items()]
    with open(os.path.join(GAME_DIR, "and.ini"), "w") as f:
        f.write("\n".join(lines) + "\n")


def run_once(label, hooks_on, level, frames, timeout, keys="", test=None):
    write_ini(hooks_on, level, frames, keys, test)
    for name in ("and-d3d.txt", os.path.join("SavedGames", "options.opt")):
        p = os.path.join(GAME_DIR, name)
        if os.path.exists(p):
            os.remove(p)
    t = time.time()
    proc = subprocess.Popen([EXE], cwd=GAME_DIR)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print(f"  {label}: timed out after {timeout}s")
    log = open(os.path.join(GAME_DIR, "and.log"), encoding="latin1").read()
    if "CRASH" in log:
        print(f"  {label}: CRASH")
        print("\n".join(l for l in log.splitlines() if "CRASH" in l or "active:" in l))
    rows = [l.split() for l in open(os.path.join(GAME_DIR, "and-d3d.txt"))] if os.path.exists(
        os.path.join(GAME_DIR, "and-d3d.txt")) else []
    print(f"  {label}: {len(rows)} frames in {time.time() - t:.0f}s")
    if DUMP:
        import shutil
        for name in os.listdir(GAME_DIR):
            if name.startswith("and-frame-"):
                shutil.move(os.path.join(GAME_DIR, name), os.path.join(OUT, f"{label}-{name}"))
    return rows, "CRASH" in log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--scenario", default="all", help="all, or comma-separated: " + ", ".join(SCENARIOS))
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--fresh", action="store_true")
    ap.add_argument("--self", action="store_true", help="candidate = original too: checks the harness is deterministic")
    ap.add_argument("--dump", default="", help="frame number (or first:every:count) to save as images in work/compare")
    a = ap.parse_args()

    global DUMP
    DUMP = a.dump
    names = hook_names()
    only = [n for n in a.only.split(",") if n]
    unknown = [n for n in only if n not in names]
    if unknown:
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
    print(f"candidate: {len(on)} recovered: {', '.join(on)}")
    scenarios = list(SCENARIOS) if a.scenario == "all" else a.scenario.split(",")
    failed = False
    for name in scenarios:
        sc = SCENARIOS[name]
        print(f"[{name}]")
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
            base, crashed = run_once("baseline", baseline, sc["level"], sc["frames"], a.timeout, sc["keys"], sc.get("test"))
            if crashed or len(base) < sc["frames"] - 5:
                return False
            if base not in variants:
                variants.append(base)
                json.dump({"harness": harness_hash, "variants": variants}, open(cache, "w"))
            return True

        if not variants and not record_baseline():
            print("  baseline run failed")
            return 2
        if variants:
            print(f"  baseline: {len(variants)} known variant(s)")
        cand, crashed = run_once("candidate", candidate, sc["level"], sc["frames"], a.timeout, sc["keys"], sc.get("test"))

        def first_divergence(base):
            n = min(len(base), len(cand))
            first = next((i for i in range(n) if base[i] != cand[i]), None)
            if first is None and len(cand) < len(base):
                first = len(cand)
            return first

        if not crashed and any(first_divergence(b) is None for b in variants):
            print(f"  PASS: {len(cand)} frames identical")
            continue
        if not crashed:
            print("  mismatch: taking one more original run to rule out a rare baseline variant")
            record_baseline()
            if any(first_divergence(b) is None for b in variants):
                print(f"  PASS: {len(cand)} frames identical (matches a newly seen baseline variant)")
                continue
        failed = True
        best = max(variants, key=lambda b: first_divergence(b) or 0)
        first = first_divergence(best)
        if crashed:
            print("  FAIL: candidate crashed")
        elif first is not None and first < len(cand) and first < len(best):
            print(f"  FAIL: first divergent frame {first}: baseline {best[first]} candidate {cand[first]}")
        else:
            print(f"  FAIL: candidate stopped at frame {len(cand)} of {len(best)}")
    print("FAIL" if failed else "PASS")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
