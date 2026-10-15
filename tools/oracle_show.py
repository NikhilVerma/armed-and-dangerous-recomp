"""Side by side view of one field over frames from an oracle scenario's two traces (tools/oracle_diff.py).

    python tools/oracle_show.py SCENARIO FIELD [--from F] [--to F] [--every N]

FIELD: a dotted path into a trace line (t, paused, roman.pos, roman.heading, mission.state, ...), "grunt:ID.FIELD"
for one grunt (e.g. grunt:67.pos, grunt:67.state / grunt:67.mind), or "events" for triggers / deaths / sounds.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_diff  # noqa: E402


def get(row, path):
    if path == "events":
        return {k: row.get(k) for k in ("triggers", "deaths", "sounds") if row.get(k)}
    if path.startswith("grunt:"):
        gid, _, field = path[6:].partition(".")
        g = next((g for g in row.get("grunts", []) if str(g["id"]) == gid), None)
        if g is None:
            return None
        return oracle_diff.grunt_mind(g) if field == "mind" else g.get(field)
    v = row
    for p in path.split("."):
        v = v.get(p) if isinstance(v, dict) else None
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("scenario")
    ap.add_argument("field")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", type=int, default=10 ** 9)
    ap.add_argument("--every", type=int, default=1)
    a = ap.parse_args()
    d = os.path.join(oracle_diff.OUT, oracle_diff.load_scenario(a.scenario)["name"])
    o = oracle_diff.read(os.path.join(d, "original.jsonl"))
    r = oracle_diff.read(os.path.join(d, "remaster.jsonl"))
    frames = [f for f in sorted(set(o) | set(r)) if a.start <= f <= a.to][:: a.every]
    for f in frames:
        vo = get(o[f], a.field) if f in o else "-"
        vr = get(r[f], a.field) if f in r else "-"
        if a.field == "events" and not vo and not vr:
            continue
        print(f"{f:5d}  original {json.dumps(vo)}  remaster {json.dumps(vr)}")


if __name__ == "__main__":
    main()
