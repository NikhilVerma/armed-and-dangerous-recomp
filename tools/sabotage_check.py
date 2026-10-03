"""Toggle a deliberate bug in Game_StartLevel to check that tools/compare.py catches it.
Usage: python work/sabotage.py on|off"""
import sys

path = r"C:\Users\nikhi\and-decomp\src\game\start_level.cpp"
good = "orig::f2u(1.0f), 0);  // level fade"
bad = "orig::f2u(0.5f), 0);  // level fade SABOTAGE TEST"
text = open(path, encoding="utf-8").read()
if sys.argv[1] == "on":
    text = text.replace(good, bad)
else:
    text = text.replace(bad, good)
open(path, "w", encoding="utf-8", newline="\n").write(text)
print("sabotage", "present" if bad in text else "absent")
