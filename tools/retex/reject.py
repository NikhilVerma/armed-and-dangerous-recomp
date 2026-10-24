"""Marks batch textures rejected (plan.json) and removes their installed overrides.
Usage: .venv/Scripts/python.exe tools/retex/reject.py NAME=reason [NAME=reason ...]"""
import json, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
plan_path = os.path.join(ROOT, "work", "retex", "batch", "plan.json")
p = json.load(open(plan_path))
for arg in sys.argv[1:]:
    n, why = arg.split("=", 1)
    p[n]["rejected"], p[n]["installed"] = why, False
    for d in ("orig/pc/GameData/overrides/textures", "work/instances/main/overrides/textures"):
        f = os.path.join(ROOT, d, n + ".png")
        if os.path.exists(f):
            os.remove(f)
    print("rejected", n)
json.dump(p, open(plan_path, "w"), indent=1)
