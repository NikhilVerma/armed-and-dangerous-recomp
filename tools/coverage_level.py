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


ALL_LEVELS = ["lev_1_m1", "lev_1_m1_b", "lev_1_m3", "lev_1_m4", "lev_2_m1", "lev_2_m2", "lev_2_m3", "lev_2_m4",
              "lev_3_m1", "lev_3_m2", "lev_3_m3", "lev_3_m4_bd", "lev_4_m1", "lev_4_m2", "lev_4_m3", "lev_4_m5",
              "lev_5_m1", "lev_5_m2", "lev_5_m3", "lev_5_m4", "lev_5_m5", "lev_bonus_m1", "lev_bonus_m2",
              "lev_bonus_m3"]

# --- generic scenarios: any level, from its export (work/remaster/<level>/mission.json) ----------------------------
# They count frames from the level's first paused message (the briefing; [test] keys_from_play, testoracle.cpp, as
# the oracle runs do), so no level's load time or start movie shifts them: play frame 0 = play_delay frames after the
# briefing appears, "0:ENTER*3" dismisses it, play_warp puts Roman at a point at play frame -10. Levels without a
# briefing start the play clock 600 frames after the load.

# behaviours of enemies (warps to their clusters) and of objects worth a warp (one per behaviour), most specific first
ENEMIES = {"grunt_regular", "grunt", "captain", "captain_sniper", "monk", "goliath", "shrubpatrol", "squad"}
OBJECTS = ["stig_mgun_turret", "pub", "forge_cannon", "zeppelin", "saucer", "colossus", "base_turret", "goliath",
           "player_tickerbomb", "pickup_upgrade_weapon", "pickup_jumper_pack", "jump_marker", "pickup_topsy_weapon",
           "pickup_blackhole", "pickup_guyfawkes", "pickup_vindaloo_mortars", "pickup_bunchoffives",
           "pickup_sticky_bombs", "pickup_health", "pickup_generator", "pickup_token", "lamb", "rexus", "sheep",
           "peasant", "penguin", "wildwoodgate", "guard_brazier", "guard_lantern", "guard_alarm", "object_ic",
           "boulder_large", "q112", "base_shrub_platform", "crane_1", "oilpump", "fx_explosion", "barracade"]


def keys_fire(start, count, every=12, key="MOUSE1"):
    return [f"{start + i * every}:{key}" for i in range(count)]


def clusters(points, radius=150.0):
    """Greedy clusters of (x, y, z) points: [(count, centroid)], largest first."""
    left = list(points)
    out = []
    while left:
        seed = max(left, key=lambda p: sum(1 for q in left if (p[0] - q[0]) ** 2 + (p[1] - q[1]) ** 2 <= radius ** 2))
        near = [q for q in left if (seed[0] - q[0]) ** 2 + (seed[1] - q[1]) ** 2 <= radius ** 2]
        left = [q for q in left if q not in near]
        out.append((len(near), tuple(sum(c) / len(near) for c in zip(*near))))
    return sorted(out, key=lambda c: -c[0])


def generic_scenarios(level, groups=2, objects=5):
    """The generic scenarios of one level: start (walk, look, fire, sticky, jump, weapons, zoom, orders), fall (from
    1500 units above the start: landing death), group<n> (the n-th largest enemy cluster: fire and stickies while
    turning, until Roman dies or the time is up), obj-<behaviour> (next to one object of that behaviour: hold Use,
    fire, sticky)."""
    m = json.load(open(os.path.join(ROOT, "work", "remaster", level, "mission.json")))
    start = m["player_start"][0]["position"] if m.get("player_start") else None
    out = {}
    if start:
        out["start"] = {"play": True, "frames": 1200,
                        "keys": ["0:ENTER*3", "30:17*240", "60:MOUSEX6*40"] + keys_fire(300, 15) +
                                ["500:MOUSE2*5", "560:57", "600:16"] + keys_fire(640, 10, 10) +
                                ["760:19", "800:15", "830:MOUSE2*5", "900:44*60", "1000:2", "1020:4"]}
        out["fall"] = {"play": True, "frames": 600, "warp": [start[0], start[1], start[2] + 1500],
                       "keys": ["0:ENTER*3"]}
    insts = m["instances"]
    foes = [tuple(i["position"]) for i in insts if i["behaviour"] in ENEMIES]
    for n, (count, c) in enumerate(clusters(foes)[:groups]):
        out[f"group{n + 1}"] = {"play": True, "frames": 1100, "warp": [c[0] + 60, c[1], c[2] + 20],
                                "about": f"{count} enemies round ({c[0]:.0f}, {c[1]:.0f}, {c[2]:.0f})",
                                "keys": ["0:ENTER*3", "20:MOUSEX10*400"] + keys_fire(30, 45, 20) +
                                        ["100:MOUSE2*5", "400:MOUSE2*5", "700:MOUSE2*5"]}
    seen = set()
    for b in OBJECTS:
        if len(seen) >= objects:
            break
        hits = [i for i in insts if i["behaviour"] == b]
        if not hits:
            continue
        seen.add(b)
        p = hits[0]["position"]
        out[f"obj-{b}"] = {"play": True, "frames": 900, "warp": [p[0] + 30, p[1], p[2] + 20],
                           "about": f"{hits[0]['template']} {hits[0]['id']} at ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.0f})",
                           "keys": ["0:ENTER*3", "20:45*150", "200:45*6", "300:MOUSEX10*200"] +
                                   keys_fire(260, 20, 15) + ["600:MOUSE2*5", "700:45*6"]}
    return out


def scenarios_for(level):
    """A level's hand-made scenarios (lev_1_m1: they cover more than the generic ones would), else the generic ones."""
    return LEVELS[level] if level in LEVELS else generic_scenarios(level)


def run_scenario(level, name, sc, timeout, shots, inst="cov-main", out=print):
    instances.wait_while_player_plays(out)
    off = {n: False for n in compare.hook_names()}
    test = {"hook_coverage": 1, "coverage_skip": ",".join(sorted(coverage.UNSAFE))}
    frames = sc["frames"]
    if sc.get("play"):
        # play-frame keys (testoracle.cpp): no frame count exit, play_frames ends the run
        test.update({"keys_from_play": 1, "play_delay": 30, "play_frames": sc["frames"], "exit_frame": 0,
                     "trace_every": 0})
        if sc.get("warp"):
            test["play_warp"] = ",".join(f"{v:.1f}" for v in sc["warp"])
        frames = 0
    elif sc.get("warp"):
        test["warp"] = sc["warp"]
    if shots:
        test.update({"hash_frames": shots, "dump_hashed": 1})
        # the camera dumps (player position, health) need the recovered Camera_SetupProjection (testshot.cpp);
        # shot runs are for steering a scenario, the counts of the plain run are the ones to keep
        off["Camera_SetupProjection"] = True
    gd = instances.instance_dir(inst)
    path = os.path.join(gd, "and-coverage.txt")
    for f in [path] + glob.glob(os.path.join(gd, "and-frame-*")):
        if os.path.exists(f):
            os.remove(f)
    t = time.time()
    compare.run_once(f"coverage {level}.{name}", off, level, frames, timeout, ",".join(sc["keys"]), test,
                     game_dir=gd, out=out)
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read()
    for line in log.splitlines():
        if "test warp" in line or "test: briefing" in line or "warp" in line.lower() and "test" in line:
            out(f"  {level}.{name}: " + line.strip())
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
        out(f"  {level}.{name}: no and-coverage.txt written ({time.time() - t:.0f}s)")
        return False
    counts = {}
    for line in open(path):
        addr, count, fn = line.split()
        counts[fn] = {"address": addr, "calls": int(count)}
    os.makedirs(RUNS, exist_ok=True)
    json.dump(counts, open(os.path.join(RUNS, f"{level}.{name}.json"), "w"), indent=0)
    out(f"  {level}.{name}: {sum(1 for v in counts.values() if v['calls'])} functions executed "
        f"({time.time() - t:.0f}s)")
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
    ap.add_argument("--level", default="lev_1_m1", help="a level, several (comma-separated) or all")
    ap.add_argument("--only", default="", help="scenario names (comma-separated)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--merge", action="store_true", help="only rebuild coverage.json from work/coverage_runs")
    ap.add_argument("--shots", default="", help="first:every:count presented frames to save as PNGs")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--jobs", type=int, default=1, help="runs at once, each in its own game instance (cov<n>)")
    ap.add_argument("--resume", action="store_true", help="skip runs whose counts are already in work/coverage_runs")
    a = ap.parse_args()
    os.chdir(ROOT)
    levels = ALL_LEVELS if a.level == "all" else a.level.split(",")
    work = []
    for lv in levels:
        scen = scenarios_for(lv)
        for n, sc in scen.items():
            if a.only and n not in a.only.split(","):
                continue
            if a.resume and os.path.exists(os.path.join(RUNS, f"{lv}.{n}.json")):
                continue
            work.append((lv, n, sc))
    if a.list:
        for lv, n, sc in work:
            print(f"{lv:13} {n:28} frames {sc['frames']:5} warp {str(sc.get('warp', '-')):40} {sc.get('about', '')}")
        return 0
    if not a.merge:
        coverage.UNSAFE = coverage.unsafe_hooked()
        tag = a.level if len(levels) == 1 else "all_levels"
        backup = os.path.join(ROOT, "work", f"coverage_before_{tag}.json")
        if not os.path.exists(backup) and os.path.exists(OUT):
            shutil.copy2(OUT, backup)
        if a.jobs <= 1:
            for lv, n, sc in work:
                run_scenario(lv, n, sc, a.timeout, a.shots)
        else:
            import queue
            import threading
            q = queue.Queue()
            for w in work:
                q.put(w)
            lock = threading.Lock()

            def say(s):
                with lock:
                    print(s, flush=True)

            def worker(slot):
                while True:
                    try:
                        lv, n, sc = q.get_nowait()
                    except queue.Empty:
                        return
                    try:
                        run_scenario(lv, n, sc, a.timeout, "", f"cov{slot}", say)
                    except Exception as e:  # one failed run must not stop the others
                        say(f"  {lv}.{n}: failed: {e}")
            threads = [threading.Thread(target=worker, args=(i,)) for i in range(a.jobs)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
    merge()
    return 0


if __name__ == "__main__":
    sys.exit(main())
