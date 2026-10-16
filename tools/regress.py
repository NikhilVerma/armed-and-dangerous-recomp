"""Run the fast remaster playtests (state-based, with expect steps) and print one line each.

    python tools/regress.py [--exe target-agent2] [--scene DIR] [--play DIR] [--perf] [NAME ...]

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
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAY = os.path.join(ROOT, 'work', 'remaster', 'play')
SUITE = ['save_write', 'save_read', 'topsyhit2', 'turn_in_place', 'turret1', 'decals_obj', 'oilpump', 'gfxopts', 'cover1', 'cover2', 'cover3', 'gunner1', 'gunner2', 'objects1',
         'menus', 'hud', 'flow', 'stats', 'pub', 'pub_screen', 'unit',
         'hits1', 'noise1', 'noise2', 'splines1', 'alarm1', 'captain1', 'taunt1', 'leader1',
         'infantry_l2m1', 'infantry_l3m1', 'infantry_l4m2', 'infantry_l2m2', 'squad_goliath', 'twiglets', 'monk_summon',
         'peasants_l2m3', 'peasants_bonus_m2', 'peasants_lost_l4m2', 'peasants_cage_l3m1',
         'mission_start', 'volumes', 'ticker', 'pickups', 'pubflow', 'winfail',
         'player_move', 'player_fall', 'player_weapons', 'player_special', 'player_topsy', 'player_banter',
         'audio_distance', 'audio_groups', 'audio_voice', 'audio_ear', 'audio_music', 'audio_stopall', 'audio_ambient',
         'anim_blend', 'anim_notes', 'camera_follow', 'camera_death', 'input_pad', 'foot_ik',
         'fx_particles', 'fx_debris', 'fx_emit', 'fx_lights', 'fx_screen', 'fx_hits',
         'movie_intro', 'movie_subs', 'movie_screens', 'movie_credits', 'movie_pub',
         'launch_newgame', 'launch_options', 'launch_restart', 'restart_parity', 'save_slots',
         'cheats', 'campaign_next', 'campaign_chain', 'campaign_save', 'campaign_movie', 'campaign_end', 'campaign_extras',
         'defence_l1m4', 'defence_l1m4_fail', 'defence_l2m4', 'defence_l3m4', 'defence_l4m5', 'defence_l5m5',
         'vehicles_rocket_tower', 'vehicles_rocket_bud', 'vehicles_zeppelin', 'vehicles_saucer', 'vehicles_colossus', 'vehicles_forge_cannon']
# the machine gun's held muzzle fx (one per burst, released when firing stops)
SUITE += ['mg_muzzle_fx']
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
# 'vehicles_*': the later levels' units (engine/src/vehicles, the forge cannon in turret.rs), each script on its own level
# ("scene"): rocket towers / buds (lev_3_m1), the zeppelin and the saucer (lev_2_m1), the colossus (lev_1_m3), the forge
# cannon (lev_2_m1): woken, moving, targeting Roman, firing, hit, destroyed
# 'unit': the engine's Rust unit tests (cargo test --release into the same target dir: pure logic such as the
# object model's damage filters, trigger fields and husk picks, the HUD feed wrapping)
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
a = ap.parse_args()
if a.play:
    PLAY = a.play
extra = ['--scene', a.scene] if a.scene else []
if a.perf:
    extra += ['--env', 'AND_PERF=1']

if not os.path.exists(os.path.join(PLAY, 'states', 'after_pub.json')):
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(PLAY, 'state_after_pub.json'), '--exe', a.exe] + extra)
bad = 0
for name in a.names or (PERF if a.perf else SUITE):
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
    p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'playtest.py'), os.path.join(script, name + '.json'), '--exe', a.exe, '--timeout', '1200'] + extra,
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
