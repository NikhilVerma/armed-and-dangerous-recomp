"""Export one level's mission logic from the retail `.big` files for the remaster engine (engine/).

  python tools/remaster_export_mission.py lev_1_m1      -> work/remaster/lev_1_m1/mission.json

The original has no mission script: a level's flow is data in its instance bank (big10_inst), run by the object
classes of game.exe through numbered triggers (Object_SendTrigger 0x00461870). docs/subsystems/mission.md describes
the model; this tool writes what the data holds and the trigger graph the code implies:

  level          level name, next level, the mission object's resolved settings
  instances      every placed instance (id = 1-based position, as the game numbers them): template, property set,
                 class (the "behavior" property, else the template name: ObjectSystem_CreateObject 0x0045f020),
                 spawn mask, 3x4 transform, position (game space, Z up), the instance's own props, the template's
                 defaults, the resolved props (instance over template) and, for trigger volumes, the volume
  triggers       per trigger id: who sends it (event), who receives it (action), each with evidence
  edges          the same as a flat list: trigger -> condition -> action
  tag_links      non-trigger links by tag (compass objectives, alarm sirens, AI marker paths, bomb parents)
  text           every text id the level's logic uses -> English text (big01_text; lang.dat selects the bank)
  voice          every voice line a sound_ear can play: subtitle text and whether the sound is in big08_aud
  subtitles      the start movie's subtitle track (big12_ui)
  hints          the context prompts ("Press %s to ...") and the input action each one names
  rules          engine rules from the code that the data does not show (start, win, lose), with evidence

Coordinates stay the game's (Z up). Every field that comes from code carries an "evidence" string; guesses say so.
Game data: the output lives under work/ and is never committed.
"""
import argparse
import json
import math
import pathlib
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "formats"))
sys.path.insert(0, str(ROOT / "tools"))
from big_kinds import aud, fx, inst, obj, objchunk, prop, text, ui  # noqa: E402
import remaster_export as rx  # noqa: E402  (read-only reuse: payload(), mesh_submeshes())

# ---------------------------------------------------------------- what the code does with each property

# Classes by behaviour name (ObjectSystem_InitBehaviourTable 0x0045df60 maps the name hash to a class id; the id
# picks the vtable in ObjectSystem_CreateObject 0x0045f020). Names not in the table make a DefaultGameObject.
CLASSES = {
    "mission": ("MissionObject", 136, "vtable 0x005a49c8"),
    "broadcast": ("TriggerObject", 138, "vtable 0x005a7c20"),
    "ui_ear": ("UiEarObject", 150, "vtable 0x005aa750 (class name guess)"),
    "sound_ear": ("SoundEar", 66, "vtable 0x005a2d28"),
    "fx_ear": ("FxEarObject", 149, "vtable 0x005a8f70"),
    "camera_fx": ("CameraFXObject", 151, "vtable 0x005ad058"),
    "camera_path": ("CameraPathObject", 135, "vtable 0x005a0f18"),
    "marker": ("MarkerObject", 75, "vtable 0x005ad720"),
    "pub": ("PubObject", 55, "vtable 0x005a1dc8"),
    "player_tickerbomb": ("PlayerTickerBomb", 52, "vtable 0x005ae380"),
    "pickup_generator": ("PickupGenerator", 51, "vtable 0x005a6258"),
    "pickup_token": ("PickupObject", 42, "vtable 0x005a3c90"),
    "peasant": ("PeasantObject", 53, "vtable 0x005a8550 (a TriggerObject: PeasantObject_Load 0x004056b0)"),
    "building": ("TwigletGenerator/BarricadeObject (barracks)", 139, "vtable 0x005aeeb8"),
    "alarm_generator": ("AlarmGenerator", 140, "vtable 0x005a2058"),
    "global_settings": ("GlobalSettings", 137, ""),
    "ambient": ("AmbientObject", 94, ""),
    "light": ("LightObject", 93, ""),
    "stig_mgun_turret": ("StigMgunTurret", 78, "vtable 0x005a19d8"),
    "grunt": ("GruntObject (bow)", 60, "GenericAIObject family"),
    "grunt_regular": ("GruntObject", 59, "GenericAIObject family"),
    "penguin": ("PenguinObject", 88, ""),
    "q111": ("Q111Object (teammate Q)", 154, ""),
    "molmer": ("MolmerObject (teammate Jonesy)", 153, ""),
    "trotter": ("PlayerObject (the player, Roman)", 161, "class id 161 stores the object in Game+0x748"),
    "fx_explosion": ("FxExplosion (explosive pipe / keg)", 90, "vtable 0x005a4300; die slot 0x0041c570 sends "
                                                               "send_trigger_on_death +0x488"),
    "envmapmarker": ("EnvMapMarker", 92, "behaviour name 'EnvMapMarker'; hash case-insensitivity not checked"),
    # later levels' mission logic (ids from the 0x0045df60 table: archive/src/game/game_03.cpp kBehaviours)
    "counter": ("CounterObject", 132, "vtable 0x005a4b00; Counter_Init 0x00415770"),
    "timer": ("TimerObject", 133, "vtable 0x005a7d58; TimerObject_ReadProps 0x00496160"),
    "army_timer": ("ArmyTimerObject", 134, "vtable 0x005a2800 (derived from TimerObject: same properties)"),
}
DEFAULT_CLASS = ("DefaultGameObject", 0, "vtable 0x005a2bf0 (unknown behaviour name)")

# Senders: (class key or "*", property) -> event. "*" = any class whose loader reads it (DefaultGameObject, AI,
# buildings all read send_trigger_on_death).
SENDS = {
    ("mission", "send_trigger_start"): ("level start (first MissionObject_Update frame)",
                                        "MissionObject_Load 0x0044c170 reads it (+0x460); MissionObject_Update "
                                        "0x0044db80 sends and clears it"),
    ("broadcast", "on_enter"): ("an object of the trigger's collision group enters the volume (player; AI too with "
                                "ai_can_trigger)",
                                "TriggerObject_Load 0x00497460 (+0x460); enter callback 0x004976a0 sets pending; "
                                "update 0x00497640 sends; one_shot clears it after the first send"),
    ("broadcast", "on_exit"): ("leaves the volume", "0x004976a0 sends +0x464 on leave"),
    ("pickup_token", "on_enter"): ("player picks it up (PickupObject derives from TriggerObject)",
                                   "PickupObject_Load 0x00471e40 calls TriggerObject_Load 0x00497460"),
    ("peasant", "on_enter"): ("Roman captures the peasant (his trigger sphere; fewer than three carried); sent once",
                              "PeasantObject_Load 0x004056b0 calls TriggerObject_Load 0x00497460 (+0x460); the contact "
                              "slot 0x00406210 sends and clears it"),
    ("player_tickerbomb", "send_trigger_set"): ("bomb armed (Use held for arm_time within message_radius)",
                                                "PlayerTickerBomb_Load 0x0044de90 (+0x46c); 0x0044e0c0 from arm "
                                                "0x0044e4a0"),
    ("player_tickerbomb", "send_trigger_explode"): ("bomb explodes (6.0 s fuse after arming)",
                                                    "update 0x0044e150 sends +0x478 when Game+0x25e4 reaches 0; "
                                                    "fuse set to 6.0 by 0x0044e4a0"),
    ("player_tickerbomb", "send_trigger_explode2"): ("bomb explodes (second slot, not sent by the update read)",
                                                     "PlayerTickerBomb_Load 0x0044de90 (+0x47c)"),
    ("player_tickerbomb", "send_trigger_explode3"): ("bomb explodes (third slot, not sent by the update read)",
                                                     "PlayerTickerBomb_Load 0x0044de90 (+0x480)"),
    ("player_tickerbomb", "send_trigger_non_enabled"): ("player with ticker bombs stands at this site while it is "
                                                        "not enabled",
                                                        "ticker prompt 0x00467450 -> 0x0044e0e0 sends +0x470"),
    ("pub", "send_trigger_entry"): ("player enters the pub", "PubObject_Load 0x0046a200 (+0x4c0); 0x0046a6f0"),
    ("pub", "send_trigger_entry_failure"): ("player tries a closed pub", "PubObject_Load 0x0046a200 (+0x4bc)"),
    ("pub", "send_trigger_exit"): ("player leaves the pub screen", "PubObject_Load 0x0046a200 (+0x4c4); "
                                                                   "0x0046a720 from 0x004690c0"),
    ("marker", "trigger"): ("an AI reaches this marker", "AI marker arrival 0x0044ba30 sends the marker's trigger"),
    ("counter", "send_trigger"): ("its activate trigger arrived `count` times (with `infinite` the count restarts, "
                                  "else the counter finishes)",
                                  "Counter_Init 0x00415770 (+0x468, count +0x470); CounterObject_OnTrigger 0x00415860 "
                                  "-> Object_SendTrigger 0x00461870"),
    **{(c, "send_trigger_time_N"): ("the timer reached its time (seconds after it started; 'trigger/time' "
                                    "entries sorted by time)",
                                    "TimerObject_ReadProps 0x00496160 parses 'send_trigger_time_%d' (first missing "
                                    "index ends the list; only without anim_name); update 0x00496750 -> "
                                    "TimerObject_Fire 0x00496660") for c in ("timer", "army_timer")},
    **{(c, "send_end_trigger"): ("the timer's animation ended (anim_name set, not looping)",
                                 "TimerObject_ReadProps 0x00496160 (+0x4a4); update 0x00496750")
       for c in ("timer", "army_timer")},
    ("*", "send_trigger_on_death"): ("this object dies / is destroyed",
                                     "DefaultGameObject_OnDeath 0x00450ee0 (+0x4e8); AI 0x00431720 path (+0x528); "
                                     "buildings +0x5a8"),
}
RECEIVES = {
    ("mission", "receive_trigger_success"): ("WIN: mission success (score += score_value, level state 2 -> "
                                             "success_text, Stats, next_level_name)",
                                             "MissionObject_OnTrigger 0x0044d600 (+0x46c) -> 0x0044a580(state 2); "
                                             "end sequence 0x0044a390 / 0x0044a230"),
    ("mission", "receive_trigger_pre_success"): ("pre-success: teammates retreat along the pre-success marker",
                                                 "MissionObject_OnTrigger 0x0044d600 (+0x470)"),
    ("mission", "receive_trigger_failure"): ("LOSE: failure (level state 4, failure_text)",
                                             "MissionObject_OnTrigger 0x0044d600 (+0x474)"),
    ("mission", "receive_trigger_failure_2"): ("LOSE: failure 2 (state 5, failure_text_2)", "0x0044d600 (+0x478)"),
    ("mission", "receive_trigger_failure_3"): ("LOSE: failure 3 (state 6, failure_text_3)", "0x0044d600 (+0x47c)"),
    ("mission", "receive_trigger_failure_4"): ("LOSE: failure 4 (state 7, failure_text_4)", "0x0044d600 (+0x480)"),
    ("mission", "alarm_trigger_activate"): ("alarm forced on (Game+0xdcc = 1)", "0x0044d600 (Game+0x26d8)"),
    ("mission", "alarm_trigger_deactivate"): ("alarm forced off", "0x0044d600 (Game+0x26dc)"),
    ("mission", "army_breach_open_trigger"): ("army breach open (Game+0xcbe = 1)", "0x0044d600 (Game+0xc3c)"),
    ("mission", "army_breach_close_trigger"): ("army breach closed", "0x0044d600 (Game+0xc40)"),
    ("mission", "peasant_saved_trigger"): ("a peasant reached safety", "0x0044d600 (Game+0x26f0)"),
    ("ui_ear", "text_trigger_receive_0N"): ("show text_display_0N as a message (queued 1 s, pauses, 'Press %s to "
                                            "continue'; also stored as the objectives text when an objective "
                                            "matched)",
                                            "0x0046acf0 loads; 0x0046afa0 -> 0x00403410(text, 3 or 0x13)"),
    ("ui_ear", "objective_trigger_receive_0N"): ("compass target = objects tagged objective_tag_0N",
                                                 "0x0046afa0: Game+0x2658 = objective_tag_0N"),
    ("sound_ear", "trigger_id"): ("play one voice line: random among roman_sound (player) and q_sound / "
                                  "jonsey_sound when that teammate is alive (within max_distance preferred); "
                                  "once unless 'reapeat'",
                                  "SoundEar_Slot6 0x00497750 loads; handler 0x00497900"),
    ("fx_ear", "trigger_id"): ("toggle the fx fx_name at this object (random initial delay when set)",
                               "FxEarObject_LoadProperties 0x0041bdd0; FxOwner_OnMessage 0x0041c050"),
    ("camera_fx", "trigger_start"): ("start the camera fx (0 = start at load)",
                                     "CameraFXObject_Load 0x004101a0; handler 0x004102b0 -> 0x004102e0"),
    ("camera_fx", "receive_trigger_kill"): ("remove the camera fx", "0x004102b0 -> vt+0xd4"),
    ("player_tickerbomb", "receive_trigger_enable"): ("enable this bomb site (until then it is disabled)",
                                                      "PlayerTickerBomb_OnTrigger 0x0044e5e0 clears +0x474"),
    ("broadcast", "enable"): ("enable the volume (without 'enable' it starts enabled)",
                              "TriggerObject 0x00497720 sets flag 8"),
    ("broadcast", "disable"): ("disable the volume", "0x00497720 clears flag 8"),
    ("pickup_token", "enable"): ("enable", "0x00497720"),
    ("pub", "listen_trigger_open"): ("open the pub (until then closed)", "PubObject_OnTrigger 0x0046a770"),
    ("pickup_generator", "listen_activate_trigger"): ("activate the generator", "PickupGenerator_OnTrigger "
                                                                                "0x00471c30"),
    ("building", "receive_trigger_activate"): ("barracks start releasing their troops (num_* towards marker tags)",
                                               "BarricadeObject_Slot14 0x0040d1b0 (+0x534 -> flag 8)"),
    ("building", "receive_trigger_deactivate"): ("barracks stop releasing", "0x0040d1b0 (+0x538)"),
    ("building", "receive_trigger_reset"): ("barracks refill troop counts", "0x0040d1b0 (+0x5a4)"),
    ("building", "receive_trigger_kill"): ("barracks destroyed", "0x0040d1b0 (+0x5ac) -> vt+0xd4"),
    ("ai", "receive_trigger_activate"): ("AI wakes up (dormant until then; trigger_los 0 = only by trigger) and "
                                         "runs its mode (e.g. trigger_marker: run its marker path)",
                                         "GenericAIObject set-up (ai_01.cpp 0x00426xxx mode table); tower/turret "
                                         "variants ai_08.cpp"),
    ("counter", "receive_trigger_activate"): ("count one (sends send_trigger on reaching `count`)",
                                              "CounterObject_OnTrigger 0x00415860 (+0x460)"),
    ("counter", "receive_trigger_deactivate"): ("stop counting for good", "CounterObject_OnTrigger 0x00415860 "
                                                                          "(+0x464 -> +0x475, vt+0xdc)"),
    **{(c, "receive_trigger_activate"): ("start the timer (without this trigger it runs from the level start)",
                                         "TimerObject_OnTrigger 0x004965d0 (+0x460 -> running +0x4a9)")
       for c in ("timer", "army_timer")},
    ("*", "receive_trigger_kill"): ("destroyed: death fx, husk, its own send_trigger_on_death",
                                    "DefaultGameObject_OnTrigger 0x00450fb0 (+0x4c4) -> vt+0xd4"),
    ("*", "receive_trigger_deactivate"): ("deactivated", "DefaultGameObject_OnTrigger 0x00450fb0 (+0x4c8)"),
    ("*", "receive_trigger_start_blocking"): ("starts blocking", "DefaultGameObject_OnTrigger 0x00450fb0 (+0x4cc)"),
}
AI_CLASSES = {"grunt", "grunt_regular", "captain", "captain_rocket", "captain_sniper", "q111", "molmer", "q112",
              "monk", "twiglet", "genericai", "penguin", "sheep", "peasant"}
# Tag links (no trigger): property -> what it points at.
TAG_LINKS = {
    "compass_objective_tag": ("tag", "initial compass target (Game+0x2658)", "MissionObject_Load 0x0044c170; "
                                                                               "compass 0x00466790 matches "
                                                                               "object +0x390 (tag)"),
    "objective_tag_0N": ("tag", "compass target after the matching objective trigger", "0x0046afa0"),
    "alarm_tag": ("tag", "siren an AI starts on reaching this marker (fx_siren; alarm generators spawn while any "
                         "siren runs)", "0x0044ba30; alarm controller 0x00404e50"),
    "lock_parent_tag": ("tag", "bomb attached to this object's bone", "0x0044e120"),
    "start_in_turret_tag": ("tag", "player starts in this turret", "0x0044d150"),
    "ai_tag": ("tag", "AI speaker of a sound_ear", "SoundEar_Slot6 0x00497750"),
    "marker_tag_min": ("marker_tag", "AI / spawned troops path: markers with marker_tag in [min, max]",
                       "AI set-up; AIMarker_NextTag 0x0044c030"),
}

# ---------------------------------------------------------------- data access


def latin(b):
    return b.decode("latin-1") if isinstance(b, (bytes, bytearray)) else b


def load_instances(level):
    p = rx.payload(level, "big10_inst")
    groups, off, out = struct.unpack_from("<I", p, 0)[0], 4, []
    for _ in range(groups):
        n = struct.unpack_from("<I", p, off)[0]
        off += 4
        for _ in range(n):
            it, size = inst.parse_instance(p, off)
            off += size
            out.append(it)
    return out


def load_props(level):
    _, sets = prop.parse(rx.payload(level, "big06_prop"))
    float_keys = {latin(k).lower() for st in sets for k, v in zip(st["keys"], st["values"])
                  if len(v) == 4 and prop._guess(v) == "float"}
    out = {}
    for st in sets:
        pairs = []
        for k, v in zip(st["keys"], st["values"]):
            k = latin(k).lower()
            pairs.append((k, prop.value_to_json(v, float_keys, k)))
        out[latin(st["name"]).lower()] = pairs
    return out


def load_text(level):
    _, strings = text.parse(rx.payload(level, "big01_text"))
    return {latin(i).lower(): s.decode("utf-16-le", "surrogatepass") for i, s in strings}


def load_ui(level, scratch):
    scratch.mkdir(parents=True, exist_ok=True)
    try:
        ui.dump(rx.payload(level, "big12_ui"), scratch)
        return json.loads((scratch / "ui.json").read_text(encoding="utf-8"))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def bank_names(level, part, padded):
    _, chunks = objchunk.split_bank(rx.payload(level, part), padded=padded)
    return {c.name.lower() for c in chunks}


# ---------------------------------------------------------------- helpers


def position(t):
    return [t[0][3], t[1][3], t[2][3]]


def to_world(t, p):
    return [t[r][0] * p[0] + t[r][1] * p[1] + t[r][2] * p[2] + t[r][3] for r in range(3)]


def class_of(template_name, resolved):
    beh = resolved.get("behavior")
    key = (beh or template_name).lower()
    c = CLASSES.get(key)
    if c:
        return key, c
    return key, DEFAULT_CLASS


def numbered(prop_name):
    """text_trigger_receive_01 -> (text_trigger_receive_0N, 1)."""
    if len(prop_name) > 3 and prop_name[-3] == "_" and prop_name[-2:].isdigit() and prop_name[-2] == "0":
        return prop_name[:-2] + "0N", int(prop_name[-2:])
    return prop_name, None


def timer_entries(cls, resolved):
    """A timer's "send_trigger_time_%d" entries as TimerObject_ReadProps 0x00496160 reads them: only without an
    anim_name, indices 1..10 until the first missing one, "trigger/time" (atol / atof), trigger 0 dropped (an entry
    without '/' is logged by the game and skipped). -> [(trigger, key, seconds)] sorted by time."""
    if cls not in ("timer", "army_timer") or str(resolved.get("anim_name", "none")).lower() != "none":
        return []
    out = []
    for i in range(1, 11):
        k = f"send_trigger_time_{i}"
        if k not in resolved:
            break
        text = str(resolved[k])
        if "/" not in text:
            continue
        tid, secs = text.split("/", 1)
        try:
            tid, secs = int(tid.strip() or 0), float(secs.strip() or 0)
        except ValueError:
            continue
        if tid:
            out.append((tid, k, secs))
    return sorted(out, key=lambda e: e[2])


def lookup(table, cls, name):
    base, _ = numbered(name)
    for k in ((cls, base), ("ai", base) if cls in AI_CLASSES else None, ("*", base)):
        if k and k in table:
            return table[k]
    return None


def model_bounds(cache, skins, model):
    if model in cache:
        return cache[model]
    b = None
    if model and model in skins:
        lo, hi = [math.inf] * 3, [-math.inf] * 3
        for sub in rx.mesh_submeshes(skins[model]):  # (texture, positions, ...): only the positions are used
            vp = sub[1]
            for v in vp:
                lo = [min(lo[i], v[i]) for i in range(3)]
                hi = [max(hi[i], v[i]) for i in range(3)]
        if lo[0] != math.inf:
            b = [[round(x, 3) for x in lo], [round(x, 3) for x in hi]]
    cache[model] = b
    return b


# ---------------------------------------------------------------- main


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("level")
    ap.add_argument("--out")
    a = ap.parse_args()
    level = a.level
    out = pathlib.Path(a.out) if a.out else ROOT / "work" / "remaster" / level
    out.mkdir(parents=True, exist_ok=True)

    props = load_props(level)
    _, tlist = obj.parse(rx.payload(level, "big07_obj"))
    templates = {latin(t["name"]).lower(): t for t in tlist}
    strings = load_text(level)
    uidata = load_ui(level, out / ".mission_ui_tmp")
    _, schunks = objchunk.split_bank(rx.payload(level, "big05_skin"), padded=True)
    skins = {c.name.lower(): c for c in schunks}
    anims = bank_names(level, "big03_anim", False)
    _, fxs = fx.parse(rx.payload(level, "big09_fx"))
    fx_names = {latin(e["name"]).lower() for e in fxs}
    _, _, sounds = aud.parse(rx.payload(level, "big08_aud"))
    sound_names = {latin(aud._name(s["header"])).lower() for s in sounds}

    bounds_cache = {}
    instances = []
    for i, it in enumerate(load_instances(level)):
        name = latin(it["object"]).lower()
        own = [[latin(k).lower(), latin(v) if isinstance(v, (bytes, bytearray)) else v] for k, v in it["props"]]
        t = templates.get(name)
        pset = latin(t["property"]).lower() if t else None
        lods = [latin(x).lower() for x in t["lods"]] if t else []
        defaults = props.get(pset, [])
        resolved = {}
        for k, v in defaults:
            resolved.setdefault(k, v)  # duplicates: the first kept (the game's lookup returns one of them)
        for k, v in own:
            resolved[k] = v
        key, (cname, cid, cev) = class_of(name, resolved)
        rec = {"id": i + 1, "template": name, "property_set": pset, "model": lods[0] if lods else None,
               "class": cname, "behaviour": key, "class_id": cid, "mask": it["mask"],
               "transform": it["transform"], "position": position(it["transform"]),
               "props": {k: v for k, v in own}, "template_props": {k: v for k, v in defaults}, "resolved": resolved}
        if key in ("broadcast", "pickup_token"):
            r = resolved.get("sphere_radius") or 0
            if r:
                rec["volume"] = {"kind": "sphere", "radius": r, "centre": rec["position"],
                                 "evidence": "TriggerObject_BuildCollision 0x004975d0: radius collision of "
                                             "sphere_radius"}
            else:
                b = model_bounds(bounds_cache, skins, rec["model"])
                vol = {"kind": "box", "model": rec["model"], "local_min": b[0] if b else None,
                       "local_max": b[1] if b else None,
                       "evidence": "sphere_radius 0: the model's collision (collide_poly). Box extents from the "
                                   "model's vertices (guess that the volume is its box)"}
                if b:
                    corners = [[(b[0], b[1])[(c >> k) & 1][k] for k in range(3)] for c in range(8)]
                    w = [to_world(it["transform"], p) for p in corners]
                    vol["world_corners"] = [[round(x, 3) for x in p] for p in w]
                    vol["world_aabb"] = [[round(min(p[k] for p in w), 3) for k in range(3)],
                                         [round(max(p[k] for p in w), 3) for k in range(3)]]
                rec["volume"] = vol
            rec["volume"]["force_ground"] = resolved.get("force_ground")
            rec["volume"]["one_shot"] = resolved.get("one_shot")
            rec["volume"]["ai_can_trigger"] = resolved.get("ai_can_trigger")
        if key == "camera_path":
            rec["camera_path"] = {"model": rec["model"], "model_in_skin_bank": rec["model"] in skins,
                                  "anims_named_like_model": sorted(x for x in anims if rec["model"] and
                                                                   rec["model"] in x),
                                  "evidence": "Camera_StartPath 0x0040ff50: finds the object named "
                                              "'camera_path', plays animation <name> on it and locks the camera "
                                              "to its 'Camera01' bone (mode 1 = cinematic, Game+0xdce)"}
        if key in ("fx_ear", "camera_fx"):
            f = (resolved.get("fx_name") or "").lower()
            rec["fx"] = {"name": f, "in_fx_bank": f in fx_names}
        instances.append(rec)
    by_id = {r["id"]: r for r in instances}

    # ------------------------------------------------ trigger graph
    triggers = {}

    def slot(tid):
        return triggers.setdefault(str(tid), {"senders": [], "receivers": []})

    for r in instances:
        cls = r["behaviour"]
        for tid, k, secs in timer_entries(cls, r["resolved"]):
            s = SENDS[(cls, "send_trigger_time_N")]
            slot(tid)["senders"].append({"id": r["id"], "template": r["template"], "class": r["class"],
                                         "property": k, "event": f"{s[0]}: {secs:g} s", "evidence": s[1],
                                         "position": r["position"], "time": secs})
        for k, v in r["resolved"].items():
            if not isinstance(v, int) or isinstance(v, bool) or v == 0:
                continue
            s = lookup(SENDS, cls, k)
            if s and (k.startswith("send_") or k in ("on_enter", "on_exit", "trigger")):
                slot(v)["senders"].append({"id": r["id"], "template": r["template"], "class": r["class"],
                                           "property": k, "event": s[0], "evidence": s[1],
                                           "position": r["position"]})
                continue
            rcv = lookup(RECEIVES, cls, k)
            if rcv and (k.startswith("receive_") or k.startswith("text_trigger") or k.startswith("objective_trig")
                        or k in ("trigger_id", "trigger_start", "enable", "disable", "listen_trigger_open",
                                 "listen_activate_trigger") or k.startswith("alarm_trigger")
                        or k.startswith("army_breach") or k == "peasant_saved_trigger"):
                action = rcv[0]
                detail = {}
                base, n = numbered(k)
                if cls == "ui_ear" and base == "text_trigger_receive_0N":
                    tid = (r["resolved"].get(f"text_display_0{n}") or "").lower()
                    detail = {"text_id": tid, "text": strings.get(tid)}
                    objs = [m for m in range(1, 6) if r["resolved"].get(f"objective_trigger_receive_0{m}") == v
                            and r["resolved"].get(f"objective_tag_0{m}")]
                    detail["flags"] = "0x13 (objective: stored as objectives text)" if objs else "3"
                if cls == "ui_ear" and base == "objective_trigger_receive_0N":
                    tag = r["resolved"].get(f"objective_tag_0{n}")
                    detail = {"objective_tag": tag,
                              "tagged": [x["id"] for x in instances if x["resolved"].get("tag") == tag]}
                if cls == "sound_ear":
                    detail = {sp: r["resolved"].get(f"{sp}_sound") for sp in ("roman", "q", "jonsey", "ai")
                              if (r["resolved"].get(f"{sp}_sound") or "none") != "none"}
                    detail["max_distance"] = r["resolved"].get("max_distance")
                if cls == "fx_ear":
                    detail = {"fx_name": r["resolved"].get("fx_name"),
                              "in_fx_bank": (r["resolved"].get("fx_name") or "").lower() in fx_names}
                if cls == "mission":
                    detail = {"success_text": r["resolved"].get("success_text"),
                              "next_level_name": r["resolved"].get("next_level_name")}
                slot(v)["receivers"].append({"id": r["id"], "template": r["template"], "class": r["class"],
                                             "property": k, "action": action, "evidence": rcv[1],
                                             "position": r["position"], **({"detail": detail} if detail else {})})
    hard = {"27": "sent by the platoon grunt death code 0x00433199 (mov ebx, 27): the only constant trigger id in "
                  "game.exe's 27 Object_SendTrigger call sites"}
    edges, unsent, unreceived = [], [], []
    for tid, t in sorted(triggers.items(), key=lambda kv: int(kv[0])):
        if tid in hard:
            t["code_sender"] = hard[tid]
        if not t["senders"] and tid not in hard:
            unsent.append(int(tid))
            t["note"] = "no sender in the level data or as a constant in game.exe: never fires (guess: cut content)"
        if not t["receivers"]:
            unreceived.append(int(tid))
            t["note"] = "no receiver in the level"
        for s in t["senders"] or [{"id": None, "template": None, "event": "(no sender)", "evidence": ""}]:
            for rv in t["receivers"] or [{"id": None, "template": None, "action": "(no receiver)", "evidence": ""}]:
                edges.append({"trigger": int(tid),
                              "from": {"id": s["id"], "template": s["template"], "event": s["event"]},
                              "to": {"id": rv["id"], "template": rv["template"], "action": rv["action"],
                                     **({"detail": rv["detail"]} if "detail" in rv else {})},
                              "evidence": f"send: {s['evidence']}; receive: {rv['evidence']}; dispatch: "
                                          "Object_SendTrigger 0x00461870 offers the id to every listener's "
                                          "vt+0x38 until one returns true (all handlers here return false, so "
                                          "every receiver gets it)"})

    tag_links = []
    for r in instances:
        for k, v in r["resolved"].items():
            base, n = numbered(k)
            if base not in TAG_LINKS or not isinstance(v, int) or v in (0, -1):
                continue
            field, what, ev = TAG_LINKS[base]
            if base == "marker_tag_min":
                hi = r["resolved"].get("marker_tag_max", v)
                targets = [x["id"] for x in instances if x["behaviour"] == "marker"
                           and isinstance(x["resolved"].get("marker_tag"), int) and v <= x["resolved"]["marker_tag"] <= hi]
                tag_links.append({"from": r["id"], "template": r["template"], "property": f"marker_tag {v}..{hi}",
                                  "meaning": what, "targets": targets, "evidence": ev})
                continue
            targets = [x["id"] for x in instances if x["resolved"].get(field) == v and x["id"] != r["id"]]
            tag_links.append({"from": r["id"], "template": r["template"], "property": k, "value": v,
                              "meaning": what, "targets": targets, "evidence": ev})

    # ------------------------------------------------ text, voice, hints, subtitles
    mission = next((r for r in instances if r["behaviour"] == "mission"), None)
    used = set()
    for r in instances:
        for k, v in r["resolved"].items():
            if isinstance(v, str) and v and (k.endswith("_text") or k.startswith("text_display") or
                                             k.startswith("failure_text") or k == "text_lookup"):
                used.add(v.lower())
    used.add("ui_timeout")
    code = level.replace("lev_", "l").replace("_m", "m").replace("_", "")  # lev_1_m1 -> l1m1
    used |= {k for k in strings if k.startswith(code + "_")}
    hint_ids = {
        "ui_stats_ok": ("ControlActionMenuSelect", 0x1B, "appended to paused messages with flag 1 ('%s<br><br>%s'): "
                        "ui_mission_briefing update 0x00466060"),
        "ui_tickerbomb_hint": ("ControlActionUse", 0x14, "ticker prompt 0x00467450: an enabled, unarmed bomb site "
                               "in range and the player has ticker bombs (Game+0xd90)"),
        "ui_tickerbomb_none": (None, None, "0x00467450: a bomb site in range but no ticker bombs"),
        "ui_tickerbomb_patience": (None, None, "0x0044e3a0: another ticker bomb is armed (Game+0xdcf)"),
        "ui_tickerbomb_set": (None, None, "progress bar while Use is held at a site (0x0044e150)"),
        "ui_pub_entry_hint": ("ControlActionUse", 0x14, "pub prompt 0x00467b60: near the pub doorway "
                              "(doorway_entry_radius)"),
        "ui_pub_fail": (None, None, "0x00467b60: pub closed (listen_trigger_open not yet received)"),
        "ui_vehicle_entry": ("ControlActionUse", 0x14, "0x004662a0: near an enterable turret (list +0xec4, its "
                             "radius)"),
        "ui_vehicle_exit": ("ControlActionUse", 0x14, "0x00466210: in a turret"),
        "ui_bof_hint": ("ControlActionFire", 0x08, "0x004671b0: once (Game+0x578) when the punching glove comes out "
                        "(Game+0xcbc, set by the player update 0x004788a0 path at 0x00478xxx); guess: out of ammo"),
        "pub_award_tickerbomb": ("ControlActionMenuSelect", 0x1B, "pub screen 0x00469xxx when has_tickerbomb"),
        "pub_award_topsy": ("ControlActionMenuSelect", 0x1B, "pub screen: special weapon award"),
        "pub_notify_new": ("ControlActionMenuSelect", 0x1B, "pub screen"),
        "pub_notify_select": ("ControlActionMenuSelect", 0x1B, "pub screen"),
        "pub_notify_exit": ("ControlActionMenuCancel", 0x1C, "pub screen"),
        "pickup_token": (None, None, "pickup_token text_lookup"),
    }
    hints = {k: {"text": strings.get(k), "action": a_, "action_code": c_, "when": w_}
             for k, (a_, c_, w_) in hint_ids.items()}
    used |= set(hint_ids)
    voice = {}
    for r in instances:
        if r["behaviour"] != "sound_ear":
            continue
        for sp in ("roman", "q", "jonsey", "ai"):
            s = (r["resolved"].get(f"{sp}_sound") or "none").lower()
            if s != "none":
                voice[s] = {"speaker": sp, "subtitle": strings.get(s), "in_audio_bank": s in sound_names,
                            "sound_ear": r["id"]}
    for s in ("rom50211_tickerarmed", "rom50212_tickerarmed"):
        voice[s] = {"speaker": "roman", "subtitle": strings.get(s), "in_audio_bank": s in sound_names,
                    "played_by": "ticker arm 0x0044e4a0 (50% each)"}
    subs = []
    start_movie = (mission["resolved"].get("start_movie") or "").lower() if mission else ""
    for m in uidata.get("subtitles", []):
        if m["movie"].lower() in (start_movie, ):
            for ln in m["lines"]:
                tid = (ln["text"] or "").lower()
                subs.append({**ln, "string": strings.get(tid)})
    text_out = {k: strings[k] for k in sorted(used) if k in strings}
    missing_text = sorted(k for k in used if k not in strings)

    # ------------------------------------------------ rules from code
    mres = mission["resolved"] if mission else {}
    rules = {
        "start": [
            {"what": "camera_fx objects with trigger_start 0 start their fx at load",
             "evidence": "CameraFXObject_Load 0x004101a0 calls 0x004102e0 when +0x480 is 0"},
            {"what": "an fx script opcode 0x73 starts a camera path in cinematic mode (camera locked to the "
                     "camera_path object's 'Camera01' bone; Game+0xdce = 1)",
             "evidence": "render_r2b.cpp case 0x73 -> Camera_StartPath 0x0040ff50 mode 1"},
             {"what": "on lev_1_m1 while the path 'camera_path_l1m1' plays, the player walks forward on its own "
                      "(stick y = 1.0)",
              "evidence": "PlayerObject_Update 0x004788a0, game_06.cpp:2451 (_stricmp with 'lev_1_m1' 0x005b1738 "
                          "and 'camera_path_l1m1' 0x0059ed8c)"},
            {"what": "first mission frame: send_trigger_start is sent",
             "evidence": "MissionObject_Update 0x0044db80"},
            {"what": "start_movie plays first, except on lev_1_m1 (its pre_1_1_opening already plays in the boot "
                     "intro list 'LucasAnim,pm_logo,pre_1_1_opening')",
             "evidence": "MissionObject_Update 0x0044db80 (_stricmp 'lev_1_m1' 0x005b1738); Game_StartLevel "
                         "start_level.cpp:120 (0x005bc108)"},
            {"what": "start_text is shown once no camera path is active and no cinematic runs: paused message "
                     "box, long form (<long> kept, <short> dropped), 'Press %s to continue'",
             "evidence": "MissionObject_ShowStart 0x0044daf0 -> 0x00403410(text, 1); 0x00466060; "
                         "Renderer_TextSkipTaggedBlock 0x005072b0"},
            {"what": "the player starts at the 'trotter' instance with the mission's weapon settings "
                     "(player_add_*, player_set_*)",
             "evidence": "behaviour 'trotter' = class 161 PlayerObject (0x0045df60 table; 0x0045f020 stores it in "
                         "Game+0x748); MissionObject_Load 0x0044c170"},
        ],
        "win": {"trigger": mres.get("receive_trigger_success"), "text": mres.get("success_text"),
                "next_level": mres.get("next_level_name"), "score_value": mres.get("score_value"),
                "sequence": "level state 2: sounds stop, success_text (paused, Press to continue), 'Stats' "
                            "screen, success_movie if any, then 'Loading' next_level_name",
                "evidence": "MissionObject_OnTrigger 0x0044d600; 0x0044a390; 0x0044a230"},
        "lose": [
            {"condition": "player dead for 5 s", "state": 3, "text": mres.get("failure_text"),
             "evidence": "PlayerObject_Update game_06.cpp:2160 (+0x588 = 5.0 countdown) -> 0x0044a580(3); "
                         "0x0044a390 pushes 'MissionFailed' with Game+0x2664 = failure_text"},
            {"condition": "receive_trigger_failure[_2.._4]",
             "triggers": [mres.get(k) for k in ("receive_trigger_failure", "receive_trigger_failure_2",
                                                 "receive_trigger_failure_3", "receive_trigger_failure_4")],
             "evidence": "MissionObject_OnTrigger 0x0044d600"},
            {"condition": "start turret destroyed (start_in_turret levels)", "state": 3,
             "evidence": "0x0049ac60 -> 0x0044a580(3)"},
            {"condition": "peasant casualties >= peasant_max_casualties", "state": 8,
             "evidence": "PeasantObject kill 0x004062a0"},
        ],
        "checkpoints": "none found: no checkpoint code (grep 'checkpoint'/'respawn' in archive/src/ finds only save "
                       "restore 0x0048cb30); levels are split instead (lev_1_m1 -> lev_1_m1_b)",
        "message_flags": {"1": "pause + 'Press %s to continue'", "2": "queue: shown 1 s later",
                          "4": "short form (<short> kept)", "8": "do not pause",
                          "0x10": "also store as the current objectives text (Game+0x1de4)",
                          "evidence": "0x00403410 game_01.cpp:801; 0x00466060 ui_02.cpp:792; 0x004031e0"},
        "spawn_mask": "instance mask bit p spawns the instance in pass p (0x0045d930); the pass value comes from "
                      "Game_StartLevel's argument (meaning unknown; mission and one ui_ear use 0x1)",
    }

    result = {
        "level": level, "up": "z",
        "next_level": mres.get("next_level_name"),
        "mission": {"id": mission["id"] if mission else None, "resolved": mres},
        "player_start": [{"id": r["id"], "position": r["position"], "transform": r["transform"]}
                         for r in instances if r["behaviour"] == "trotter"],
        "rules": rules,
        "triggers": triggers,
        "edges": edges,
        "unsent_triggers": unsent,
        "unreceived_triggers": unreceived,
        "tag_links": tag_links,
        "text": text_out,
        "missing_text_ids": missing_text,
        "voice": voice,
        "hints": hints,
        # Key names for the %s of a prompt: Input_CodeText 0x00415580 looks up the binding's "PMS_INPUT_*" id
        # (0x00414c90) in the text bank; the action's own name is its "ControlAction*" id (0x004155a0).
        "input_text": {k: v for k, v in sorted(strings.items())
                       if k.startswith("pms_input_") or k.startswith("controlaction")},
        "subtitles": {"movie": start_movie, "lines": subs},
        "instances": instances,
    }
    (out / "mission.json").write_text(json.dumps(result, indent=1, ensure_ascii=False), encoding="utf-8")
    logic = sum(1 for r in instances if r["behaviour"] not in ("pine_tree",) and r["class"] != "DefaultGameObject")
    print(f"{level}: {len(instances)} instances ({logic} with a known class), {len(triggers)} trigger ids, "
          f"{len(edges)} edges, unsent {unsent}, unreceived {unreceived}, {len(text_out)} texts "
          f"-> {out / 'mission.json'}")


if __name__ == "__main__":
    main()
