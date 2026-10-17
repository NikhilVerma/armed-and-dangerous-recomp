# Weapons: Roman's roster (engine/src/weapons, 2026-10-17)

The player's guns and specials beyond the rifle / machine gun / sticky bomb / Topsy Turvy of the first port
(player.md 10), built as a framework the earlier weapons now sit in. Sources: src/game/game_09.cpp (the weapon state
+0xc24 and the PlayerWeapon classes), game_06.cpp / game_07.cpp (the projectile classes), ui_01.cpp (the HUD's target
passes), ai_03.cpp / ai_04.cpp / ai_06.cpp (the AI's answers), missing_01.cpp (the pub screen).

## The framework

| piece | file | original |
| --- | --- | --- |
| `Arsenal`: three gun slots, a count per special, the gun and special in hand | weapons/arsenal.rs | the slot lists 0x004a0b10 (3 primary slots, the special list), the ammo records, add 0x004a0ef0, swap 0x004a0f70, equip 0x004a10c0 / 0x004a10e0, next / previous 0x004a0c10 / 0x004a0c70, add ammo 0x004a13a0, set ammo 0x004a14e0 |
| `GunSpec` / `GunProps`: a gun's typed data, read once (`Props::typed`) | weapons/spec.rs | WeaponObject load 0x004a1bf0, PlayerWeapon load 0x004a2150, PlayerVindaloo 0x004a24d0; the descriptors' max clip / max ammo / range (p_globalsettings `<name>_max_clip` ...) |
| the target passes: auto-aim, the shark's prey, the vindaloo's lock-ons, the sticky's | weapons/targeting.rs | Hud_UpdateCrosshair 0x00406f50, HudFrustum_Build 0x00408390, 0x00408050 / 0x00408190 / 0x00408330 |
| `GunFired` (a launcher's or the sniper's shot), `ThrowSpecial` (a queued special leaves the hand), `ThrowQueue` | weapons/mod.rs | fire vt+0x140, the special queue 0x004a1930 / 0x004a1890 / 0x004a1970 |
| the HUD: each gun's icon and crosshair from the weapon table, the special's icon and count, the four lock-on markers | hud.rs | Hud_ProjectMarkers 0x00406840 |
| the pub: buy any gun the pub sells, carry / swap when the three slots are full, every held gun topped up | pubscreen.rs | 0x004695d0 (+0x74), 0x00467fe0 |
| the state log's `weapons` (playtests) | weapons/mod.rs `log_weapons` | |

player.rs keeps Roman's own handling: switching (Q: the next usable slot, the change at frame 13), reloading (each
gun's load sound frame 0x0059eef4 and the frame-43 refill), the recycle gate, the empty clip's fallback to the previous
usable gun, the dry-fire sounds (0x0047a938), the launchers' kick (300 x mass for 0.1 s back, 0.2 of it up: jump table
0x0047a920) and their fire anim (trotter_fire_rr, state 2: charanim.rs), Fire Special by the special's id (jump table
0x0047a910). The bullets of the rifle and the machine gun are player.rs projectiles; with auto-aim they fly to the
picked target's point. Pickups (pickup.rs) refill any gun or special, pickup_upgrade_weapon sets the vindaloo upgrade.
The mission's loadout gives every gun player_add_* names (in id order, three at most) and every special's count.

## The weapons

| weapon | file | what it does (addresses in the files) |
| --- | --- | --- |
| sniper rifle | sniper.rs + player.rs | fires along the camera ray from the muzzle's depth to snipergun_range; the bullet (4500 a second) goes through every character on its way, each once, and when it stops a count above 2 leaves pickup_health_2, 2 pickup_health_1, 15 in front of Roman |
| Gurner mortar | mortar.rs + vehicles/rockets.rs | lobs gurner_rocket at the crosshair's point (the forge cannon's launch: about a second's flight); area damage 100 within 40 / 60 |
| vindaloo | vindaloo.rs + vehicles/rockets.rs `Flight::Vindaloo` | up to four lock-ons (markers); a fan of two rockets (four upgraded) along the camera's facing +-a yaw, s + b / s - b/2 pitch; after 0.4 s each homes on its lock-on (ref_hit_00), faster as it closes, and goes off when it passes it (the target takes the direct damage, the blast reaches all) |
| land shark | landshark.rs | fires only at a picked target (frozen on guard by the 0x2000 message); the shark is lobbed 20 ahead, dives where it lands, swims at 50 under the ground with its fin out (depth -3, -10 for the attack within 40), bites within 3 (the landshark model rises playing sharkbite_grunt / _captain, the victim plays grunt_sharkbite: killed outright), hunts the next prey near Roman (up to max_kills 6), circles Roman at 15..30 without one, sinks when its 15 s are up |
| black hole | blackhole.rs | the box is lobbed 20 ahead, lands, opens; the hole rises 40; 2 s later every AI within 200 is carried off: runs from it 1..1.75 s, then is pulled in (lifted and flailing after 1..2 s) and gone within 5 of it or when it closes; while it sucks (6 s) Roman is pulled toward it ((1 - d/5000) x 200 a second, turned to face it, trotter_blackhole_pull standing), every projectile is steered at it and the vindaloo stops homing; one at a time (14 s) |
| Guy Fawkes traitor bomb | traitor.rs | thrown at the crosshair's point (10 s between throws); where it hits, fireworks (fx_guy_fawkes_bomb) and every bad guy within 200 turns traitor: the disc over his head, his target dropped, outside 50 he runs to the circle round the bomb; traitors hunt the loyal grunts and the loyal hunt them (ai.rs `search`), their bullets hurt each other (player.rs's team test) |
| auto-aim | targeting.rs + options.rs + gamemenu.rs | the Controls page's Auto-Aim (off by default): with the rifle or the machine gun, not zoomed, at normal difficulty or easier, the character nearest inside the narrow view (autoaim_factor_*) in range and in sight is the target: its marker shows and the shots fly to it |
| weapon swap | arsenal.rs + pubscreen.rs | with three guns, Select on a pub gun carries it (the cursor to his third slot); Select on one of his slots swaps (ui_pub_putdown, the new gun in hand, the old one back in the pub); Back there drops the carry |

## Tests

engine/playtests (in tools/regress.py's SUITE): weapon_sniper (three grunts in a line, one bullet, pickup_health_2),
weapon_mortar (a lobbed kill, the recoil anim state), weapon_vindaloo (two lock-ons, two rockets, two kills; upgraded:
four rockets on one), weapon_shark (no fire without a target, arc, dive, two bites, the end), weapon_blackhole (three
carried off and swallowed, Roman's pull anim, one at a time, the close), weapon_guyfawkes (three traitors, the 10 s
wait, a loyal grunt and the traitors hunting each other), weapon_autoaim (a miss without, the menu toggle, a hit with),
weapon_swap / weapon_swap_drop (the pub's carry, swap, drop). Unit tests: arsenal (slots, cycling, ammo), spec (typed
read), targeting (the narrowed view), sniper, vindaloo (lock-on sharing, fan), landshark (pull, depth), blackhole
(the frame-rate-free rise). The playtest step `look_at` (bot.rs) keeps the crosshair on a game-space point.

## Open (tuning pass)

docs/remaster-engine.md "Weapons (2026-10-17)" lists the visible differences and guesses to check against the
original. Not ported: the knockout bomb (special 7, feature knockout-bomb).
