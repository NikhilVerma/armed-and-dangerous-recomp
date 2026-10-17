"""pre-commit helper: a modified file keeps the line endings it has in HEAD.

Agents' editors and merges turned CRLF files into LF, which shows as the whole file rewritten and makes every later
merge of it conflict. Prints one line per file whose endings flipped; exit 1 if any.
"""
import subprocess
import sys


def style(b):
    crlf = b.count(b"\r\n")
    lf = b.count(b"\n") - crlf
    return "crlf" if crlf > lf else "lf" if lf > crlf else None


names = subprocess.run(["git", "diff", "--cached", "--name-only", "--diff-filter=M"], capture_output=True, text=True).stdout.split()
bad = 0
for n in names:
    old = subprocess.run(["git", "show", "HEAD:" + n], capture_output=True).stdout
    new = subprocess.run(["git", "show", ":" + n], capture_output=True).stdout
    if b"\0" in old[:8000]:
        continue
    a, b = style(old), style(new)
    if a and b and a != b:
        print(f"pre-commit: {n}: {a} in HEAD, {b} staged (keep the file's own endings; tools/crlf_edit.py)")
        bad = 1
sys.exit(bad)
