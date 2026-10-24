//! Markers and the alarm chain (docs/subsystems/mission.md "lev_1_m1 alarm chain", grunt-ai.md 13).
//!
//! - Markers (template `marker`, MarkerObject_LoadProperties 0x0044b130 and its getters Marker_GetSequence 0x0044b480,
//!   Marker_GetTrigger 0x0044b8b0 ...): the splines by tag (MarkerObject_BuildSpline 0x0044b540), followed by grunts
//!   (ai.rs `follow_spline`, `marker_arrival`: AI_FollowMarkerSpline 0x0042d1b0, Marker_ProcessArrival 0x0044ba30,
//!   AIMarker_Advance 0x0044b980). On arrival with an alarm_tag the object of that tag (Object_FindByTag 0x004592e0:
//!   `tagged`) joins the siren list World+0xef0 once: fx_siren on it and the runner's Help bark (vt+0x264, 0x00438f90).
//! - The alarm controller (0x00404e50, ai_01.cpp:468-660; its globals 0x0063a1e8.. are this resource) while a siren is
//!   listed or the mission forces the alarm (alarm_trigger_activate, Game+0xdcc): its spawn timer (rolled in
//!   alarm_min..max_delay at load by 0x00404940, counting down only then) runs out -> one troop while fewer than
//!   alarm_generator_max_troops (10) of its troops live, from the generator (AlarmGenerator_Slot6 0x00405490) nearest
//!   the player that he does not see (0x00404d00) and is not one of the last 3 spawn points (else the first such: the
//!   fallback), the troop type by the six weights, made at the generator's ref_spawn_00; the troop walks to its
//!   ref_exit_00 and follows the spline of a tag in the generator's marker_tag_min..max (0x0042dd50), or charges with no
//!   tags; the timer is rolled again (spawn or not).
//! - The controller's other timer (+0x460, 5..15 s): when it runs out, every troop of a max_distance generator farther
//!   than alarm_ai_max_distance (Game+0xd64) from the player that he does not see dies (vt+0xd4), so a far-off
//!   generator's troops do not pile up out of the fight (`cull_far_troops`).
//! - "Seen" (0x00404d00): in the player's 180-degree view (vt+0xf0), and then a level ray at his eye's height from
//!   above the generator toward his ref_hit_01 (5 past it) over the collidable objects (World+0xe9c, the AI excluded;
//!   not the landscape) meets nothing; any collidable object met (a building, a wall) hides the generator. Oracle (the hook's alarm gates): lev_2_m2
//!   (siren raised) and lev_bonus_m1 (alarm forced, 9 generators) run the timer out again and again with no troop in
//!   10-20 s; lev_bonus_m2's generators 169 / 171 send theirs (218, 414: the delays are rand rolls, so the frames
//!   differ). The generators' troop weights fall back to the grunt_generator template's (num_regular 1 there).
//! - A destroyed siren leaves the list (guess: Object_Remove unlinks it; unverified) and its loop stops.

use crate::levelreset::LevelAppExt;
use crate::level::{InstanceId, game_to_engine};
use bevy::prelude::*;
use bevy::ecs::system::SystemParam;
use std::collections::HashMap;

/// A marker's properties (MarkerObject_LoadProperties 0x0044b130; getters 0x0044b470..0x0044b960).
pub struct Marker {
    pub id: u32,
    pub pos: Vec3,
    /// its facing (0x00453150: the placement's forward), used with face_dir
    pub dir: Vec3,
    /// the placement's game-space matrix (vt+0x5c: a tower companion takes it on arrival, ai/tower.rs)
    pub matrix: Mat4,
    pub tag: i64,
    /// "sequence" +0x46c: the order on its tag's spline (0 = the head)
    pub sequence: i64,
    /// "marker_next_min" / "marker_next_max" +0x464 / +0x468: the spline to switch to here (0: stay)
    pub next: (i64, i64),
    /// +0x490 bit 0 "strict": no target update on the way to the next marker; bit 7 "face_dir"
    pub strict: bool,
    pub face_dir: bool,
    pub alarm_tag: i64,
    /// "trigger" +0x498: sent on arrival (Object_SendTrigger 0x00461870)
    pub trigger: i64,
    pub action: String,
    /// the action flags +0x490 the load maps the name to: hide 0x100000, attention_guard 0x200000, ready_guard
    /// 0x400000, trigger_charge 0x800000, trigger_charge_tag 0x2000000, ic_* 0x1000000; other names (ready_roaming,
    /// trigger_marker on lev_1_m1) set nothing; and the booleans "kill" 4 and "deactivate" 8 (ai_07.cpp:799-804).
    /// (obj_charge_tag_min / _max / obj_charge_pct, trigger_charge_tag's object pick, are not read: no level's marker
    /// sets them or that action; trigger_charge_tag is taken as the plain charge)
    pub flags: u32,
    pub scatter: (f32, f32),
    /// "speed" +0x494 (Marker_GetSpeed 0x0044b490): the walk speed of the leg that ends here, 0 the walker's own
    pub speed: f32,
}

impl Marker {
    /// "kill": the arriving AI dies (vt+0xd4(0))
    pub const KILL: u32 = 4;
    /// "deactivate": the arriving AI is removed (vt+0xdc)
    pub const DEACTIVATE: u32 = 8;
    pub const HIDE: u32 = 0x100000;
    pub const ATTENTION_GUARD: u32 = 0x200000;
    pub const READY_GUARD: u32 = 0x400000;
    pub const TRIGGER_CHARGE: u32 = 0x800000;
    pub const IC: u32 = 0x1000000;
    pub const TRIGGER_CHARGE_TAG: u32 = 0x2000000;

    /// How this marker ends an AI that reaches it (Marker_ProcessArrival 0x0044ba30, ai_07.cpp:1343-1353, before any
    /// action): deactivate (8) removes him (vt+0xdc), else kill (4) is his death (vt+0xd4(0)).
    pub fn ends_ai(&self) -> Option<crate::ai::notes::End> {
        if self.flags & Self::DEACTIVATE != 0 {
            Some(crate::ai::notes::End::Remove)
        } else if self.flags & Self::KILL != 0 {
            Some(crate::ai::notes::End::Kill)
        } else {
            None
        }
    }
}

/// An alarm generator (AlarmGenerator, vtable 0x005a2058; its set-up slot 6 0x00405490).
struct Generator {
    id: u32,
    pos: Vec3,
    /// its model's "ref_spawn_00" (where troops are made) and "ref_exit_00" (where they walk first, 0x0042dd50)
    spawn: Vec3,
    exit: Vec3,
    /// the six weights +0x464.. in the controller's type order (0x00404e50): num_bow "grunt", num_regular
    /// "grunt_regular", num_jumper_captain "captain" (jumper), num_captain "captain", num_rocket_captain (no type: the
    /// original's bug, logged and never reached on lev_1_m1), num_monk "monk"
    weights: [i64; 6],
    /// min_spawn_distance +0x494; max_distance +0x461 (a byte: set, the generator sends troops only while it is within
    /// 0.75 x alarm_ai_max_distance of the player, and its troops die far from him: `cull_far_troops`)
    min_distance: f32,
    max_distance: bool,
    /// marker_tag_min / max +0x480 / +0x484, alert_group +0x490, priority_target_tag_min / max +0x488 / +0x48c
    tags: (i64, i64),
    alert_group: i64,
    priority: (i64, i64),
}

#[derive(Resource, Default)]
pub struct Markers {
    pub markers: Vec<Marker>,
    /// the splines by tag (MarkerObject_BuildSpline 0x0044b540 on each head, sequence 0): the tag's markers in
    /// sequence order; a tag without a head has none (Marker_FindSpline 0x0044c030 looks the head up)
    pub splines: HashMap<i64, Vec<usize>>,
    generators: Vec<Generator>,
    /// instance ids by their `tag` property (alarm targets)
    tagged: HashMap<i64, Vec<u32>>,
    sirens: Vec<Entity>,
    /// the spawn timer 0x0063a1f4 (None: not rolled yet, at the first frame of the level)
    timer: Option<f32>,
    /// the far troops' cull timer, the controller's +0x460 (rolled with the spawn timer)
    cull_timer: f32,
    /// the last three spawn points 0x0063a1f8.. (slot 0 the newest); a fallback's slot is emptied, so one can be None
    recent: [Option<u32>; 3],
    max_troops: usize,
    delay: (f32, f32),
    /// alarm_ai_max_distance (Game+0xd64, the level's global settings, 1000)
    range: f32,
    /// far troops culled (playtest log)
    culled: u32,
}

impl Markers {
    /// The raised sirens (the list the controller sends troops for while it is not empty).
    pub fn sirens(&self) -> &[Entity] {
        &self.sirens
    }

    /// The index of the marker with instance id `id` (a MarkerArrival's `marker`).
    pub fn index_of(&self, id: u32) -> Option<usize> {
        self.markers.iter().position(|m| m.id == id)
    }

    /// The marker at `index`.
    pub fn marker(&self, index: usize) -> Option<&Marker> {
        self.markers.get(index)
    }

    /// A siren raised in a saved game, back on the list (savegame.rs, 0x0048c530); false when it already is.
    pub fn raise_restored(&mut self, e: Entity) -> bool {
        if self.sirens.contains(&e) {
            return false;
        }
        self.sirens.push(e);
        true
    }

    /// From the level's placements, each with its class template's values under its own (as the original's property
    /// readers fall back to the template: a grunt_generator's num_regular 1 when the placement names only num_bow 0 and
    /// num_captain 0, lev_bonus_m2 169..171).
    pub fn load(dir: &std::path::Path, data: &crate::gamedata::GameData) -> Self {
        let scene = std::fs::read_to_string(dir.join("scene.json")).ok().and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok());
        // alarm_ai_max_distance: the level's global_settings over p_globalsettings (read into Game+0xd64 with the
        // default 1000, render_r6.cpp:1096)
        let mut settings = data.props("p_globalsettings");
        if let Some(own) = scene.as_ref().and_then(|s| s.get("settings")).and_then(|s| s.as_object()) {
            settings.0.extend(own.iter().map(|(k, v)| (k.clone(), v.clone())));
        }
        let mut m = Markers { max_troops: 10, delay: (2.0, 4.0), range: settings.f("alarm_ai_max_distance", 1000.0), ..default() };
        let Ok(text) = std::fs::read_to_string(dir.join("instance_props.json")) else { return m };
        let Ok(list) = serde_json::from_str::<Vec<serde_json::Value>>(&text) else { return m };
        let num = |p: &crate::gamedata::Props, k: &str| p.0.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0);
        // the placements' matrices (scene.json): a marker's facing, a generator's ref_spawn_00 / ref_exit_00
        let mats: HashMap<u32, Mat4> = scene
            .as_ref()
            .and_then(|s| {
                let list = s.get("instances")?.as_array()?.clone();
                Some(
                    list.iter()
                        .filter_map(|i| {
                            let t: [[f32; 4]; 3] = serde_json::from_value(i.get("transform")?.clone()).ok()?;
                            Some((i.get("id")?.as_u64()? as u32, crate::scene::game_matrix(&t)))
                        })
                        .collect(),
                )
            })
            .unwrap_or_default();
        for v in &list {
            let id = v.get("id").and_then(|x| x.as_u64()).unwrap_or(0) as u32;
            let tpl = v.get("template").and_then(|x| x.as_str()).unwrap_or("");
            let gpos = v.get("pos").and_then(|p| p.as_array()).map(|a| Vec3::new(a[0].as_f64().unwrap_or(0.0) as f32, a[1].as_f64().unwrap_or(0.0) as f32, a[2].as_f64().unwrap_or(0.0) as f32)).unwrap_or_default();
            let pos = game_to_engine() * gpos;
            let mat = mats.get(&id).copied().unwrap_or(Mat4::from_translation(gpos));
            let own = v.get("props").and_then(|x| x.as_object()).cloned().unwrap_or_default();
            let p = data.instance_props(tpl, &own);
            let tag = num(&p, "tag") as i64;
            if tag != 0 {
                m.tagged.entry(tag).or_default().push(id);
            }
            match tpl {
                "marker" => {
                    let action = p.0.get("action").and_then(|x| x.as_str()).unwrap_or("none").to_owned();
                    // the action name -> flags (0x0044b130: _stricmp, "ic_" by a 3-character compare)
                    let flags = match action.to_ascii_lowercase().as_str() {
                        "hide" => Marker::HIDE,
                        "attention_guard" => Marker::ATTENTION_GUARD,
                        "ready_guard" => Marker::READY_GUARD,
                        "trigger_charge" => Marker::TRIGGER_CHARGE,
                        "trigger_charge_tag" => Marker::TRIGGER_CHARGE_TAG,
                        a if a.starts_with("ic_") => Marker::IC,
                        _ => 0,
                    } | if num(&p, "kill") != 0.0 { Marker::KILL } else { 0 }
                        | if num(&p, "deactivate") != 0.0 { Marker::DEACTIVATE } else { 0 };
                    m.markers.push(Marker {
                        id,
                        pos,
                        // guess: the placement's -y axis as for the trenches' ref_barracade (0x00453150 not traced)
                        dir: (game_to_engine() * -mat.y_axis.truncate()).with_y(0.0).normalize_or(Vec3::Z),
                        matrix: mat,
                        tag: num(&p, "marker_tag") as i64,
                        sequence: num(&p, "sequence") as i64,
                        next: (num(&p, "marker_next_min") as i64, num(&p, "marker_next_max") as i64),
                        strict: num(&p, "strict") != 0.0,
                        face_dir: num(&p, "face_dir") != 0.0,
                        alarm_tag: num(&p, "alarm_tag") as i64,
                        trigger: num(&p, "trigger") as i64,
                        action,
                        flags,
                        scatter: (num(&p, "scatter_min") as f32, num(&p, "scatter_max") as f32),
                        speed: num(&p, "speed") as f32,
                    })
                }
                "grunt_generator" => {
                    // AlarmGenerator_Slot6 0x00405490: the six counts (num_rocket_captain and num_monk default 0), the
                    // distances and tags
                    let weights = ["num_bow", "num_regular", "num_jumper_captain", "num_captain", "num_rocket_captain", "num_monk"].map(|k| num(&p, k) as i64);
                    let bone = |b: &str| {
                        let local = crate::mission::pub_bone(dir, "grunt_generator", b).unwrap_or(Vec3::ZERO);
                        game_to_engine() * mat.transform_point3(local)
                    };
                    if weights.iter().sum::<i64>() > 0 {
                        m.generators.push(Generator {
                            id,
                            pos,
                            spawn: bone("ref_spawn_00"),
                            exit: bone("ref_exit_00"),
                            weights,
                            min_distance: num(&p, "min_spawn_distance") as f32,
                            max_distance: num(&p, "max_distance") != 0.0,
                            tags: (num(&p, "marker_tag_min") as i64, num(&p, "marker_tag_max") as i64),
                            alert_group: num(&p, "alert_group") as i64,
                            priority: (num(&p, "priority_target_tag_min") as i64, num(&p, "priority_target_tag_max") as i64),
                        });
                    }
                }
                _ => {}
            }
        }
        // MarkerObject_BuildSpline 0x0044b540: every head (sequence 0) collects the markers of its tag in sequence order
        let mut tags: Vec<i64> = m.markers.iter().filter(|k| k.sequence == 0).map(|k| k.tag).collect();
        tags.dedup();
        for tag in tags {
            let mut list: Vec<usize> = (0..m.markers.len()).filter(|&i| m.markers[i].tag == tag).collect();
            list.sort_by_key(|&i| m.markers[i].sequence);
            m.splines.insert(tag, list);
        }
        // the mission's alarm settings (Game+0x26d4, +0xb3c / +0xb40)
        if let Ok(mt) = std::fs::read_to_string(dir.join("mission.json")).map(|t| serde_json::from_str::<serde_json::Value>(&t).unwrap_or_default()) {
            let s = |k: &str| mt.pointer(&format!("/mission/resolved/{k}")).and_then(|v| v.as_f64());
            if let Some(n) = s("alarm_generator_max_troops") {
                m.max_troops = n as usize;
            }
            if let (Some(a), Some(b)) = (s("alarm_min_delay"), s("alarm_max_delay")) {
                m.delay = (a as f32, b as f32);
            }
        }
        info!("alarm: {} markers, {} generators, max troops {}", m.markers.len(), m.generators.len(), m.max_troops);
        m
    }

    /// AI_FollowMarkerSpline 0x0042d1b0's pick: a tag in min..=max by ((rand * (max - min + 1)) >> 15) + min, and its
    /// spline (Marker_FindSpline 0x0044c030: the head, sequence 0). None: the original logs "AI : Can't find marker
    /// spline group" and removes the grunt (vt+0xdc); here he stays where he is (guess-free fallback: no lev_1_m1 tag
    /// lacks a head).
    pub fn pick_spline(&self, tags: (i64, i64), r: f32) -> Option<i64> {
        let n = (tags.1 - tags.0 + 1).max(1);
        let tag = tags.0 + ((r * n as f32) as i64).min(n - 1);
        self.splines.contains_key(&tag).then_some(tag)
    }

    /// The marker index `i` of a spline.
    pub fn spline_marker(&self, tag: i64, i: usize) -> Option<&Marker> {
        self.splines.get(&tag).and_then(|s| s.get(i)).map(|&k| &self.markers[k])
    }

    /// The marker's index into `markers`.
    pub fn spline_index(&self, tag: i64, i: usize) -> Option<usize> {
        self.splines.get(&tag).and_then(|s| s.get(i)).copied()
    }

    /// The speed of the walk to marker `i` of the spline (AIMarker_PrevSpeed 0x0044b4f0): the speed of the marker
    /// before it, for the head the last marker's; 0 when it names none.
    pub fn leg_speed(&self, tag: i64, i: usize) -> f32 {
        let Some(s) = self.splines.get(&tag).filter(|s| !s.is_empty()) else { return 0.0 };
        let prev = if i > 0 { i - 1 } else { s.len() - 1 };
        s.get(prev).map_or(0.0, |&k| self.markers[k].speed)
    }

    pub fn spline_len(&self, tag: i64) -> usize {
        self.splines.get(&tag).map_or(0, |s| s.len())
    }
}

/// A grunt reached its marker.
#[derive(Message, Clone)]
pub struct MarkerArrival {
    pub marker: usize,
    pub at: Vec3,
    pub voice_set: u8,
}

/// A troop of the alarm controller (the list 0x0063a1ec: counted toward its cap while alive).
#[derive(Component, Clone, Copy, Debug, Default)]
pub struct AlarmTroop {
    /// sent by a max_distance generator (also on the list 0x0063a1e8): dies when far from the player and unseen
    /// (`cull_far_troops`)
    pub far_cull: bool,
}

pub struct AlarmPlugin;

impl Plugin for AlarmPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<Markers>().add_message::<MarkerArrival>().add_systems(Update, (arrivals, sirens_gone, controller, log).chain().run_if(crate::playing));
    }
}

/// Arrival at an alarm marker (Marker_ProcessArrival 0x0044ba30): the object of its alarm_tag (Object_FindByTag
/// 0x004592e0: the first one in load order that still exists; each level's alarm tags name one object) joins the siren
/// list once, with fx_siren and the runner's Help bark (vt+0x264).
fn arrivals(
    mut m: ResMut<Markers>,
    mut ev: MessageReader<MarkerArrival>,
    ids: Query<(Entity, &InstanceId)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    for a in ev.read() {
        let alarm = m.markers[a.marker].alarm_tag;
        if alarm == 0 {
            continue;
        }
        // (a destroyed alarm object without a husk is gone)
        let Some((e, id)) = m.tagged.get(&alarm).and_then(|tagged| tagged.iter().find_map(|&id| ids.iter().find(|(_, i)| i.0 == id).map(|(e, _)| (e, id)))) else { continue };
        if m.sirens.contains(&e) {
            continue;
        }
        m.sirens.push(e);
        fx.write(crate::fx::StartFx { name: "fx_siren".into(), at: Vec3::ZERO, rotation: Quat::IDENTITY, follow: Some(e), position_only: false });
        sounds.write(crate::audio::PlaySound::at(crate::ai::voice("Help", a.voice_set), a.at));
        info!("EVENT alarm: siren {id} raised (marker {})", m.markers[a.marker].id);
    }
}

/// A destroyed siren leaves the list and its loop stops.
fn sirens_gone(mut m: ResMut<Markers>, mut died: MessageReader<crate::fx::Died>, mut stop: MessageWriter<crate::audio::StopSound>) {
    for d in died.read() {
        if let Some(i) = m.sirens.iter().position(|&e| e == d.0) {
            m.sirens.remove(i);
            stop.write(crate::audio::StopSound("alm_airraid_lp".into()));
            info!("EVENT alarm: siren destroyed");
        }
    }
}

/// The placed objects in the collidable list World+0xe9c (DefaultGameObject_Load 0x0044fd20, game_03.cpp:526-567: every
/// object with a collider but the collide barriers, which get filter 0x40 and no lists), by their colliders. (Husks
/// made later are not listed here: none stands between a generator and the player at the level starts.) A collide
/// barrier (an invisible wall) is in no list, so it hides nothing.
type Collidable = std::collections::HashSet<oxijolt::prelude::BodyId>;

fn collidable(placed: &Query<(&InstanceId, &crate::level::Placed)>, dir: &std::path::Path, data: &crate::gamedata::GameData) -> Collidable {
    let props = crate::vehicles::placements(dir, data);
    placed
        .iter()
        .filter(|(id, _)| props.get(&id.0).is_none_or(|p| p.f("collide_barrier", 0.0) == 0.0))
        .filter_map(|(_, pl)| pl.collider)
        .collect()
}

/// 0x00404d00: the player sees the object at `at` (a generator, or a far troop: `cull_far_troops`). It must be in his
/// 180-degree view (vt+0xf0(object, 180)). Then a level ray at the height of his eye (his ref_hit_01) from above the
/// object toward the eye, 5 units past it (K(0x005bcbdc); Ray_CastQuery 0x00414110: only the collidable list World+0xe9c,
/// the AI list World+0xe7c excluded: not the landscape, not the characters) decides: nothing met is seen; any
/// collidable object met hides it. (The original lets one kind of object through: one on the player's side, vt+0xe4,
/// that is a mover, 0x004531e0: flag 0x40 of +0x424, set for the characters and weapons by 0x00457810, or a move speed
/// from vt+0x80, the AI's drive speeds; and that the camera is not fading, +0x424 bit 11, camfade.rs. Every listed
/// object here is a placed static one, a peasants' house included, so none passes: guess that a house's vt+0x80 gives
/// no speed, no capture.) Roman's own body has no collider here (a walker): the ray runs past the eye and stops.
fn seen_by_player(physics: &crate::physics::Physics, at: Vec3, eye: Vec3, fwd: Vec3, collidable: &Collidable) -> bool {
    let to = (eye - at).with_y(0.0);
    if to.normalize_or_zero().dot(fwd) > 0.0 {
        return false;
    }
    let dir = to.normalize_or(Vec3::X);
    let (mut from, mut left) = (at.with_y(eye.y), to.length() + 5.0);
    for _ in 0..32 {
        let Some((b, hit, _)) = physics.raycast(from, dir, left) else { return true };
        if collidable.contains(&b) {
            return false;
        }
        left -= hit.distance(from) + 0.05;
        if left <= 0.0 {
            return true;
        }
        from = hit + dir * 0.05;
    }
    true
}

/// What the player sees (0x00404d00's inputs): where he stands, his eye (ref_hit_01) and his facing, and the
/// collidable objects that hide things from him.
#[derive(SystemParam)]
struct Sight<'w, 's> {
    player: Query<'w, 's, (&'static GlobalTransform, &'static crate::player::Roman, Option<&'static crate::skin::Animator>)>,
    joints: Query<'w, 's, &'static GlobalTransform>,
    physics: NonSend<'w, crate::physics::Physics>,
    placed: Query<'w, 's, (&'static InstanceId, &'static crate::level::Placed)>,
    data: Res<'w, crate::gamedata::GameData>,
    lib: Res<'w, crate::skin::AnimLibrary>,
    listed: crate::levelreset::PerLevel<'w, 's, Option<Collidable>>,
}

/// The player's view this frame.
struct View<'a> {
    pos: Vec3,
    eye: Vec3,
    fwd: Vec3,
    physics: &'a crate::physics::Physics,
    listed: &'a Collidable,
}

impl View<'_> {
    /// 0x00404d00 for an object at `at`.
    fn sees(&self, at: Vec3) -> bool {
        seen_by_player(self.physics, at, self.eye, self.fwd, self.listed)
    }

    /// The squared distance from the player in the plane (the original's x / y in game space).
    fn dist2(&self, at: Vec3) -> f32 {
        (at - self.pos).with_y(0.0).length_squared()
    }
}

impl Sight<'_, '_> {
    fn view(&mut self) -> Option<View<'_>> {
        let (pt, r, anim) = self.player.single().ok()?;
        let pos = pt.translation();
        let eye = anim.and_then(|a| a.skeleton.index.get("ref_hit_01").and_then(|&i| self.joints.get(a.joints[i]).ok())).map_or(pos + Vec3::Y * 7.0, |j| j.translation());
        let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
        let (placed, lib, data) = (&self.placed, &self.lib, &self.data);
        let listed = self.listed.get().get_or_insert_with(|| collidable(placed, &lib.dir, data));
        Some(View { pos, eye, fwd, physics: &self.physics, listed })
    }
}

/// The alarm controller (0x00404e50): its far troops' cull, then, while the alarm is on, the spawn timer and a troop
/// from the best generator.
fn controller(
    time: Res<Time>,
    mut m: ResMut<Markers>,
    mut rng: ResMut<crate::fx::FxStats>,
    troops: Query<(Entity, &GlobalTransform, &crate::fx::Health, &AlarmTroop)>,
    mut sight: Sight,
    // the spawn holds (0x00404e50 tests the same four specials as the barracks' 0x0040cb10: mission.rs SpecialsHold)
    (mission, holds): (Option<Res<crate::mission::Mission>>, crate::mission::SpecialsHold),
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
    mut ends: MessageWriter<crate::ai::notes::ScriptEnd>,
) {
    if m.generators.is_empty() {
        return;
    }
    let dt = time.delta_secs();
    // rolled when the controller is made at load (0x004049a0): its +0x460 by its set-up (0x00404a40), then the spawn
    // timer (0x00404940)
    let Some(timer) = m.timer else {
        m.cull_timer = cull_delay(&mut rng);
        m.timer = Some(rng.range(m.delay.0, m.delay.1));
        return;
    };
    let view = sight.view();
    m.cull_timer -= dt;
    if m.cull_timer <= 0.0 {
        if let Some(v) = &view {
            cull_far_troops(&mut *m, v, &troops, &mut ends);
        }
        m.cull_timer = cull_delay(&mut rng);
    }
    // the sirens listed (World+0xef0) or the alarm forced (Game+0xdcc)
    if m.sirens.is_empty() && !mission.is_some_and(|ms| ms.alarm_forced) {
        return;
    }
    if holds.holds() {
        return;
    }
    let left = timer - dt;
    if left > 0.0 {
        m.timer = Some(left);
        return;
    }
    // (rolled again whatever the outcome: the end of 0x00404e50)
    m.timer = Some(rng.range(m.delay.0, m.delay.1));
    if troops.iter().filter(|t| t.2.0 > 0.0).count() >= m.max_troops {
        return;
    }
    let Some(v) = view else { return };
    let Some(gi) = m.pick_generator(&v) else { return };
    let g = &m.generators[gi];
    // the troop type by weight: pick = (rand * total >> 15) + 1, the first type whose running sum reaches it
    let total: i64 = g.weights.iter().sum();
    let pick = ((rng.rand() * total as f32) as i64).min(total - 1) + 1;
    let (mut sum, mut ty) = (g.weights[0], 0usize);
    while pick > sum && ty < 5 {
        ty += 1;
        sum += g.weights[ty];
    }
    // "grunt", "grunt_regular", "captain" (a jumper captain: 0x00425d30(1)), "captain", none (num_rocket_captain:
    // "Generator : unkown troop type" and the original's stack-slot bug, not reached: no level sets it), "monk". (The
    // troop's tag argument of 0x0045f020 is the controller's own +0x394, which 0x004049a0 fills with 0x3ff, the mask
    // slot of a placement: no tag the generator gives.)
    let Some(tpl) = ["grunt", "grunt_regular", "captain", "captain", "", "monk"].get(ty).filter(|t| !t.is_empty()) else { return };
    let (id, at) = (g.id, g.spawn);
    // 0x0042dd50: the generator's marker tags and alert group; the walk to its ref_exit_00 (state 0x5a), then the marker
    // spline of a tag, or with no tags 20..50 ahead and the charge (+0x488 0x200, state 0x5b -> 0x0042ce70); 0x0042cc60:
    // its priority targets; a max_distance generator's troop joins the far list 0x0063a1e8 too
    let tags = (g.tags.0 != 0).then_some(g.tags);
    spawn.write(crate::level::SpawnGrunt { at, template: (*tpl).into(), alarm: true, alarm_far_cull: g.max_distance, jumper: ty == 2, exit: Some(g.exit), marker_tags: tags, alert_group: g.alert_group, priority_tags: g.priority, ..default() });
    // the newest spawn point: the oldest drops out
    m.recent = [Some(id), m.recent[0], m.recent[1]];
    info!("EVENT alarm: generator {id} sent a {tpl}");
}

/// The controller's cull delay +0x460: 5 + rand * 10 / 32768 seconds (0x00404a40, 0x00404e50).
fn cull_delay(rng: &mut crate::fx::FxStats) -> f32 {
    5.0 + rng.rand() * 10.0
}

/// 0x00404e50 part 1: every troop on the far list (a max_distance generator's) farther than alarm_ai_max_distance from
/// the player in the plane that he does not see (0x00404d00) dies (vt+0xd4 with a record of kind 0x8000 and no hit:
/// the death anim, ai/notes.rs `End::Kill`).
fn cull_far_troops(m: &mut Markers, v: &View, troops: &Query<(Entity, &GlobalTransform, &crate::fx::Health, &AlarmTroop)>, ends: &mut MessageWriter<crate::ai::notes::ScriptEnd>) {
    for (e, t, h, troop) in troops {
        let at = t.translation();
        if !troop.far_cull || h.0 <= 0.0 || v.dist2(at) <= m.range * m.range || v.sees(at) {
            continue;
        }
        ends.write(crate::ai::notes::ScriptEnd { entity: e, end: crate::ai::notes::End::Kill });
        m.culled += 1;
        info!("EVENT alarm: far troop {e:?} culled");
    }
}

impl Markers {
    /// The generator to send a troop (0x00404e50's walk of the list 0x0063a1f0, in load order): a candidate no nearer
    /// than the best so far is skipped first; then one inside its min_spawn_distance, one with max_distance beyond
    /// 0.75 x alarm_ai_max_distance, one the player sees; one of the last three spawn points becomes the fallback (the
    /// first such met) instead of the best. A fallback used gives up its spawn-point slot (emptied, not closed up).
    fn pick_generator(&mut self, v: &View) -> Option<usize> {
        let far = 0.75 * self.range;
        let (mut best, mut best_d, mut fallback): (Option<usize>, f32, Option<(usize, usize)>) = (None, 0.0, None);
        for (i, g) in self.generators.iter().enumerate() {
            let d2 = v.dist2(g.pos);
            if best.is_some() && d2 >= best_d {
                continue;
            }
            if g.min_distance > 0.0 && d2 < g.min_distance * g.min_distance {
                continue;
            }
            if g.max_distance && d2 > far * far {
                continue;
            }
            if v.sees(g.pos) {
                continue;
            }
            // (the original searches the slots from the oldest)
            if let Some(slot) = (0..3).rev().find(|&s| self.recent[s] == Some(g.id)) {
                fallback = fallback.or(Some((i, slot)));
                continue;
            }
            best = Some(i);
            best_d = d2;
        }
        best.or_else(|| {
            let (i, slot) = fallback?;
            self.recent[slot] = None;
            Some(i)
        })
    }
}

/// The raised sirens in the playtest state log.
fn log(m: Res<Markers>, ids: Query<&InstanceId>, troops: Query<(), With<AlarmTroop>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let raised: Vec<u32> = m.sirens.iter().filter_map(|&e| ids.get(e).ok().map(|i| i.0)).collect();
    state.0.insert("alarm".into(), serde_json::json!({ "sirens": raised.len(), "raised": raised, "troops": troops.iter().count(), "culled": m.culled }));
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ai::notes::End;

    fn marker(flags: u32) -> Marker {
        Marker {
            id: 75,
            pos: Vec3::ZERO,
            dir: Vec3::Z,
            matrix: Mat4::IDENTITY,
            tag: 500,
            sequence: 1,
            next: (0, 0),
            strict: false,
            face_dir: false,
            alarm_tag: 0,
            trigger: 5000,
            action: String::new(),
            flags,
            scatter: (0.0, 0.0),
            speed: 0.0,
        }
    }

    /// 0x0044ba30: deactivate (the breach spline's last marker, lev_3_m4_bd 75) removes the AI before kill is looked
    /// at; kill alone is a death; neither leaves him to the action.
    #[test]
    fn kill_and_deactivate_end_the_ai() {
        assert_eq!(marker(Marker::DEACTIVATE).ends_ai(), Some(End::Remove));
        assert_eq!(marker(Marker::DEACTIVATE | Marker::KILL).ends_ai(), Some(End::Remove));
        assert_eq!(marker(Marker::KILL | Marker::HIDE).ends_ai(), Some(End::Kill));
        assert_eq!(marker(Marker::HIDE).ends_ai(), None);
    }
}
