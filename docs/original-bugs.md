# Original bugs

Defects found in the original game. Reproduced faithfully and not fixed before M5.

| Bug | Where | Evidence | Reproduced in recovered code |
| --- | --- | --- | --- |
| Game speed and physics may run fast on modern systems | Unknown | PCGamingWiki | n/a |
| Display mode list has no capacity check (150 entries at Renderer+0x9f4) | 0x00508870 | Disassembly: appends while count grows, no bound | **Deviation:** recovered `Renderer_AddDisplayModes` stops at 150. Unreachable with original filters; reachable once `all_modes` widens them. |
| Resolution lock: only 640-1600 wide, 16:9/4:3/5:4 | 0x00508870 | `cmp edx, 0x280` / `cmp edx, 0x640` and aspect tests | Faithful by default; `[display] all_modes` / `width` widen it. |
| Saved resolution in `SavedGames/options.opt` overrides the start-up size | options loader (not yet located) | 1440x1080 reappeared after a windowed run | n/a |
| lev_1_m1 opening: at frame steps below ~11.8 ms (above ~85 fps) the starting token is collected during the opening camera path; its message pauses the world mid-path, the path never sets "finished", game+0xdce (cinematic) stays 1 and the mission start (0x0044daf0) never runs | token pickup 0x00472a00 (collected flag +0x4bf); camera path end check 0x0040f683; mission start 0x0044daf0 | `[test] fixed_dt_ms` sweep at 30 fps: 33.3/12 ms ok, 10.5/9.5/8.4 ms stall. `tools/watch.py` on the camera slot: path time advances by dt and needs 5.0 s; at 10.5 ms updates stop at 4.46 s (~425 frames) when the token message appears. Why the token is collected early: not yet known (suspect player physics drift at small steps). | n/a |
