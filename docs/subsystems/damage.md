# Damage filters and what stops a bullet

Traced from the decompiled code (2026-10-14). Vtable slots read from the unpacked exe: Load = vt+0x18, damage
handler = vt+0xd0. The remaster engine ports these rules in `engine/src/fx.rs` (`Destructible::accepts`, `Src`,
`DClass`) and `engine/src/level.rs` (`physics.no_bullets`).

## Classes

| Behaviour, class | Vtable | Load | Defaults | Damage handler and rule |
|---|---|---|---|---|
| none (default object): guard alarm, village window, arch, boom island, oil pump, prison gate, rocket tower, invisible walls, igloo | 0x005a2bf0 | 0x0044fd20 (game_03.cpp:432) | takes_damage 0, takes_bullet_damage 0, blocks_projectiles 0, takes_crushing_damage 1 | 0x00450da0 (game_03.cpp:919): ignores records from itself, when dead (0x10080), with 0x400 unless takes_bullet_damage (flag 2), crushing (0x4000) unless takes_crushing; applies only with takes_damage (flag 1) |
| building (id 139) | 0x005aeeb8 | 0x0040ad80 (ai_01.cpp:927) | takes_damage 1, player 1, teammate 1, badguy 0, takes_bullet 0 (ai_01.cpp:961-971); does not read blocks_projectiles | 0x0040ce40 (ai_01.cpp:1801): needs takes_damage and not destroyed; 0x400 needs takes_bullet; when the source's vt+0xe0 is false: player needs player_damage, else vt+0xe4 needs teammate_damage; then vt+0xe8 needs badguy_damage |
| tree (id 143) | 0x005a2e60 | 0x00496e90 (game_08.cpp:204) | none | Object_TakeDamage 0x004555e0: no filter |
| fx_explosion (id 90): kegs, pipes | 0x005a4300 | 0x0041c180 (game_02.cpp:652) | player 1, teammate 0, badguy 0 | 0x0041c4e0 (game_02.cpp:724): no bullet test; passes when source vt+0xe0 or flags & 0x180000, else the player / teammate / badguy bytes |
| rocket_tower_turret | 0x005a8b88 | 0x0049c320 | | Object_TakeDamage, no filter |
| pub | 0x005a1dc8 | | | 0x00433e70 does nothing |

Sources (+0x2c): the player (0x005aa888) e0 false, e4 true, e8 false; grunts (0x005a9cf0) e0 = IsDead 0x0043ec50,
e4 false, e8 true; plain objects (kegs, default objects) e0 true (0x00433fa0), so they pass every source filter.
Quirk: a dead grunt's damage passes too.

## Damage records

- Bullet direct hit 0x0047eec0 (game_06.cpp:1556): flags |= vt+0x13c, +0x28 projectile, +0x2c owner (+0x504, the
  shooter); head +4, chest +8. vt+0x13c 0x0047eba0 (game_06.cpp:1430): **0x400 = damages_buildings off** (the
  default) and 0x80 allow_cartoon_death. lev_1_m1 damages_buildings: p_player_stickybomb, p_stig_mgun_bullet,
  p_molmer_dynamite. The base Projectile vtable 0x005ac790 adds 0x20000 (0x0047f4d0).
- Area damage 0x0047f020 (game_06.cpp:1610): list +0xe94, skipping the excluded object and the source; flags given
  | 1, +0x2c owner, no +0x28.
- Sticky bomb 0x00481ab0 (game_07.cpp:1278): flags 0x80 | 1, owner the thrower (0x0047e3a0); the bomb excluded.
- Keg / pipe death 0x0041c570 (game_02.cpp:813): source and owner the keg itself, flags 0x40 | 1 (0x40 only lights
  another keg's fuse). Default object death 0x00450ee0: owner the object, flags 1.
- Ticker bomb record: not traced (engine: the player).

## What stops a bullet

Projectile_Update 0x0047e580 (game_06.cpp:1281-1295): include only list +0xe90 (0x00413dc0), exclude the hit list
+0x570 and the owner chain +0x574. Broadphase filter 0x00413a90 (physics_01.cpp:2797-2809): an owned body is hit
only if its object is in +0xe90; ownerless level geometry always; material "invisible_material" never (0x004138c0).
The hit handler 0x0047eae0 returns false: the first hit stops the bullet.

In +0xe90: default objects with takes_damage or blocks_projectiles and not no_collide (game_03.cpp:560); buildings
unless base_defend or invisible (ai_01.cpp:1063); trees (game_08.cpp:210); kegs (game_02.cpp:677); turrets
(game_08.cpp:892); characters (ai_01.cpp:2340). So trees stop bullets; p_invisible_walls (blocks_projectiles 0)
does not.

The crosshair's aim ray (Hud_UpdateCrosshair 0x00406f50, ui_01.cpp:540-548) uses list +0xe9c (mode 0) / +0xea4:
every collidable default object, so it stops on invisible walls that bullets fly through. Target sight checks
(0x00414110) use +0xea0.

## Open

- Which projectiles use vtable 0x005aec10 (flags from 0x00481010).
- Character damage filters (0x00430340, 0x0047afa0) beyond the same-team rule.
