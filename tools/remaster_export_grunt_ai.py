"""Export one level's enemy AI parameters (grunts, captains and every other AI template of the level) for the
remaster port: the reference the engine's AI (engine/src/ai.rs, alarm.rs) is checked against.

  python tools/remaster_export_grunt_ai.py lev_1_m1     -> work/remaster/lev_1_m1/grunt_ai.json

Needs tools/remaster_export.py LEVEL (properties.json, templates.json) first; reads the instance bank (big10_inst)
and game.unpacked.exe (read only: which property keys the exe has a string for). State machine and evidence:
docs/subsystems/grunt-ai.md.

Output (game data: lives under work/, never committed):
  constants          code constants of the AI (not data)
  templates          per AI template placed or spawned in the level (alarm generators, barracks): its property set,
                     model, weapon, and
                       params                   the values Ai_InitGeneric 0x004247f0 reads (archive/src/ai/ai_01.cpp:2289-2590;
                                                fire range 0x00425d30), the template's value or the code default
                       data_only_unread_by_exe  keys of the set the exe has no string for (never read)
                       extra                    the set's other keys the exe reads elsewhere (physics, health, model)
  weapons            the weapons' and their projectiles' property sets
  instances          every placed AI instance with its own overrides (they apply on top of params)
  markers, alarm_generators, alarm_objects    the AI's markers, the alarm troop generators (resolved sets) and the
                     alarm / mounted-gun objects
  mission, globals   the mission's alarm settings and overrides, the global settings the AI uses
"""
import argparse
import json
import pathlib

from remaster_level import WORK, Level, exe_reads_key

# Ai_InitGeneric 0x004247f0 (ai_01.cpp:2347-2590) and 0x00425d30: the parameters in the port's order, code default.
# A string default is an expression of other parameters (the code computes it from the resolved values).
PARAMS = [
    ("bof_anti_gravity", 0.5), ("stig_damage_multiplier", 1.0), ("forge_damage_multiplier", 1.0),
    ("can_be_sharked", 1), ("jumper_captain", 0),
    ("fire_num_clips_min", 1), ("fire_num_clips_max", 1), ("fire_clip_delay_min", 0.0), ("fire_clip_delay_max", 0.0),
    ("fire_num_rounds_min", 1), ("fire_num_rounds_max", 1), ("fire_num_bursts_min", 1), ("fire_num_bursts_max", 1),
    ("fire_burst_delay_min", 0.0), ("fire_burst_delay_max", 0.0), ("fire_inaccuracy_height", 10.0),
    ("fire_inaccuracy_angle", 15.0), ("fire_accuracy_min", 50.0), ("fire_accuracy_max", 150.0),
    ("fire_infinite_accuracy_min", 400.0), ("fire_infinite_accuracy_max", 800.0),
    ("target_priority_tag_min", 0), ("target_priority_tag_max", 0), ("send_trigger_on_death", 0),
    ("receive_trigger_kill", 0), ("alert_group", 0), ("scan_distance", 300.0), ("hear_distance", 20.0),
    ("allfours_pct", 20.0), ("long_hit_pct", 60.0), ("hit_level_1", 10.0), ("hit_level_2", 25.0),
    ("hit_ragdoll", 50.0), ("ledge_death", 0), ("move_power", 30.0), ("move_speed", 46.0),
    ("limp_speed", "move_speed*0.35"), ("limp_pct", 0), ("patrol_speed", "move_speed*0.25"),
    ("anim_run_min_speed", 0.0), ("anim_run_max_speed", 100.0), ("anim_run_low_factor", 0.0),
    ("anim_run_low_speed", 0.0), ("anim_run_high_factor", 1.0), ("anim_run_high_speed", 17.0),
    ("anim_limp_min_speed", 0.0), ("anim_limp_max_speed", 100.0), ("anim_limp_low_factor", 0.0),
    ("anim_limp_low_speed", 0.0), ("anim_limp_high_factor", 1.0), ("anim_limp_high_speed", 17.0),
    ("pickup_drop_specific", ""), ("pickup_drop_percent", 100.0), ("pickup_grab_dist", 100.0),
    ("mode", "attention_roaming"), ("score_value", 0.0), ("fire_range_min", 200.0), ("fire_range_max", 350.0),
    ("trigger_los", 1), ("receive_trigger_activate", 0), ("marker_tag_min", 0), ("marker_tag_max", 0),
    ("marker_looping", 0), ("marker_patrol", 0), ("marker_startloc", 0), ("marker_strict_patrol", 0),
    ("obj_tag", 0), ("generic_txt", "none"),
]
PARAM_KEYS = {k for k, _ in PARAMS}
# keys of the set left out of "extra": per-LOD distances, shadows, and the stig / forge / rocket fire variants (read
# by their own code paths; their data-only typos still show under data_only_unread_by_exe)
NOT_EXTRA = ("lodcutoff_", "shadow_", "stig_", "forge_", "rkt_")
# the player's companions are AI too (p_q has a mode) but are the player's side: player.json has them
COMPANIONS = {"q111", "molmer"}
# the weapon template each class holds (0x004a0ef0's index -> the weapon table, game_01.cpp:600; engine/src/ai.rs
# Kind::weapon) and the captain's thrown grenade (its close attack, vt+0x1e0)
WEAPON = {"grunt_regular": "grunt_rifle", "grunt": "grunt_bow", "captain": "captain_mg"}
# guess: by name only (an aiweapon template named after the class); the classes' weapon index is not traced
WEAPON_GUESS = {"captain_sniper": "captain_snipergun", "monk": "monk_gun", "q112": "q112_chaingun"}
EXTRA_WEAPONS = {"captain": ["captain_grenade"]}
# alarm generator / barracks troop counts -> template (the controller's type order, 0x00404e50; num_rocket_captain
# has no type: the original's bug, docs/subsystems/mission.md)
TROOPS = {"num_bow": "grunt", "num_regular": "grunt_regular", "num_jumper_captain": "captain",
          "num_captain": "captain", "num_monk": "monk"}
ALARM_OBJECTS = ("stig_mgun", "guard_alarm")   # the mounted gun grunts man (0x0042a100) and the alarm (0x0044ba30)
GLOBALS = ["grunt_density", "grunt_friction", "grunt_restitution", "bof_speed_factor", "bof_gravity_factor",
           "bof_hit_force_factor", "bof_up_force_punch_1", "bof_up_force_punch_2", "bof_up_force_punch_3",
           "music_nearby_range", "music_agro_range"]

CONSTANTS = {
    "fire_range_pick": "per grunt: engage range +0x5d8 = fire_range_min + rand()/32768*(fire_range_max-fire_range_min) "
                       "(0x00425d30)",
    "second_voice_chance": 0.5,
    "target_search_period_s": {"normal": [0.75, 1.25], "alt (0x0044e600 true)": [2.0, 4.0]},
    "facing_test_deg": {"hear_distance==0": 180.0, "otherwise": 190.0},
    "window_tag_range_scale": 1.5,
    "alert_group_delay_s": [0.25, 1.0],
    "hurt_voice_min_interval_s": 4.0,
}


def is_ai(level, template):
    """A soldier AI template: its set has a mode and the soldier fire / hit parameters (grunt, grunt_regular,
    captain, captain_sniper, monk, q112). The other templates with a mode are vehicles, squads and ic objects
    (saucer, zeppelin, goliath, colossus, squad_*, object_ic, shrubpatrol) with their own classes, not covered here."""
    p = level.template_props(template)
    return (template not in COMPANIONS and "mode" in p and "hit_level_1" in p and "fire_num_clips_min" in p)


def ai_templates(level):
    """AI templates in order of first appearance: placed, or spawned by a generator / barracks with a count > 0."""
    order = []

    def add(t):
        if t and t not in order and is_ai(level, t):
            order.append(t)

    for inst in level.instances:
        add(inst["template"])
        r = level.resolved(inst)
        for key, t in TROOPS.items():
            if isinstance(r.get(key), (int, float)) and r[key] > 0 and t in level.templates:
                add(t)
    return order


def template_entry(level, name):
    p = level.template_props(name)
    params = {}
    for key, default in PARAMS:
        params[key] = p.get(key, default)
    data_only, extra = {}, {}
    # the plain keys first, then the variant families (stig_ / forge_ / rkt_ ...)
    for key, v in sorted(p.items(), key=lambda kv: kv[0].startswith(NOT_EXTRA)):
        if not exe_reads_key(key):
            data_only[key] = v
        elif key not in PARAM_KEYS and not key.startswith(NOT_EXTRA):
            extra[key] = v
    entry = {"property": level.property_set(name), "model": level.templates[name].get("model"),
             "weapon": weapon_of(level, name)}
    if name not in WEAPON:
        entry["weapon_note"] = ("guess: the aiweapon template named after the class; its weapon index is not traced"
                                if entry["weapon"] else "not traced")
    entry.update({"params": params, "data_only_unread_by_exe": data_only, "extra": extra})
    return entry


def weapon_of(level, template):
    if template in WEAPON:
        return WEAPON[template]
    w = WEAPON_GUESS.get(template)
    return w if w in level.templates else None


def weapon_sets(level, templates):
    out = {}
    for t in templates:
        for w in [x for x in [weapon_of(level, t)] if x] + EXTRA_WEAPONS.get(t, []):
            ps = level.property_set(w)
            if not ps:
                continue
            out[ps] = level.pset(ps)
            proj = level.pset(ps).get("projectile_name")
            pps = level.property_set(proj) if proj else None
            if pps:
                out[pps] = level.pset(pps)
    return out


def build(level):
    templates = ai_templates(level)
    mission = level.first("mission")
    m = level.resolved(mission) if mission else level.pset("p_mission")
    g = dict(level.pset("p_globalsettings"))
    gs = level.first("global_settings")
    if gs:
        g.update({k.lower(): v for k, v in gs["props"].items()})
    return {
        "_doc": f"Grunt AI parameters for {level.name}. 'params' = effective values: code default (Ai_InitGeneric "
                "0x004247f0, archive/src/ai/ai_01.cpp:2347-2420; fire range 0x00425d30) overridden by the template's property "
                "set; per-instance overrides in 'instances' apply on top. State machine: docs/subsystems/grunt-ai.md.",
        "constants": CONSTANTS,
        "templates": {t: template_entry(level, t) for t in templates},
        "weapons": weapon_sets(level, templates),
        "instances": [{"id": i["id"], "template": i["template"], "pos": i["pos"], "overrides": i["props"]}
                      for i in level.instances if i["template"] in templates],
        "markers": [{"id": i["id"], "pos": i["pos"], "props": i["props"]} for i in level.all("marker")],
        "alarm_generators": [{"id": i["id"], "pos": i["pos"], "props": level.resolved(i)} for i in level.instances
                             if level.template_props(i["template"]).get("behavior") == "alarm_generator"],
        "alarm_objects": [{"id": i["id"], "template": i["template"], "pos": i["pos"], "props": i["props"]}
                          for i in level.instances if i["template"] in ALARM_OBJECTS],
        "mission": {**{k: m.get(k) for k in ("alarm_generator_max_troops", "alarm_min_delay", "alarm_max_delay")},
                    "alarm_ai_max_distance": g.get("alarm_ai_max_distance"),
                    "overrides": mission["props"] if mission else {}},
        "globals": {k: g[k] for k in GLOBALS if k in g},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out", help="level folder (default work/remaster/<level>)")
    a = ap.parse_args()
    out = pathlib.Path(a.out) if a.out else WORK / a.level
    data = build(Level(a.level, out))
    (out / "grunt_ai.json").write_text(json.dumps(data, indent=1))
    print(f"grunt_ai.json: templates {list(data['templates'])}, {len(data['instances'])} placed, "
          f"{len(data['markers'])} markers, {len(data['alarm_generators'])} alarm generators")


if __name__ == "__main__":
    main()
