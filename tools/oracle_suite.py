"""The oracle regression suite: every tools/oracle_scenarios/*.json with "suite": true, run in both games and compared
(tools/oracle_diff.py). Separate from tools/regress.py because each scenario also runs the original game (load and
briefing about 1 to 3 minutes of wall time per scenario on top of the remaster's run).

    python tools/oracle_suite.py [--exe target-wt-oracle] [--no-run] [--jobs N] [--instance oracle] [NAME ...]

One line per scenario (ORACLE NAME: PASS / FAIL ...); exit code 1 when any scenario shows an unexpected divergence
or a run failed. Expected differences are listed in each scenario's "expected" with the reason.
"""
import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENARIOS = os.path.join(ROOT, "tools", "oracle_scenarios")


def suite():
    import json
    names = []
    for path in sorted(glob.glob(os.path.join(SCENARIOS, "*.json"))):
        if json.load(open(path)).get("suite"):
            names.append(os.path.splitext(os.path.basename(path))[0])
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("names", nargs="*")
    ap.add_argument("--exe", default="target-wt-oracle")
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--jobs", type=int, default=1, help="scenarios at once (the original of each in instance <instance><n>)")
    ap.add_argument("--instance", default="oracle", help="the original's game instance name (parallel agents: their own)")
    a = ap.parse_args()
    import queue
    import threading
    q = queue.Queue()
    for name in a.names or suite():
        q.put(name)
    lock = threading.Lock()
    bad = []

    def worker(slot):
        while True:
            try:
                name = q.get_nowait()
            except queue.Empty:
                return
            inst = a.instance if a.jobs <= 1 else f"{a.instance}{slot}"
            cmd = [sys.executable, os.path.join(ROOT, "tools", "oracle_diff.py"), name, "--exe", a.exe,
                   "--instance", inst]
            if a.no_run:
                cmd.append("--no-run")
            p = subprocess.run(cmd, capture_output=True, text=True)
            lines = [l for l in p.stdout.splitlines() if l.startswith("ORACLE ") or "DIVERGE" in l]
            with lock:
                print("\n".join(lines) if lines else f"ORACLE {name}: run failed (exit {p.returncode})\n{p.stdout[-1500:]}{p.stderr[-1500:]}",
                      flush=True)
                if p.returncode != 0:
                    bad.append(name)
    threads = [threading.Thread(target=worker, args=(i,)) for i in range(max(1, a.jobs))]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
