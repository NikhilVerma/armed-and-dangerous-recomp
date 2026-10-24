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

## Developer menu

Open it with **F1** anywhere (title, in play, pause menu), the pad's **View / Create + Y / Triangle** (hold View, press Y), or the
**Developer menu (F1)** button at the bottom left of the title and pause menus (click or tap it). The game pauses
under it. Click an item, or move with Up / Down (W / S, D-pad, stick) and choose with Enter / Space / A; Left / Right
flips a toggle; Esc / right click / B goes back; F1 closes.

- **Level select**: every campaign and bonus level by name and id. It starts at the level's start as New Game does
  (5000 money, the level's own loadout, your tokens); after a win the campaign carries on to the next level.
- **Toggles** (saved in options.json under "dev", applied whenever a level loads, shown [on] / [off]): God mode,
  Invincible, Infinite ammo, Big heads / hands / boots, Topsy turvy screen, Unlock all levels and movies (Extras),
  Show FPS / frame ms, Show state overlay (level, mission state and goal, Roman's position, health, weapon, special).
  They switch the same flags as the cheat codes below.
- **Actions**: Full health, Full ammo, Give every special and fill the gun slots (Roman carries three guns; the
  Weapons page puts any gun in his hands), Quick save (F5), Quick load (F9), Restart level, Bookmark (F8).

Hotkeys in play: **F5** quick save, **F9** quick load (the quick save; it works with checkpoint saves on too),
**F8** bookmark, **F1** developer menu, F11 fullscreen.

Command line (arguments after `engine\play.bat`, or added to the desktop shortcut's target):

    engine\play.bat --level lev_3_m1 --dev god,infammo

`--level` starts in that level (no title menu, New Game setup). `--dev` switches toggles on for that run: `god`,
`invincible`, `infammo`, `bigheads`, `bighands`, `bigboots`, `topsy`, `unlock`, `fps`, `state`. `--no-session` turns
the session recording off for that launch.

## Bookmarks and session recordings

Every launch records a session (only what the game's own window receives; local files, nothing is sent anywhere) in
`%LOCALAPPDATA%\AndRemaster\sessions\<date>_<time>\`: the game log (warnings, errors, crashes with backtrace), an events
log (levels, mission events, deaths, wins / fails, saves / loads, dev toggles), the game state twice a second, your
input per frame and frame-time statistics. The newest 20 sessions are kept (at most 2 GB); older ones are deleted
automatically.

Press **F8** when something is wrong: it takes a screenshot and a save of that moment, then asks for an optional note
(type it, Enter to keep it, Esc for none). Feedback can be as simple as "session 2026-10-19_20-15-03, bookmarks 1-4"
(or just "the last session"): the note, screenshot and save are enough for the next session to load that exact moment
(`python tools/session.py show latest`, `python tools/session.py repro latest 3` turns bookmark 3 into a playtest).

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

Bookmarks with notes (F8, above) are the quickest: name the session and the bookmark numbers. Otherwise write notes
in any form; this table is the easiest for the next session to work through. Add rows under a new
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
