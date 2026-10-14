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
SUITE = ['topsyhit2', 'turn_in_place', 'turret1', 'decals_obj', 'oilpump', 'gfxopts', 'cover1', 'cover2', 'cover3', 'gunner1', 'gunner2', 'objects1', 'unit']
# 'unit': the engine's Rust unit tests (cargo test --release into the same target dir: pure logic such as the
# object model's damage filters, trigger fields and husk picks)
# scripts kept in git (engine/playtests) win over work/remaster/play
TRACKED = os.path.join(ROOT, 'engine', 'playtests')

ap = argparse.ArgumentParser()
ap.add_argument('names', nargs='*')
ap.add_argument('--exe', default='target-agent2')
a = ap.parse_args()

if not os.path.exists(os.path.join(PLAY, 'states', 'after_pub.json')):
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(PLAY, 'state_after_pub.json'), '--exe', a.exe])
bad = 0
for name in a.names or SUITE:
    if name == 'unit':
        env = dict(os.environ, CARGO_TARGET_DIR=os.path.join(ROOT, 'engine', a.exe))
        cargo = os.path.join(os.path.expanduser('~'), '.cargo', 'bin', 'cargo')
        p = subprocess.run([cargo if os.path.exists(cargo + '.exe') or os.path.exists(cargo) else 'cargo', 'test', '--release'],
                           cwd=os.path.join(ROOT, 'engine'), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           errors='replace')
        res = re.search(r'test result: (\w+)\. (\d+) passed; (\d+) failed', p.stdout)
        ok = p.returncode == 0 and res is not None
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} unit: {res.group(2) + ' passed, ' + res.group(3) + ' failed' if res else 'no result'}")
        for l in [l for l in p.stdout.splitlines() if 'FAILED' in l or 'panicked' in l or l.startswith('error')][:5]:
            print('     ', l)
        continue
    script =next((d for d in (TRACKED, PLAY) if os.path.exists(os.path.join(d, name + '.json'))), PLAY)
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(script, name + '.json'), '--exe', a.exe],
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
