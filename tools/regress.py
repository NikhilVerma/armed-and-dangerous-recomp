"""Run the fast remaster playtests (state-based, with expect steps) and print one line each.

    python tools/regress.py [--exe target-agent2] [NAME ...]

Scripts live in work/remaster/play (not in git; each starts from states/after_pub.json, which
state_after_pub.json writes). Exit code 1 when any script fails. Add a script here when it guards a ported mechanic.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAY = os.path.join(ROOT, 'work', 'remaster', 'play')
SUITE = ['topsyhit2', 'turn_in_place', 'turret1', 'decals_obj', 'oilpump', 'gfxopts']

ap = argparse.ArgumentParser()
ap.add_argument('names', nargs='*')
ap.add_argument('--exe', default='target-agent2')
a = ap.parse_args()

if not os.path.exists(os.path.join(PLAY, 'states', 'after_pub.json')):
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(PLAY, 'state_after_pub.json'), '--exe', a.exe])
bad = 0
for name in a.names or SUITE:
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(PLAY, name + '.json'), '--exe', a.exe],
                       stdout=subprocess.PIPE, text=True, errors='replace')
    res = re.search(r'PLAYTEST RESULT: (\d+) passed, (\d+) failed', p.stdout)
    fails = [l for l in p.stdout.splitlines() if 'EXPECT FAIL' in l or 'panicked' in l]
    ok = p.returncode == 0 and res is not None
    bad += not ok
    t = re.search(r'in (\d+) s', p.stdout)
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {res.group(1) + ' passed' if res else 'no result'}{', ' + res.group(2) + ' failed' if res and res.group(2) != '0' else ''} ({t.group(1) if t else '?'} s)")
    for l in fails[:5]:
        print('     ', l)
sys.exit(1 if bad else 0)
