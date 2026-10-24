"""Answer "where in the remaster engine is X" from the source, in a few lines.

    python tools/engine_map.py writes Health        systems that take &mut Health / ResMut<Health> / write it
    python tools/engine_map.py reads Topsy          systems that take Topsy (Res, Query, Option<Res>)
    python tools/engine_map.py sends Explode        systems with MessageWriter<Explode> (and .write(Explode {)
    python tools/engine_map.py receives Explode     systems with MessageReader<Explode>
    python tools/engine_map.py addr 0x0047afa0      engine lines that cite an original address
    python tools/engine_map.py fn projectiles       one function: file:line, parameter count, its parameters
    python tools/engine_map.py params               systems near Bevy's 16-parameter limit (>= 14)

Systems are found by their parameter types (Query / Res / ResMut / Commands / Message* / Local / NonSend*), so
helpers count too. Output: file:line function [n params] and the matching parameter.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'engine', 'src')
FN = re.compile(r'^\s*(?:pub(?:\([^)]*\))?\s+)?fn\s+(\w+)\s*(?:<[^>]*>)?\s*\(', re.M)


def functions():
    # engine/src and its module folders (engine/src/fxvm/...): names relative to src
    names = sorted(os.path.relpath(os.path.join(d, f), SRC).replace('\\', '/') for d, _, fs in os.walk(SRC) for f in fs if f.endswith('.rs'))
    for name in names:
        text = open(os.path.join(SRC, name), encoding='utf-8').read()
        for m in FN.finditer(text):
            # the parameter list: up to the matching ')'
            i, depth = m.end(), 1
            while depth and i < len(text):
                depth += {'(': 1, ')': -1}.get(text[i], 0)
                i += 1
            params = text[m.end():i - 1]
            # the body: up to the next top-level fn (good enough for a lookup)
            nxt = FN.search(text, i)
            body = text[i:nxt.start() if nxt else len(text)]
            line = text.count('\n', 0, m.start()) + 1
            parts = split_params(params)
            yield name, line, m.group(1), parts, body


def split_params(p):
    out, depth, cur = [], 0, ''
    for ch in p:
        if ch in '<([':
            depth += 1
        elif ch in '>)]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return [' '.join(x.split()) for x in out]


def is_system(parts):
    return any(re.search(r'\b(Query|Res|ResMut|Commands|MessageWriter|MessageReader|NonSend|NonSendMut|Local)\b', p) for p in parts)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    mode, arg = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else '')
    t = re.escape(arg)
    for f, line, name, parts, body in functions():
        loc = f'{f}:{line} {name} [{len(parts)}]'
        if mode == 'addr':
            for k, l in enumerate(body.splitlines()):
                if arg.lower() in l.lower():
                    print(f'{f}:{line + k + 1} {name}: {l.strip()[:140]}')
            continue
        if mode == 'fn':
            if name == arg:
                print(loc)
                for p in parts:
                    print('   ', p[:140])
            continue
        if not is_system(parts):
            continue
        if mode == 'params':
            if len(parts) >= 14:
                print(loc)
            continue
        hits = []
        for p in parts:
            if mode == 'writes' and (re.search(rf'&mut\s+(?:[\w:]+::)?{t}\b', p) or re.search(rf'ResMut<(?:[\w:]+::)?{t}\b', p)):
                hits.append(p)
            elif mode == 'reads' and re.search(rf'\b{t}\b', p):
                hits.append(p)
            elif mode == 'sends' and re.search(rf'MessageWriter<(?:[\w:]+::)?{t}\b', p):
                hits.append(p)
            elif mode == 'receives' and re.search(rf'MessageReader<(?:[\w:]+::)?{t}\b', p):
                hits.append(p)
        if hits:
            print(loc, '|', ' ; '.join(h[:110] for h in hits))


if __name__ == '__main__':
    main()
