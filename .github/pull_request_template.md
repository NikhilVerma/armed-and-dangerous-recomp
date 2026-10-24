## What and why

<!-- The behaviour this changes, in play terms. For original behaviour: the original's addresses (archive/src HOOK(0x...)). -->

## Evidence

<!-- How you know it matches the original (or is better): playtests and their EXPECT lines, a capture comparison,
     the recovered C++ you followed. Guesses labelled as guesses. -->

- [ ] `cargo check` clean; the changed area's playtests pass (`python tools/playtest.py engine/playtests/NAME.json --exe target`)
- [ ] One implementation per original function: I searched for the addresses first (`python tools/engine_map.py addr 0x...`)
- [ ] New behaviour has a playtest with `expect` steps (or a unit test for pure logic)
- [ ] docs updated where they describe what changed (docs/subsystems, docs/port/*.csv dispositions)
- [ ] No game data, captures or files from orig/, work/ or ref/ in the diff
