"""Run one remaster playtest headless (silent, off-screen, no mouse grab) and print only what matters.

    python tools/playtest.py SCRIPT.json [--exe target-agent2] [--timeout 300] [--grep REGEX] [--size 640x372]

Prints EVENT / EXPECT / PLAYTEST lines (plus --grep matches, e.g. AIDBG), the wall time and the exit code; the full
log goes to SCRIPT.out. Exit code 1 when an expect failed or the game crashed or timed out.

Fast scripts start from a saved state: a script with {"save": "NAME"} writes work/remaster/play/states/NAME.json,
and a script with "load": "states/NAME.json" starts there (intro skipped). See docs/remaster-engine.md "Playtests".
"""
import argparse
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANSI = re.compile(r'\x1b\[[0-9;]*m')

ap = argparse.ArgumentParser()
ap.add_argument('script')
ap.add_argument('--exe', default='target-agent2', help='cargo target dir under engine/ (never "target": the player\'s copy)')
ap.add_argument('--timeout', type=float, default=300)
ap.add_argument('--grep', default=None)
ap.add_argument('--size', default='640x372')
ap.add_argument('--env', action='append', default=[], help='NAME=VALUE, e.g. AIDBG=1')
a = ap.parse_args()

engine = os.path.join(ROOT, 'engine')
exe = os.path.join(engine, a.exe, 'release', 'and_remaster.exe')
script = os.path.abspath(a.script)
out = os.path.splitext(script)[0] + '.out'
env = dict(os.environ)
for kv in a.env:
    k, v = kv.split('=', 1)
    env[k] = v
keep = re.compile(r'EVENT |EXPECT |PLAYTEST |panicked|ERROR' + (f'|{a.grep}' if a.grep else ''))
t0 = time.time()
code = 0
with open(out, 'w', encoding='utf-8', errors='replace') as f:
    try:
        p = subprocess.run([exe, '--background', '--size', a.size, '--play', script], cwd=engine, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=a.timeout)
        text = p.stdout.decode('utf-8', 'replace')
        code = p.returncode
    except subprocess.TimeoutExpired as e:
        text = (e.stdout or b'').decode('utf-8', 'replace')
        code = 'timeout'
    text = ANSI.sub('', text)
    f.write(text)
for line in text.splitlines():
    if keep.search(line):
        # drop the timestamp / level / module prefix
        print(re.sub(r'^\S+\s+\w+\s+[\w:]+: ', '', line).strip())
print(f'exit {code} in {time.time() - t0:.0f} s (full log {os.path.relpath(out, ROOT)})')
sys.exit(0 if code == 0 else 1)
