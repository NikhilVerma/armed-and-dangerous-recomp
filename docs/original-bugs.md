# Original bugs

Defects found in the original game. Reproduced faithfully and not fixed before M5.

| Bug | Where | Evidence | Reproduced in recovered code |
| --- | --- | --- | --- |
| Game speed and physics may run fast on modern systems | Unknown | PCGamingWiki | n/a |
| Display mode list has no capacity check (150 entries at Renderer+0x9f4) | 0x00508870 | Disassembly: appends while count grows, no bound | **Deviation:** recovered `Renderer_AddDisplayModes` stops at 150. Unreachable with original filters; reachable once `all_modes` widens them. |
| Resolution lock: only 640-1600 wide, 16:9/4:3/5:4 | 0x00508870 | `cmp edx, 0x280` / `cmp edx, 0x640` and aspect tests | Faithful by default; `[display] all_modes` / `width` widen it. |
| Saved resolution in `SavedGames/options.opt` overrides the start-up size | options loader (not yet located) | 1440x1080 reappeared after a windowed run | n/a |
| Opening of lev_1_m1 stalls on the token message at 119 fps but continues at 30 fps (no input given) | not yet located | `tools/run.py --set display.fps_limit=30` vs `0`, screenshots at 22 s, reproduced in alternating order 2026-10-03 | n/a |
