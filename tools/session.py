"""The remaster's recorded play sessions (engine/src/devtools/session.rs): list them, summarise one, and turn a bookmark
into a playtest that loads its save, so a reported bug can be reproduced headless.

    python tools/session.py list [--root DIR]
    python tools/session.py show [SESSION|latest] [--root DIR]
    python tools/session.py repro SESSION|latest N [--out SCRIPT.json] [--root DIR]

SESSION is a folder name (or a unique prefix of one); the root defaults to the engine's sessions folder
(%LOCALAPPDATA%/AndRemaster/sessions on Windows, ~/Library/Application Support/AndRemaster/sessions on macOS).
`repro` writes a playtest script (default work/remaster/play/bookmark_<session>_<N>.json) that starts from the
bookmark's save and takes a screenshot; run it with `python tools/playtest.py SCRIPT --exe target-...` and add steps.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import platform_paths  # noqa: E402


def default_root():
    base = platform_paths.app_data_dir()
    return str(base / 'sessions') if base else os.path.join(ROOT, 'engine', 'target', 'release', 'sessions')


SESSION_NAME = re.compile(r'^\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d(_\d+)?$')


def sessions(root):
    """Session folder names, newest first."""
    if not os.path.isdir(root):
        return []
    return sorted((n for n in os.listdir(root) if SESSION_NAME.match(n) and os.path.isdir(os.path.join(root, n))), reverse=True)


def find(root, name):
    all_ = sessions(root)
    if not all_:
        sys.exit(f'no sessions under {root}')
    if name in (None, 'latest'):
        return all_[0]
    hits = [n for n in all_ if n.startswith(name)]
    if len(hits) != 1:
        sys.exit(f'{name}: {len(hits)} sessions match' + (': ' + ', '.join(hits[:5]) if hits else ''))
    return hits[0]


def jsonl(path):
    out = []
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        out.append(json.loads(line))
                    except ValueError:
                        pass
    except OSError:
        pass
    return out


def dir_size(p):
    return sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(p) for f in fs)


def summarise(sdir):
    """What happened in a session: levels, deaths, mission results, errors, panics, slow frames, bookmarks."""
    events = jsonl(os.path.join(sdir, 'events.jsonl'))
    frames = jsonl(os.path.join(sdir, 'frames.jsonl'))
    marks = jsonl(os.path.join(sdir, 'bookmarks.jsonl'))
    texts = [e.get('text', '') for e in events]
    levels = [m.group(1) for t in texts for m in [re.match(r'session: level (\S+) started', t)] if m]
    warnings, errors = [], []
    try:
        with open(os.path.join(sdir, 'game.log'), encoding='utf-8', errors='replace') as f:
            for line in f:
                if ' WARN ' in line:
                    warnings.append(line.strip())
                elif ' ERROR ' in line:
                    errors.append(line.strip())
    except OSError:
        pass
    panic = None
    if os.path.exists(os.path.join(sdir, 'panic.txt')):
        with open(os.path.join(sdir, 'panic.txt'), encoding='utf-8', errors='replace') as f:
            panic = f.readline().strip()
    play = [f for f in frames if f.get('state') == 'Playing']
    n = sum(f.get('frames', 0) for f in play)
    worst = sorted(play, key=lambda f: f.get('p99_ms', 0), reverse=True)[:3]
    return {
        'session': os.path.basename(sdir),
        'duration_s': round(max([e.get('t', 0) for e in events] + [f.get('t', 0) for f in frames] + [0]), 1),
        'levels': levels,
        'deaths': sum(1 for t in texts if t.startswith('session: Roman died')),
        'won': [t.split(' on ')[-1] for t in texts if t.startswith('session: mission Won')],
        'failed': [t[len('session: mission Failed on '):] for t in texts if t.startswith('session: mission Failed')],
        'dev': [t[len('dev: '):] for t in texts if t.startswith('dev: ') and t != 'dev: menu opened'],
        'warnings': len(warnings), 'errors': len(errors), 'first_errors': errors[:5], 'panic': panic,
        'frames_playing': n,
        'mean_ms_playing': round(sum(f['mean_ms'] * f['frames'] for f in play) / n, 2) if n else None,
        'over_33ms': sum(f.get('over_33ms', 0) for f in play), 'over_100ms': sum(f.get('over_100ms', 0) for f in play),
        'slow_frame_events': [t for t in texts if t.startswith('perf: slow frame')][:10],
        'worst_windows': [{k: w.get(k) for k in ('t', 'level', 'mean_ms', 'p99_ms', 'max_ms')} for w in worst],
        'bookmarks': [{'n': m.get('n'), 'level': m.get('level'), 'note': m.get('note', ''), 'pos': m.get('pos'),
                       'screenshot': os.path.join(sdir, m['screenshot']) if m.get('screenshot') else None,
                       'save': os.path.join(sdir, m['save']) if m.get('save') else None} for m in marks],
    }


def bookmark_to_script(entry, sdir, session):
    """A playtest script that starts from the bookmark's save (its level comes from the save) and screenshots it."""
    if not entry.get('save'):
        raise ValueError(f"bookmark {entry.get('n')} has no save (made outside a level)")
    save = os.path.abspath(os.path.join(sdir, entry['save'])).replace('\\', '/')
    note = entry.get('note') or ''
    steps = [{'wait': 2.0}, {'shot': f"bookmark_{entry.get('n')}"}]
    if entry.get('level'):
        steps.append({'expect': {'campaign.level': entry['level']}})
    steps.append({'quit': True})
    return {'comment': f"session {session} bookmark {entry.get('n')} on {entry.get('level')}: {note}".strip(),
            'load': save, 'steps': steps}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['list', 'show', 'repro'])
    ap.add_argument('session', nargs='?')
    ap.add_argument('n', nargs='?', type=int)
    ap.add_argument('--root', default=None)
    ap.add_argument('--out', default=None)
    a = ap.parse_args(argv)
    root = a.root or default_root()
    if a.cmd == 'list':
        for name in sessions(root):
            s = summarise(os.path.join(root, name))
            print(f"{name}  {s['duration_s']:7.0f} s  {dir_size(os.path.join(root, name)) / 1e6:7.1f} MB  levels {','.join(dict.fromkeys(s['levels'])) or '-'}"
                  f"  bookmarks {len(s['bookmarks'])}  deaths {s['deaths']}  errors {s['errors']}{'  PANIC' if s['panic'] else ''}")
        return 0
    name = find(root, a.session)
    sdir = os.path.join(root, name)
    if a.cmd == 'show':
        print(json.dumps(summarise(sdir), indent=1))
        return 0
    marks = {m.get('n'): m for m in jsonl(os.path.join(sdir, 'bookmarks.jsonl'))}
    if a.n not in marks:
        sys.exit(f'{name}: no bookmark {a.n} (have {sorted(marks)})')
    script = bookmark_to_script(marks[a.n], sdir, name)
    out = a.out or os.path.join(ROOT, 'work', 'remaster', 'play', f'bookmark_{name}_{a.n}.json')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(script, f, indent=1)
    print(f'wrote {out}\nrun: python tools/playtest.py {out} --exe target-...')
    return 0


if __name__ == '__main__':
    sys.exit(main())
