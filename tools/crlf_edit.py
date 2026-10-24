"""Exact-match edits that keep a file's line endings (CRLF or LF).

Usage from a script:
    import sys; sys.path.insert(0, 'tools'); from crlf_edit import edit
    edit('engine/src/fx.rs', [(old, new), ...])

Each `old` must match exactly once (newlines in it match CRLF or LF); `new` is written with the file's own line
ending. Nothing is written unless every pair matches. Command line: python tools/crlf_edit.py FILE OLD_FILE NEW_FILE
"""
import re
import sys


def edit(path, pairs):
    s = open(path, 'rb').read().decode()
    nl = '\r\n' if s.count('\r\n') > s.count('\n') // 2 else '\n'
    for a, b in pairs:
        pat = r'\r?\n'.join(re.escape(x) for x in a.split('\n'))
        m = list(re.finditer(pat, s))
        if len(m) != 1:
            raise SystemExit(f'{path}: {len(m)} matches for:\n{a}')
        s = s[:m[0].start()] + b.replace('\n', nl) + s[m[0].end():]
    open(path, 'wb').write(s.encode())


if __name__ == '__main__':
    f, old, new = sys.argv[1:4]
    edit(f, [(open(old, encoding='utf-8').read(), open(new, encoding='utf-8').read())])
