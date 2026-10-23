# Contributing

Thanks for helping. The short version: the original game is the specification, the recovered C++ in `archive/src`
explains it, and the engine reproduces it in clean, idiomatic Rust / Bevy, checked by headless playtests.

## Setting up

Follow the README's [Getting started](README.md#getting-started): your own copy of the game, `tools/setup_assets.py`,
`cargo build --release` in `engine/`. Never commit anything from `orig/`, `work/` or `ref/` (game data, exports,
captures); `.gitignore` covers them and the pre-commit guard refuses them:

```bat
git config core.hooksPath tools/hooks
```

The guard also rejects files over 1 MB and binary game formats, files whose line endings flipped (the repository
mixes CRLF and LF files: keep each file's own; GNU `sed -i` rewrites CRLF to LF), and dishonest port dispositions.

## The rules of the port

- **Behaviour is the spec.** A feature's numbers, timings, rules and states come from the original: read its
  recovered function (`grep -rn "HOOK(0x0047afa0" archive/src`), the struct atlas for object fields (summaries in
  `docs/structs/`; build the full index once with `python tools/struct_atlas.py static` then `infer`, then
  `python tools/struct_atlas.py field PlayerObject 0xa34`) and the subsystem notes in `docs/subsystems/`. Cite the
  original's addresses in doc comments. Guesses are labelled `guess:` with what would settle them.
- **The structure is yours.** Write it the way you would write a Bevy game from scratch: typed components, systems in
  a plugin, typed level data deserialised once (serde), game specifics in data. No transliterated control flow, no
  field offsets as struct design, no hacks (a workaround is either the real mechanism or logged as debt).
- **One implementation per original function.** Before porting an address, look for it:
  `python tools/engine_map.py addr 0x0047afa0` (or grep `engine/src`). Extend the existing code instead of writing a
  second version.
- **Original bugs** are reproduced and logged in `docs/original-bugs.md`; a fix for the remaster goes behind a
  setting.
- **Dispositions.** When a function is replaced, not needed or deferred, record it in `docs/port/<subsystem>.csv`
  (docs/port/README.md); `python tools/port_queue.py --lint` must pass.

## Testing

Playtests drive the real game headless (off-screen, muted) and check `expect` steps:

```bat
python tools/playtest.py engine/playtests/launch_newgame.json --exe target
python tools/regress.py --exe target            :: the fast suite (minutes); --long plays every level to the end
cd engine && cargo test --release               :: unit tests (no game data needed)
```

`--exe` names the cargo target directory under `engine/` (`target` for a plain `cargo build --release`). Most
scripts start from `states/after_pub.json`, which `engine/playtests/state_after_pub.json` writes the first time.
A new mechanic gets a playtest with `expect` steps that cannot pass by luck: drive the outcome directly (warp, kill,
damage steps), give ranges a margin, and add it to `tools/regress.py`'s suite.

Comparing with the running original (captures, lockstep traces) is described in docs/remaster-engine.md; those tools
need the archived hook DLL and an executable it can patch, so most contributors will not run them.

## Code style

- Rust: `cargo check` clean; `cargo clippy` for new code. `engine/rustfmt.toml` holds the house style (120 columns);
  format the files you touch. The whole tree will be formatted once after the open branches merge.
- Bevy systems take at most 16 parameters: bundle extras in tuples (`python tools/engine_map.py params`).
- Python tools: standard library plus `requirements.txt`; each tool's docstring says what it does and how to run it,
  and prints a small structured result rather than a dump.
- Names say what things do; the original's addresses go in doc comments as provenance.

## Pull requests

Small and coherent. Say what changed in play, which original functions it follows, and what you tested (the PR
template asks). CI builds the engine, runs the unit tests and checks the Python tools; playtests run locally.

## AI agents

Much of this project was written by AI coding agents. If you use one, point it at [AGENTS.md](AGENTS.md) (operating
rules, re-read every session), docs/remaster-engine.md (the engine handover) and docs/agent-brief.md (a brief
template for background agents). The same rules apply to its output as to yours: evidence for every claim, tests
that pass for the right reason.

## Conduct and licence

[Code of conduct](CODE_OF_CONDUCT.md). Contributions are dual-licensed MIT OR Apache-2.0, as the README says.
