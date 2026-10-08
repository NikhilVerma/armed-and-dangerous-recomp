"""Run the game once with the Jolt shadow mirror ([physics] engine=jolt_shadow, docs/subsystems/physics-jolt.md)
and print its log lines. Havok still simulates; only the recovered PhysicsManager_Init/Shutdown/Step are hooked
(they hold the enhancement points), everything else runs the original code. Runs in the "main" instance
(tools/instances.py), never the player's game folder.

Usage: python tools/jolt_shadow_run.py [--scenario play] [--hooks all|physics] [--timeout 240]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import compare  # noqa: E402
import instances  # noqa: E402

PHYSICS = ("PhysicsManager_Init", "PhysicsManager_Shutdown", "PhysicsManager_Step")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="play", choices=sorted(compare.SCENARIOS))
    ap.add_argument("--hooks", default="physics", choices=("physics", "all"),
                    help="physics: only the three PhysicsManager hooks; all: every recovered function")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--filter", type=int, default=2, help="[physics] jolt_filter: 2 both game filters, 1 0x00411710, 0 none")
    a = ap.parse_args()
    names = compare.hook_names()
    hooks = {n: a.hooks == "all" or n in PHYSICS for n in names}
    sc = compare.SCENARIOS[a.scenario]
    test = dict(sc.get("test") or {})
    if a.scenario == "play" or sc["level"]:
        test.setdefault("close_frame", sc["frames"])  # a normal shutdown, so the shutdown summary is logged
        test.setdefault("exit_frame", 0)
    rows, crashed = compare.run_once(f"jolt_shadow {a.scenario}", hooks, sc["level"], sc["frames"], a.timeout,
                                     sc["keys"], test, extra_sections={"physics": {"engine": "jolt_shadow", "jolt_filter": a.filter}})
    log = os.path.join(instances.instance_dir("main"), "and.log")
    lines = [l.rstrip() for l in open(log, encoding="latin1") if "physics(jolt)" in l or "CRASH" in l]
    print("\n".join(lines[:5] + (["  ..."] if len(lines) > 25 else []) + lines[max(5, len(lines) - 20):]))
    return 1 if crashed or not lines else 0


if __name__ == "__main__":
    sys.exit(main())
