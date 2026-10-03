# Decisions

Every target or approach decision, with the evidence and numbers that triggered it. Newest first.

## 2026-10-03: DRM gate on the Steam build

- **Evidence:** `orig/pc/GameData/game.exe` (SHA-256 `960df24c...5b45`) has a `.bind` section, its entry
  point is inside `.bind`, and `.text` has entropy 8.00. This is the SteamStub wrapper. See `docs/recon.md`.
- **Asked the human.** Options offered: a DRM-free executable from the human, or the Xbox build as target.
- **Decision (human):** keep the PC Steam build as primary target. The human unwraps their own copy with
  Steamless and places the output in `orig/pc/GameData/`. Agents never unpack or patch protected executables.
  The human also declined using a third-party download of the game.
- **Approach:** none chosen yet. Default hypothesis from the handover is Approach B on the PC build; M1
  decides from recon evidence and the 20-function spike.
- **Budget for M0/M1:** no fixed limit; report at each milestone.
