"""Export everything the remaster engine (engine/) loads for one level, or for every level: runs each exporter in
order and stops a level at its first failure with that exporter's error.

  python tools/export_level.py lev_2_m1                  -> work/remaster/lev_2_m1/
  python tools/export_level.py --all                      every level folder of orig/pc/GameData (lev_*, not the shell)
  python tools/export_level.py --all --steps player,grunt_ai --report work/remaster/export_report.json

Run it with the project's .venv python (numpy, pefile, Pillow); the exporters run under the same interpreter. Steps,
in order (each reads only the banks and what the steps before it wrote):

  scene          tools/remaster_export.py            scene.json, models/, textures/, skeletons/, properties.json,
                                                     templates.json, fx.json
  anims          tools/remaster_export_anim.py       anims/, skeletons/
  audio          tools/remaster_export_audio.py      audio/ (wav/, sounds.json, voice.json, ambient.json)
  mission        tools/remaster_export_mission.py    mission.json
  ui             tools/remaster_export_ui.py         ui/ (layout.json, font, textures/, texts.json)
  icons          tools/remaster_export_icons.py      ui/icons.png, ui/icons.json (input prompt atlas, Kenney CC0 pack)
  player         tools/remaster_export_player.py     player.json
  instance_props tools/remaster_export_instance_props.py  instance_props.json
  grunt_ai       tools/remaster_export_grunt_ai.py   grunt_ai.json
  player_anims   tools/remaster_export_player_anims.py    player_anims.json
  textures       tools/remaster_textures.py          textures_remaster/ (the Remaster preset's retextures)

Each level prints one summary line; --all ends with a table (level, each step ok / FAILED, size). --report writes the
same as JSON. Game-derived output: work/ is never committed.
"""
import argparse
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN = ROOT if (ROOT / "orig").exists() else ROOT.parents[2]   # a worktree reads the main checkout's orig/ and work/
GAME = MAIN / "orig" / "pc" / "GameData"
WORK = MAIN / "work" / "remaster"
TOOLS = ROOT / "tools"


def steps_for(level, out):
    """(name, argv) per exporter, in dependency order."""
    lv = [level, "--out", str(out)]
    return [
        ("scene", ["remaster_export.py", *lv]),
        ("anims", ["remaster_export_anim.py", *lv]),
        ("audio", ["remaster_export_audio.py", level, "--out", str(out / "audio")]),
        ("mission", ["remaster_export_mission.py", *lv]),
        ("ui", ["remaster_export_ui.py", level, "--out", str(out / "ui")]),
        ("icons", ["remaster_export_icons.py", "--out", str(out / "ui")]),
        ("player", ["remaster_export_player.py", *lv]),
        ("instance_props", ["remaster_export_instance_props.py", *lv]),
        ("grunt_ai", ["remaster_export_grunt_ai.py", *lv]),
        ("player_anims", ["remaster_export_player_anims.py", *lv]),
        ("textures", ["remaster_textures.py", level, "--work", str(out.parent)]),
    ]


STEP_NAMES = [name for name, _ in steps_for("x", pathlib.Path("x"))]


def levels():
    return sorted(p.name for p in GAME.iterdir() if p.is_dir() and p.name.startswith("lev_") and p.name != "lev_shell"
                  and (p / f"{p.name}_big10_inst.big").exists())


def folder_size(path):
    files = [f for f in path.rglob("*") if f.is_file()] if path.exists() else []
    return sum(f.stat().st_size for f in files), len(files)


def last_lines(text, n=6):
    lines = [x for x in text.strip().splitlines() if x.strip()]
    return " | ".join(lines[-n:])


def export(level, out, only=None, quiet=False):
    """Runs the steps for one level. Returns {"level", "steps": {name: "ok" | "skipped" | error}, "seconds", ...}."""
    out.mkdir(parents=True, exist_ok=True)
    result = {"level": level, "steps": {}, "messages": {}}
    t0 = time.time()
    failed = False
    for name, argv in steps_for(level, out):
        if only and name not in only:
            continue
        if failed:
            result["steps"][name] = "skipped"
            continue
        t = time.time()
        p = subprocess.run([sys.executable, str(TOOLS / argv[0]), *argv[1:]], capture_output=True, text=True,
                           encoding="utf-8", errors="replace", cwd=str(ROOT))
        if p.returncode != 0:
            failed = True
            result["steps"][name] = f"FAILED (exit {p.returncode}): {last_lines(p.stderr or p.stdout)}"
        else:
            result["steps"][name] = "ok"
        result["messages"][name] = last_lines(p.stdout, 3)
        if not quiet:
            print(f"  {level} {name}: {result['steps'][name] if failed else 'ok'} ({time.time() - t:.0f} s) "
                  f"{result['messages'][name][:160]}", flush=True)
    result["seconds"] = round(time.time() - t0, 1)
    result["bytes"], result["files"] = folder_size(out)
    return result


def summary(r):
    bad = [f"{k} {v}" for k, v in r["steps"].items() if v not in ("ok", "skipped")]
    state = "ok" if not bad else bad[0]
    return (f"{r['level']}: {state}; {sum(v == 'ok' for v in r['steps'].values())}/{len(r['steps'])} steps, "
            f"{r['bytes'] / 1e6:.0f} MB in {r['files']} files, {r['seconds']:.0f} s")


def table(results):
    names = [n for n in STEP_NAMES if any(n in r["steps"] for r in results)]
    head = "| level | " + " | ".join(names) + " | MB | files |"
    rows = [head, "|" + " --- |" * (len(names) + 3)]
    for r in results:
        cells = []
        for n in names:
            v = r["steps"].get(n, "-")
            cells.append("ok" if v == "ok" else "skipped" if v == "skipped" else "-" if v == "-" else "FAILED")
        rows.append(f"| {r['level']} | " + " | ".join(cells) + f" | {r['bytes'] / 1e6:.0f} | {r['files']} |")
    errors = [f"- {r['level']} {n}: {v}" for r in results for n, v in r["steps"].items() if v not in ("ok", "skipped")]
    return "\n".join(rows + ([""] + errors if errors else []))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("level", nargs="?")
    ap.add_argument("--all", action="store_true", help="every level of orig/pc/GameData")
    ap.add_argument("--work", default=str(WORK), help="output root (a folder per level), default work/remaster")
    ap.add_argument("--steps", help=f"comma-separated subset of: {','.join(STEP_NAMES)}")
    ap.add_argument("--report", help="write the results as JSON here")
    ap.add_argument("--quiet", action="store_true", help="only the summary lines")
    a = ap.parse_args()
    if not a.level and not a.all:
        ap.error("give a level or --all")
    only = set(a.steps.split(",")) if a.steps else None
    if only and only - set(STEP_NAMES):
        ap.error(f"unknown steps {sorted(only - set(STEP_NAMES))}")
    todo = levels() if a.all else [a.level]
    results = []
    for level in todo:
        if not (GAME / level).is_dir():
            print(f"{level}: no such level folder in {GAME}")
            sys.exit(2)
        r = export(level, pathlib.Path(a.work) / level, only, a.quiet)
        results.append(r)
        print(summary(r), flush=True)
    if a.all:
        print()
        print(table(results))
    if a.report:
        pathlib.Path(a.report).write_text(json.dumps(results, indent=1))
    sys.exit(0 if all(v in ("ok", "skipped") for r in results for v in r["steps"].values()) else 1)


if __name__ == "__main__":
    main()
