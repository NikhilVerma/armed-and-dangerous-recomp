# Play guide: the end-to-end build (2026-10-18)

The desktop shortcut "Armed and Dangerous Remaster" runs this build with the Remaster preset. Every one of the 24
levels has been played to mission complete by a test (the objective chains work), but those tests used shortcuts
(invincibility, warps past long walks), so this is the first real end-to-end play. Expect rough edges; the list
below is what is already known, so you can skip reporting those.

## Getting past a blocker

Options > Cheats takes the original game's codes. Use them freely; they don't break anything:

| Code | Effect |
|---|---|
| SHIELDME | invincible (toggle) |
| PRAYTOME | god mode |
| AMBULNCE | full health now |
| SHOOTMOR | full ammo now |
| NDMRBLTS | infinite ammo (toggle) |
| MISSIONS | every level in Extras > Replay Level (skip a level you're stuck on) |
| AMATINEE | every movie in Extras > Cutscenes |
| VERYHIIQ / EATGRENZ / SHOESIZE | big heads / hands / boots |
| WAAAAAAH | upside-down screen |

## Known rough edges (no need to report)

- lev_2_m1: the sniper on the canyon bridge can't be targeted with the crosshair; lob a sticky bomb or mortar at him.
- lev_3_m2: civilian deaths sometimes reach the mission's limit and fail it.
- Melee: a grunt in reach hits you on every swing (the original's knock-back that pushes you away is missing).
- lev_5_m2: the rocket captains stop short of the third village.
- The Q112 swings late (it fires one round per fire animation).
- Paratroopers' parachutes don't animate open / closed; the glove sits in Roman's hand, not on his wrist.
- Timings, AI aim, physics feel and weapon feel are best-shot from the original's code, not yet tuned against it.
- Not built yet: see "Still missing" at the end.

## How to give feedback

Write notes in any form; this table is the easiest for the next session to work through. Add rows under a new
heading in docs/player-feedback.md (or paste them in chat):

| Level | What happened (your words) | Blocker? | Rough time / where |
|---|---|---|---|
| lev_1_m1 | e.g. "Q got stuck at the gate" | no | 5 min in, by the pub |

Most useful: anything that stops you finishing a level, crashes or freezes, things that look or feel clearly
wrong compared with the original, and anything slow (note the level).

## Still missing from this build

Built on two branches but stopped before their final tests, so not in this build (they come next week):

- World and vehicles (branch agent-world): the mob that comes over the wall when a base defence is lost, boulders
  that roll and crush, the goliath no longer pushing Roman out from between its legs, cover by trees and buildings,
  vehicle bosses' hit reactions, rocket / turret path details, the monk's hang, saved sirens, reward posters.
- Presentation (branch agent-presentation): key rebinding, HUD meters (peasant counters, the defence wall meter),
  legal / loading / exit screens, save thumbnails, Q's cape, arrows that stick, body-fall effects and fades, run
  animation speed, more AI voice lines, the camera's level-start path.
