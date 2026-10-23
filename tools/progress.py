"""Progress badges and charts from the repository's own data, as plain SVG in docs/progress/ (README "Status").

    python tools/progress.py            write docs/progress/*.svg and print the numbers
    python tools/progress.py --check    exit 1 when the committed SVGs differ from what the data gives (CI)
    python tools/progress.py --json     the numbers only, as JSON

Sources (all committed; no game data, no work/ folder needed):
  decompilation  symbols/functions.csv: every function of game.exe with its stage. Game code is everything but the
                 library subsystems (Havok, qhull, D3DX, CRT, zlib: proprietary or third-party, never recovered,
                 docs/decision.md 2026-10-05). Recovered = stage S1 or later (written as C++, compile-checked; S2 =
                 also verified against the running game, archive/README.md).
  port           the game-logic functions (tools/port_queue.py's subsystems) and what the engine does with each:
                 ported (an engine/src file cites its address), explained (docs/port/*.csv: replaced by another
                 mechanism, or no effect in the shipped game), deferred (docs/port/*.csv: behaviour not built yet),
                 not started (none of these).
  levels         engine/playtests/complete_<level>.json: a playtest that plays the level to "mission": "Won" and on to
                 the next level; the levels are engine/src/extras.rs LEVELS + BONUS_LEVELS (lev_bonus_m4 is in the
                 text bank only: no data shipped).
  audit          docs/audit/scope.csv (functions read, differences found) and the outcome column of every
                 docs/journal/2026-10-22-fix2-g*.md table.

Light and dark versions of each chart (README uses <picture> to follow the viewer's GitHub theme); badges carry their
own background and read on both. Colours: the dataviz reference palette (categorical slots in fixed order, validated
for colour-vision deficiency on both surfaces); every segment is also named with its count in the legend.
"""
import argparse
import collections
import csv
import glob
import html
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import port_queue  # noqa: E402

OUT = os.path.join(ROOT, 'docs', 'progress')
LIBRARY = ('havok', 'qhull', 'd3dx', 'crt', 'zlib', 'thunk')  # thunk: the compiler's jump stubs, no code of their own

THEMES = {
    'light': {'surface': '#fcfcfb', 'text': '#0b0b0b', 'text2': '#52514e', 'grid': '#e4e3df', 'empty': '#e4e3df',
              'series': ['#2a78d6', '#eb6834', '#1baf7a', '#eda100', '#e87ba4', '#008300'],
              'ramp': ['#184f95', '#5598e7', '#b7d3f6']},
    'dark': {'surface': '#1a1a19', 'text': '#f0efec', 'text2': '#c3c2b7', 'grid': '#383835', 'empty': '#383835',
             'series': ['#3987e5', '#d95926', '#199e70', '#c98500', '#d55181', '#008300'],
             'ramp': ['#86b6ef', '#3987e5', '#1c5cab']},
}
FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', Helvetica, Arial, sans-serif"


# ------------------------------------------------------------------------------------------------------- data
def decomp():
    rows = list(csv.DictReader(open(os.path.join(ROOT, 'symbols', 'functions.csv'), encoding='utf-8')))
    by = collections.defaultdict(collections.Counter)
    lib = collections.Counter()
    for r in rows:
        if r['subsystem'] in LIBRARY:
            lib[r['subsystem']] += 1
            continue
        st = r['stage']
        by[r['subsystem']]['verified' if st in ('S2', 'S3', 'S4') else 'recovered' if st == 'S1' else 'not recovered'] += 1
    total = sum(sum(c.values()) for c in by.values())
    done = sum(c['verified'] + c['recovered'] for c in by.values())
    verified = sum(c['verified'] for c in by.values())
    return {'total': total, 'recovered': done, 'verified': verified, 'all_functions': len(rows),
            'library': dict(lib), 'by': {k: dict(v) for k, v in by.items()}}


def port():
    rows = list(csv.DictReader(open(os.path.join(ROOT, 'symbols', 'functions.csv'), encoding='utf-8')))
    cited = set(port_queue.cited_lines())
    why = port_queue.explained()
    by = collections.defaultdict(collections.Counter)
    names = {'ported': 'ported', 'explained': 'explained', 'deferred': 'deferred', 'left': 'not started'}
    for r in rows:
        if r['subsystem'] in port_queue.GAME:
            by[r['subsystem']][names[port_queue.state(int(r['address'], 16), cited, why)]] += 1
    tot = collections.Counter()
    for c in by.values():
        tot.update(c)
    return {'total': sum(tot.values()), **{k: tot[k] for k in names.values()},
            'by': {k: dict(by[k]) for k in port_queue.GAME if k in by}}


def levels():
    src = open(os.path.join(ROOT, 'engine', 'src', 'extras.rs'), encoding='utf-8').read()
    names = []
    for const in ('LEVELS', 'BONUS_LEVELS'):
        m = re.search(r'pub const ' + const + r': \[&str; \d+\] = \[(.*?)\];', src, re.S)
        names += re.findall(r'"(lev_\w+)"', m.group(1))
    names = [n for n in names if n != 'lev_bonus_m4']
    done = {}
    for n in names:
        p = os.path.join(ROOT, 'engine', 'playtests', f'complete_{n}.json')
        done[n] = os.path.exists(p) and '"mission": "Won"' in open(p, encoding='utf-8').read()
    return {'total': len(names), 'completable': sum(done.values()), 'levels': done}


AUDIT_CATS = [('original bug', 'original bug'), ('done in', 'fixed'), ('partly fixed', 'fixed'), ('fixed', 'fixed'),
              ('already correct', 'already correct'), ('no change', 'already correct'),
              ('unreachable', 'unreachable'), ('false', 'false claim'), ('deferred', 'deferred'),
              ('not needed', 'deferred')]
AUDIT_ORDER = ['fixed', 'already correct', 'unreachable', 'false claim', 'original bug', 'deferred']


def audit_outcome(text):
    t = text.lower().strip('`* ')
    for key, cat in AUDIT_CATS:
        if t.startswith(key):
            return cat
    for key, cat in AUDIT_CATS:
        if key in t:
            return cat
    return 'other'


def audit():
    scope = next(csv.DictReader(open(os.path.join(ROOT, 'docs', 'audit', 'scope.csv'), encoding='utf-8')))
    tot = collections.Counter()
    for path in sorted(glob.glob(os.path.join(ROOT, 'docs', 'journal', '2026-10-22-fix2-g*.md'))):
        col = None
        for line in open(path, encoding='utf-8'):
            s = line.strip()
            if not s.startswith('|'):
                col = None
                continue
            cells = [c.strip() for c in s.strip('|').split('|')]
            low = [c.lower() for c in cells]
            if 'outcome' in low:
                col = low.index('outcome')
            elif col is not None and not set(s) <= set('|-: ') and len(cells) > col:
                tot[audit_outcome(cells[col])] += 1
    triaged = sum(tot.values())
    return {'functions_read': int(scope['functions_read']), 'differences': int(scope['differences']),
            'triaged': triaged, **{k: tot[k] for k in AUDIT_ORDER}, 'other': tot['other']}


# ------------------------------------------------------------------------------------------------------- svg
def esc(s):
    return html.escape(str(s), quote=True)


def svg(width, height, body, title, theme=None):
    bg = f'<rect width="{width}" height="{height}" rx="6" fill="{theme["surface"]}"/>' if theme else ''
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" '
            f'role="img" aria-label="{esc(title)}" font-family="{FONT}">\n<title>{esc(title)}</title>\n{bg}\n'
            + '\n'.join(body) + '\n</svg>\n')


def badge(label, value, colour):
    """A two-part badge (shields.io look) with its own background, readable on light and dark pages."""
    w1, w2 = 7 * len(label) + 12, 7 * len(value) + 12
    w = w1 + w2
    body = [f'<clipPath id="r"><rect width="{w}" height="20" rx="3"/></clipPath>',
            f'<g clip-path="url(#r)"><rect width="{w1}" height="20" fill="#555"/>'
            f'<rect x="{w1}" width="{w2}" height="20" fill="{colour}"/></g>',
            f'<g fill="#fff" font-size="11" text-anchor="middle">'
            f'<text x="{w1 / 2}" y="14">{esc(label)}</text><text x="{w1 + w2 / 2}" y="14">{esc(value)}</text></g>']
    return svg(w, 20, body, f'{label}: {value}')


def stacked_rows(title, subtitle, rows, cats, colours, theme, label_w=110, bar_w=420):
    """Horizontal 100 % bars, one per row: rows [(name, Counter)], cats in stacking order."""
    t = theme
    subtitle = [subtitle] if isinstance(subtitle, str) else subtitle
    top, row_h, gap = 50 + 16 * len(subtitle), 22, 8
    legend_y = top + len(rows) * (row_h + gap) + 10
    height = legend_y + 26 + 18 * ((len(cats) + 2) // 3)
    width = label_w + bar_w + 90
    body = [f'<text x="16" y="26" font-size="15" font-weight="600" fill="{t["text"]}">{esc(title)}</text>',
            *[f'<text x="16" y="{46 + 16 * i}" font-size="12" fill="{t["text2"]}">{esc(s)}</text>'
              for i, s in enumerate(subtitle)]]
    for i, (name, c) in enumerate(rows):
        y = top + i * (row_h + gap)
        n = sum(c.get(k, 0) for k in cats)
        body.append(f'<text x="{label_w - 8}" y="{y + 15}" font-size="12" text-anchor="end" fill="{t["text"]}">'
                    f'{esc(name)}</text>')
        body.append(f'<rect x="{label_w}" y="{y}" width="{bar_w}" height="{row_h}" rx="4" fill="{t["empty"]}"/>')
        x = label_w
        segs = [(k, c.get(k, 0)) for k in cats if c.get(k, 0)]
        for j, (k, v) in enumerate(segs):
            w = bar_w * v / n
            first, last = j == 0, j == len(segs) - 1
            # 2 px surface gap between segments; rounded only at the bar's ends
            w_draw = max(w - (0 if last else 2), 0.5)
            body.append(f'<path d="{seg_path(x, y, w_draw, row_h, 4 if first else 0, 4 if last else 0)}" '
                        f'fill="{colours[k]}"><title>{esc(name)}: {esc(k)} {v} of {n}</title></path>')
            x += w
        body.append(f'<text x="{label_w + bar_w + 8}" y="{y + 15}" font-size="12" fill="{t["text2"]}">{n:,}</text>')
    tot = collections.Counter()
    for _, c in rows:
        tot.update(c)
    for j, k in enumerate(cats):
        lx, ly = 16 + (j % 3) * 205, legend_y + (j // 3) * 18
        body.append(f'<rect x="{lx}" y="{ly}" width="10" height="10" rx="2" fill="{colours[k]}"/>'
                    f'<text x="{lx + 16}" y="{ly + 9}" font-size="12" fill="{t["text"]}">{esc(k)} '
                    f'<tspan fill="{t["text2"]}">{tot[k]:,}</tspan></text>')
    return svg(width, height, body, f'{title}: {" ".join(subtitle)}', t)


def seg_path(x, y, w, h, rl, rr):
    rl, rr = min(rl, w / 2), min(rr, w / 2)
    return (f'M{x + rl:.1f},{y} H{x + w - rr:.1f} Q{x + w:.1f},{y} {x + w:.1f},{y + rr} V{y + h - rr} '
            f'Q{x + w:.1f},{y + h} {x + w - rr:.1f},{y + h} H{x + rl:.1f} Q{x:.1f},{y + h} {x:.1f},{y + h - rl} '
            f'V{y + rl} Q{x:.1f},{y} {x + rl:.1f},{y} Z')


def level_map(lv, theme):
    """The progress map: one tile per level, by chapter, filled when a playtest plays it to mission complete."""
    t = theme
    chapters = collections.OrderedDict()
    for name in lv['levels']:
        m = re.match(r'lev_(\w+?)_m', name)
        chapters.setdefault(m.group(1), []).append(name)
    tile_w, tile_h, gap, label_w = 92, 34, 6, 92
    cols = max(len(v) for v in chapters.values())
    width = label_w + cols * (tile_w + gap) + 10
    top = 64
    height = top + len(chapters) * (tile_h + gap) + 34
    done_c, open_c = t['series'][0], t['empty']
    body = [f'<text x="16" y="26" font-size="15" font-weight="600" fill="{t["text"]}">Levels completable</text>',
            f'<text x="16" y="46" font-size="12" fill="{t["text2"]}">{lv["completable"]} of {lv["total"]}: '
            f'a playtest plays each to mission complete (engine/playtests/complete_*.json)</text>']
    for i, (ch, names) in enumerate(chapters.items()):
        y = top + i * (tile_h + gap)
        body.append(f'<text x="{label_w - 10}" y="{y + 21}" font-size="12" text-anchor="end" fill="{t["text"]}">'
                    f'{"Bonus" if ch == "bonus" else "Chapter " + ch}</text>')
        for j, n in enumerate(names):
            x = label_w + j * (tile_w + gap)
            ok = lv['levels'][n]
            body.append(f'<rect x="{x}" y="{y}" width="{tile_w}" height="{tile_h}" rx="4" fill="{done_c if ok else open_c}">'
                        f'<title>{n}: {"completable" if ok else "no completion playtest"}</title></rect>')
            body.append(f'<text x="{x + tile_w / 2}" y="{y + 21}" font-size="11" text-anchor="middle" '
                        f'fill="{"#ffffff" if ok else t["text2"]}">{esc(n[4:])}{" ✓" if ok else ""}</text>')
    ly = height - 22
    body.append(f'<rect x="16" y="{ly}" width="10" height="10" rx="2" fill="{done_c}"/><text x="32" y="{ly + 9}" '
                f'font-size="12" fill="{t["text"]}">completable ✓ <tspan fill="{t["text2"]}">{lv["completable"]}</tspan></text>')
    body.append(f'<rect x="200" y="{ly}" width="10" height="10" rx="2" fill="{open_c}" stroke="{t["text2"]}" '
                f'stroke-width="0.5"/><text x="216" y="{ly + 9}" font-size="12" fill="{t["text"]}">not yet '
                f'<tspan fill="{t["text2"]}">{lv["total"] - lv["completable"]}</tspan></text>')
    return svg(width, height, body, f'Levels completable: {lv["completable"]} of {lv["total"]}', t)


def build():
    d, p, lv, a = decomp(), port(), levels(), audit()
    files = {
        'badge-decompiled.svg': badge('decompiled', f"{100 * d['recovered'] / d['total']:.0f}% of game code", '#2a78d6'),
        'badge-ported.svg': badge('engine port', f"{100 * (p['ported'] + p['explained']) / p['total']:.0f}% of game logic",
                                  '#2a78d6'),
        'badge-levels.svg': badge('levels completable', f"{lv['completable']}/{lv['total']}",
                                  '#0a7d0a' if lv['completable'] == lv['total'] else '#2a78d6'),
        'badge-audit.svg': badge('accuracy audit', f"{a['triaged']:,}/{a['differences']:,} triaged", '#2a78d6'),
    }
    for mode, t in THEMES.items():
        s = t['series']
        dc = {'verified': t['ramp'][0], 'recovered': t['ramp'][1], 'not recovered': t['empty']}
        rows = sorted(d['by'].items(), key=lambda kv: -sum(kv[1].values()))
        rows = [(k, collections.Counter(v)) for k, v in rows if sum(v.values()) >= 20]
        small = collections.Counter()
        for k, v in d['by'].items():
            if sum(v.values()) < 20:
                small.update(v)
        if small:
            rows.append(('other', small))
        lib = ', '.join(f'{k} {v:,}' for k, v in sorted(d['library'].items(), key=lambda kv: -kv[1]))
        files[f'decompilation-{mode}.svg'] = stacked_rows(
            'Decompilation: game code recovered as C++',
            [f"{d['recovered']:,} of {d['total']:,} game functions ({d['verified']:,} also verified in the running game)",
             f"not recovered, by design: library code and stubs ({lib})"],
            rows, ['verified', 'recovered', 'not recovered'], dc, t, bar_w=470)
        pc = {'ported': s[0], 'explained': s[2], 'deferred': s[3], 'not started': t['empty']}
        files[f'port-{mode}.svg'] = stacked_rows(
            'Engine port: game logic by subsystem',
            [f"{p['total']:,} game-logic functions: {p['ported']:,} ported, {p['explained']:,} replaced or not needed,",
             f"{p['deferred']:,} deferred, {p['not started']:,} not started"],
            [(k, collections.Counter(v)) for k, v in p['by'].items()],
            ['ported', 'explained', 'deferred', 'not started'], pc, t)
        files[f'levels-{mode}.svg'] = level_map(lv, t)
        ac = dict(zip(AUDIT_ORDER, s))
        files[f'audit-{mode}.svg'] = stacked_rows(
            'Accuracy audit: engine vs the recovered C++',
            [f"{a['functions_read']:,} ported functions read against it; {a['differences']:,} differences found,",
             f"{a['triaged']:,} of them fixed or triaged (docs/journal/2026-10-22-fix2-*.md)"],
            [('differences', collections.Counter({k: a[k] for k in AUDIT_ORDER}))], AUDIT_ORDER, ac, t,
            label_w=100, bar_w=480)
    return {'decompilation': d, 'port': {k: v for k, v in p.items()}, 'levels': lv, 'audit': a}, files


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true', help='exit 1 when docs/progress/ is out of date')
    ap.add_argument('--json', action='store_true', help='print the numbers as JSON and stop')
    a = ap.parse_args()
    data, files = build()
    if a.json:
        print(json.dumps(data, indent=1))
        return
    stale = []
    for name, text in files.items():
        path = os.path.join(OUT, name)
        old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
        if old != text:
            stale.append(name)
            if not a.check:
                os.makedirs(OUT, exist_ok=True)
                with open(path, 'w', encoding='utf-8', newline='\n') as f:
                    f.write(text)
    d, p, lv, au = data['decompilation'], data['port'], data['levels'], data['audit']
    print(f"decompiled: {d['recovered']}/{d['total']} game functions ({d['verified']} verified)")
    print(f"port: {p['ported']} ported, {p['explained']} explained, {p['deferred']} deferred, "
          f"{p['not started']} not started of {p['total']}")
    print(f"levels: {lv['completable']}/{lv['total']} completable")
    print(f"audit: {au['functions_read']} read, {au['differences']} differences, {au['triaged']} triaged: "
          + ', '.join(f"{k} {au[k]}" for k in AUDIT_ORDER) + (f", other {au['other']}" if au['other'] else ''))
    if a.check:
        print('out of date: ' + ', '.join(stale) + ' (run python tools/progress.py)' if stale else 'docs/progress up to date')
        sys.exit(1 if stale else 0)
    print(f"wrote {len(stale)} of {len(files)} files in docs/progress/")


if __name__ == '__main__':
    main()
