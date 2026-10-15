"""Smoke-test levels in the remaster engine: load each exported level headless (--background, muted), dismiss the
start message, let it play, and report one line per level.

    python tools/level_smoke.py [--exe target-agent2] [--seconds 20] [LEVEL ...]

Levels default to every folder under work/remaster/ that has a scene.json. Per level it prints: loaded (the mission
started), Roman (player position known), grunts alive, health at the end, median frame ms from the state log, and the
first panic / error line. Writes work/remaster/level_smoke.json. Exit 1 when any level fails to load or panics.

A level counts as "playing" when the mission started, Roman exists and the run lasted the requested time without a
panic. That is the floor, not 1:1: each level's mechanics are checked by its own playtests and comparisons.
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, 'work', 'remaster')
ANSI = re.compile(r'\x1b\[[0-9;]*m')

ap = argparse.ArgumentParser()
ap.add_argument('levels', nargs='*')
ap.add_argument('--exe', default='target-agent2')
ap.add_argument('--seconds', type=float, default=20.0)
ap.add_argument('--timeout', type=float, default=240.0)
a = ap.parse_args()

levels = a.levels or sorted(d for d in os.listdir(WORK) if d.startswith('lev_') and os.path.exists(os.path.join(WORK, d, 'scene.json')))
exe = os.path.join(ROOT, 'engine', a.exe, 'release', 'and_remaster.exe')
script_dir = os.path.join(WORK, 'smoke')
os.makedirs(script_dir, exist_ok=True)
results = {}
bad = 0
print(f"{'level':14} {'loaded':6} {'roman':5} {'grunts':6} {'health':6} {'ms':5}  first problem")
for lv in levels:
    script = os.path.join(script_dir, f'{lv}.json')
    # dismiss whatever message the level starts with, then play; shots at the end for a look
    steps = [{"until": "message"}, {"wait": 0.5}, {"tap": "Enter"}, {"wait": 3.5}, {"tap": "Enter"}, {"wait": a.seconds},
             {"shot": "end"}, {"quit": True}]
    json.dump({"steps": steps}, open(script, 'w'))
    try:
        p = subprocess.run([exe, '--background', '--size', '1280x720', '--scene', os.path.join(WORK, lv), '--play', script],
                           cwd=os.path.join(ROOT, 'engine'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=a.timeout)
        out = ANSI.sub('', p.stdout.decode('utf-8', 'replace'))
        code = p.returncode
    except subprocess.TimeoutExpired as e:
        out = ANSI.sub('', (e.stdout or b'').decode('utf-8', 'replace'))
        code = 'timeout'
    open(os.path.join(script_dir, f'{lv}.out'), 'w', encoding='utf-8').write(out)
    log = os.path.join(script_dir, f'{lv}.log.jsonl')
    states = [json.loads(l) for l in open(log, encoding='utf-8')] if os.path.exists(log) else []
    loaded = any('mission: start' in l for l in out.splitlines())
    last = states[-1] if states else {}
    pos = last.get('pos') or [0, 0, 0]
    roman = any(abs(v) > 0.01 for v in pos)
    grunts = last.get('grunts_alive')
    health = last.get('health')
    ms = [s['frame_ms'] for s in states if isinstance(s.get('frame_ms'), (int, float))]
    med = round(statistics.median(ms), 1) if ms else None
    problem = next((l.strip() for l in out.splitlines() if 'panicked' in l or ' ERROR ' in l), '')
    if not problem and code not in (0,):
        problem = f'exit {code}'
    ok = loaded and roman and not problem
    bad += not ok
    results[lv] = {'loaded': loaded, 'roman': roman, 'grunts_alive': grunts, 'health': health, 'frame_ms': med, 'exit': code, 'problem': problem}
    print(f"{lv:14} {str(loaded):6} {str(roman):5} {str(grunts):6} {str(health):6} {str(med):5}  {problem[:110]}")
json.dump(results, open(os.path.join(WORK, 'level_smoke.json'), 'w'), indent=1)
print(f"{len(levels) - bad} of {len(levels)} levels load and play")
sys.exit(1 if bad else 0)
