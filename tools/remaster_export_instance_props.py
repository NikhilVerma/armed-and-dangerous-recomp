"""Export one level's per-instance property overrides for the remaster engine (engine/src/alarm.rs and others).

  python tools/remaster_export_instance_props.py lev_1_m1    -> work/remaster/lev_1_m1/instance_props.json

Output (game data: lives under work/, never committed): a list, one entry per instance of the instance bank
(big10_inst) in bank order:
  {"id": 1-based position (the game's numbering), "template": object name (lower case),
   "pos": [x, y, z] (the transform's translation, game space, Z up), "props": {key: value} the instance's own values,
   keys as stored in the bank}
An instance value overrides the template's property set (Object_LoadProperties 0x004517b0 reads the instance first);
the merged view is the engine's job. Format: docs/formats/big-inst.md.
"""
import argparse
import json
import pathlib

from remaster_level import WORK, Level


def instance_props(level):
    return [{"id": i["id"], "template": i["template"], "pos": i["pos"], "props": i["props"]}
            for i in level.instances]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out", help="level folder (default work/remaster/<level>)")
    a = ap.parse_args()
    out = pathlib.Path(a.out) if a.out else WORK / a.level
    rows = instance_props(Level(a.level, out))
    out.mkdir(parents=True, exist_ok=True)
    (out / "instance_props.json").write_text(json.dumps(rows, indent=0, default=str))
    print(f"instance_props.json: {len(rows)} instances, {sum(1 for r in rows if r['props'])} with overrides")


if __name__ == "__main__":
    main()
