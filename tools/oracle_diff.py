"""Lockstep difference harness: the ORIGINAL game is the oracle for the remaster engine.

Runs one scenario in both games on the same scripted input from the same start, then compares their per-frame state
traces and reports the first divergence per field family (docs/remaster-engine.md "Oracle runs").

    python tools/oracle_diff.py SCENARIO [--exe target-wt-oracle] [--no-run] [--only original|remaster]
                                         [--hooks off|on] [--json]

SCENARIO: a name in tools/oracle_scenarios/ (or a path to a .json). A scenario:
    {"level": "lev_1_m1", "keys": "0:ENTER*3,40:17*300", "frames": 400, "trace_every": 2,
     "warp": [x, y, z] (optional), "play_delay": 30 (optional),
     "tolerance": {"pos": 6, "grunt_pos": 12, "heading": 12, "health": 1, "frames": 20, "event_frames": 30},
     "expected": {"family": "why this family differs (an accepted difference, with evidence)"},
     "suite": true (part of tools/oracle_suite.py)}
Keys are the hook's keys_frames form (hook/src/input_hooks.cpp; the remaster parses the same text, engine/src/oracle.rs):
`frame:KEY` or `frame:KEY*held_frames`, frame 0 = play_delay frames after the briefing appeared (the scenario's own
frame-0 ENTER dismisses it). Scan codes: W 17, A 30, S 31, D 32, Space 57, Use X 45, Q 16 next weapon, Tab 15 next
special, R 19 reload; MOUSE1 fire, MOUSE2 special (sticky bomb), MOUSEX<n> / MOUSEY<n> mouse counts per frame.

Both games: the original through the hook DLL ([test] offscreen=1: no window on the desk, no focus, muted; virtual
clock 1/60 s a frame; CRT srand(12345) at the end of the level load), the remaster with --background and 1/60 s a
frame. Traces: work/oracle/<scenario>/original.jsonl and remaster.jsonl; the report: report.txt there.

Exit code: 0 no unexpected divergence, 1 divergences, 2 a run failed.
"""
import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENARIOS = os.path.join(ROOT, "tools", "oracle_scenarios")
OUT = os.path.join(ROOT, "work", "oracle")

# The original's AI state (+0x460, docs/subsystems/grunt-ai.md) as the remaster's Mind (engine/src/ai.rs: the variants
# document the states they stand for). Hit reactions (0x12f..0x132) and falling (0x82) have no Mind of their own: the
# remaster keeps the mind the grunt had, so they compare as "any".
STATE_MIND = {1: "Roam", 0xf: "Roam", 2: "Chase", 0x3c: "Chase", 0x3e: "Chase", 0x79: "Guard", 0xdd: "Guard",
              0x7a: "Guard", 0x8c: "WaitTrigger", 0xa: "Fire", 0x11: "Fire", 0x32: "Reload", 0xc1: "Reload",
              0xc8: "Flee", 0x96: "ToMarker", 0x50: "Home", 0x3d: "Wander", 0x6e: "Taunt", 0x10: "Throw",
              **{s: "Cover" for s in range(0xa0, 0xa6)}, **{s: "Gunner" for s in (0x7b, 0x7c, 0x7d)},
              0x82: "*", 0x12f: "*", 0x130: "*", 0x131: "*", 0x132: "*"}
# The level state (Game+0xa64 +0xc: 1 playing, 2 / 10 won, 3..9 failed) as the remaster's mission Outcome
MISSION = {0: "Playing", 1: "Playing", 2: "Won", 10: "Won", **{s: "Failed" for s in range(3, 10)}}
SOUND_HOOKS = ("fn_004f32f0", "fn_004f3740", "fn_004f3c60")
TOL = {"pos": 6.0, "grunt_pos": 12.0, "heading": 12.0, "health": 1.0, "time": 0.1, "frames": 20, "event_frames": 30}


def load_scenario(name):
    path = name if name.endswith(".json") else os.path.join(SCENARIOS, name + ".json")
    sc = json.load(open(path))
    sc.setdefault("name", os.path.splitext(os.path.basename(path))[0])
    sc.setdefault("level", "lev_1_m1")
    sc.setdefault("trace_every", 2)
    sc.setdefault("play_delay", 30)
    return sc


def out_dir(sc):
    d = os.path.join(OUT, sc["name"])
    os.makedirs(d, exist_ok=True)
    return d


# --- runs ------------------------------------------------------------------------------------------------------

def run_original(sc, hooks_on=False, timeout=900):
    """The original game in the "oracle" instance (tools/instances.py), every recovered function off (hooks off: the
    shipped code) unless hooks_on. Returns the trace path or None."""
    import compare
    import instances
    gd = instances.instance_dir("oracle")
    for f in ("and-trace.jsonl", "and.log"):
        p = os.path.join(gd, f)
        if os.path.exists(p):
            os.remove(p)
    test = {"keys_from_play": 1, "trace_every": sc["trace_every"], "play_delay": sc["play_delay"],
            "play_frames": sc["frames"], "reseed_on_load": 1, "d3d_trace": 0, "offscreen": 1, "parallel": 1,
            # no exit by frame count: play_frames ends the run
            "exit_frame": 0}
    if sc.get("warp"):
        test["play_warp"] = ",".join(str(v) for v in sc["warp"])
    hooks = {n: hooks_on for n in compare.hook_names()}
    # The sound log is a callback in the recovered play functions (StartVoice, src/audio/audio_02.cpp:1495): these
    # three run recovered code (S2, verified by tools/compare.py) so the original's sounds are traced too.
    for n in SOUND_HOOKS:
        hooks[n] = True
    t = time.time()
    compare.write_ini(hooks, sc["level"], 0, sc["keys"], test, None, None, gd)
    opt = os.path.join(gd, "SavedGames", "options.opt")
    if os.path.exists(opt):
        os.remove(opt)
    proc = compare.launch(gd)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        print(f"  original: timed out after {timeout} s")
    src = os.path.join(gd, "and-trace.jsonl")
    log = open(os.path.join(gd, "and.log"), encoding="latin1").read() if os.path.exists(os.path.join(gd, "and.log")) else ""
    if "CRASH" in log:
        print("  original: CRASH (" + os.path.join(gd, "and.log") + ")")
    if not os.path.exists(src):
        print(f"  original: no trace ({os.path.join(gd, 'and.log')})")
        return None
    dst = os.path.join(out_dir(sc), "original.jsonl")
    shutil.copy2(src, dst)
    shutil.copy2(os.path.join(gd, "and.log"), os.path.join(out_dir(sc), "original.log"))
    print(f"  original: {sum(1 for _ in open(dst))} lines in {time.time() - t:.0f} s")
    return dst


def run_remaster(sc, exe, timeout=900):
    """The remaster engine headless (tools/playtest.py: --background, silent, no mouse grab) on a script with the
    same keys (engine/src/oracle.rs). Returns the trace path or None."""
    d = out_dir(sc)
    script = os.path.join(d, "remaster.json")
    oracle = {"keys": sc["keys"], "frames": sc["frames"], "trace_every": sc["trace_every"],
              "play_delay": sc["play_delay"]}
    if sc.get("warp"):
        oracle["warp"] = sc["warp"]
    body = {"oracle": oracle}
    if sc.get("level", "lev_1_m1") != "lev_1_m1":
        print("  remaster: only lev_1_m1 is wired (the playtest scene default)")
    json.dump(body, open(script, "w"), indent=1)
    trace = os.path.join(d, "remaster.trace.jsonl")
    if os.path.exists(trace):
        os.remove(trace)
    t = time.time()
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "playtest.py"), script, "--exe", exe,
                        "--timeout", str(timeout)], capture_output=True, text=True)
    if not os.path.exists(trace):
        print("  remaster: no trace\n" + r.stdout[-2000:])
        return None
    dst = os.path.join(d, "remaster.jsonl")
    shutil.copy2(trace, dst)
    print(f"  remaster: {sum(1 for _ in open(dst))} lines in {time.time() - t:.0f} s")
    return dst


# --- comparison --------------------------------------------------------------------------------------------------

def read(path):
    rows = {}
    for line in open(path):
        line = line.strip()
        if line:
            r = json.loads(line)
            rows[r["f"]] = r
    return rows


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def angle(a, b):
    return abs((a - b + 180) % 360 - 180)


def grunt_mind(g):
    if "mind" in g:
        return g["mind"]
    if g.get("health", 1) <= 0:
        return "Dead"
    return STATE_MIND.get(g.get("state"), f"state 0x{g.get('state', 0):x}")


UNLIMITED = 4294967295
# the remaster's weapon names as the original's descriptor ids (Game+0x2770 + id * 0x150; hud.md: 0 machine gun, 2 rifle)
WEAPONS = {"machinegun": 0, "rifle": 2}


def weapon_id(w):
    return WEAPONS.get(w, w) if isinstance(w, str) else w


def sound_key(name):
    """A sound definition's name without its random variant number (fs_boot_heavy_snow_run_3 -> fs_boot_heavy_snow_run):
    the variant is a roll, and the two games' random streams differ."""
    n = str(name).lower()
    head, _, tail = n.rpartition("_")
    return head if head and tail.isdigit() and len(tail) <= 2 else n


def pause_spans(rows):
    spans, start, last = [], None, None
    for f in sorted(rows):
        p = rows[f].get("paused")
        if p and start is None:
            start = f
        if not p and start is not None:
            spans.append((start, f))
            start = None
        last = f
    if start is not None:
        spans.append((start, last))
    return spans


def match_grunts(o0, r0, tol):
    """id -> id: the same instance id when the frame-0 positions agree (the original's +0x396 entry number and the
    remaster's InstanceId are both the level's 1-based object entry), else the nearest unclaimed grunt."""
    ro = {g["id"]: g for g in r0.get("grunts", [])}
    pairs, claimed, how = {}, set(), "id"
    for g in o0.get("grunts", []):
        r = ro.get(g["id"])
        if r and dist(g["pos"], r["pos"]) <= tol and g["id"] not in claimed:
            pairs[g["id"]] = g["id"]
            claimed.add(g["id"])
    for g in o0.get("grunts", []):
        if g["id"] in pairs:
            continue
        best = min((r for r in r0.get("grunts", []) if r["id"] not in claimed),
                   key=lambda r: dist(g["pos"], r["pos"]), default=None)
        if best and dist(g["pos"], best["pos"]) <= tol:
            pairs[g["id"]] = best["id"]
            claimed.add(best["id"])
            how = "position"
    return pairs, how


class Families:
    """First divergence per field family: {family: (frame, detail)}; a family is reported once, at its first frame."""

    def __init__(self):
        self.first = {}
        self.checked = set()

    def check(self, family, frame, ok, detail):
        self.checked.add(family)
        if not ok and family not in self.first:
            self.first[family] = (frame, detail)


def persistent(rows, frames, f, key, value, window):
    """True when some row within +-window frames of f has key(row) == value (timing tolerance for discrete fields)."""
    for g in frames:
        if abs(g - f) <= window and key(rows[g]) == value:
            return True
    return False


def compare_traces(sc, orig, rem):
    tol = dict(TOL, **sc.get("tolerance", {}))
    fam = Families()
    frames = sorted(set(orig) & set(rem))
    notes = []
    if not frames:
        return fam, ["no common frames"], {}
    o0, r0 = orig[frames[0]], rem[frames[0]]
    pairs, how = match_grunts(o0, r0, tol["grunt_pos"] * 2)
    notes.append(f"frames compared: {frames[0]}..{frames[-1]} ({len(frames)} lines); grunts matched by {how}: "
                 f"{len(pairs)} of {len(o0.get('grunts', []))} original / {len(r0.get('grunts', []))} remaster")
    unmatched = [g["id"] for g in o0.get("grunts", []) if g["id"] not in pairs]
    if unmatched:
        notes.append(f"original grunts with no remaster counterpart at frame {frames[0]}: "
                     + ", ".join(f"{g['id']} at {g['pos']} ({grunt_mind(g)})" for g in o0.get("grunts", []) if g["id"] not in pairs))
    claimed = set(pairs.values())
    extra = [g for g in r0.get("grunts", []) if g["id"] not in claimed]
    if extra:
        notes.append(f"remaster grunts with no original counterpart at frame {frames[0]}: "
                     + ", ".join(f"{g['id']} at {g['pos']} ({g.get('mind')})" for g in extra))
    win = tol["frames"]
    rf = sorted(rem)
    of = sorted(orig)
    for f in frames:
        o, r = orig[f], rem[f]
        fam.check("timing", f, abs(o["t"] - r["t"]) <= tol["time"],
                  f"game time original {o['t']:.2f} s, remaster {r['t']:.2f} s")
        ro, rr = o.get("roman"), r.get("roman")
        if ro and rr:
            d = dist(ro["pos"], rr["pos"])
            fam.check("roman.pos", f, d <= tol["pos"],
                      f"Roman at {ro['pos']} vs {rr['pos']} ({d:.1f} units apart)")
            fam.check("roman.heading", f, angle(ro["heading"], rr["heading"]) <= tol["heading"],
                      f"heading {ro['heading']:.1f} vs {rr['heading']:.1f} deg")
            fam.check("roman.health", f, abs(ro["health"] - rr["health"]) <= tol["health"],
                      f"health {ro['health']} vs {rr['health']}")
            fam.check("roman.weapon", f, persistent(rem, rf, f, lambda x: weapon_id((x.get("roman") or {}).get("weapon")), ro.get("weapon"), win),
                      f"weapon {ro.get('weapon')} vs {rr.get('weapon')}")
            for k in ("clip", "ammo", "stickies"):
                if k == "ammo" and rr.get(k) == UNLIMITED:
                    # the remaster keeps no count for an unlimited reserve (the rifle, weapon type 2): only the
                    # original's own count is shown, never compared
                    continue
                fam.check(f"roman.{k}", f, persistent(rem, rf, f, lambda x, k=k: (x.get("roman") or {}).get(k), ro[k], win),
                          f"{k} {ro[k]} vs {rr[k]}")
        oc, rc = o.get("companions", []), r.get("companions", [])
        for i, name in enumerate(("jonesy", "q")):
            a = oc[i] if i < len(oc) else None
            b = rc[i] if i < len(rc) else None
            if a and b:
                d = dist(a[:3], b[:3])
                fam.check(f"companion.{name}.pos", f, d <= tol["grunt_pos"], f"{name} at {a[:3]} vs {b[:3]} ({d:.1f} apart)")
                fam.check(f"companion.{name}.health", f, abs(a[3] - b[3]) <= tol["health"], f"{name} health {a[3]} vs {b[3]}")
        og = {g["id"]: g for g in o.get("grunts", [])}
        rg = {g["id"]: g for g in r.get("grunts", [])}
        alive_o = sum(1 for g in og.values() if g["health"] > 0)
        alive_r = sum(1 for g in rg.values() if g["health"] > 0 and g.get("mind") != "Dead")
        fam.check("grunts.alive", f, persistent(rem, rf, f, lambda x: sum(1 for g in x.get("grunts", []) if g["health"] > 0 and g.get("mind") != "Dead"), alive_o, win),
                  f"grunts alive {alive_o} vs {alive_r}")
        for oid, rid in pairs.items():
            a, b = og.get(oid), rg.get(rid)
            if not a or not b:
                continue
            if a["health"] > 0 and b["health"] > 0:
                d = dist(a["pos"], b["pos"])
                fam.check("grunt.pos", f, d <= tol["grunt_pos"], f"grunt {oid} at {a['pos']} vs {b['pos']} ({d:.1f} apart)")
            fam.check("grunt.health", f, abs(a["health"] - b["health"]) <= tol["health"] or
                      persistent(rem, rf, f, lambda x, rid=rid: next((round(g["health"]) for g in x.get("grunts", []) if g["id"] == rid), None), round(a["health"]), win),
                      f"grunt {oid} health {a['health']} vs {b['health']}")
            m = grunt_mind(a)
            if m != "*":
                fam.check("grunt.mind", f, persistent(rem, rf, f, lambda x, rid=rid: next((grunt_mind(g) for g in x.get("grunts", []) if g["id"] == rid), None), m, win),
                          f"grunt {oid} mind {m} (state 0x{a.get('state', 0):x}) vs {b.get('mind')}")
            ta = a.get("target")
            tb = b.get("target")
            ta = pairs.get(ta, ta) if isinstance(ta, int) else ta
            fam.check("grunt.target", f, persistent(rem, rf, f, lambda x, rid=rid: next((g.get("target") for g in x.get("grunts", []) if g["id"] == rid), None), ta, win),
                      f"grunt {oid} target {a.get('target')} vs {tb}")
        om, rm = o.get("mission", {}), r.get("mission", {})
        fam.check("mission.state", f, MISSION.get(om.get("state"), str(om.get("state"))) == rm.get("state", "Playing"),
                  f"mission {om.get('state')} ({MISSION.get(om.get('state'))}) vs {rm.get('state')}")
        fam.check("mission.objective", f, persistent(rem, rf, f, lambda x: (x.get("mission") or {}).get("objective"), om.get("objective"), win),
                  f"objective tag {om.get('objective')} vs {rm.get('objective')}")
    # pauses (briefings, the token message, menus): each span of at least 4 frames starts within `frames` of one in the
    # other game (a 1 or 2 frame difference is input latency: the original dismisses a message a frame after the key)
    so, sr = pause_spans(orig), pause_spans(rem)
    for side, mine, other in (("original", so, sr), ("remaster", sr, so)):
        for a, b in mine:
            if b - a >= 4 and not any(abs(a - c) <= win for c, d in other if d - c >= 4):
                fam.check("paused", a, False, f"paused frames {a}..{b} only in the {side}")
                break
        fam.checked.add("paused")
    # events: each one must have a counterpart in the other game within event_frames
    ew = tol["event_frames"]
    last = frames[-1]
    for kind, norm in (("triggers", lambda v: v), ("deaths", lambda v: v), ("sounds", sound_key)):
        oe = [(f, norm(v)) for f in of if f <= last for v in orig[f].get(kind, [])]
        re_ = [(f, norm(v)) for f in rf if f <= last for v in rem[f].get(kind, [])]
        for side, mine, other in (("original", oe, re_), ("remaster", re_, oe)):
            used = [False] * len(other)
            for f, v in mine:
                hit = next((i for i, (g, w) in enumerate(other) if not used[i] and w == v and abs(g - f) <= ew), None)
                if hit is None:
                    fam.check(f"events.{kind}", f, False, f"{kind[:-1]} {v} at frame {f} only in the {side}")
                    break
                used[hit] = True
            fam.checked.add(f"events.{kind}")
        if kind == "sounds":
            notes.append(f"sounds: original {len(oe)}, remaster {len(re_)}")
    return fam, notes, pairs


def report(sc, fam, notes, as_json=False):
    expected = sc.get("expected", {})
    rows = []
    for family in sorted(fam.checked):
        if family in fam.first:
            f, detail = fam.first[family]
            status = "EXPECTED" if family in expected else "DIVERGE"
            rows.append((family, status, f, detail + (f"  [{expected[family]}]" if family in expected else "")))
        else:
            rows.append((family, "same", "", ""))
    lines = [f"oracle {sc['name']}: {sc.get('about', '')}"] + [f"  {n}" for n in notes]
    w = max(len(r[0]) for r in rows) if rows else 10
    lines.append(f"  {'family':<{w}}  status    frame  first divergence")
    for family, status, f, detail in rows:
        lines.append(f"  {family:<{w}}  {status:<8}  {str(f):>5}  {detail}")
    bad = [r for r in rows if r[1] == "DIVERGE"]
    lines.append(f"ORACLE {sc['name']}: {'PASS' if not bad else 'FAIL'} ({len(bad)} unexpected divergence(s), "
                 f"{sum(1 for r in rows if r[1] == 'EXPECTED')} expected, {sum(1 for r in rows if r[1] == 'same')} same)")
    text = "\n".join(lines)
    if as_json:
        return json.dumps({"scenario": sc["name"], "rows": rows, "notes": notes, "pass": not bad})
    return text, not bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario")
    ap.add_argument("--exe", default="target-wt-oracle", help="the remaster's cargo target dir under engine/")
    ap.add_argument("--no-run", action="store_true", help="compare the traces already in work/oracle/<scenario>/")
    ap.add_argument("--only", choices=["original", "remaster"], help="rerun one side, reuse the other's trace")
    ap.add_argument("--hooks", choices=["off", "on"], default="off",
                    help="the original's recovered functions: off = the shipped code (default), on = recovered code")
    ap.add_argument("--timeout", type=float, default=900)
    a = ap.parse_args()
    sc = load_scenario(a.scenario)
    d = out_dir(sc)
    if not a.no_run:
        if a.only != "remaster":
            if not run_original(sc, a.hooks == "on", a.timeout):
                sys.exit(2)
        if a.only != "original":
            if not run_remaster(sc, a.exe, a.timeout):
                sys.exit(2)
    o, r = os.path.join(d, "original.jsonl"), os.path.join(d, "remaster.jsonl")
    if not (os.path.exists(o) and os.path.exists(r)):
        sys.exit(f"missing traces in {d}")
    fam, notes, _ = compare_traces(sc, read(o), read(r))
    text, ok = report(sc, fam, notes)
    print(text)
    open(os.path.join(d, "report.txt"), "w").write(text + "\n")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
