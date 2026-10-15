"""Widen hook coverage (work/coverage.json) to everything one level does: runs the ORIGINAL game (every hook off,
[test] hook_coverage=1) through a list of short scenarios that each warp Roman next to one mechanic and drive it
with scripted keys, and merges the call counts into work/coverage.json, so tools/port_queue.py lists every
function the level reaches.

Usage:
  python tools/coverage_level.py                         run every scenario of lev_1_m1, merge
  python tools/coverage_level.py --only pub,win          some of them
  python tools/coverage_level.py --list                  the scenarios
  python tools/coverage_level.py --merge                 rebuild coverage.json from the saved runs only
  python tools/coverage_level.py --only pub --shots 600:100:8   also dump frames (work/shots/cov-<name>-<f>.png)

Each run's counts are kept in work/coverage_runs/<level>.<scenario>.json; the merge subtracts any earlier
contribution of the same scenario first, so reruns never double count, and the counts of tools/coverage.py's
scenarios are kept (rerunning tools/coverage.py drops the level runs from coverage.json: run --merge after it).

Scenario keys: "frame:KEY*N" in presented frames (hook/src/input_hooks.cpp): W 17, S 31, A 30, D 32, Use X 45,
Q 16 (next weapon), Tab 15 (next special), R 19, Space 57, 1 / 3 teammate orders, MOUSE1 fire, MOUSE2 special
(sticky / topsy), MOUSEX<n> turn by n counts per poll. At most 64 keys. The briefing is up from ~480; MOUSE1 at
500 dismisses it. [test] warp=FRAME:x,y,z (game axes, z up; one warp per run) moves Roman; he keeps his heading
(facing -y on lev_1_m1 after the intro path).
"""
import argparse
import glob
import json
import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402
import coverage  # noqa: E402
import instances  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS = os.path.join(ROOT, "work", "coverage_runs")
OUT = os.path.join(ROOT, "work", "coverage.json")


def fire(start, count, every=12, key="MOUSE1"):
    return [f"{start + i * every}:{key}" for i in range(count)]


DISMISS = ["500:MOUSE1"]  # the briefing (paused message box)

# lev_1_m1 (positions from work/remaster/lev_1_m1/instance_props.json and mission.json; game axes, z up)
LEVELS = {
    "lev_1_m1": {
        # The pub (225 at (210, -106)): Use at the door enters the pub screen (tickers, topsy, health), Enter takes
        # the awards, Esc leaves (sends 900: arrow killed, gate-1 site 252 enabled, sub2 text). Trigger box 153
        # (185, -64) on the way wakes barracks 25 (810). Then Tab to the topsy and throw it (MOUSE2), stickies.
        "pub": {"frames": 2000, "warp": "560:194,-80,13",
                "keys": DISMISS + ["620:45*20", "700:ENTER", "760:ENTER", "820:ENTER", "880:ENTER", "940:ENTER",
                                   "1000:ESC", "1100:MOUSE1", "1160:MOUSE1", "1220:15", "1260:15", "1300:MOUSE2*10",
                                   "1500:15", "1540:MOUSE2*10", "1700:16", "1760:MOUSE1*60"]},
        # Gate 1 with tickers: the pub as above, then walk to site 252 (-84, 41) without turning (Roman faces -y:
        # S moves +y, D -x), hold Use 2.8 s to arm, the fuse (6 s) blows gate 248 (556): grunt 67 runs to the
        # siren 33 (alarm chain), generators 152 / 215 feed troops; fire at whoever comes.
        "gate1": {"frames": 3000, "warp": "560:194,-80,13",
                  "keys": DISMISS + ["620:45*20", "700:ENTER", "760:ENTER", "820:ENTER", "880:ENTER", "940:ENTER",
                                     "1000:ESC", "1100:MOUSE1", "1180:31*100", "1290:32*240", "1540:31*125", "1670:32*195", "1880:45*170",
                                     "2060:30*120"] + fire(2300, 30, 20)},
        # Gate 1 without tickers: "send_trigger_non_enabled" 7 (ui_ear 253 text); Use held at the site.
        "gate1_locked": {"frames": 900, "warp": "560:-75,45,20",
                         "keys": DISMISS + ["620:45*150", "800:MOUSE1"]},
        # Second encounter: the courtyard's nine guards (-50..30, -550..-490) and the 98 boxes (-44..30, -501..-471)
        # that wake barracks 235; fire and stickies until Roman dies (death, fx_trotter_death, MissionFailed 5 s
        # later, failed page).
        "courtyard": {"frames": 2500, "warp": "560:25,-468,12",
                      "keys": DISMISS + fire(600, 30, 15) + fire(620, 6, 75, "MOUSE2*5") + ["1100:19",
                                                                                          "1800:DOWN", "1860:ENTER",
                                                                                          "2000:ENTER"]},
        # The alarm side: grunt 67 / window grunts 212, 213, the siren 33 (-201, -65), kegs 226-228, trenches
        # 37-41, generator 152: fire at the kegs ahead and let the troops come.
        "alarm": {"frames": 1900, "warp": "560:-200,10,14",
                  "keys": DISMISS + fire(600, 40, 20) + fire(700, 4, 200, "MOUSE2*5")},
        # The mounted gun stig_mgun 32 at (71, 173) (prompt "Press %s to enter mounted gun"): Use, fire, turn,
        # Use to leave; kegs 168 / 169 below it, the guards 82 / 196 / 34 around.
        "mgun": {"frames": 1500, "warp": "560:71,186,14",
                 "keys": DISMISS + ["600:45*6", "680:MOUSE1*120", "700:MOUSEX20*30", "820:MOUSEX-20*60",
                                    "900:MOUSE1*200", "1150:45*6", "1250:MOUSE1*100"]},
        # The west slope: kegs 94/95/130/155/156 by grunts 72/129/154/157, box 194 (808: grunt 34 to the gun),
        # the pickup generator 219 (83, 72) on the way back; fire forward at the keg group.
        "kegs": {"frames": 1700, "warp": "560:-150,470,30",
                 "keys": DISMISS + fire(600, 50, 18) + fire(650, 3, 250, "MOUSE2*5")},
        # The pipes 97-117 and boom_island 131 (795, 493): warp onto the pipe line and shoot along it.
        "pipes": {"frames": 1300, "warp": "560:-40,640,25",
                  "keys": DISMISS + fire(600, 40, 15) + ["700:MOUSEX15*40"]},
        # Pickups: generator 219 (83, 72) gives the current weapon's ammo; walk over it after spending a clip.
        "pickup": {"frames": 1200, "warp": "560:83,110,16",
                   "keys": DISMISS + fire(580, 10, 6) + ["700:17*70", "900:19"]},
        # A 110-unit drop next to the extras token 246 (-216, 973): landing damage, then walk over the token; jump,
        # teammate orders (1 / 3), held zoom (Z), weapon switch (Q), reload (R).
        "fall_token": {"frames": 1400, "warp": "560:-216,1000,240",
                       "keys": DISMISS + ["760:17*45", "840:57", "880:2", "920:4", "960:44*80", "1080:16",
                                          "1180:19"] + fire(1220, 8, 20)},
        # The penguins 197-199 (-141, 747): shoot them (any hit kills, -10 accidents, screams) and a sticky.
        "penguins": {"frames": 1100, "warp": "560:-141,775,60",
                     "keys": DISMISS + fire(600, 20, 15) + ["700:MOUSE2*5"]},
        # Win: the goal boxes 64 / 259 / 260 behind gate 2 send 123 (mission success): score, sounds stop,
        # "Well Done!" text, Enter, the Stats screen.
        "win": {"frames": 1500, "warp": "560:322,-974,12",
                "keys": DISMISS + ["800:MOUSE1", "900:MOUSE1", "1000:MOUSE1", "1100:MOUSE1", "1200:MOUSE1"]},
    },
}


def run_scenario(level, name, sc, timeout, shots):
    off = {n: False for n in compare.hook_names()}
    test = {"hook_coverage": 1, "coverage_skip": ",".join(sorted(coverage.UNSAFE))}
    if sc.get("warp"):
        test["warp"] = sc["warp"]
    if shots:
        test.update({"hash_frames": shots, "dump_hashed": 1})
        # the camera dumps (player position, health) need the recovered Camera_SetupProjection (testshot.cpp);
        # shot runs are for steering a scenario, the counts of the plain run are the ones to keep
        off["Camera_SetupProjection"] = True
    gd = instances.instance_dir("main")
    path = os.path.join(gd, "and-coverage.txt")
    for f in [path] + glob.glob(os.path.join(gd, "and-frame-*")):
        os.remove(f)
    t = time.time()
    compare.run_once(f"coverage {level}.{name}", off, level, sc["frames"], timeout, ",".join(sc["keys"]), test)
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read()
    for line in log.splitlines():
        if "test warp" in line:
            print("  " + line.strip())
    if shots:
        from PIL import Image
        os.makedirs(os.path.join(ROOT, "work", "shots"), exist_ok=True)
        for f in sorted(glob.glob(os.path.join(gd, "and-frame-*.bmp"))):
            n = int(os.path.basename(f)[10:15])
            out = os.path.join(ROOT, "work", "shots", f"cov-{name}-{n}.png")
            Image.open(f).convert("RGB").resize((640, 360)).save(out)
            cam = f[:-4] + ".camera.json"
            if os.path.exists(cam):
                c = json.load(open(cam))
                print(f"  frame {n}: player {[round(v) for v in c.get('player', [])]} health "
                      f"{c.get('player_health', 0):.0f}")
    if not os.path.exists(path):
        print(f"  {name}: no and-coverage.txt written ({time.time() - t:.0f}s)")
        return False
    counts = {}
    for line in open(path):
        addr, count, fn = line.split()
        counts[fn] = {"address": addr, "calls": int(count)}
    os.makedirs(RUNS, exist_ok=True)
    json.dump(counts, open(os.path.join(RUNS, f"{level}.{name}.json"), "w"), indent=0)
    print(f"  {name}: {sum(1 for v in counts.values() if v['calls'])} functions executed ({time.time() - t:.0f}s)")
    return True


def merge():
    cov = json.load(open(OUT)) if os.path.exists(OUT) else {}
    runs = {os.path.basename(f)[:-5]: json.load(open(f)) for f in sorted(glob.glob(os.path.join(RUNS, "*.json")))}
    # drop earlier contributions of these runs (idempotent); a level run listed without its file cannot be undone
    lost = {t for e in cov.values() for t in e["scenarios"] if t.startswith("lev_") and t not in runs}
    if lost:
        print(f"warning: coverage.json counts runs whose files are gone ({', '.join(sorted(lost))}); restore "
              f"the coverage_before_<level>.json backup and --merge")
    for tag, counts in runs.items():
        for fn, e in cov.items():
            if tag in e["scenarios"]:
                e["calls"] -= counts.get(fn, {}).get("calls", 0)
                e["scenarios"].remove(tag)
    for tag, counts in runs.items():
        for fn, v in counts.items():
            e = cov.setdefault(fn, {"address": v["address"], "calls": 0, "scenarios": []})
            e["calls"] += v["calls"]
            if v["calls"]:
                e["scenarios"].append(tag)
    json.dump(cov, open(OUT, "w"), indent=1)
    print(f"{sum(1 for v in cov.values() if v['calls'])} of {len(cov)} registered functions executed -> {OUT}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--level", default="lev_1_m1")
    ap.add_argument("--only", default="")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--merge", action="store_true", help="only rebuild coverage.json from work/coverage_runs")
    ap.add_argument("--shots", default="", help="first:every:count presented frames to save as PNGs")
    ap.add_argument("--timeout", type=int, default=240)
    a = ap.parse_args()
    os.chdir(ROOT)
    scenarios = LEVELS[a.level]
    if a.list:
        for n, sc in scenarios.items():
            print(f"{n:14} frames {sc['frames']:5} warp {sc.get('warp', '-'):24} keys {len(sc['keys'])}")
        return 0
    if not a.merge:
        coverage.UNSAFE = coverage.unsafe_hooked()
        names = a.only.split(",") if a.only else list(scenarios)
        backup = os.path.join(ROOT, "work", f"coverage_before_{a.level}.json")
        if not os.path.exists(backup) and os.path.exists(OUT):
            shutil.copy2(OUT, backup)
        for n in names:
            run_scenario(a.level, n, scenarios[n], a.timeout, a.shots)
    merge()
    return 0


if __name__ == "__main__":
    sys.exit(main())
