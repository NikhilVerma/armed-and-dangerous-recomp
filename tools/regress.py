"""Run the fast remaster playtests (state-based, with expect steps) and print one line each.

    python tools/regress.py [--exe target-agent2] [--scene DIR] [--play DIR] [--perf | --long | --all] [NAME ...]

By default the fast SUITE (the mechanics, minutes). --long runs LONG instead: every level played to mission complete
(complete_*, 5 to 25 minutes each) and the long defence runs; run it before handing a build over and after merges that
touch missions, AI or levels. --all runs both.

--perf runs the wall-clock performance checks instead (PERF: the scripts' "expect_perf" limits, e.g. restart_parity's
frame / main-world times after a restart). Run it on an idle machine: a frame's wall time depends on what else runs,
so those limits are skipped (logged "EXPECT SKIP (perf)") in the correctness SUITE, which must pass however loaded
the machine is (docs/remaster-engine.md "Traps": scripted runs step game time by a fixed 1/60 s; nothing in the game
may read the wall clock, the audio device's position or how many frames an asset took to load).

From a git worktree without work/: --play the main checkout's work/remaster/play (its untracked scripts and states),
--scene its work/remaster/lev_1_m1 (passed on to playtest.py).

Scripts live in work/remaster/play (not in git; each starts from states/after_pub.json, which
state_after_pub.json writes). Exit code 1 when any script fails. Add a script here when it guards a ported mechanic.
"""
import argparse
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAY = os.path.join(ROOT, 'work', 'remaster', 'play')
SUITE = ['save_write', 'save_read', 'topsyhit2', 'turn_in_place', 'turret1', 'decals_obj', 'oilpump', 'gfxopts', 'cover1', 'cover2', 'cover3', 'gunner1', 'gunner2', 'objects1',
         'menus', 'hud', 'hud_crosshair', 'flow', 'stats', 'pub', 'pub_screen', 'unit',
         'hits1', 'noise1', 'noise2', 'splines1', 'alarm1', 'captain1', 'taunt1', 'leader1',
         'infantry_l2m1', 'infantry_l3m1', 'infantry_l4m2', 'infantry_l2m2', 'squad_goliath', 'twiglets', 'monk_summon', 'tower_l4m2', 'tower_l4m2_fail',
         'peasants_l2m3', 'peasants_bonus_m2', 'peasants_lost_l4m2', 'peasants_cage_l3m1',
         'mission_start', 'volumes', 'ticker', 'ticker_goliath', 'ticker_goliath_l4m1', 'pickups', 'pubflow', 'winfail',
         'player_move', 'player_fall', 'player_weapons', 'player_special', 'player_topsy', 'player_banter', 'jetpack_l3m1', 'jetpack_l4m3',
         'audio_distance', 'audio_groups', 'audio_voice', 'audio_ear', 'audio_music', 'audio_stopall', 'audio_ambient', 'audio_mg_bursts',
         'anim_blend', 'anim_notes', 'camera_follow', 'camera_death', 'camera_lookup', 'input_pad', 'foot_ik',
         'fx_particles', 'fx_debris', 'fx_emit', 'fx_lights', 'fx_screen', 'fx_hits', 'keg_launch', 'keg_blast', 'debris_settle', 'overpass_l1m1',
         'movie_intro', 'movie_subs', 'movie_screens', 'movie_credits', 'movie_pub',
         'launch_newgame', 'launch_options', 'launch_restart', 'restart_parity', 'save_slots',
         'cheats', 'campaign_next', 'campaign_chain', 'campaign_save', 'campaign_movie', 'campaign_end', 'campaign_extras',
         'defence_l1m4', 'defence_l1m4_fail', 'defence_l2m4', 'defence_l3m4', 'defence_l4m5', 'defence_l5m5',
         'vehicles_rocket_tower', 'vehicles_rocket_bud', 'vehicles_zeppelin', 'vehicles_saucer', 'vehicles_colossus', 'vehicles_forge_cannon']
# 'complete_<level>' (levels 4, 5 and the bonus levels): the level played to mission complete through its real
# objective chain (land / damage steps skip travel and fights, SHIELDME keeps Roman alive), then Stats -> the next level
SUITE += ['complete_lev_4_m3', 'complete_lev_5_m1', 'complete_lev_5_m3', 'complete_lev_5_m4', 'complete_lev_bonus_m1', 'complete_lev_bonus_m3', 'complete_lev_4_m5', 'complete_lev_5_m5', 'complete_lev_bonus_m2', 'companion_death_fail', 'complete_lev_4_m1', 'complete_lev_5_m2', 'complete_lev_4_m2']
# 'complete_<level>' for levels 1 to 3 (agent-complete-a; status table in docs/journal/2026-10-16-complete-a.md)
SUITE += ['complete_lev_1_m1', 'complete_lev_1_m1_b', 'gun_save_write', 'gun_save_read', 'complete_lev_1_m3', 'complete_lev_1_m4', 'complete_lev_3_m3', 'mg_muzzle_fx', 'complete_lev_2_m3', 'complete_lev_2_m2', 'complete_lev_2_m4', 'complete_lev_3_m2', 'complete_lev_3_m4_bd', 'complete_lev_2_m1', 'complete_lev_3_m1']
# the companions (ai.rs, ai/carried.rs, ai/close_kill.rs, ai/dialogue.rs; docs/subsystems/companions.md): the Topsy
# bomb's stun and a pull (modes 5 / 10), a close kill (paired IC anims, the grunt dead by its "kill" note), the end of a
# fight (mode 3), the leader swap on Roman's turns, the last-shot stamps holding the idle banter back, the kill meter's
# dismay by the speaker test; the Q112's swing, eye loop and Fight line (ai/q112.rs, lev_2_m2)
SUITE += ['companion_carried', 'companion_close_kill', 'companion_engage_exit', 'companion_leader_swap', 'companion_idle_cue', 'dialogue_dismay', 'q112_extras']
# the player's weapon roster (engine/src/weapons): the sniper rifle's bullet through three grunts and its health pickup, the
# Gurner's lobbed mortar, the vindaloo's lock-ons and homing fan (four rockets upgraded), the land shark's arc, dive, two
# bites and its end, the black hole's carry-off and swallow, the Guy Fawkes traitors at war with their old side, the
# Auto-Aim option (a miss without, a hit with), the pub's carry / swap with three guns and its drop
SUITE += ['weapon_sniper', 'weapon_mortar', 'weapon_vindaloo', 'weapon_shark', 'weapon_blackhole', 'weapon_guyfawkes', 'weapon_autoaim', 'weapon_swap', 'weapon_swap_drop']
# the knockout bomb (weapons/glove.rs): three grunts held, flung one a second and punched away (the first launched), the
# special spent only with targets; unpunched, the first goes down and the glove lets go of the rest; the land shark's
# target test (targeting.rs can_be_bitten_now): a gloved grunt is no prey
SUITE += ['weapon_glove', 'weapon_glove_miss', 'weapon_shark_prey']
# the villagers a destroyed house throws out (peasant/toss.rs, lev_3_m3): one to three per house, flying, landing as
# ragdolls with their chairs let go
SUITE += ['peasants_tossed']
# effects and the thrown bomb (player feedback 2026-10-18, agent-fb-fx): the camera's snow (domes and flake emitter, also
# after a load), the sticky bomb's blast effects, its homing (also with 22 bodies around Roman) and GoRagdoll's launch
SUITE += ['fx_snow', 'sticky_blast_fx', 'sticky_homing', 'sticky_homing_bodies', 'sticky_launch']
# the GPU snowfall (graphics/snow.rs): Remaster snows on the GPU with the cylinders hidden; Original keeps the cylinders
SUITE += ['gpu_snow', 'gpu_snow_original']
# the dead's bodies (corpses.rs): lev_1_m4's army killed two a second; the Original's timers (bodies gone, at most four
# ragdolls, entities and physics bodies not growing) and the Remaster's 48 bodies with settled ragdolls
SUITE += ['corpses_l1m4', 'corpses_l1m4_remaster']
# presentation (agent-presentation, docs/journal/2026-10-18-presentation.md): the Controls page rebinds keys, mouse and pad
# buttons by the original's rule and the prompts follow (bindings.rs); the legal splash before the intro movies, the
# loading screen of a restart and the exit screen, each holding the music where the original does (splash.rs)
SUITE += ['controls_rebind', 'legal_splash', 'loading_screen', 'exit_screen', 'save_thumbnail', 'run_anim_speed', 'arrow_models', 'bodyfall_fx', 'character_fade', 'hud_meters', 'hud_meters_l2m3', 'hud_meters_l1m4', 'ai_voice_lines', 'cloth_cape']
# the old remaster's engine-limit options in the engine (agent-port-limits, docs/engine-limits.md "Remaster engine"):
# the decal pool's size, kept explosion debris and its time, the master volume, the Engine page
SUITE += ['limits_decal_pool', 'limits_debris_keep', 'limits_master_volume', 'limits_menu']
# 'army_l2m4_long' (LONG): lev_2_m4's piled-up army (~1,600 marching members at 150 s) stays under 25k entities: the
# members are crowd meshes (crowd.rs), not rigged grunts (110k entities before)
SUITE += ['army_l2m4_long']
# agent-world (2026-10-17): the world features (docs/journal/2026-10-17-world.md): G1 the goliath's bone hulls
SUITE += ['goliath_legs', 'boulders_l2m1', 'monk_hang', 'twiglets_topsy', 'save_sirens_write', 'save_sirens_read', 'goliath_hurt', 'vehicles_colossus_hits', 'turret_kickout', 'defence_launchers', 'posters_l1m3', 'toss_l1m4', 'smoking_troop', 'tree_cover']
# the later levels' infantry (agent-enemyai, docs/journal/2026-10-17-enemyai.md): the shooter limit (lev_1_m4), the
# captain's grenade and rocket, the gunner's led aim, falls and parachute drops (lev_2_m1), jump markers held after a
# landing (lev_bonus_m1), the wall assault with ladders and scales and the script ends over the wall (lev_1_m4), the
# companions' takedown and the grunts' close attack, a hidden grunt knocked away with his host (lev_1_m3)
SUITE += ['max_fire_l1m4', 'captain_grenade', 'captain_rocket', 'gunner_lead', 'ai_fall', 'paratroopers_l2m1', 'jump_markers_bm1',
          'ladders_l1m4', 'melee1', 'close_attack', 'ic_knock_l1m3']
# 'launch_*': the real launch path as the player plays (the Remaster preset chosen by the options, the intro, the
# title menu): New Game shows the world (menu art gone, every camera on one target, the strips beside the 4:3 centre
# not black), Abandon -> title -> New Game in-process, the Graphics page applies, Restart / Quick Load / Restart from
# last save in-process (level_generation)
# 'restart_parity': wall-clock game time ("real_time", as the player's game): after Restart Level, Abandon -> title ->
# New Game and Restart from last save, the counts (Romans, companions, grunts, entities) and the camera's pitch match a
# fresh New Game's (feedback 28); its frame / main-world time and game-step limits are its "expect_perf" steps (--perf only)
# 'campaign_*': the level sequence (campaign.rs): a won level's Stats -> the next level in-process with the money, tokens
# and difficulty carried and the next mission's loadout (next); three levels by their win triggers, with a success
# movie (chain); a save in the second level loaded from the main menu (save); a defence level's next level with its
# start movie (movie); lev_5_m5 -> its end movie -> the credits -> the title (end); Extras: tokens open Play Cutscene
# and Replay Mission, a movie seen is listed and plays, a level reached is replayed and returns to the title (extras)
# 'defence_*': the base-defence levels (army.rs, defence.rs, icobject.rs, goliath.rs, shrubpatrol.rs): Roman starts in
# the base turret, the army marches and releases, kills and the win / fail chains (each script names its level)
# 'save_slots': the save / load slot screens (feedback 26): save to slot 2, then slot 1, load slot 2 -> slot 2's
# state; the main menu's Load Game and the pause menu's Save Game open the slot list
# 'tower_l4m2*': Jonesy's tower task on lev_4_m2 (ai/tower.rs): he starts where the level placed him, 7001 sends him
# off (warped near the tower first), he arrives healed, the marker's 802 forces the alarm and starts the countdown,
# the plant anim puts 8 bombs on the doors, the bomb loop, the countdown (skipped ahead) blows the doors (4000) and
# wins; _fail: his death sends 5001, the mission fails with "Jonesy just died!"
# 'ticker_goliath*': ticker bomb sites locked to the squads' goliaths (mission.rs lock_sites, 0x0044e120): lev_3_m1's three
# sites ride their goliaths as they walk, each is armed on its goliath and blows it (2001..2003, counter 7 -> 3998, the
# objective); lev_4_m1's site 368 the same (2110 -> 3998)
# 'jetpack_*': the jumper pack (jetpack.rs): lev_3_m1 starts with it (the model on his chest, a tap launch, the wings by
# themselves past the top, folded with Jump held and spread again, the landing); lev_4_m3: found at pack 702 (3997),
# launched and gliding, the 15 bullseye houses down (7001: the objective), a launch over the exit slab 415 -> 4000, Won
# 'vehicles_*': the later levels' units (engine/src/vehicles, the forge cannon in turret.rs), each script on its own level
# ("scene"): rocket towers / buds (lev_3_m1), the zeppelin and the saucer (lev_2_m1), the colossus (lev_1_m3), the forge
# cannon (lev_2_m1): woken, moving, targeting Roman, firing, hit, destroyed
# 'unit': the engine's Rust unit tests (cargo test --release into the same target dir: pure logic such as the
# object model's damage filters, trigger fields and husk picks, the HUD feed wrapping)
# the developer tools (engine/src/devtools; docs/play-guide.md): the menu from play (F1) toggles god mode (two blasts
# do nothing, then one hurts with it off), infinite ammo and big heads through the cheat flags, Full health; level select
# from play and from the title loads the chosen level with the New Game setup; F5 / F9 quick save and load round trip;
# F8 writes a bookmark with its screenshot and save into the script's session folder; 'pyunit': tools/test_*.py
# (tools/session.py's summary and bookmark-to-playtest conversion)
SUITE += ['devmenu_toggles', 'devmenu_level', 'devmenu_title', 'devtools_quicksave', 'devtools_bookmark', 'pyunit']
# scripts kept in git (engine/playtests) win over work/remaster/play
TRACKED = os.path.join(ROOT, 'engine', 'playtests')
# the wall-clock checks (--perf; their "expect_perf" steps run only with AND_PERF=1)
PERF = ['restart_parity']

ap = argparse.ArgumentParser()
ap.add_argument('names', nargs='*')
ap.add_argument('--exe', default='target-agent2')
ap.add_argument('--scene', default=None, help='scene folder for playtest.py (a worktree without work/)')
ap.add_argument('--play', default=None, help='folder of the untracked scripts and states (default work/remaster/play)')
ap.add_argument('--perf', action='store_true', help='run the wall-clock performance checks (PERF) on an idle machine')
ap.add_argument('--long', action='store_true', help='run LONG: every level played to completion (slow)')
ap.add_argument('--all', action='store_true', help='run SUITE and LONG')
a = ap.parse_args()
# LONG: the level completions and other runs of minutes each, kept out of the fast SUITE
LONG = [n for n in SUITE if n.startswith('complete_') or n.endswith('_long')]
SUITE = [n for n in SUITE if n not in LONG]
if a.play:
    PLAY = a.play
extra = ['--scene', a.scene] if a.scene else []
if a.perf:
    extra += ['--env', 'AND_PERF=1']

if not os.path.exists(os.path.join(PLAY, 'states', 'after_pub.json')):
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(PLAY, 'state_after_pub.json'), '--exe', a.exe] + extra)
bad = 0
for name in a.names or (PERF if a.perf else LONG if a.long else SUITE + LONG if a.all else SUITE):
    if name == 'pyunit':
        p = subprocess.run([sys.executable, '-m', 'unittest', 'discover', '-s', os.path.join(ROOT, 'tools'), '-p', 'test_*.py'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        res = re.search(r'Ran (\d+) tests?', p.stdout)
        ok = p.returncode == 0 and res is not None
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} pyunit: {res.group(1) + ' tests' if res else 'no result'}")
        for l in [l for l in p.stdout.splitlines() if l.startswith(('FAIL:', 'ERROR:'))][:5]:
            print('     ', l)
        continue
    if name == 'unit':
        env = dict(os.environ, CARGO_TARGET_DIR=os.path.join(ROOT, 'engine', a.exe))
        cargo = os.path.join(os.path.expanduser('~'), '.cargo', 'bin', 'cargo')
        p = subprocess.run([cargo if os.path.exists(cargo + '.exe') or os.path.exists(cargo) else 'cargo', 'test', '--release'],
                           cwd=os.path.join(ROOT, 'engine'), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                           errors='replace')
        res = re.search(r'test result: (\w+)\. (\d+) passed; (\d+) failed', p.stdout)
        ok = p.returncode == 0 and res is not None
        bad += not ok
        print(f"{'ok  ' if ok else 'FAIL'} unit: {res.group(2) + ' passed, ' + res.group(3) + ' failed' if res else 'no result'}")
        for l in [l for l in p.stdout.splitlines() if 'FAILED' in l or 'panicked' in l or l.startswith('error')][:5]:
            print('     ', l)
        continue
    script = next((d for d in (TRACKED, PLAY) if os.path.exists(os.path.join(d, name + '.json'))), PLAY)
    # the wall-clock limit only catches a hung game: game time is stepped per frame, so a loaded machine (several
    # agents' runs at once) takes several times longer for the same run (defence_l5m5: 66 game seconds took 320 s)
    # (a script's own "timeout" counts when it is longer: the level completions run up to 25 minutes under load, x3)
    path = os.path.join(script, name + '.json')
    try:
        own = float(json.load(open(path, encoding='utf-8')).get('timeout', 0)) * 3
    except (OSError, ValueError):
        own = 0
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), path, '--exe', a.exe, '--timeout', str(max(1200, own))] + extra,
                       stdout=subprocess.PIPE, text=True, errors='replace')
    res = re.search(r'PLAYTEST RESULT: (\d+) passed, (\d+) failed', p.stdout)
    fails = [l for l in p.stdout.splitlines() if 'EXPECT FAIL' in l or 'panicked' in l]
    ok = p.returncode == 0 and res is not None
    bad += not ok
    t = re.search(r'in (\d+) s', p.stdout)
    print(f"{'ok  ' if ok else 'FAIL'} {name}: {res.group(1) + ' passed' if res else 'no result'}{', ' + res.group(2) + ' failed' if res and res.group(2) != '0' else ''} ({t.group(1) if t else '?'} s)")
    for l in fails[:5]:
        print('     ', l)
sys.exit(1 if bad else 0)
