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
    for f in glob.glob(os.path.join(ROOT, 'engine', 'src', '**', '*.rs'), recursive=True):
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


def newly_reached(before_path, out_md):
    """The functions work/coverage.json reaches that the coverage in before_path did not, by subsystem and source file,
    with the levels whose runs reach them (the runs' tags, "<level>.<scenario>"), as a Markdown page; port state per
    function: ported (an engine file cites it), explained (docs/port/*.csv) or left."""
    hit, cited, where, rows = load()
    why = explained()
    before = json.load(open(before_path))
    old = {int(v['address'], 16) for v in before.values() if v['calls'] > 0}
    cov = json.load(open(os.path.join(ROOT, 'work', 'coverage.json')))
    levels = {}
    for v in cov.values():
        levels[int(v['address'], 16)] = sorted({t.split('.')[0] for t in v['scenarios'] if t.startswith('lev_')})
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for x in rows:
        a = int(x['address'], 16)
        if a in hit and a not in old:
            state = 'ported' if a in cited else ('explained' if a in why else 'left')
            by[x['subsystem']][where.get(a, '?')].append((a, x['name'], x['size'], state, levels.get(a, [])))
    lines = ['# Port queue after the all-levels coverage', '',
             f'Functions the original executes on the other levels (tools/coverage_level.py --level all) that the '
             f'coverage before ({os.path.basename(before_path)}) did not reach, by subsystem and recovered source file. '
             f'State: **left** = the remaining port work (no engine file cites it, no reason in docs/port/*.csv); '
             f'ported / explained = already handled. Levels: the levels whose coverage runs execute it. Regenerate: '
             f'`python tools/port_queue.py --new-since {os.path.relpath(before_path, ROOT).replace(os.sep, "/")} '
             f'--md {os.path.relpath(out_md, ROOT).replace(os.sep, "/")}`.', '']
    total = collections.Counter()
    summary = []
    for s in sorted(by, key=lambda s: (s not in GAME, GAME.index(s) if s in GAME else 0, s)):
        c = collections.Counter(e[3] for f in by[s].values() for e in f)
        total.update(c)
        summary.append(f"| {s} | {sum(c.values())} | {c['left']} | {c['ported']} | {c['explained']} |")
    lines += ['| subsystem | newly reached | left | ported | explained |', '| --- | ---: | ---: | ---: | ---: |'] + summary
    lines += [f"| all | {sum(total.values())} | {total['left']} | {total['ported']} | {total['explained']} |", '']
    lines += ['## Left: the remaining port work', '', '| subsystem | file | address | name | size | levels |',
              '| --- | --- | --- | --- | ---: | --- |']
    for s in sorted(by, key=lambda s: (s not in GAME, GAME.index(s) if s in GAME else 0, s)):
        for f in sorted(by[s]):
            for a, name, size, state, lv in sorted(by[s][f]):
                if state == 'left':
                    lines.append(f"| {s} | {f} | 0x{a:08x} | {name} | {size} | {', '.join(l[4:] for l in lv)} |")
    lines += ['', '## Every newly reached function', '']
    for s in sorted(by, key=lambda s: (s not in GAME, GAME.index(s) if s in GAME else 0, s)):
        lines += [f'## {s}', '']
        for f in sorted(by[s]):
            lines += [f'### {f}', '', '| address | name | size | state | levels |', '| --- | --- | ---: | --- | --- |']
            for a, name, size, state, lv in sorted(by[s][f]):
                short = ', '.join(l[4:] for l in lv) if len(lv) < 24 else 'all'
                lines.append(f'| 0x{a:08x} | {name} | {size} | {state} | {short} |')
            lines.append('')
    open(out_md, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print(f"{sum(total.values())} newly reached ({total['left']} left, {total['ported']} ported, "
          f"{total['explained']} explained) -> {out_md}")


def main():
    if '--new-since' in sys.argv:
        i = sys.argv.index('--new-since')
        md = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else os.path.join(ROOT, 'docs', 'port', 'queue-new.md')
        newly_reached(sys.argv[i + 1], md)
        return
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
