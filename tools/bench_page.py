"""Benchmark page for the remaster engine: every run in work/remaster/bench/*.json (written by
`and_remaster --bench SECONDS --out FILE`) as one HTML page with a summary table and frame-time charts.

  python tools/bench_page.py                -> work/remaster/bench/index.html
"""
import html
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
BENCH = ROOT / "work" / "remaster" / "bench"

PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Remaster Engine Benchmark</title>
<style>
:root{--bg:#f6f7f9;--fg:#16191d;--mut:#5d6670;--card:#fff;--line:#d9dde3;--acc:#2f6fd6;--ok:#1f8a4c;--bad:#c2410c}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){--bg:#111418;--fg:#e8eaed;--mut:#9aa3ad;--card:#1a1e24;--line:#2c323a;--acc:#6aa1ff;--ok:#4ade80;--bad:#fb923c}}
:root[data-theme="dark"]{--bg:#111418;--fg:#e8eaed;--mut:#9aa3ad;--card:#1a1e24;--line:#2c323a;--acc:#6aa1ff;--ok:#4ade80;--bad:#fb923c}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,sans-serif}
main{max-width:1100px;margin:0 auto;padding:24px 16px}
h1{font-size:22px;margin:0 0 4px}p.sub{color:var(--mut);margin:0 0 20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:16px;margin:0 0 16px;overflow-x:auto}
table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}
th,td{text-align:right;padding:6px 10px;border-bottom:1px solid var(--line);white-space:nowrap}
th:first-child,td:first-child{text-align:left}th{color:var(--mut);font-weight:600}
.ok{color:var(--ok);font-weight:600}.bad{color:var(--bad);font-weight:600}
svg{width:100%;height:auto;display:block}.lbl{fill:var(--mut);font-size:11px}
</style></head><body><main>
<h1>Remaster engine benchmark</h1>
<p class="sub">Scripted camera flight over the level, frame times after a 3 s warm-up. Target: above 100 fps at 3840x2160.</p>
__BODY__
</main></body></html>"""


def chart(ms, target=10.0):
    w, h = 1000, 180
    top = max(max(ms), target * 1.5)
    n = len(ms)
    pts = " ".join(f"{i * w / max(1, n - 1):.1f},{h - v / top * h:.1f}" for i, v in enumerate(ms))
    ty = h - target / top * h
    return (f'<svg viewBox="0 0 {w} {h + 16}" role="img" aria-label="frame times">'
            f'<line x1="0" x2="{w}" y1="{ty:.1f}" y2="{ty:.1f}" stroke="var(--bad)" stroke-dasharray="4 4"/>'
            f'<text class="lbl" x="4" y="{ty - 4:.1f}">10 ms = 100 fps</text>'
            f'<polyline fill="none" stroke="var(--acc)" stroke-width="1.2" points="{pts}"/>'
            f'<text class="lbl" x="4" y="{h + 13}">frame time (ms), max {top:.1f} ms at the top</text></svg>')


def main():
    runs = []
    for f in sorted(BENCH.glob("*.json")):
        try:
            runs.append((f.stem, json.loads(f.read_text())))
        except (ValueError, OSError):
            continue
    rows = []
    charts = []
    for name, r in runs:
        ok = r.get("fps_1pct_low", 0) >= 100
        rows.append(
            f"<tr><td>{html.escape(name)}</td><td>{html.escape(str(r.get('level', '')))}</td>"
            f"<td>{html.escape(str(r.get('resolution', '')))}</td><td>{r.get('instances', '')}</td>"
            f"<td>{r.get('triangles', 0):,}</td><td>{r.get('fps_avg', 0):.0f}</td>"
            f"<td class=\"{'ok' if ok else 'bad'}\">{r.get('fps_1pct_low', 0):.0f}</td>"
            f"<td>{r.get('ms_median', 0):.2f}</td><td>{r.get('ms_p99', 0):.2f}</td></tr>")
        charts.append(f'<div class="card"><b>{html.escape(name)}</b>{chart(r.get("frame_ms") or [0])}</div>')
    table = ("<div class=\"card\"><table><tr><th>run</th><th>level</th><th>resolution</th><th>objects</th>"
             "<th>triangles</th><th>fps avg</th><th>1% low</th><th>median ms</th><th>p99 ms</th></tr>"
             + "".join(rows) + "</table></div>")
    out = BENCH / "index.html"
    out.write_text(PAGE.replace("__BODY__", table + "".join(charts)), encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
