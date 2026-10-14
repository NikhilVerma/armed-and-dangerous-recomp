"""The remaster port's work queue per subsystem: functions the original executes (work/coverage.json, from hook
coverage runs) that no engine/src file cites by address yet, grouped by recovered source file.

    python tools/port_queue.py                    summary: per subsystem, reached / ported (cited) / left
    python tools/port_queue.py ai                 the queue for one subsystem: source file, address, name, size
    python tools/port_queue.py ai --all           also the functions not reached by the coverage scenarios

"Ported" means an engine comment cites the address (the porting rule: addresses cited in the source). Plumbing
the engine replaces wholesale (loader, the D3D renderer, physics glue, containers, libraries) is listed but is
not porting work. Widen the reach with a coverage run of a full level (tools/coverage.py).

A function the port decided not to cite carries its reason in docs/port/<subsystem>.csv (address, disposition
"replaced" or "not needed", reason): those leave the queue ("explained"); `--explained` lists them.
"""
import collections
import csv
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = ['ai', 'game', 'ui', 'audio', 'anim', 'camera', 'input', 'save', 'render-fx', 'movie']


def load():
    cov = json.load(open(os.path.join(ROOT, 'work', 'coverage.json')))
    hit = {int(v['address'], 16): v['calls'] for v in cov.values() if v['calls'] > 0}
    cited = set()
    for f in glob.glob(os.path.join(ROOT, 'engine', 'src', '*.rs')):
        for m in re.finditer(r'0x00([4-5][0-9a-fA-F]{5})\b', open(f, encoding='utf-8').read()):
            cited.add(int('0x00' + m.group(1), 16))
    # recovered source file per address (HOOK(0x..., "name") lines)
    where = {}
    for f in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        for m in re.finditer(r'HOOK\(0x([0-9a-fA-F]{8})', open(f, encoding='utf-8', errors='replace').read()):
            where[int(m.group(1), 16)] = os.path.relpath(f, ROOT).replace('\\', '/')
    rows = list(csv.DictReader(open(os.path.join(ROOT, 'symbols', 'functions.csv'), encoding='utf-8')))
    return hit, cited, where, rows


def explained():
    """address -> (disposition, reason) from docs/port/*.csv"""
    out = {}
    for f in glob.glob(os.path.join(ROOT, 'docs', 'port', '*.csv')):
        for r in csv.DictReader(open(f, encoding='utf-8')):
            out[int(r['address'], 16)] = (r['disposition'], r['reason'])
    return out


def main():
    hit, cited, where, rows = load()
    why = explained()
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        print(f"{'subsystem':14} {'reached':>7} {'ported':>6} {'explained':>9} {'left':>5}   (all functions)")
        for s in GAME:
            r = [x for x in rows if x['subsystem'] == s]
            reached = [x for x in r if int(x['address'], 16) in hit]
            ported = [x for x in reached if int(x['address'], 16) in cited]
            ex = [x for x in reached if int(x['address'], 16) not in cited and int(x['address'], 16) in why]
            print(f"{s:14} {len(reached):7} {len(ported):6} {len(ex):9} {len(reached) - len(ported) - len(ex):5}   ({len(r)})")
        return
    s = args[0]
    every = '--all' in sys.argv
    show = '--explained' in sys.argv
    by = collections.defaultdict(list)
    n_ex = 0
    for x in rows:
        a = int(x['address'], 16)
        if x['subsystem'] != s or a in cited or (a not in hit and not every):
            continue
        if a in why:
            n_ex += 1
            if not show:
                continue
        elif show:
            continue
        by[where.get(a, '?')].append((a, x['name'], x['size'], hit.get(a, 0)))
    n = 0
    for f in sorted(by):
        print(f)
        for a, name, size, calls in sorted(by[f]):
            n += 1
            extra = f"  {why[a][0]}: {why[a][1]}" if show else ''
            print(f"  0x{a:08x} {name:40} size {size:>6} calls {calls}{extra}")
    print(f'{n} functions {"explained" if show else "left"} in {s}' + ('' if show else f' ({n_ex} explained in docs/port/{s}.csv)'))


if __name__ == '__main__':
    main()
