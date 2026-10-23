"""Export one level's player numbers (Roman, template `trotter`) for the remaster engine (engine/src/camera.rs,
charanim.rs, main.rs read the `camera` and `movement.anim_speed_tables` parts).

  python tools/remaster_export_player.py lev_1_m1       -> work/remaster/lev_1_m1/player.json

Needs tools/remaster_export.py LEVEL (properties.json, templates.json) first; reads the instance bank (big10_inst),
the anim bank (big03_anim: the camera and companion anchor nodes of Trotter_ref) and game.unpacked.exe (the
difficulty table, read only). What each number means and its evidence: docs/subsystems/player.md.

Values are the level's retail property sets (p_trotter, p_globalsettings, p_mission with the mission instance's
overrides, the weapons' sets) and, where a set has no key, the code default the game uses (PlayerObject_Load
0x00473da0, archive/src/game/game_05.cpp:557-724). Notes that are the same for every level (controls table, formulas, camera
behaviour) are constants of this tool, each with its evidence. Units: game units, seconds, radians unless noted; Z up.
"""
import argparse
import json
import math
import pathlib

from remaster_level import WORK, Level, exe_f32, plain_number, round_vec

GRAVITY = 64.348        # Havok world cinfo (docs/subsystems/havok-layouts.md): 2 x 32.174 ft/s^2
HALF = 0.5              # the constant 0x005bcb4c the load multiplies every anti_gravity by
ASPECT = 1.702128       # camera +0x154: horizontal -> vertical FOV

# PlayerObject_Load 0x00473da0 (archive/src/game/game_05.cpp:573-711): key -> code default when p_trotter has no value
DEFAULTS = {
    "friction_min": 0.3, "friction_max": 2.0, "friction_exponent": 4.0,
    "fwd_max_speed": 10.0, "side_max_speed": 10.0, "fwd_max_speed_min": 4.0, "side_max_speed_min": 4.0,
    "landing_turbo_power_min": 0.0, "landing_turbo_power_max": 0.0, "landing_jump_power_min": 0.0,
    "landing_jump_power_max": 0.0, "landing_power_min": 0.0, "landing_power_max": 0.0,
    "movement_power_min": 10.0, "movement_power_max": 10.0,
    "targeting_exponent": 2.0, "targeting_bias": 0.0, "zoom_targeting_exponent": 5.0, "zoom_targeting_bias": 0.0,
    "control_exponent": 2.0, "control_bias": 0.0, "zoom_control_exponent": 5.0,
    "zoom_control_bias": 0.0,   # data key; the exe reads "zoom_control_control_bias" (game_05.cpp:583), default 0
    "targeting_control_drag": 0.5, "zoom_targeting_control_drag": 0.25, "turn_power_min": 40.0,
    "turn_power_max": 40.0, "turn_max_speed": 0.1, "pitch_max_speed": 1.0, "roll_factor": 0.005,
    "machinegun_turn_max_speed": 0.025, "machinegun_pitch_max_speed": 0.1, "sniper_turn_max_speed": 0.025,
    "sniper_pitch_max_speed": 0.1, "rifle_turn_max_speed": 0.025, "rifle_pitch_max_speed": 0.1,
    "sharkgun_turn_max_speed": 0.025, "sharkgun_pitch_max_speed": 0.1, "gurner_turn_max_speed": 0.025,
    "gurner_pitch_max_speed": 0.1, "vindaloo_turn_max_speed": 0.025, "vindaloo_pitch_max_speed": 0.1,
    "turbo_fwd_max_speed": 2.0, "turbo_side_max_speed": 2.0, "turbo_force": 175.0, "turbo_boost_force": 300.0,
    "turbo_time": 3.0, "turbo_anti_gravity": 0.25, "turbo_glide_zvel": 10.0, "turbo_glide_xyvel": 30.0,
    "turbo_glide_power_min": 10.0, "turbo_glide_power_max": 10.0, "turbo_glide_fwd_max_speed": 50.0,
    "turbo_glide_bkd_max_speed": 40.0, "turbo_glide_side_max_speed": 4.0, "turbo_movement_power": 10.0,
    # turbo_glide_anti_gravity: default turbo_anti_gravity * 0.5 * 0.25 (0x005bcbe4), see turbo_glide_anti_gravity()
    "jump_fwd_max_speed": 2.0, "jump_side_max_speed": 2.0, "jump_movement_power": 10.0,
    "fall_speed": -50.0, "fall_speed_turbo": -50.0, "fall_speed_turbo_glided": -50.0, "fall_speed_jump": -50.0,
    "fall_land_hard_time": 0.1, "land_speed": 40.0, "land_speed_jump": 40.0, "land_speed_turbo": 35.0,
    "jump_impulse": 10.0, "jump_anti_gravity": 0.25,
    "anim_walk_run_switch": 0.4, "normal_twist_limit": 45.0, "running_twist_limit": 45.0,
    "landing_damage_min_hurt": 70.0, "landing_damage_max_hurt": 200.0, "landing_damage_min_speed": 40.0,
    "landing_damage_max_speed": 100.0, "landing_damage_min_hurt_turbo": 70.0, "landing_damage_max_hurt_turbo": 200.0,
    "landing_damage_min_speed_turbo": 40.0, "landing_damage_max_speed_turbo": 100.0,
    "death_falling_time": 3.0, "death_falling_distance": 200.0, "turbo_death_falling_time": 3.0,
    "turbo_death_falling_distance": 200.0,
}
# the anim rate tables (game_05.cpp:660-707): min, max, low_factor, low_speed, high_factor, high_speed defaults
ANIM_TABLE_DEFAULTS = {"run": [0.0, 100.0, 0.0, 0.0, 1.0, 17.0], "walk": [0.0, 100.0, 0.0, 0.0, 1.0, 4.3],
                       "side_run": [0.0, 100.0, 0.0, 0.0, 1.0, 17.0], "side_walk": [0.0, 100.0, 0.0, 0.0, 1.0, 17.0]}
# zoomed turn / pitch speeds per weapon (0x00475990 / 0x004759e0); sniper2 is data only (no string in the exe)
ZOOM_WEAPONS = ["machinegun", "rifle", "sniper", "sniper2", "gurner", "vindaloo", "sharkgun"]
# the follow camera's nodes in the order the port reads them: eye pivot and look-at first, then the weapon nodes
CAMERA_ORDER = ["Camera_start", "Camera_end", "Camera01", "Camera_rifle", "Camera_machinegun", "Camera_sniper",
                "Camera_sniper_2", "Camera_gurner", "Camera_vindaloo"]
# player companions: key -> (template, weapon templates); the weapons are the companion classes' own (code)
COMPANIONS = {"q": ("q111", {"weapon": "q_chaingun"}),
              "jonesy": ("molmer", {"weapon": "molmer_pistol", "dynamite": "molmer_dynamite"})}
HEALTH_PICKUPS = ["pickup_health_1", "pickup_health_2", "pickup_healthytea"]

# Controls_LoadDefaultBindings 0x004144a0: the default table 0x00588ed8 (exe), action names from 0x004155a0
BINDINGS = {
    "_note": "Default table 0x00588ed8 (Controls_LoadDefaultBindings 0x004144a0), action names from 0x004155a0. "
             "[primary, secondary]; DIK scan codes as key names. MOUSE_1 = 0x10004 (assumed left button), MOUSE_2 = "
             "0x10005 (right), MOUSE_3 = 0x10006 (middle).",
    "MoveForward": ["W"], "MoveBackward": ["S"], "MoveLeft": ["A"], "MoveRight": ["D"],
    "TurnLeft": ["MOUSE_LEFT", "LEFT_ARROW"], "TurnRight": ["MOUSE_RIGHT", "RIGHT_ARROW"],
    "LookUp": ["MOUSE_UP", "UP_ARROW"], "LookDown": ["MOUSE_DOWN", "DOWN_ARROW"],
    "Fire": ["MOUSE_1"], "FireSpecialWeapon": ["MOUSE_2"], "ToggleZoom": [], "Zoom": ["Z", "E"], "Reload": ["R"],
    "TurboJump": ["SPACE", "MOUSE_3"], "IncWeapon": ["Q"], "DecWeapon": [], "IncSpecialWeapon": ["TAB"],
    "DecSpecialWeapon": [], "TeammatesSend": ["1"], "TeammatesReturn": ["3"], "Use": ["X", "F"],
    "NRMStart": ["ESCAPE"], "NRMEscape": ["ESCAPE"], "MenuUp": ["UP_ARROW"], "MenuDown": ["DOWN_ARROW"],
    "MenuLeft": ["LEFT_ARROW"], "MenuRight": ["RIGHT_ARROW"], "MenuSelect": ["MOUSE_1", "RETURN"],
    "MenuCancel": ["ESCAPE", "MOUSE_2"], "RoamCam1": ["TAB"], "RoamCam2": ["1"], "FastMovement": ["BACKSPACE"],
    "MoveLiftUp": ["ESCAPE"], "MoveLiftDown": ["RETURN"], "SpecialEvent": ["H"],
    "_mouse": "sensitivity multiplier = m_mouseSensitivity + 0.5 (default option 0.5 -> 1.0); mouse delta converted "
              "to a 30 Hz rate; no invert option (swap MOUSE_UP/DOWN bindings, 0x00414520); no crouch action; Zoom is "
              "held (ToggleZoom unbound) (game_06.cpp:2819-2836)",
}

ANIMATIONS = {
    "skeleton": "Trotter_ref",
    "split": "_b = lower body, _t = upper body (guess from names)",
    "locomotion": ["trotter_idle_1..5", "trotter_walk_*", "trotter_run_*", "trotter_run_left_*", "trotter_run_right_*",
                   "trotter_walk_left_*", "trotter_walk_right_*", "trotter_turn_left_*", "trotter_turn_right_*"],
    "jump": ["trotter_jump_into_b/t", "trotter_jump_idle_b/t", "trotter_jump_fire_rr_b/t", "trotter_jump_land",
             "trotter_land_soft", "trotter_land_hard", "trotter_fall_into", "trotter_fall_loop"],
    "turbo": ["trotter_turbo_{idle,forward,backward,left,right}_{aim,fire,mfire,reload}_b/t", "trotter_turbo_land",
              "trotter_turbo_skid"],
    "weapon": ["trotter_aim_b/t", "trotter_fire_rr_b/t", "trotter_fire_sg_b/t", "trotter_reload_b/t",
               "trotter_wchange_b/t", "trotter_throw", "trotter_run_throw", "trotter_windup"],
    "hit": ["trotter_hit_small", "trotter_hit_med"],
    "footsteps": ["fol_rome_walk_1..4", "fol_rome_run_1..4"],
}


def num(x):
    """A number as the doc strings print it (40.0 -> 40, 0.3 -> 0.3)."""
    return f"{x:g}"


class Player:
    def __init__(self, level):
        self.lv = level
        self.p = level.template_props("trotter")
        self.g = dict(level.pset("p_globalsettings"))
        gs = level.first("global_settings")
        if gs:
            self.g.update({k.lower(): v for k, v in gs["props"].items()})
        self.mission_inst = level.first("mission")
        self.m = level.resolved(self.mission_inst) if self.mission_inst else dict(level.pset("p_mission"))

    def f(self, key):
        """A p_trotter float with the code default."""
        return float(self.p.get(key, DEFAULTS[key]))

    def raw(self, key, default):
        return self.p.get(key, default)

    # ------------------------------------------------------------------ parts
    def spawn(self):
        lv = self.lv
        out = {}
        me = lv.first("trotter")
        if me:
            out["player"] = {"template": "trotter", "instance": me["id"], "pos": me["pos"],
                             "yaw_matrix_row0": me["transform"][0][:3]}
        for key, (tpl, _) in COMPANIONS.items():
            c = lv.first(tpl)
            if c:
                out[key] = {"template": tpl, "instance": c["id"], "pos": c["pos"]}
        out["force_ground"] = "PlayerObject_Load 0x00473da0 calls Object_ForceGround 0x00455860(player, 1000.0, 10000.0)"
        return out

    def model(self):
        t = self.lv.templates.get("trotter", {})
        return {"template": "trotter", "model": t.get("model"), "lods": t.get("lods"), "collision": t.get("collision"),
                "collision_radius": self.p.get("collision_radius"),
                "skeleton_anim": "Trotter_ref",   # PlayerObject_Load plays "Trotter_ref" (0x0059f8dc, game_05.cpp:565)
                "material": self.p.get("material"), "fx_death": self.p.get("fx_death")}

    def health(self):
        f = self.f
        table = exe_f32(0x00589188, 20)
        rows = [[plain_number(x) for x in table[i * 5:i * 5 + 5]] for i in range(4)]
        pickups = {}
        for name in HEALTH_PICKUPS:
            ps = self.lv.property_set(name)
            if ps:
                pickups[name] = ps
        return {
            "max_health": self.p.get("max_health"),
            "difficulty_scale_note": "PlayerObject_Load: +0x458 = +0x454 = g_game+0x554 * +0x458 (health scale, guess)",
            "damage_flash_duration": self.g.get("damage_flash_duration"),
            "landing_damage": {"min_hurt": f("landing_damage_min_hurt"), "max_hurt": f("landing_damage_max_hurt"),
                               "min_speed": f("landing_damage_min_speed"), "max_speed": f("landing_damage_max_speed"),
                               "turbo_min_hurt": f("landing_damage_min_hurt_turbo"),
                               "turbo_max_hurt": f("landing_damage_max_hurt_turbo"),
                               "turbo_min_speed": f("landing_damage_min_speed_turbo"),
                               "turbo_max_speed": f("landing_damage_max_speed_turbo")},
            "death_falling": {"time": f("death_falling_time"), "distance": f("death_falling_distance"),
                              "turbo_time": f("turbo_death_falling_time"),
                              "turbo_distance": f("turbo_death_falling_distance")},
            "health_pickups": pickups,
            "death": "health <= 0 -> ragdoll + fx_trotter_death, 5 s timer, then level state 3 = mission failed "
                     "(0x0044a580(Game+0xa64, 3), game_06.cpp:2154-2168). No checkpoint/respawn found.",
            "difficulty_table_0x00589188": {"_cols": "player health, enemy health, health pickups, unknown, unknown",
                                            "easy": rows[0], "normal": rows[1], "hard": rows[2], "index3": rows[3]},
            "low_health_cue": "below 20% health, every 1 s (game_06.cpp:2144)",
            "hit": "health -= damage (halved when Game+0xac8 set), hit side to Game+0x26d0 for ui_hit_* (0x0047afa0)",
        }

    def turbo_glide_anti_gravity(self):
        if "turbo_glide_anti_gravity" in self.p:
            return float(self.p["turbo_glide_anti_gravity"])
        return self.f("turbo_anti_gravity") * HALF * 0.25

    def movement(self):
        f = self.f
        friction_from_code = [k for k in ("friction_min", "friction_max", "friction_exponent") if k not in self.p]
        out = {k: f(k) for k in ("fwd_max_speed", "side_max_speed", "fwd_max_speed_min", "side_max_speed_min",
                                 "movement_power_min", "movement_power_max", "friction_min", "friction_max",
                                 "friction_exponent")}
        if friction_from_code:
            out["friction_note"] = ("friction_* not in p_trotter: code defaults (game_05.cpp PlayerObject_Load)"
                                    if len(friction_from_code) == 3 else
                                    f"{', '.join(friction_from_code)} not in p_trotter: code defaults "
                                    "(game_05.cpp PlayerObject_Load)")
        out["roll_factor"] = f("roll_factor")
        out["jump_impulse"] = f("jump_impulse")
        out["jump_anti_gravity_raw"] = f("jump_anti_gravity")
        out["jump_anti_gravity_effective"] = f("jump_anti_gravity") * HALF
        for k in ("jump_fwd_max_speed", "jump_side_max_speed", "jump_movement_power", "fall_speed", "fall_speed_jump",
                  "fall_speed_turbo", "fall_speed_turbo_glided", "fall_land_hard_time", "land_speed",
                  "land_speed_jump", "land_speed_turbo", "landing_power_min", "landing_power_max",
                  "landing_jump_power_min", "landing_jump_power_max", "landing_turbo_power_min",
                  "landing_turbo_power_max"):
            out[k] = f(k)
        glide_ag = self.turbo_glide_anti_gravity()
        out["turbo"] = {
            "fwd_max_speed": f("turbo_fwd_max_speed"), "side_max_speed": f("turbo_side_max_speed"),
            "force": f("turbo_force"), "boost_force": f("turbo_boost_force"), "time": f("turbo_time"),
            "anti_gravity_raw": f("turbo_anti_gravity"), "anti_gravity_effective": f("turbo_anti_gravity") * HALF,
            "movement_power": f("turbo_movement_power"), "glide_zvel": f("turbo_glide_zvel"),
            "glide_xyvel": f("turbo_glide_xyvel"), "glide_fwd_max_speed": f("turbo_glide_fwd_max_speed"),
            "glide_bkd_max_speed": f("turbo_glide_bkd_max_speed"),
            "glide_side_max_speed": f("turbo_glide_side_max_speed"), "glide_power_min": f("turbo_glide_power_min"),
            "glide_power_max": f("turbo_glide_power_max"), "glide_anti_gravity_raw": glide_ag,
            "glide_anti_gravity_effective": glide_ag * HALF}
        per_weapon = {}
        for w in ZOOM_WEAPONS:
            tk, pk = f"{w}_turn_max_speed", f"{w}_pitch_max_speed"
            if tk in self.p or tk in DEFAULTS:
                per_weapon[w] = [float(self.p.get(tk, DEFAULTS.get(tk))), float(self.p.get(pk, DEFAULTS.get(pk)))]
        out["turn"] = {"turn_max_speed": f("turn_max_speed"), "pitch_max_speed": f("pitch_max_speed"),
                       "per_weapon": per_weapon,
                       "per_weapon_note": "[turn, pitch] used while zoomed (mode +0xacc), 0x00475990 / 0x004759e0"}
        for k in ("turn_power_min", "turn_power_max", "targeting_exponent", "targeting_bias", "control_exponent",
                  "control_bias", "zoom_targeting_exponent", "zoom_targeting_bias", "zoom_control_exponent",
                  "zoom_control_bias", "targeting_control_drag", "zoom_targeting_control_drag"):
            out["turn"][k] = f(k)
        out["turn"]["normal_twist_limit_deg"] = f("normal_twist_limit")
        out["turn"]["running_twist_limit_deg"] = f("running_twist_limit")
        tables = {}
        for name, d in ANIM_TABLE_DEFAULTS.items():
            keys = ["min_speed", "max_speed", "low_factor", "low_speed", "high_factor", "high_speed"]
            tables[name] = {k.replace("_speed", "") if k in ("min_speed", "max_speed") else k:
                            self.raw(f"anim_{name}_{k}", d[i]) for i, k in enumerate(keys)}
        tables["walk_run_switch"] = f("anim_walk_run_switch")
        out["anim_speed_tables"] = tables
        out["formulas"] = self.formulas()
        return out

    def formulas(self):
        f = self.f
        ag = f("jump_anti_gravity") * HALF
        g_net = round(-GRAVITY * ag, 2)
        g_turbo = round(-GRAVITY * f("turbo_anti_gravity") * HALF, 2)
        g_glide = round(-GRAVITY * self.turbo_glide_anti_gravity() * HALF, 2)
        imp = f("jump_impulse")
        apex = imp * imp / (2 * -g_net)
        t_apex = imp / -g_net
        fwd, sw = f("fwd_max_speed"), f("anim_walk_run_switch")
        jetpack = self.m.get("player_has_jetpack", 0)
        jet = (f"p_mission player_has_jetpack = 0, so no turbo in {self.lv.name}" if not jetpack else
               f"p_mission player_has_jetpack = {jetpack}: the jet pack is available in {self.lv.name}")
        return {
            "gravity_net": "net vertical accel = -64.348 * A, A = effective anti_gravity of the current state (glide "
                           "flag 4: turbo_glide_anti_gravity*0.5; turbo flag 2: turbo_anti_gravity*0.5; else "
                           "jump_anti_gravity*0.5); applied every frame as an upward force mass*(64.348 - 64.348*A) on "
                           "top of world gravity (PlayerObject_Update 0x004788a0, game_06.cpp:3079-3093)",
            f"gravity_net_{self.lv.name}": {"normal_and_jump": g_net, "turbo": g_turbo, "glide": g_glide},
            "jump": f"impulse mass*jump_impulse (dv = {num(imp)} up) via Body_ApplyImpulse 0x00458440 once flags "
                    "0x40|0x80 are set; 0x80 at once if |v.xy| > 20, else when trotter_jump_into finishes (0x0047aaf0, "
                    f"game_06.cpp:359-476). Derived: apex {num(imp)}^2/(2*{-g_net:.2f}) = {apex:.1f} units, time to "
                    f"apex {t_apex:.3f} s",
            "target_velocity": "per axis: v = max_speed*x + (1-x)*max_speed_min (sign follows x); local forward = -Y "
                               "(game_06.cpp:2586-2630)",
            "stick_shaping": "stick clamped to length 1; magnitude -> 0x00414c00(control_exponent, control_bias) "
                             "(guess: pow(m+bias,exp)-pow(bias,exp)); if > 0.225 add 0.4, cap 1 (game_06.cpp:2303-2488)",
            "drive_force": "power = lerp(movement_power_min, max, stick magnitude); force ~ mass*power*(v_target - "
                           "v_current), horizontal, capped at mass*power*max_speed (guess, moderately confident; "
                           "0x00471810 / 0x00455a30); in jump/fall states v_current is treated as 0",
            "yaw": "facing -= turn_axis * turn_max_speed * dt * 10 (rad); zoomed: weapon turn speed; times "
                   "targeting_control_drag when g_game+0xa6c byte +0x3ac (game_06.cpp:2757)",
            "pitch": "camera pitch += look_axis * pitch_max_speed * dt, clamp [-1.5608, 1.15] (Camera_Update 0x0040e350)",
            "walk_run": f"run anim when |v.xy| > fwd_max_speed * anim_walk_run_switch ({num(fwd)}*{num(sw)} = "
                        f"{num(round(fwd * sw, 6))}) (game_05.cpp:1905-1913)",
            "anim_rate": "rate = lerp(low_factor, high_factor, (clamp(v.dir, min, max) - low_speed)/(high_speed - "
                         "low_speed)) (0x00409d90)",
            "ground": "grounded when a contact normal has z > 0.7 (0x00478200)",
            "fall": f"falling (fall anims) when vz < fall_speed ({num(f('fall_speed'))}; {num(f('fall_speed_jump'))} "
                    f"after a jump; {num(f('fall_speed_turbo'))} with the jet pack)",
            "landing_damage": "t = (-min_vz - landing_damage_min_speed) / landing_damage_max_speed (divides by max, "
                              "original quirk); if t > 0: damage = lerp(min_hurt, max_hurt, min(t,1)) (0x00478100)",
            "landing_anim": "trotter_land_hard when fall time > fall_land_hard_time "
                            f"({num(f('fall_land_hard_time'))}) else trotter_jump_land; turbo: Trotter_turbo_skid when "
                            "|v.xy| > land_speed limit else Trotter_turbo_land",
            "instant_death": "z < -1000, or drop from last ground z > death_falling_distance "
                             f"({num(f('death_falling_distance'))}), or fall time > death_falling_time "
                             f"({f('death_falling_time'):.1f} s); turbo variants "
                             f"{num(f('turbo_death_falling_distance'))} / {f('turbo_death_falling_time'):.1f} s",
            "turbo": "jet pack only (object +0xabc, pickup_jumper_pack): hold TurboJump grounded -> force "
                     "mass*turbo_force (mass*turbo_boost_force with the boost pickup) for turbo_time; press again while "
                     f"vz < 0 -> glide. {jet}",
            "knockback": "hit impulse direction*impulse*10, capped 4000 (8000 with flag 0x40000), z*0.4 "
                         "(ai_07.cpp:3842)",
        }

    def camera(self, anim):
        nodes, kinds = {}, set()
        if anim is not None:
            cams = {n.name: n for n in anim.nodes if n.kind == 1}
            order = [n for n in CAMERA_ORDER if n in cams] + sorted(n for n in cams if n not in CAMERA_ORDER)
            for name in order:
                n = cams[name]
                pos = anim.world(n, anim.start)[:3, 3]
                tgt = anim.world(n.target, anim.start)[:3, 3] if n.target else None
                kinds.add(n.fov_kind)
                nodes[name] = {"pos": round_vec(pos, 3), "target": round_vec(tgt, 3) if tgt is not None else None,
                               "fov": round(n.fov, 7), "near": n.near, "far": n.far}
        fov_kind = ("node word +0x98 = 0 for all: horizontal FOV, converted to vertical with aspect 1.702128 (camera "
                    "+0x154): v = 2*atan(tan(h/2)/1.702128)" if kinds == {0} else
                    f"node word +0x98 values {sorted(kinds)}: 0 = horizontal FOV, converted to vertical with aspect "
                    "1.702128 (camera +0x154): v = 2*atan(tan(h/2)/1.702128); 1 = vertical (0x004eb710)")
        zoom_node = "Camera_rifle" if self.loadout_has("rifle") else "Camera_machinegun"
        return {
            "_note": "Follow camera = Camera_Update 0x0040e350 with camera name 'Camera_start' and look-at name "
                     "'Camera_end' (set by 0x004757b0). Node rest transforms from anim Trotter_ref "
                     f"({self.lv.name}_big03_anim), model space of the player: X right, -Y forward (guess, the targets "
                     "lie at -Y), Z up. Which of the two nodes is the eye is a guess (see player.md).",
            "nodes": nodes,
            "fov_kind": fov_kind,
            "pitch_clamp": [-1.5608, 1.15],
            "pitch_rate": f"pitch_max_speed ({num(self.f('pitch_max_speed'))}) * targeting_control_drag (when "
                          "g_game+0xa6c byte +0x3ac), times dt; zoomed: weapon pitch speed",
            "collision": "five rays from the eye (towards the look point and +-5 along the look matrix axes) shorten "
                         "the distance to the nearest hit; occluding objects fade out",
            "zoom": {"camera_zoom_secs": self.g.get("camera_zoom_secs", 0.5),
                     "mode": "aim/zoom flag +0xacc swaps the camera node to the current weapon's node "
                             f"({zoom_node} in {self.lv.name})"},
            "letterbox": {"y": 0.1125, "h": 0.775},
            "default_vertical_fov_ctor": 1.0471976,
            "fade_rates": {"camera_fade_in_rate": self.g.get("camera_fade_in_rate"),
                           "camera_fade_out_rate": self.g.get("camera_fade_out_rate")},
        }

    def loadout_has(self, w):
        return bool(self.m.get(f"player_add_{w}", 0)) and not self.m.get(f"player_remove_{w}", 0)

    def weapons(self):
        lv, g = self.lv, self.g
        mi = self.mission_inst
        adds = {k: v for k, v in (mi["props"].items() if mi else []) if k.lower().startswith(("player_add_",
                                                                                                "player_remove_"))}
        sticky_src = ("mission instance" if mi and "player_set_num_stickybomb" in {k.lower() for k in mi["props"]}
                      else "p_mission")
        evidence = (f"mission instance {mi['id']} overrides " + ", ".join(f"{k} {v}" for k, v in adds.items()) + "; "
                    if mi and adds else "p_mission defaults; ")
        evidence += (f"{sticky_src} player_set_num_stickybomb {self.m.get('player_set_num_stickybomb')}; "
                     "player_set_ammo_* -2 (guess: default/full)")
        loadout = {"rifle": self.loadout_has("rifle"), "machinegun": self.loadout_has("machinegun")}
        for w in ("snipergun", "gurner", "sharkgun", "vindaloo"):
            if self.loadout_has(w):
                loadout[w] = True
        loadout["sticky_bombs"] = self.m.get("player_set_num_stickybomb")
        loadout["evidence"] = evidence

        def ps(template):
            return lv.template_props(template)

        def pick(d, keys):
            return {k: d[k] for k in keys if k in d}

        rifle_p, rb = ps("player_rifle"), ps(ps("player_rifle").get("projectile_name", "player_rifle_bullet"))
        rifle = {"template": "player_rifle", **pick(rifle_p, ["behavior", "recycle_time", "clip_reload_time",
                                                               "two_handed"]),
                 "muzzle_fx": rifle_p.get("muzzle_fx_name"), "camera_node": "Camera_rifle",
                 "max_clip": g.get("rifle_max_clip"), "max_ammo": g.get("rifle_max_ammo"), "range": g.get("rifle_range"),
                 "projectile": {"template": rifle_p.get("projectile_name"),
                                **pick(rb, ["behavior", "speed", "lifetime", "damage", "head_shot_multiplier",
                                            "impact_force", "allow_cartoon_death", "fx_start", "fx_end_material"])},
                 # the rifle's sounds (code: 0x004a2450 fire, 0x004a2220 load; dry fire 0x004a23b0)
                 "sounds": ["wpn_rome_rifle_fire", "wpn_rome_rifle_load", "wpn_rifle_dry_fire_1", "wpn_rifle_dry_fire_2"]}
        mg_p = ps("player_machinegun")
        mb = ps(mg_p.get("projectile_name", "player_machinegun_bullet"))
        machinegun = {"template": "player_machinegun", **pick(mg_p, ["behavior", "recycle_time", "clip_reload_time",
                                                                     "rapid_fire"]),
                      "max_clip": g.get("machinegun_max_clip"), "max_ammo": g.get("machinegun_max_ammo"),
                      "spread_pct": g.get("machinegun_spread_pct"), "range": g.get("machinegun_range"),
                      "camera_node": "Camera_machinegun",
                      "projectile": {"template": mg_p.get("projectile_name"),
                                     **pick(mb, ["behavior", "speed", "lifetime", "damage", "head_shot_multiplier",
                                                 "impact_force"])}}
        sb = ps("player_sticky_bomb")
        sticky = {"template": "player_sticky_bomb",
                  **pick(sb, ["behavior", "launch_angle_degrees", "speed", "lifetime", "linger_lifetime", "damage",
                              "area_damage", "damage_radius_inner", "damage_radius_outer", "impact_force",
                              "damages_buildings"]),
                  "max_ammo": g.get("stickybomb_max_ammo"), "range": g.get("sticky_bomb_range")}
        tt = ps("player_topsyturvy")
        placed = [i["id"] for i in lv.all("pickup_topsy_weapon")]
        if placed:
            where = f"pickup placed (pickup_topsy_weapon instances {', '.join(map(str, placed))})"
        elif "pickup_topsy_weapon" in lv.templates:
            where = "pickup only (pickup_topsy_weapon template exists; no placed instance)"
        else:
            where = "not in the level's banks (no pickup_topsy_weapon template)"
        topsy = {**pick(tt, ["behavior", "recycle_time"]), "duration": g.get("topsy_turvy_duration"),
                 "radius": g.get("topsy_turvy_radius"), "max_ammo": g.get("topsyturvy_max_ammo"),
                 f"in_{lv.name}": where}
        speed3 = rb.get("speed", 0) * 3
        return {
            f"loadout_{lv.name}": loadout,
            "rifle": rifle, "machinegun": machinegun, "sticky_bomb": sticky, "topsyturvy": topsy,
            "punch": {"anims": ["trotter_punch_1", "trotter_punch_2", "trotter_punch_3"],
                      **pick(g, ["bof_hit_force_factor", "bof_speed_factor", "bof_gravity_factor"])},
            "autoaim": {k.replace("autoaim_", ""): g[k] for k in (
                "autoaim_factor_horz", "autoaim_factor_vert", "autoaim_base_factor_horz", "autoaim_base_factor_vert",
                "autoaim_multitarget_factor_horz", "autoaim_multitarget_factor_vert", "autoaim_sticky_factor_horz",
                "autoaim_sticky_factor_vert") if k in g},
            "weapon_upgrade_duration": g.get("weapon_upgrade_duration"),
            "projectile_model": "not hitscan: Projectile_Load 0x0047e0c0 launch speed = speed*3 (rifle bullet "
                                f"{num(speed3)} units/s); segment ray-cast each frame (0x0047e580); head/neck bone x "
                                "head_shot_multiplier, chest/belly x torso_shot_multiplier (0x0047eec0)",
            "fire_gate": "ready when never fired or time - lastFire > recycle_time (0x004a1e40); one clip round per shot "
                         "(0x004a2450)",
            "autoaim_note": "auto-aim off when zoomed or difficulty > 1, else option byte Game+0x4c2; factors scale "
                            "target-picking frustum planes (0x00408390)",
        }

    def companions(self, anim):
        out = {}
        for key, (tpl, weapons) in COMPANIONS.items():
            if not self.lv.first(tpl):
                continue
            p = self.lv.template_props(tpl)
            c = {"template": tpl, "property": self.lv.property_set(tpl), "max_health": p.get("max_health"),
                 "formation_angle_deg": p.get("formation_angle"), "formation_distance": p.get("formation_distance"),
                 "delay_distance": p.get("delay_distance"), "scan_distance": p.get("scan_distance"),
                 "move_speed": p.get("move_speed"), "move_power": p.get("move_power")}
            if p.get("tag"):
                c["tag"] = p["tag"]
            c.update(weapons)
            c["fire"] = {"rounds": [p.get("fire_num_rounds_min"), p.get("fire_num_rounds_max")],
                         "bursts": [p.get("fire_num_bursts_min"), p.get("fire_num_bursts_max")],
                         "burst_delay": [p.get("fire_burst_delay_min"), p.get("fire_burst_delay_max")],
                         "clip_delay": [p.get("fire_clip_delay_min"), p.get("fire_clip_delay_max")],
                         "range": [p.get("fire_range_min"), p.get("fire_range_max")],
                         "inaccuracy_height": p.get("fire_inaccuracy_height"),
                         "inaccuracy_xy": p.get("fire_inaccuracy_xy"),
                         "accuracy": [p.get("fire_accuracy_min"), p.get("fire_accuracy_max")]}
            ref = f"ref_{tpl if tpl != 'q111' else 'q'}_00"
            node = anim.by_name.get(ref) if anim is not None else None
            if node is not None:
                c["player_ref_node"] = {"name": node.name, "pos": round_vec(anim.world(node, anim.start)[:3, 3], 3)}
            out[key] = c
        out["teammate_command_fx"] = self.g.get("teammate_command_fx")
        out["follow"] = ("formation point = anchor (player bones ref_q_00 / ref_molmer_00 in follow mode) + reversed "
                         "heading rotated by +-formation_angle, scaled by formation_distance (0x00483c30); no move while "
                         "the player is within delay_distance (0x00483810); path over the nav mesh (0x00483d60). Code "
                         "defaults: delay 50, defend_min_distance 100, formation_distance 30, angle 20 (0x00483250, "
                         "ai_08.cpp:192)")
        out["orders"] = ("TeammatesSend (1): attack the object under the crosshair or defend that point; TeammatesReturn "
                         "(3): return to the player (0x0047a9a0 -> 0x004852b0)")
        return out

    def build(self):
        anims = self.lv.anim_bank()
        anim = anims.get("trotter_ref")
        return {
            "_doc": f"Player (Roman = template 'trotter') numbers for the remaster port of {self.lv.name}. Evidence and "
                    "labels in docs/subsystems/player.md. Units: game units (feet, guess), seconds, radians unless "
                    "noted; Z up. Values are the retail p_trotter / p_globalsettings / p_mission data "
                    f"({self.lv.name}_big06_prop) unless 'code_default' says otherwise.",
            "world": {"gravity": [0.0, 0.0, -GRAVITY],
                      "gravity_note": "Havok world cinfo set by the game (docs/subsystems/havok-layouts.md); 2 x 32.174 "
                                      "ft/s^2",
                      "half_constant_0x005bcb4c": HALF},
            "spawn": self.spawn(),
            "model": self.model(),
            "health": self.health(),
            "movement": self.movement(),
            "bindings": BINDINGS,
            "camera": self.camera(anim),
            "weapons": self.weapons(),
            "companions": self.companions(anim),
            "animations": ANIMATIONS,
        }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out", help="level folder (default work/remaster/<level>)")
    a = ap.parse_args()
    out = pathlib.Path(a.out) if a.out else WORK / a.level
    lv = Level(a.level, out)
    if not lv.template_props("trotter"):
        raise SystemExit(f"{a.level}: no p_trotter property set (template trotter)")
    data = Player(lv).build()
    (out / "player.json").write_text(json.dumps(data, indent=1))
    cam = data["camera"]["nodes"]
    print(f"player.json: {len(cam)} camera nodes, companions {[k for k in data['companions'] if k in COMPANIONS]}, "
          f"spawn instance {data['spawn'].get('player', {}).get('instance')}")


if __name__ == "__main__":
    main()
