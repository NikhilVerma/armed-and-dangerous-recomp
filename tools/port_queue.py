"""The remaster port's work queue per subsystem: functions the original executes (work/coverage.json, from hook
coverage runs) that no engine/src file cites by address yet, plus the functions docs/port/*.csv marks `deferred`
(game behaviour not ported yet), grouped by recovered source file.

    python tools/port_queue.py                    summary: per subsystem, reached / ported / explained / deferred / left
    python tools/port_queue.py ai                 the open queue of one subsystem (left + deferred): file, address, name
    python tools/port_queue.py ai --all           also the functions not reached by the coverage scenarios
    python tools/port_queue.py ai --explained     the rows the port explained (replaced / not needed), with reasons
    python tools/port_queue.py --deferred         deferred work by feature, with the levels it affects, ranked
    python tools/port_queue.py --deferred --md docs/port/deferred-DATE.md     the same as a Markdown page
    python tools/port_queue.py --lint             fails (exit 1) when a disposition is dishonest (docs/port/README.md)

Dispositions (docs/port/README.md): `ported` (an engine file cites the address), `replaced` (a different mechanism
gives the behaviour: the reason names where), `not needed` (no effect in the remaster: dead in the shipped game,
debug-only, platform / D3D / Havok / CRT plumbing, editor-only), `deferred` (game behaviour not ported yet; the reason
starts with "feature: <slug>;", the slug a row of docs/port/features.csv). Only `replaced` and `not needed` leave the
queue; `deferred` is open work and counts as such everywhere, even when an engine comment cites the address.

work/ lives in the main checkout only: from a worktree the tool finds it through git's common dir (or $AND_WORK).
"""
import collections
import csv
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME = ['ai', 'game', 'ui', 'audio', 'anim', 'camera', 'input', 'save', 'render-fx', 'movie']
PORT = os.path.join(ROOT, 'docs', 'port')
DISPOSITIONS = ('ported', 'replaced', 'not needed', 'deferred')
EXPLAINED = ('replaced', 'not needed')
EFFECT_RANK = {'blocks objective': 0, 'visible gameplay': 1, 'unknown': 2, 'cosmetic': 3}
# a `not needed` reason must say why the behaviour has no effect, not that the port has not got to it yet
LINT_WORDS = re.compile(r'not ported|\blater\b|not in lev_|not on lev_|\blev_1_m1\b|not in this port|\byet\b|'
                        r'\bopen:|\bTODO\b|another owner|not drawn|\bdebt\b', re.I)


def work_dir():
    if os.environ.get('AND_WORK'):
        return os.environ['AND_WORK']
    w = os.path.join(ROOT, 'work')
    if os.path.isdir(w):
        return w
    try:
        common = subprocess.run(['git', 'rev-parse', '--git-common-dir'], cwd=ROOT, capture_output=True,
                                text=True).stdout.strip()
        main = os.path.dirname(os.path.abspath(os.path.join(ROOT, common)))
        if os.path.isdir(os.path.join(main, 'work')):
            return os.path.join(main, 'work')
    except OSError:
        pass
    return w


WORK = work_dir()


def coverage():
    return json.load(open(os.path.join(WORK, 'coverage.json')))


def cited_lines():
    """address -> [(engine file, line text)] for every engine/src citation 0x004xxxxx / 0x005xxxxx"""
    out = collections.defaultdict(list)
    for f in glob.glob(os.path.join(ROOT, 'engine', 'src', '**', '*.rs'), recursive=True):
        rel = os.path.relpath(f, os.path.join(ROOT, 'engine', 'src')).replace('\\', '/')
        for line in open(f, encoding='utf-8'):
            for m in re.finditer(r'0x00([4-5][0-9a-fA-F]{5})\b', line):
                out[int('0x00' + m.group(1), 16)].append((rel, line.strip()))
    return out


def load():
    cov = coverage()
    hit = {int(v['address'], 16): v['calls'] for v in cov.values() if v['calls'] > 0}
    cited = set(cited_lines())
    # recovered source file per address (HOOK(0x..., "name") lines)
    where = {}
    for f in glob.glob(os.path.join(ROOT, 'archive', 'src', '**', '*.cpp'), recursive=True):
        for m in re.finditer(r'HOOK\(0x([0-9a-fA-F]{8})', open(f, encoding='utf-8', errors='replace').read()):
            where[int(m.group(1), 16)] = os.path.relpath(f, ROOT).replace('\\', '/')
    rows = list(csv.DictReader(open(os.path.join(ROOT, 'symbols', 'functions.csv'), encoding='utf-8')))
    return hit, cited, where, rows


def port_rows():
    """[(csv file name, row dict)] of every docs/port/<subsystem>.csv (features.csv is not one)"""
    out = []
    for f in sorted(glob.glob(os.path.join(PORT, '*.csv'))):
        rd = csv.DictReader(open(f, encoding='utf-8'))
        if 'address' not in (rd.fieldnames or []):
            continue
        out += [(os.path.basename(f), r) for r in rd]
    return out


def explained():
    """address -> (disposition, reason) from docs/port/*.csv"""
    return {int(r['address'], 16): (r['disposition'], r['reason']) for _, r in port_rows()}


def feature_of(reason):
    m = re.match(r'\s*feature:\s*([\w-]+)\s*;', reason or '')
    return m.group(1) if m else None


def features():
    p = os.path.join(PORT, 'features.csv')
    return {r['feature']: r for r in csv.DictReader(open(p, encoding='utf-8'))} if os.path.exists(p) else {}


def state(a, cited, why):
    """ported / explained / deferred / left for one address"""
    d = why.get(a, (None,))[0]
    if d == 'deferred':
        return 'deferred'
    if a in cited:
        return 'ported'
    if d in EXPLAINED or d == 'ported':
        return 'explained' if d in EXPLAINED else 'ported'
    return 'left'


def levels_by_address():
    out = {}
    for v in coverage().values():
        if v['calls'] > 0:
            out[int(v['address'], 16)] = {t.split('.')[0] for t in v['scenarios'] if t.startswith('lev_')}
    return out


def placed_levels():
    """template / property name -> the levels whose scene.json places it"""
    out = collections.defaultdict(set)
    for f in glob.glob(os.path.join(WORK, 'remaster', 'lev_*', 'scene.json')):
        lv = os.path.basename(os.path.dirname(f))
        d = json.load(open(f))
        for sec in ('logic', 'instances'):
            for i in d.get(sec, []):
                for k in (i.get('template'), i.get('property')):
                    if k:
                        out[k].add(lv)
    return out


def short(levels):
    return ', '.join(l[4:] for l in sorted(levels)) if levels else '-'


def deferred_summary(md=None):
    why = explained()
    names = {int(r['address'], 16): r['name'] for r in csv.DictReader(
        open(os.path.join(ROOT, 'symbols', 'functions.csv'), encoding='utf-8'))}
    lv_of = levels_by_address()
    placed = placed_levels()
    feats = features()
    by = collections.defaultdict(list)
    for a, (d, reason) in why.items():
        if d == 'deferred':
            by[feature_of(reason) or '?'].append(a)
    table = []
    for f, addrs in by.items():
        meta = feats.get(f, {})
        # a function every level reaches is a load / per-frame check (e.g. the turbo pack set off at load), not
        # evidence that a level uses the feature: such functions do not widen the levels affected
        nlev = len({l for s in lv_of.values() for l in s}) or 24
        reached = set().union(*[lv_of.get(a, set()) for a in addrs if len(lv_of.get(a, ())) < nlev])
        tpl = set()
        for t in (meta.get('templates') or '').split():
            tpl |= placed.get(t, set())
        objective = set((meta.get('objective_levels') or '').split())
        affects = reached | tpl | objective
        table.append({'feature': f, 'effect': meta.get('effect', '?'), 'owner': meta.get('owner') or '-',
                      'what': meta.get('what', ''), 'n': len(addrs), 'reached': len([a for a in addrs if a in lv_of]),
                      'objective': objective, 'affects': affects, 'addrs': sorted(addrs)})
    table.sort(key=lambda t: (EFFECT_RANK.get(t['effect'], 9), -len(t['objective']), -len(t['affects']), t['feature']))
    if not md:
        print(f"{'#':>2} {'feature':26} {'effect':17} {'fns':>4} {'lv':>3} {'obj':>3}  owner / objective levels")
        for i, t in enumerate(table, 1):
            print(f"{i:2} {t['feature']:26} {t['effect']:17} {t['n']:4} {len(t['affects']):3} {len(t['objective']):3}  "
                  f"{t['owner']}{' / objective ' + short(t['objective']) if t['objective'] else ''}")
        print(f"{sum(t['n'] for t in table)} deferred functions in {len(table)} features")
        return table
    lines = ['# Deferred port work', '',
             'Generated by `python tools/port_queue.py --deferred --md <this file>` from docs/port/*.csv (rows with '
             'disposition `deferred`), docs/port/features.csv, work/coverage.json (the levels whose coverage runs '
             'execute a function, not counting functions every level reaches: those are load / per-frame checks) and '
             'work/remaster/<level>/scene.json (the levels that place a feature\'s templates). Ranked by effect, then '
             'by the number of levels whose objective involves the feature, then by the number of levels it affects. '
             'Effect: **blocks objective** (a level cannot be finished as in the original), **visible gameplay** '
             '(missing behaviour the player sees), **unknown** (not traced), **cosmetic** (looks / sound only). '
             'Objective levels: the levels whose mission objective involves the feature\'s objects (mission.json '
             'start texts, trigger chains). Dispositions: docs/port/README.md.', '',
             '| # | feature | effect | functions (reached) | levels affected | objective levels | owner | what |',
             '| ---: | --- | --- | ---: | --- | --- | --- | --- |']
    for i, t in enumerate(table, 1):
        aff = 'all 24' if len(t['affects']) >= 24 else f"{len(t['affects'])}: {short(t['affects'])}"
        lines.append(f"| {i} | {t['feature']} | {t['effect']} | {t['n']} ({t['reached']}) | {aff} | "
                     f"{short(t['objective'])} | {t['owner']} | {t['what']} |")
    lines += ['', '## Functions per feature', '']
    for t in table:
        lines += [f"### {t['feature']}", '', '| address | name | levels reached |', '| --- | --- | --- |']
        for a in t['addrs']:
            lines.append(f"| 0x{a:08x} | {names.get(a, '?')} | {short(lv_of.get(a, set()))} |")
        lines.append('')
    open(md, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print(f"{sum(t['n'] for t in table)} deferred functions in {len(table)} features -> {md}")
    return table


def lint():
    """Fails on dishonest dispositions; warns on suspicious ones. Returns the exit code."""
    errors, warnings = [], []
    feats = features()
    seen = {}
    for f, r in port_rows():
        a, d, reason = r['address'], r['disposition'], r['reason'] or ''
        tag = f"{f} {a} {r['name']}"
        if a.lower() in seen:
            errors.append(f"{tag}: duplicate (also in {seen[a.lower()]})")
        seen[a.lower()] = f
        if d not in DISPOSITIONS:
            errors.append(f"{tag}: unknown disposition {d!r} (one of {', '.join(DISPOSITIONS)})")
        elif d == 'not needed':
            m = LINT_WORDS.search(reason)
            if m:
                errors.append(f"{tag}: 'not needed' but the reason says {m.group(0)!r}: deferred work? ({reason[:90]})")
        elif d == 'deferred':
            ft = feature_of(reason)
            if not ft:
                errors.append(f"{tag}: deferred without 'feature: <slug>;' at the start of the reason")
            elif ft not in feats:
                errors.append(f"{tag}: feature {ft!r} is not in docs/port/features.csv")
        elif d == 'replaced' and re.search(r'not ported|\blev_1_m1\b', reason, re.I):
            warnings.append(f"{tag}: 'replaced' but the reason says lev_1_m1 / not ported: check the other levels")
    why = explained()
    for a, cites in sorted(cited_lines().items()):
        if why.get(a, (None,))[0] == 'deferred':
            continue
        for file, line in cites:
            if re.search(r'not ported', line, re.I):
                warnings.append(f"engine/src/{file} cites 0x{a:08x} in a 'not ported' comment: counted as ported "
                                f"unless docs/port marks it deferred")
                break
    for w in warnings:
        print('warning:', w)
    for e in errors:
        print('error:', e)
    print(f"port lint: {len(errors)} errors, {len(warnings)} warnings")
    return 1 if errors else 0


def newly_reached(before_path, out_md):
    """The functions work/coverage.json reaches that the coverage in before_path did not, by subsystem and source file,
    with the levels whose runs reach them (the runs' tags, "<level>.<scenario>"), as a Markdown page; port state per
    function: ported (an engine file cites it), explained (replaced / not needed), deferred, or left."""
    hit, cited, where, rows = load()
    why = explained()
    before = json.load(open(before_path))
    old = {int(v['address'], 16) for v in before.values() if v['calls'] > 0}
    levels = {a: sorted(v) for a, v in levels_by_address().items()}
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for x in rows:
        a = int(x['address'], 16)
        if a in hit and a not in old:
            by[x['subsystem']][where.get(a, '?')].append((a, x['name'], x['size'], state(a, cited, why),
                                                          levels.get(a, [])))
    lines = ['# Port queue after the all-levels coverage', '',
             f'Functions the original executes on the other levels (tools/coverage_level.py --level all) that the '
             f'coverage before ({os.path.basename(before_path)}) did not reach, by subsystem and recovered source file. '
             f'State: **left** = no engine file cites it and no row in docs/port/*.csv; **deferred** = game behaviour '
             f'not ported yet (docs/port/*.csv); both are open work. ported / explained = handled. Levels: the levels '
             f'whose coverage runs execute it. Regenerate: '
             f'`python tools/port_queue.py --new-since {os.path.relpath(before_path, ROOT).replace(os.sep, "/")} '
             f'--md {os.path.relpath(out_md, ROOT).replace(os.sep, "/")}`.', '']
    total = collections.Counter()
    summary = []
    order = sorted(by, key=lambda s: (s not in GAME, GAME.index(s) if s in GAME else 0, s))
    for s in order:
        c = collections.Counter(e[3] for f in by[s].values() for e in f)
        total.update(c)
        summary.append(f"| {s} | {sum(c.values())} | {c['left']} | {c['deferred']} | {c['ported']} | {c['explained']} |")
    lines += ['| subsystem | newly reached | left | deferred | ported | explained |',
              '| --- | ---: | ---: | ---: | ---: | ---: |'] + summary
    lines += [f"| all | {sum(total.values())} | {total['left']} | {total['deferred']} | {total['ported']} | "
              f"{total['explained']} |", '']
    lines += ['## Open: left and deferred', '', '| subsystem | file | address | name | size | state | levels |',
              '| --- | --- | --- | --- | ---: | --- | --- |']
    for s in order:
        for f in sorted(by[s]):
            for a, name, size, st, lv in sorted(by[s][f]):
                if st in ('left', 'deferred'):
                    lines.append(f"| {s} | {f} | 0x{a:08x} | {name} | {size} | {st} | {', '.join(l[4:] for l in lv)} |")
    lines += ['', '## Every newly reached function', '']
    for s in order:
        lines += [f'## {s}', '']
        for f in sorted(by[s]):
            lines += [f'### {f}', '', '| address | name | size | state | levels |', '| --- | --- | ---: | --- | --- |']
            for a, name, size, st, lv in sorted(by[s][f]):
                sh = ', '.join(l[4:] for l in lv) if len(lv) < 24 else 'all'
                lines.append(f'| 0x{a:08x} | {name} | {size} | {st} | {sh} |')
            lines.append('')
    open(out_md, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print(f"{sum(total.values())} newly reached ({total['left']} left, {total['deferred']} deferred, {total['ported']} "
          f"ported, {total['explained']} explained) -> {out_md}")


def main():
    if '--lint' in sys.argv:
        sys.exit(lint())
    if '--deferred' in sys.argv:
        md = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else None
        deferred_summary(md)
        return
    if '--new-since' in sys.argv:
        i = sys.argv.index('--new-since')
        md = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else os.path.join(PORT, 'queue-new.md')
        newly_reached(sys.argv[i + 1], md)
        return
    hit, cited, where, rows = load()
    why = explained()
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        print(f"{'subsystem':14} {'reached':>7} {'ported':>6} {'explained':>9} {'deferred':>8} {'left':>5} {'open':>5}"
              f"   (all functions; deferred counts reached or not)")
        tot = collections.Counter()
        for s in GAME:
            r = [int(x['address'], 16) for x in rows if x['subsystem'] == s]
            c = collections.Counter(state(a, cited, why) for a in r if a in hit)
            dfr = sum(1 for a in r if why.get(a, (None,))[0] == 'deferred')
            reached = sum(c.values())
            tot.update({'reached': reached, 'ported': c['ported'], 'explained': c['explained'], 'deferred': dfr,
                        'left': c['left'], 'all': len(r)})
            print(f"{s:14} {reached:7} {c['ported']:6} {c['explained']:9} {dfr:8} {c['left']:5} {dfr + c['left']:5}"
                  f"   ({len(r)})")
        print(f"{'all':14} {tot['reached']:7} {tot['ported']:6} {tot['explained']:9} {tot['deferred']:8} {tot['left']:5} "
              f"{tot['deferred'] + tot['left']:5}   ({tot['all']})")
        if tot['deferred'] + tot['left']:
            print("open work remains: `--deferred` ranks the deferred features; `<subsystem>` lists the queue")
        return
    s = args[0]
    every = '--all' in sys.argv
    show = '--explained' in sys.argv
    by = collections.defaultdict(list)
    n_ex = 0
    for x in rows:
        a = int(x['address'], 16)
        if x['subsystem'] != s:
            continue
        st = state(a, cited, why)
        if st == 'ported' or (a not in hit and not every and st != 'deferred'):
            continue
        if st == 'explained':
            n_ex += 1
            if not show:
                continue
        elif show:
            continue
        by[where.get(a, '?')].append((a, x['name'], x['size'], hit.get(a, 0), st))
    n = collections.Counter()
    for f in sorted(by):
        print(f)
        for a, name, size, calls, st in sorted(by[f]):
            n[st] += 1
            extra = f"  {why[a][0]}: {why[a][1]}" if show else (
                f"  deferred ({feature_of(why[a][1])})" if st == 'deferred' else '')
            print(f"  0x{a:08x} {name:40} size {size:>6} calls {calls}{extra}")
    if show:
        print(f"{n['explained']} functions explained in {s}")
    else:
        print(f"{n['left'] + n['deferred']} functions open in {s}: {n['left']} left, {n['deferred']} deferred "
              f"({n_ex} explained in docs/port/{s}.csv)")


if __name__ == '__main__':
    main()
