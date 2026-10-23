//! Armed and Dangerous specifics: the game's axes and units, and how a placed object's property set
//! (`behavior`, `collision_lod_index`, health and damage keys) decides what it becomes in the engine.

use crate::levelreset::LevelAppExt;
use crate::ai;
use crate::fx::{Bounds, DClass, Destructible, Died, Health};
use crate::gamedata::{GameData, Props};
use crate::physics::Physics;
use crate::player::BombAssets;
use crate::scene::{self, ClonedParts, Library, PartBind, SceneFile};
use bevy::light::{CascadeShadowConfigBuilder, NotShadowCaster};
use bevy::prelude::*;
use oxijolt::prelude::*;

/// 1 game unit in metres: a grunt is about 9.8 units tall (guess: 2 m), a pine 144 units (29 m).
pub const METERS_PER_UNIT: f32 = 0.2;

/// Game axes (Z up, as authored in 3ds Max) to the engine's (Y up): a rotation, so handedness is kept.
pub fn game_to_engine() -> Quat {
    Quat::from_rotation_x(-std::f32::consts::FRAC_PI_2)
}

#[derive(Resource, Default, Clone)]
pub struct LevelInfo {
    pub name: String,
    pub center: Vec3,
    pub size: Vec3,
    pub instances: usize,
    pub models: usize,
    pub draws: usize,
    pub triangles: usize,
    pub static_bodies: usize,
    pub static_triangles: usize,
    pub grunts: usize,
    pub destructibles: usize,
    /// The level's fog (global_settings): near, far (game units), colour.
    pub fog: Option<(f32, f32, Color)>,
    pub player: Option<Entity>,
    /// camera_fx scripts to start on the camera: (fx name, position only)
    pub camera_fx: Vec<(String, bool)>,
}

/// The placed instance's 1-based id in the level's inst bank (what triggers and tags refer to).
#[derive(Component, Clone, Copy)]
pub struct InstanceId(pub u32);

/// An object's tag (+0x390, its getter 0x00453290) on an object made at run time that something finds by tag
/// (Object_FindByTag 0x004592e0): a squad's goliath takes its squad's tag (goliath.rs), which the ticker bomb sites
/// locked to it look up (mission.rs `lock_sites`). Placed objects' tags are read from their property sets.
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
pub struct ObjectTag(pub i64);

/// `behavior = background` (the sky dome): drawn around the camera, unlit, no fog, no collision.
/// BackgroundObject_Load 0x0040a890 reads sorting_order (draw order among backgrounds) and fog (default 0: the
/// materials' fog bit 0x10 cleared). Every background of the shipped levels (one dome, two on lev_4_m1 / 4_m2 /
/// bonus_m1) uses p_background: sorting_order 1, fog 0, one model per template, so the original's fog-slot bug
/// (docs/original-bugs.md: meshes 2+ follow the previous mesh's part count) never fires and equal orders sort nothing:
/// neither is read here (a background with fog 1 would need the level fog on its material). Its slot 7
/// (0x0040a9e0) sets three flag bytes of its collision description (guess: no collision): no collider here.
#[derive(Component)]
pub struct Background;

/// On an object in the damage takers' list World+0xea4 ([`in_damage_list`]; the AIs are in it too): what stops the
/// crosshair's ray on a gun.
#[derive(Component, Clone, Copy, Debug)]
pub struct DamageTaker;

/// The model parts (children) and static collider of a placed object, so its husk can replace them.
#[derive(Component)]
pub struct Placed {
    /// the class template's name (+0x414, 0x004555d0; Object_GetName 0x00453270). The tag (+0x390, 0x00453290) is
    /// read from the property set where it is used (mission.rs tags, the window grunts' obj_tag).
    pub template: String,
    pub collider: Option<BodyId>,
    /// the husk template it becomes when it dies, picked at load (Object_LoadBaseProps 0x00451360: husk, or one of
    /// husk, husk1..husk9 at random), with the instance's own values over the template's
    pub husk: Option<String>,
    /// a default object's trigger fields (DefaultGameObject_OnTrigger 0x00450fb0)
    pub triggers: ObjectTriggers,
}

/// A husk: the object a dead one became (Object_SpawnHusk 0x00455660 creates it through ObjectSystem_CreateObject
/// 0x0045f020 from the husk template alone: no placement values, no instance id of its own). The engine keeps the
/// entity (and its InstanceId, which the save game uses); this marks that it is no longer the placed object, so the
/// placed object's send_trigger_on_death is not sent again when a destructible husk dies (mission.rs `deaths`).
#[derive(Component)]
pub struct Husk;

/// The receive_trigger_* fields of a default object (DefaultGameObject_Load 0x0044fd20: +0x4c4 kill, +0x4c8
/// deactivate, +0x4cc start_blocking). 0 = none. Each clears itself when it fires (0x00450fb0).
#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct ObjectTriggers {
    pub kill: i64,
    pub deactivate: i64,
    pub start_blocking: i64,
}

/// What a received trigger does to a default object (0x00450fb0).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TriggerAct {
    /// vt+0xd4(0): the death slot (DefaultGameObject_OnDeath 0x00450ee0), then Object_Kill (fx_death, husk)
    Kill,
    /// vt+0xdc(): removal (Object_Remove 0x004547b0): gone, no death fx, no husk, no death trigger, no score
    Deactivate,
    /// collision vt+0xf8(0x40, 0) and the projectile list World +0xe90 (0x004546d0): bullets stop on it
    StartBlocking,
}

impl ObjectTriggers {
    pub fn of(p: &Props) -> Self {
        ObjectTriggers {
            kill: p.i("receive_trigger_kill", 0),
            deactivate: p.i("receive_trigger_deactivate", 0),
            start_blocking: p.i("receive_trigger_start_blocking", 0),
        }
    }
    /// DefaultGameObject_OnTrigger 0x00450fb0: the first field equal to the trigger (kill, deactivate, start
    /// blocking, in that order) acts and is cleared; the handler always returns false, so Object_SendTrigger
    /// 0x00461870 goes on offering the trigger to the other objects. Trigger 0 never matches (0x00461870 drops it).
    pub fn receive(&mut self, trigger: i64) -> Option<TriggerAct> {
        if trigger == 0 {
            return None;
        }
        if trigger == self.kill {
            self.kill = 0;
            Some(TriggerAct::Kill)
        } else if trigger == self.deactivate {
            self.deactivate = 0;
            Some(TriggerAct::Deactivate)
        } else if trigger == self.start_blocking {
            self.start_blocking = 0;
            Some(TriggerAct::StartBlocking)
        } else {
            None
        }
    }
}

/// The husk an object becomes (Object_LoadBaseProps 0x00451360): none when `husk` is missing; with husk1..husk9
/// (read while they are present, in order) one of the n names at random, (rand() * n) >> 15. A present "null" is a
/// name like any other: it is drawn among them (the draw is made), and picked it means no husk (Object_SpawnHusk
/// 0x00455660 skips "null").
pub fn pick_husk(p: &Props, rng: &mut crate::fxvm::CrtRand) -> Option<String> {
    let first = p.s("husk")?;
    let mut names = vec![first.to_owned()];
    for n in 1..=9 {
        match p.s(&format!("husk{n}")) {
            Some(h) => names.push(h.to_owned()),
            None => break,
        }
    }
    // (the original draws rand() for a single name too)
    let i = ((rng.rand() as usize) * names.len()) >> 15;
    names.get(i).filter(|h| !h.eq_ignore_ascii_case("null")).cloned()
}

/// A troop to spawn (barracks / alarm generators): a grunt template at a point, awake and hunting the player.
#[derive(Message, Clone, Default)]
pub struct SpawnGrunt {
    pub at: Vec3,
    pub template: String,
    /// a troop of the alarm controller (alarm.rs)
    pub alarm: bool,
    /// an alarm troop of a max_distance generator: culled far from the player (alarm.rs `AlarmTroop::far_cull`)
    pub alarm_far_cull: bool,
    /// a jumper captain (troop type 2 of a barracks or an alarm generator: 0x00425d30(1), the turbo pack and the
    /// jumper fire range, as the property jumper_captain gives a placed captain)
    pub jumper: bool,
    /// a troop of this barracks (mission.rs): it walks out to `exit`, then to a marker of `marker_tags`
    pub barracks: Option<u32>,
    pub exit: Option<Vec3>,
    pub marker_tags: Option<(i64, i64)>,
    pub alert_group: i64,
    /// the generator's priority_target_tag_min / max, the troop's priority targets (0x0042cc60: +0x858 / +0x85c)
    pub priority_tags: (i64, i64),
    /// template switches turned on for this troop (a barracks' jumper / rocket captain: "jumper_captain",
    /// "rocket_captain", mission.rs TROOP_TYPES)
    pub switches: Vec<&'static str>,
    /// a grunt released by the army (army.rs): the orders it carries, inserted as its component
    pub army: Option<crate::army::ArmyGrunt>,
    /// a squad member (ai/squad.rs): his squad and bone; idle at it until the squad moves
    pub squad: Option<ai::squad::SquadLink>,
    /// a grunt hidden in a host at load (ai/ic.rs): waiting at its node for his move's trigger
    pub ic: Option<ai::ic::IcSpawn>,
    /// a troop dropped from a vehicle by its anim's note (vehicles/boss.rs)
    pub dropped: Option<DroppedTroop>,
    /// a grunt a destroyed building throws out dead (tossed.rs, 0x0040d390): his body's velocity
    pub thrown: Option<Vec3>,
    /// the smoking wounded troop a destroyed barracks' ruin sends out of its exit slot (tossed.rs, 0x0042dc20)
    pub smoking: bool,
}

/// A troop a vehicle drops (the note handler 0x00409140): create_rocket_captain_IC REF MOVER makes a "captain" that is a
/// rocket captain (RocketCaptain_ReadProperties 0x00434ac0) firing from 50..100 (0x00434a80(50, 100)), counted as an
/// enemy (Game+0x2710), and gives it the IC anim MOVER (vt+0x238), whose notes set what it does on landing
/// (GenericAIObject_HandleEvent 0x0043d180: freefall_attack_village LO HI makes houses of tags LO..HI its targets,
/// freefall_attack the player); create_object_ic TYPE REF MOVER makes TYPE with MOVER likewise.
/// A troop a vehicle dropped (the list Game+0xa84 +0xf24 it joins, 0x004546d0).
#[derive(Component)]
pub struct Dropped;

#[derive(Clone, Debug, Default)]
pub struct DroppedTroop {
    pub rocket: bool,
    /// the IC anim the troop plays from the drop point (its notes are read; the rappel itself is not played: debt, the
    /// troop starts on the ground under the drop point)
    pub mover: Option<String>,
}

/// A husk object to make where a body was removed (Object_SpawnHusk 0x00455660 for a character: corpses.rs): the
/// template's model, collision and properties as a placed object's husk stage, at `world` (engine space).
#[derive(Message, Clone)]
pub struct SpawnHusk {
    pub template: String,
    pub world: Mat4,
}

/// The `Center` translation track of a rigid (unskinned) model's own anim, played in a loop on its placement.
#[derive(Component)]
pub struct RigidAnim {
    keys: Vec<(f32, Vec3)>,
    duration: f32,
    t: f32,
    base: Option<Vec3>,
}

impl RigidAnim {
    fn load(dir: &std::path::Path, model: &str) -> Option<Self> {
        let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("anims").join(format!("{model}.json"))).ok()?).ok()?;
        let track = v.get("tracks")?.get("Center")?.get("t")?.as_array()?;
        let keys: Vec<(f32, Vec3)> = track
            .iter()
            .filter_map(|k| {
                let k = k.as_array()?;
                let f = |i: usize| k.get(i).and_then(|x| x.as_f64()).map(|x| x as f32);
                Some((f(0)?, Vec3::new(f(1)?, f(2)?, f(3)?)))
            })
            .collect();
        if keys.len() < 2 {
            return None;
        }
        Some(RigidAnim { duration: v.get("duration")?.as_f64()? as f32, keys, t: 0.0, base: None })
    }
}

fn rigid_anims(time: Res<Time>, mut q: Query<(&mut RigidAnim, &mut Transform)>) {
    for (mut a, mut t) in &mut q {
        let base = *a.base.get_or_insert(t.translation);
        a.t = (a.t + time.delta_secs()).rem_euclid(a.duration.max(1e-3));
        let i = a.keys.iter().rposition(|k| k.0 <= a.t).unwrap_or(0);
        let (t0, p0) = a.keys[i];
        let (t1, p1) = a.keys[(i + 1).min(a.keys.len() - 1)];
        let p = if t1 > t0 { p0.lerp(p1, (a.t - t0) / (t1 - t0)) } else { p0 };
        // game space offset (z up) on the placement's own axes
        t.translation = base + t.rotation * p;
    }
}

/// The default animation every object starts at load: Object_LoadProperties 0x004517b0 plays anim slot vt+0x40
/// (0x00451780: slot 1 of the template, slot 0 being "staticmodel") through 0x00408e30 with flags 0x81 (flag 1 =
/// loop). Placed objects whose model has a skeleton get it: the oil pumps' oilpump_cycle (4 s), the guard alarm's
/// guard_alarm_0 (the siren turning, 6.7 s), stig_mgun_ref (a still pose). Characters' own anim systems (charanim.rs)
/// replace it: it is played only when nothing else has started an anim by the frame after spawning. With "anim_rand"
/// (the lev_5 guard lanterns' swing) it starts at a random point instead (flags 0xa01, phase rand() / 32768 of its
/// length), so such objects do not move in step.
#[derive(Component)]
pub struct DefaultAnim {
    anim: String,
    seen: bool,
    /// anim_rand's start, a fraction of the anim's length
    phase: Option<f32>,
}

impl DefaultAnim {
    /// `anim` looped from the frame after the spawn unless something else plays first.
    pub fn new(anim: &str) -> Self {
        DefaultAnim { anim: anim.to_owned(), seen: false, phase: None }
    }
}

fn default_anims(mut commands: Commands, mut q: Query<(Entity, &mut DefaultAnim, &mut crate::skin::Animator)>, mut lib: ResMut<crate::skin::AnimLibrary>) {
    for (e, mut d, mut a) in &mut q {
        // one Update pass first, for the characters' systems to pick their own anim
        if !d.seen {
            d.seen = true;
            continue;
        }
        commands.entity(e).remove::<DefaultAnim>();
        if a.layers.first().is_some_and(|l| !l.anim.is_empty()) || lib.anim(&d.anim).is_none() {
            continue;
        }
        match d.phase {
            Some(phase) => a.play_at(0, &d.anim, true, crate::skin::Mask::All, phase * lib.duration(&d.anim).unwrap_or(0.0)),
            None => a.play(0, &d.anim, true, crate::skin::Mask::All),
        }
    }
}

/// The state log's "oilpump" (playtest expect steps): the lowest-id oil pump's playing anim and how far its
/// bone_pump joint has travelled up and down so far (model space z, game units: lo, hi, travel = hi - lo).
fn log_oilpump(
    q: Query<(&InstanceId, &Placed, &crate::skin::Animator, &GlobalTransform)>,
    globals: Query<&GlobalTransform>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    mut range: crate::levelreset::PerLevel<Option<(u32, f32, f32)>>,
) {
    let Some(mut state) = state else { return };
    let range = range.get();
    let Some((id, _, a, g)) = q.iter().filter(|(_, p, _, _)| p.template == "oilpump").min_by_key(|(i, _, _, _)| i.0) else { return };
    let Some(j) = a.skeleton.index.get("bone_pump").and_then(|&i| globals.get(a.joints[i]).ok()) else { return };
    let z = g.affine().inverse().transform_point3(j.translation()).z;
    let r = range.get_or_insert((id.0, z, z));
    r.1 = r.1.min(z);
    r.2 = r.2.max(z);
    let anim = a.layers.first().map(|l| l.anim.clone()).unwrap_or_default();
    state.0.insert("oilpump".into(), serde_json::json!({ "id": id.0, "anim": anim, "z": z, "lo": r.1, "hi": r.2, "travel": r.2 - r.1 }));
}

/// The object model's systems: the original's ObjectSystem (ObjectSystem_Construct 0x0045a4e0: the object lists,
/// the force action pool, the behaviour table) as plugin state; each object's update (Object_Update 0x004540b0, once
/// per game time, 0x004540a0 resets it: the animation advance and its events) is Bevy's Update schedule plus
/// skin.rs; the level's teardown (kill everything 0x0045d670, delete 0x0045da20) is the process ending or
/// restarting (a load starts a fresh process, savegame.rs).
pub struct LevelPlugin;

impl Plugin for LevelPlugin {
    fn build(&self, app: &mut App) {
        // husks after mission.rs `deaths`: a dead placed object is told from a dying husk by the Husk marker the swap
        // inserts, so the death trigger must be read first. And before the fx vm: the death fx's debris is made with the
        // husk's collider already in place, so a piece made inside the husk passes through it (fxvm/body.rs
        // make_dynamic's `inside`). The arch's keystone (fx_explosion_arch -> fx_debri_arch_1) is made in the gap of
        // the broken arch v_archway_a_da, whose broken faces meet its own: with the husk made after it, the keystone
        // touched it at once, set off its on-hit blast (0x76: 300 damage, radius 30 / 60) up there and stuck; now it
        // drops through and the blast kills the grunts under the arch, as in the original (player report 2026-10-18).
        // The original makes the fx first (Object_SpawnHusk 0x00455660: Fx_Create, then the husk); the order of its
        // Havok contacts at creation is not traced
        app.add_message::<SpawnGrunt>()
            .add_message::<RestoreStage>()
            .add_level_message::<SpawnHusk>()
            .init_level_resource::<TriggerLog>()
            .add_systems(Update, (husks.after(crate::fx::FxSet).after(crate::mission::deaths).before(crate::fxvm::FxVmSet), restore_stages, spawn_grunts, spawn_husks, rigid_anims, audio_loops, object_triggers))
            .add_systems(PostUpdate, follow_camera.before(TransformSystems::Propagate))
            .add_systems(PostUpdate, sky_never_culled)
            .add_systems(PostUpdate, default_anims.before(crate::skin::AnimateSet))
            .add_systems(PostUpdate, (log_oilpump, log_objects).after(TransformSystems::Propagate));
    }
}

/// A collide barrier waiting for its start_blocking trigger (DefaultGameObject_Load 0x0044fd20: collide_barrier with a
/// receive_trigger_start_blocking gets collision filter 0x4000, which blocks nothing, and no lists): it has no
/// collider until the trigger makes it solid. lev_4_m3's invisible wall w_l4_m3_05 (660) waits for 2345, the trigger
/// that also clears the 80 trees in its way.
#[derive(Component)]
pub struct DormantBarrier {
    /// its collision model (the render model for a collide_poly wall)
    collision: String,
    material: Option<String>,
}

/// Triggers delivered to default objects (Object_SendTrigger 0x00461870 offers every trigger to every object's
/// handler; mission.rs dispatches them as SendTrigger messages). DefaultGameObject_OnTrigger 0x00450fb0: kill is
/// delivered by mission.rs (`Action::Destroy`, from mission.json's "destroyed" edges, which are exactly the
/// receive_trigger_kill fields), so here only its field is cleared; deactivate removes the object (Object_Remove
/// 0x004547b0: no fx, no husk, no death trigger; its collider or movable body, decals and sound loop go);
/// start_blocking switches its collision to blocking (vt+0xf8(0x40, 0): a dormant barrier gets its collider) and puts
/// it in the bullets' list +0xe90. lev_4_m3's trees deactivate on 2345 (lev_4_m1's tree 556 on 4001).
#[allow(clippy::type_complexity)]
fn object_triggers(
    mut events: MessageReader<crate::mission::SendTrigger>,
    mut objects: Query<(Entity, &mut Placed, Option<&InstanceId>, &Transform, Option<&DormantBarrier>)>,
    mut swap: StageSwap,
    mut sent: ResMut<TriggerLog>,
    mut stop: MessageWriter<crate::audio::StopSoundsOn>,
) {
    for crate::mission::SendTrigger(t) in events.read() {
        *sent.0.entry(*t).or_default() += 1;
        for (e, mut p, id, transform, barrier) in &mut objects {
            let Some(act) = p.triggers.receive(*t) else { continue };
            info!("EVENT object {}: trigger {t} -> {act:?}", id.map(|i| i.0).unwrap_or(0));
            match act {
                TriggerAct::Kill => {}
                TriggerAct::Deactivate => {
                    stop.write(crate::audio::StopSoundsOn(e));
                    swap.remove(e, &p);
                }
                TriggerAct::StartBlocking => match barrier {
                    Some(b) => {
                        swap.commands.entity(e).remove::<DormantBarrier>();
                        p.collider = swap.make_solid(b, transform.to_matrix());
                    }
                    None => {
                        if let Some(c) = p.collider {
                            swap.physics.no_bullets.remove(&c);
                        }
                    }
                },
            }
        }
    }
}

/// Every trigger sent so far, with how many times (the state log's "triggers": "triggers.301" = 1).
#[derive(Resource, Default)]
pub struct TriggerLog(pub std::collections::BTreeMap<i64, u32>);

/// The state log's "objects" (playtest expect steps): per placed object's instance id, its template (a husk's once
/// it died), health and max health (destructibles), whether it is a husk and has a collider; "removed" once it is gone. And
/// "triggers": the triggers sent so far.
#[allow(clippy::type_complexity)]
fn log_objects(
    q: Query<(&InstanceId, &Placed, Option<&Destructible>, Has<Husk>)>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    mut seen: crate::levelreset::PerLevel<std::collections::HashSet<u32>>,
    sent: Res<TriggerLog>,
    fx: Res<crate::fx::FxStats>,
    posters: Query<(), With<RewardPoster>>,
) {
    let Some(mut state) = state else { return };
    let seen = seen.get();
    state.0.insert("posters".into(), posters.iter().count().into());
    // every object death so far (fx.rs `dying`: placed objects, windows, kegs, thrown bombs)
    state.0.insert("object_deaths".into(), fx.deaths.into());
    let triggers: serde_json::Map<String, serde_json::Value> = sent.0.iter().map(|(t, n)| (t.to_string(), (*n).into())).collect();
    state.0.insert("triggers".into(), triggers.into());
    let mut out = serde_json::Map::new();
    for (i, p, d, husk) in &q {
        // the objects the object model acts on (not every rock: the log is written every second)
        if d.is_none() && !husk && p.triggers == ObjectTriggers::default() && !seen.contains(&i.0) {
            continue;
        }
        seen.insert(i.0);
        let mut o = serde_json::json!({ "template": p.template, "husk": husk, "collider": p.collider.is_some() });
        if let Some(d) = d {
            o["health"] = d.health.into();
            o["max_health"] = d.max_health.into();
        }
        out.insert(i.0.to_string(), o);
    }
    for id in seen.iter() {
        out.entry(id.to_string()).or_insert_with(|| serde_json::json!({ "removed": true }));
    }
    state.0.insert("objects".into(), out.into());
}

/// The sky dome's parts are never frustum culled: the camera's far plane is the original's (options.rs), the dome
/// lies far beyond it.
fn sky_never_culled(mut commands: Commands, parts: Query<(Entity, &ChildOf), Added<Mesh3d>>, sky: Query<(), With<Background>>) {
    for (e, parent) in &parts {
        if sky.contains(parent.parent()) {
            commands.entity(e).insert(bevy::camera::visibility::NoFrustumCulling);
        }
    }
}

fn follow_camera(cam: Query<&Transform, (With<Camera3d>, Without<Background>)>, mut sky: Query<&mut Transform, With<Background>>) {
    let Ok(c) = cam.single() else { return };
    // horizontally only: the dome keeps its authored height, so its rim stays at the ground's horizon
    for mut t in &mut sky {
        t.translation.x = c.translation.x;
        t.translation.z = c.translation.z;
    }
}

/// What skinned models need while spawning.
pub struct SkinCtx<'a> {
    pub lib: &'a mut crate::skin::AnimLibrary,
    pub binds: &'a mut Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>,
}

/// Spawns one model's parts under `e`; a skinned model also gets its joint entities and an Animator on `e`
/// (upper/lower body split at "belly", the bone the _t / _b anim halves divide at). Returns (draws, triangles).
pub(crate) fn attach_model(commands: &mut Commands, e: Entity, model: &scene::Model, shadows: bool, skin: Option<&mut SkinCtx>, hand_bone: &str) -> (usize, usize, Option<Entity>) {
    let mut inverse_bind: Vec<Mat4> = Vec::new();
    let rig = match (model.skeleton.as_deref(), skin) {
        (Some(name), Some(ctx)) => ctx.lib.skeleton(name, ctx.binds).map(|sk| {
            inverse_bind = ctx.binds.get(&sk.inverse_bind).map(|b| b.to_vec()).unwrap_or_default();
            crate::skin::build_rig(commands, e, sk, Some("belly"))
        }),
        _ => None,
    };
    for (i, (mesh, mat)) in model.parts.iter().enumerate() {
        let mut c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())));
        if !shadows {
            c.insert(NotShadowCaster);
        }
        // the part's owner: the model's entity, or (a rigid part) its bone's joint, the part placed at the bone's
        // inverse bind so that it follows the bone as the original's one-matrix blend does (joint world * inverse
        // bind * vertex, fn_004ed5d0)
        let mut owner = e;
        if let Some(r) = &rig {
            match model.binds.get(i) {
                Some(PartBind::Skinned) | None => {
                    c.insert((crate::skin::skinned_mesh(r), bevy::camera::visibility::NoFrustumCulling));
                }
                Some(PartBind::Bone(b)) => {
                    if let (Some(&j), Some(ib)) = (r.skeleton.index.get(b), r.skeleton.index.get(b).and_then(|&j| inverse_bind.get(j))) {
                        owner = r.joints[j];
                        c.insert(Transform::from_matrix(*ib));
                    }
                }
                Some(PartBind::None) => {}
            }
        }
        let c = c.id();
        commands.entity(owner).add_child(c);
    }
    // the weapon bone (characters carry their weapon at "ref_rhand": grunt init 0x00434fb0; ai::Kind::weapon_bone)
    let hand = rig.as_ref().and_then(|r| r.skeleton.index.get(hand_bone).map(|&i| r.joints[i]));
    if let Some(r) = rig {
        commands.entity(e).insert(r);
    }
    (model.parts.len(), model.triangles, hand)
}

/// A held model in its rest pose under the hand: a skinned one (the Topsy Turvy bomb, the launchers) is drawn as
/// authored, its joint attributes dropped.
fn attach_rest_pose(commands: &mut Commands, hand: Entity, parts: &[(Handle<Mesh>, Handle<StandardMaterial>)], meshes: &mut Assets<Mesh>) -> Entity {
    let held = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
    commands.entity(hand).add_child(held);
    for (mh, mt) in parts {
        let Some(x) = meshes.get(mh).cloned() else { continue };
        let mesh = if x.attribute(Mesh::ATTRIBUTE_JOINT_INDEX).is_some() { meshes.add(x.with_removed_attribute(Mesh::ATTRIBUTE_JOINT_INDEX).with_removed_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT)) } else { mh.clone() };
        let c = commands.spawn((Mesh3d(mesh), MeshMaterial3d(mt.clone()))).id();
        commands.entity(held).add_child(c);
    }
    held
}

/// A weapon model in a character's hand: its ref_hand_00 bone is at the weapon's origin, so the model hangs
/// straight off the hand joint. The original's attachment (Object_Attach 0x004539f0: parent +0x34 referenced, the
/// parent and child bone names, flags +0x424 bits 1 / 2; Object_Detach 0x00453b20; Object_GetParent 0x004539d0) is
/// the Bevy hierarchy here (a child entity under the bone's joint; 0x004539e0 "has a parent" is ChildOf).
pub(crate) fn attach_weapon(commands: &mut Commands, hand: Entity, model: &scene::Model, fix: Transform) -> Entity {
    let holder = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
    commands.entity(hand).add_child(holder);
    for (mesh, mat) in &model.parts {
        let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()), fix)).id();
        commands.entity(holder).add_child(c);
    }
    holder
}

/// A critter: a penguin (PenguinObject, vtable 0x005a1768) or a sheep (SheepObject, 0x005a8d00). The two classes share
/// every method but the anim names (Slot105 0x0048fc90 / 0x0048fac0), the screams and the death lines (0x0048fe20 /
/// 0x0048fc60). Any hit kills one (damage 0x0048fa20, no amount threshold, no team filter), score_value -10 to
/// Accidents; `dead` counts the time since (blown_up, then land_dead, the body lingering 5 s and fading 0.5 s:
/// GoRagdoll 0x00431310's corpse timer). Its grazing and walking: charanim.rs `critters`.
#[derive(Component)]
pub struct Critter {
    pub species: Species,
    /// +0xb60 (the property read 0x0048f140): a critter with a generic_txt, loaded after the mission object of a
    /// level that starts in a turret (0x0044e600, `turret_start_known`), walks between its grazes
    pub roams: bool,
    /// the update's state (+0xb58) and timer (+0xb5c); None until the first update
    pub state: Option<CritterState>,
    pub dead: Option<f32>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Species {
    Penguin,
    Sheep,
}

/// PenguinObject_Update 0x0048f690's states 1 and 2, with their timers.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum CritterState {
    /// 1: the walk anim (8), physics off, for 5..10 s (GenericAI_Update meanwhile)
    Walking(f32),
    /// 2: graze / scan / bleat (anims 0x17 / 0x18 / 0x19), physics on, for 20..35 s
    Grazing(f32),
}

impl Species {
    /// The class of a placed object (its template name: neither class has a behaviour).
    pub fn of(template: &str) -> Option<Species> {
        match template {
            "penguin" => Some(Species::Penguin),
            "sheep" => Some(Species::Sheep),
            _ => None,
        }
    }

    /// An anim id's name (Slot105: PenguinObject 0x0048fc90, SheepObject 0x0048fac0).
    pub fn anim(self, id: u32) -> &'static str {
        match (self, id) {
            (Species::Penguin, 0..=3) => "pngn_stand",
            (Species::Penguin, 8..=0xa) => "pngn_walk",
            (Species::Penguin, 0xb | 0xc) => "pngn_run",
            (Species::Penguin, 0xd) => "pngn_salute",
            (Species::Penguin, 0x18) => "pngn_scan_chat1",
            (Species::Penguin, 0x17 | 0x19) => "pngn_scan_chat",
            (Species::Penguin, 0x49 | 0x4a | 0x61) => "pngn_blown_up",
            (Species::Penguin, 0x5d) => "pngn_land_dead",
            (Species::Penguin, _) => "penguin_error_anim",
            (Species::Sheep, 0..=3) => "sheep_turn",
            (Species::Sheep, 8..=0xa) => "sheep_walk",
            (Species::Sheep, 0xb | 0xc) => "sheep_run_human",
            (Species::Sheep, 0xd) => "sheep_salute",
            (Species::Sheep, 0x17) => "sheep_graze",
            (Species::Sheep, 0x18) => "sheep_scan",
            (Species::Sheep, 0x19) => "sheep_bleat",
            (Species::Sheep, 0x49 | 0x4a | 0x61) => "sheep_blown_up",
            (Species::Sheep, 0x5d) => "sheep_land_dead",
            (Species::Sheep, _) => "sheep_error_anim",
        }
    }

    /// The death lines (DeadAtPosition vt+0x158 through Audio_PlayRandomSound 0x00493b70: the penguin's 6 at
    /// 0x005cb178, the sheep's 3 at 0x005cb16c).
    pub fn death_lines(self) -> &'static [&'static str] {
        match self {
            Species::Penguin => &["ctr_penguin_scream_1", "ctr_penguin_scream_2", "ctr_penguin_scream_3", "ctr_penguin_scream_4", "ctr_penguin_surprise_1", "ctr_penguin_surprise_2"],
            Species::Sheep => &["ctr_sheep_scream_1", "ctr_sheep_scream_2", "ctr_sheep_scream_3"],
        }
    }
}

/// What 0x0044e600 answers while instance `id` loads: whether the level starts with Roman in a turret, the
/// mission's start_in_turret_tag (Game+0x26cc), which is set only once the mission object has loaded: the instances
/// load in bank (id) order, so only those after it see the tag (lev_3_m4_bd: its sheep 157..191 roam, sheep 6 does
/// not; lev_2_m4's and lev_5_m5's all come before their mission and graze only, as the original's traces show).
/// It answers false until the player exists (Game+0x748, the trotter's load); in the shipped turret levels the
/// trotter loads two ids after the mission, with no critter between.
fn turret_start_known(scene: &SceneFile, id: u32) -> bool {
    let player_loaded = scene.instances.iter().any(|i| i.template == "trotter" && i.id < id);
    player_loaded
        && scene
        .logic
        .iter()
        .find(|l| l.template == "mission" && l.id < id)
        .and_then(|m| m.props.get("start_in_turret_tag"))
        .and_then(|v| v.as_i64())
        .unwrap_or(0)
        != 0
}

/// A building's reward poster (prop_rewardposter, 0x0040bf40), attached at its ref_wantedposter.
#[derive(Component)]
pub struct RewardPoster;

/// A node's matrix in its model (game axes) from the model's reference anim, as 0x00453c50 finds an attachment matrix
/// by name: the node's first key (translation, rotation) composed up its parent chain (the anim's `parents`; a parent
/// without a track counts as identity). The buildings' ref_* nodes are all roots; a character's hang under its bones.
pub(crate) fn ref_node(dir: &std::path::Path, anim: &str, node: &str) -> Option<Mat4> {
    let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("anims").join(format!("{anim}.json"))).ok()?).ok()?;
    let tracks = v.get("tracks")?.as_object()?;
    let parents = v.get("parents").and_then(|p| p.as_object());
    let f = |x: &serde_json::Value| x.as_f64().unwrap_or(0.0) as f32;
    let local = |track: &serde_json::Value| -> Option<Mat4> {
        let t = track.get("t")?.as_array()?.first()?.as_array()?;
        let r = track.get("r").and_then(|r| r.as_array()?.first()?.as_array().cloned());
        let rot = r.filter(|r| r.len() >= 5).map_or(Quat::IDENTITY, |r| Quat::from_xyzw(f(&r[1]), f(&r[2]), f(&r[3]), f(&r[4])).normalize());
        Some(Mat4::from_rotation_translation(rot, Vec3::new(f(&t[1]), f(&t[2]), f(&t[3]))))
    };
    let (name, track) = tracks.iter().find(|(k, _)| k.eq_ignore_ascii_case(node))?;
    let mut m = local(track)?;
    let mut at = name.as_str();
    // (bounded: a malformed export with a cycle stops)
    for _ in 0..64 {
        let Some(parent) = parents.and_then(|p| p.get(at)).and_then(|p| p.as_str()) else { break };
        m = tracks.get(parent).and_then(local).unwrap_or(Mat4::IDENTITY) * m;
        at = parent;
    }
    Some(m)
}

/// A building's window pane (TwigletGenerator_Slot84 0x0040ad80 "windows": a "village_window" DefaultGameObject at
/// each ref_win_0%d matrix of the building's model, attached to it): shot out on its own (p_village_window: 5 health,
/// takes bullets, fx_explosion_village_window); gone with its building.
#[derive(Component)]
pub struct Window {
    pub building: Entity,
    /// its ref_win_0N (the save game names a shot-out pane by its building's instance id and this)
    pub index: u8,
}

/// One of Roman's weapon models in his hand (player.rs shows the one in use).
#[derive(Component, Clone, Copy, Debug, PartialEq, Eq)]
pub enum HandWeapon {
    /// a gun's object (player_rifle, player_rlvindaloo, ...)
    Gun(crate::weapons::WeaponId),
    /// the Topsy Turvy bomb while it is used
    Topsy,
    /// the knockout bomb's punching glove (player_punchingglove) while it is on (weapons/glove.rs)
    Glove,
}

impl HandWeapon {
    /// The state log's name (the gun's mission name, "topsyturvy").
    pub fn name(self) -> &'static str {
        match self {
            HandWeapon::Gun(w) => w.name(),
            HandWeapon::Topsy => "topsyturvy",
            HandWeapon::Glove => "punchingglove",
        }
    }
}

/// A model as a static mesh collider placed at `world`, with its triangles' surface names (surface.rs).
fn static_collider(physics: &mut Physics, model: &scene::Model, world: Mat4) -> Option<BodyId> {
    let v: Vec<Vec3> = model.vertices.iter().map(|p| world.transform_point3(*p)).collect();
    let id = physics.add_static_mesh(&v, &model.tris)?;
    if let Some(m) = crate::surface::SurfaceMap::new(&v, &model.tris, &model.surfaces) {
        physics.surfaces.insert(id, m);
    }
    Some(id)
}

/// A placed object's collider joins the world: its material, and whether bullets stop on it. Bullets hit only
/// objects in the projectile list World +0xe90 (Projectile_Update 0x0047e580, filter 0x00413a90): buildings (unless
/// base_defend or invisible, ai_01.cpp:1063), trees, kegs, turrets always; default objects (`is_default_object`) only when
/// they take damage or block projectiles, and neither no_collide nor collide_barrier (DefaultGameObject_Load
/// 0x0044fd20, game_03.cpp:526-567, lists +0xe90 / +0xe94 / +0xea0 / +0xea4); invisible_material never (0x004138c0).
/// Invisible walls (collide_barrier, blocks_projectiles 0) let bullets through.
pub(crate) fn collider_lists(physics: &mut Physics, p: &Props, c: BodyId) {
    if let Some(m) = p.s("material") {
        physics.materials.insert(c, m.to_owned());
    }
    if !in_bullet_list(p) {
        physics.no_bullets.insert(c);
    }
}

/// Objects whose class hides them at load, whatever their property set says: their models are editor markers. The
/// alarm generator (behaviour alarm_generator, template grunt_generator: a purple pyramid, "what's this purple shit",
/// session 2026-10-18_17-00-51 bookmark 7 at lev_1_m1's generator 215) ends its load, AlarmGenerator_Slot6
/// 0x00405490 (ai_01.cpp), with vt+0xf4(0), the hide (0x004536b0) that Object_LoadBaseProps 0x00451360 applies for
/// "visible" 0. Its p_grunt_generator set has invisible 0, so the set alone left it drawn.
pub(crate) fn hidden_by_class(behavior: &str) -> bool {
    matches!(behavior, "alarm_generator")
}

/// Whether a behaviour makes a DefaultGameObject (DefaultGameObject_Load 0x0044fd20): no behaviour (the template name
/// hashes to no class in the behaviour table, 0x0045df60), or "defaultgameobject" by name (the lev_4 trees and logs'
/// p_l4_trees; molmer's make_tnt creates one by it, game_02.cpp:2806). Both read the default object's keys: the
/// receive_trigger_* fields, no_collide, collide_barrier, the projectile-list rule, impact_force 2010.
pub fn is_default_object(behavior: &str) -> bool {
    matches!(behavior, "" | "defaultgameobject")
}

/// The anim whose nodes 0x00453c50 reads for a placed object: the one set at load (Object_LoadProperties 0x004517b0
/// plays the template's default anim, slot 1), else its ref_anim_name, else the model's own (ww_house_2f_b carries
/// v_house_2f_b_0: its windows and bullseye are that anim's nodes).
fn reference_anim(tpl: Option<&crate::gamedata::Template>, p: &Props, model: &str) -> String {
    tpl.and_then(|t| t.default_anim.clone()).or_else(|| p.s("ref_anim_name").map(str::to_owned)).unwrap_or_else(|| model.to_owned())
}

/// Whether an object is in the damage takers' list World+0xea4 (the crosshair's ray list on a gun, 0x00406f50 with
/// Game+0xac8 set): buildings with takes_damage (+0x460 0x1000) unless base_defend or invisible (ai_01.cpp:1063-1068),
/// default objects (`is_default_object`) with takes_damage and neither no_collide nor collide_barrier (game_03.cpp:526-566), the rocket
/// tower and bud turrets (0x0049c320 / 0x0049c9b0). Trees, kegs and peasants are not.
pub(crate) fn in_damage_list(p: &Props) -> bool {
    let flag = |k: &str, d: f32| p.f(k, d) != 0.0;
    match p.s("behavior").unwrap_or("") {
        "building" => flag("takes_damage", 1.0) && !(flag("invisible", 0.0) || flag("base_defend", 0.0)),
        "rocket_tower_turret" | "rocket_bud_turret" => true,
        b if is_default_object(b) => flag("takes_damage", 0.0) && !flag("no_collide", 0.0) && !flag("collide_barrier", 0.0),
        _ => false,
    }
}

/// Whether an object's collider stops bullets (see `collider_lists`).
fn in_bullet_list(p: &Props) -> bool {
    let flag = |k: &str| p.f(k, 0.0) != 0.0;
    let listed = match p.s("behavior").unwrap_or("") {
        "building" => !(flag("invisible") || flag("base_defend")),
        "tree" | "fx_explosion" | "rocket_tower_turret" => true,
        // PeasantObject_Load links +0xe9c / +0xef8 / +0xefc only: bullets pass the peasants
        "peasant" => false,
        b if is_default_object(b) => (flag("takes_damage") || flag("blocks_projectiles")) && !flag("no_collide") && !flag("collide_barrier"),
        _ => true,
    };
    listed && p.s("material") != Some("invisible_material")
}

pub(crate) fn destructible(p: &Props) -> Option<Destructible> {
    // Objects with a death fx or a husk and finite health die from damage (behaviour fx_explosion, building, tree).
    // (an object that takes damage and has a death fx but no max_health, the guard alarm: the base default health,
    // 100 (guess: Object_Load's default is not traced))
    let takes = p.f("takes_damage", 0.0) != 0.0 && p.s("fx_death").is_some();
    if !((p.has("max_health") || takes) && (p.s("fx_death").is_some() || p.s("husk").is_some())) || p.f("max_health", 0.0) >= 100000.0 {
        return None;
    }
    // impact_force defaults to 2010 for default objects (0x0044fd20) and fx_explosion props (FxExplosion_Load
    // 0x0041c180, game_02.cpp:700); every lev_1_m1 set that has area damage gives its own
    let behavior = p.s("behavior").unwrap_or("");
    let default_force = if is_default_object(behavior) || behavior == "fx_explosion" { 2010.0 } else { 0.0 };
    // a keg's fuse defaults (FxExplosion_Load 0x0041c180, game_02.cpp:646): start_delay 1.0, acceleration_rate 0.75,
    // min_delay 0.1 (every shipped keg and snow pipe sets all three); other classes pass their blast on at once
    let (start_delay, acceleration, min_delay) = if behavior == "fx_explosion" { (1.0, 0.75, 0.1) } else { (0.0, 1.0, 0.0) };
    Some(Destructible {
        health: p.f("max_health", 100.0),
        max_health: p.f("max_health", 100.0),
        fx_death: p.s("fx_death").filter(|s| !s.eq_ignore_ascii_case("null")).map(str::to_owned),
        husk: p.s("husk").map(str::to_owned),
        area_damage: p.f("area_damage", 0.0),
        radius_inner: p.f("damage_radius_inner", 0.0),
        radius_outer: p.f("damage_radius_outer", 0.0),
        impact_force: p.f("impact_force", default_force),
        chain: p.s("behavior") == Some("fx_explosion"),
        score: p.f("score_value", 0.0),
        class: match p.s("behavior").unwrap_or("") {
            "building" => DClass::Building,
            "fx_explosion" => DClass::Keg,
            "tree" | "rocket_tower_turret" => DClass::Open,
            _ => DClass::Default,
        },
        // defaults by class: DefaultGameObject_Load 0x0044fd20, BuildingObject 0x0040ad80 (ai_01.cpp:961),
        // FxExplosion_Load 0x0041c180
        takes_damage: p.f("takes_damage", if p.s("behavior") == Some("building") { 1.0 } else { 0.0 }) != 0.0,
        takes_bullet: p.f("takes_bullet_damage", 0.0) != 0.0,
        player_damage: p.f("player_damage", 1.0) != 0.0,
        teammate_damage: p.f("teammate_damage", if p.s("behavior") == Some("building") { 1.0 } else { 0.0 }) != 0.0,
        badguy_damage: p.f("badguy_damage", 0.0) != 0.0,
        start_delay: p.f("start_delay", start_delay),
        min_delay: p.f("min_delay", min_delay),
        acceleration: p.f("acceleration_rate", acceleration),
        dying: None,
        chain_delay: 0.0,
    })
}

/// The level's objects (docs/subsystems/objects.md). The original spawns every entry of the level's object table
/// whose spawn mask has the pass's bit (0x0045d930 -> 0x0045d7a0) through ObjectSystem_CreateObject 0x0045f020:
/// the `behavior` name (or the template name) hashed into the behaviour table (0x0045df60) gives a class id, the id
/// a constructor (Object_Construct 0x004511d0 plus the class vtable, or GruntObject 0x00461510, BowGruntObject
/// 0x00461530, PenguinObject 0x00461750, MolmerObject 0x00461800, Q111Object 0x00461820, PlayerObject 0x00461840
/// stored as the player), then its Load (vt+0x18) and, outside a save restore, vt+0x20 (0x0045d9b0). Here the
/// `behavior` match below picks the components instead; every instance is spawned (the spawn mask is not used).
/// The object lists of the ObjectSystem (World +0xe6c.., ObjectList_Link 0x004546d0 / _Unlink 0x00454750 /
/// _Contains 0x00454690, Object_UnlinkAll 0x004546f0) are components and queries; finding an object by id
/// (0x0045d570, word +0x396) or name (ObjectSystem_FindByName 0x0045d5f0, Object_GetName 0x00453270) is a query on
/// InstanceId / Placed.template. The level table walk (0x0045d750) is scene.json's instance list; reference counts
/// (+0x28, the release callback 0x0045a450) and the "not removed" test (0x00454770) are entity lifetimes.
#[allow(clippy::too_many_arguments)]
pub fn spawn(
    commands: &mut Commands,
    scene: &SceneFile,
    data: &GameData,
    lib: &mut Library,
    physics: &mut Physics,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
    skin: &mut SkinCtx,
) -> LevelInfo {
    let axes = Mat4::from_quat(game_to_engine());
    let mut rng = crate::fxvm::CrtRand::default();
    // husk picks draw from their own stream, so the grunts' rolls stay as they were
    let mut husk_rng = crate::fxvm::CrtRand::default();
    // and the anim_rand phases from theirs
    let mut anim_rng = crate::fxvm::CrtRand::default();
    let mut info = LevelInfo { name: scene.level.clone(), ..default() };

    // landscape: its own mesh is its collision
    let skip_visual = lib.skip_landscape_visual;
    let mut decal_world = crate::decals::DecalWorld::new(&[], &[]);
    let to_game = axes.inverse();
    if let Some(model) = lib.load_model(&scene.landscape, false, assets, meshes, materials) {
        if !skip_visual {
            let e = commands.spawn((Transform::from_matrix(axes), Visibility::default(), Name::new("landscape"))).id();
            let (d, t, _) = attach_model(commands, e, model, true, None, "ref_rhand");
            info.draws += d;
            info.triangles += t;
        }
        physics.landscape = static_collider(physics, model, axes);
        decal_world = crate::decals::DecalWorld::new(&model.vertices, &model.tris);
    }

    for inst in &scene.instances {
        let p = data.instance_props(&inst.template, &inst.props);
        let tpl = data.templates.get(&inst.template);
        let behavior = p.s("behavior").unwrap_or("");
        // markers, trigger volumes, cameras, sound emitters: logic only, never drawn
        // camera_fx (weather domes, intro overlays): their fx starts at load attached to the camera (trigger_start 0;
        // CameraFXObject_Load 0x004101a0), position-only when position_only = 1 (docs/subsystems/weather.md)
        if behavior == "camera_fx" {
            if p.f("trigger_start", 0.0) == 0.0 {
                if let Some(fx) = p.s("fx_name") {
                    info.camera_fx.push((fx.to_owned(), p.f("position_only", 1.0) != 0.0));
                }
            }
            continue;
        }
        // a squad (ai/squad.rs, Squad_Init 0x00433220): an undrawn anchor with its members to make; a jump marker
        // (JumpMarker 0x005ac628): a landing point for the jumper captains (ai/jumper.rs)
        if behavior == "squad" || behavior == "jump_marker" {
            let m = scene::game_matrix(&inst.transform);
            let mut at = axes.transform_point3(m.w_axis.truncate());
            // force_ground: Object_ForceGround 0x00455860 (1000, 10000), whose ray sees the landscape only (a marker
            // under an arch or a roof lands on the ground, not on the object); no collision body to lift out
            if p.f("force_ground", 0.0) != 0.0 {
                if let Some(g) = physics.force_ground(at, 1000.0, 10000.0, None, 0.0) {
                    at = g;
                }
            }
            let (_, r, _) = (axes * m).to_scale_rotation_translation();
            let (yaw, _, _) = (r * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
            let mut e = commands.spawn((Transform::from_translation(at), Visibility::default(), Name::new(inst.template.clone()), InstanceId(inst.id)));
            if behavior == "squad" {
                e.insert(ai::squad::Squad::from_props(&p, &lib.dir, &inst.template, at, yaw));
            } else {
                e.insert(ai::JumpMarker { tag: p.i("marker_tag", 0), occupant: None });
            }
            continue;
        }
        // (the big red arrow over the pub is drawn: the original shows it until trigger 900 removes it)
        // a trigger volume is hidden unless its set has "visible" (TriggerObject_Load 0x00497460: vt+0xf4(0) when not
        // set): the door mats (p_doormat, visible 1) are drawn, the bb_* boxes are not
        let shown_volume = behavior == "broadcast" && p.f("visible", 0.0) != 0.0;
        if (matches!(behavior, "broadcast" | "fx_ear") && !shown_volume) || inst.template == "marker" {
            continue;
        }
        // the placement's scale is not applied: the original draws scaled placements (the archways' 0.5, pine trees'
        // 0.55..0.88) at full size; measured against its captures (frame 580 of the synced walk: the arch and the
        // trees' tops land where the unscaled models project, docs/remaster-engine.md). Only rotation and position.
        // the object's matrices (world +0x2e8 / scaled +0x32c rebuilt from the body, 0x00457c20; the getters
        // 0x00457d60, 0x00457d90, 0x00457ea0, 0x00457eb0, 0x00457ec0, 0x00457f40, 0x00457f80; the layout transpose
        // 0x00457ba0; without a body 0x00457f00 / 0x00458270; the position offset +0x2dc 0x00457820 / 0x00457850; Object_SetScale
        // 0x004535d0 / 0x004535a0; the body +0x1c4, 0x004592c0) are the entity's Transform / GlobalTransform and physics.rs bodies
        let placed = scene::game_matrix(&inst.transform);
        let (_, r, tr) = placed.to_scale_rotation_translation();
        let mut world = axes * Mat4::from_rotation_translation(r, tr);
        // the grunts this object hides (ai/ic.rs: buildings, trees, powder kegs), at the nodes of its reference anim
        // (the template's default anim: Anim_Set 0x81 at load; 0x00453c50 reads its matrices)
        if !ai::ic::sources(behavior).is_empty() {
            let anim_name = tpl.and_then(|t| t.default_anim.clone()).or_else(|| p.s("ref_anim_name").map(str::to_owned)).unwrap_or_else(|| inst.model.clone());
            let anim = skin.lib.anim(&anim_name);
            let node = |n: &str| {
                let a = anim.as_ref()?;
                Some(Mat4::from_rotation_translation(a.rotation(n, 0.0).unwrap_or(Quat::IDENTITY), a.translation(n, 0.0)?))
            };
            let landing = |tag: i64| scene.instances.iter().find(|h| h.props.get("tag").and_then(|v| v.as_i64()) == Some(tag)).map(|h| h.id);
            for ic in ai::ic::hidden_grunts(&p, behavior, inst.id, to_game * world, node, landing) {
                let at = game_to_engine() * ic.origin.w_axis.truncate();
                commands.write_message(SpawnGrunt { at, template: "grunt_regular".into(), ic: Some(ic), ..default() });
                info.grunts += 1;
            }
        }
        let background = behavior == "background";
        // visible (default 1; 0 hides it: Object_LoadBaseProps 0x00451360, vt+0xf4(0)): p_invisible_walls and p_ui
        // have 0; buildings read their own "invisible" (BuildingObject 0x0040ad80). Hiding (0x004536b0: +0x424 bit 4,
        // out of the shadow list +0xe78 and the render list, 0x00453640 / 0x00453670; 0x00453740 asks) is not
        // spawning the model here, or Bevy's Visibility
        // and some classes hide themselves at load whatever their set says (`hidden_by_class`)
        let invisible = p.f("visible", 1.0) == 0.0 || p.f("invisible", 0.0) != 0.0 || hidden_by_class(behavior);
        let lib_dir = lib.dir.clone();
        let Some(model) = lib.load_model(&inst.model, background, assets, meshes, materials) else { continue };
        let (scale, _, _) = world.to_scale_rotation_translation();
        let bounds = Bounds { center: (model.min + model.max) * 0.5, radius: (model.max - model.min).length() * 0.5 * scale.max_element() };
        // force_ground (Object_ForceGround 0x00455860 with (1000, 10000), from the base load 0x004517b0): onto the
        // landscape under the placement; a character's body (the sphere of collision_radius, 0x004527d0) is lifted out
        // of the landscape it touches there, by steps of its bounding radius (vt+0x90; the model's bounds here: guess).
        // physics.rs force_ground
        if p.f("force_ground", 0.0) != 0.0 {
            let character = p.has("mode") && ai::Kind::from_template(&inst.template).is_some();
            let sphere = character.then(|| ai::body::CharacterBody::new(&p).radius);
            if let Some(at) = physics.force_ground(world.w_axis.truncate(), 1000.0, 10000.0, sphere, bounds.radius) {
                world.w_axis.y = at.y;
            }
        }
        let (_, rot, pos) = world.to_scale_rotation_translation();
        let mut e = commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(inst.template.clone()), InstanceId(inst.id)));
        if background {
            e.insert(Background);
        }
        let e = e.id();
        // its bounding spheres for the HUD's lock-on test (vt+0x90 / vt+0x94; weapons/targeting.rs)
        commands.entity(e).insert(crate::weapons::targeting::HudSphere::from_box(model.min, model.max));
        // a twiglet tree (twiglet.rs, TwigletGenerator_Slot6 0x0049d3c0): its initial_ic movers start at the nodes of
        // its reference anim of the same names (0x0049d590: 0x00453c50)
        if behavior == "twiglet_generator" {
            let anim_name = tpl.and_then(|t| t.default_anim.clone()).unwrap_or_else(|| inst.model.clone());
            let anim = skin.lib.anim(&anim_name);
            let node = |n: &str| {
                let a = anim.as_ref()?;
                Some(Mat4::from_rotation_translation(a.rotation(n, 0.0).unwrap_or(Quat::IDENTITY), a.translation(n, 0.0)?))
            };
            commands.entity(e).insert(crate::twiglet::Generator::from_props(&p, to_game * world, node));
        }
        // a rigid model's own anim moving its root (the big red arrow bobs: Center z 20..46, anim bigredarrow_0)
        let mut animated = false;
        if model.skeleton.is_none() {
            if let Some(ra) = RigidAnim::load(&lib_dir, &inst.model) {
                commands.entity(e).insert(ra);
                animated = true;
            }
        } else if !invisible {
            // a model with a skeleton plays its template's default anim (oilpump: oilpump_cycle), see DefaultAnim
            if let Some(anim) = tpl.and_then(|t| t.default_anim.clone()) {
                let phase = (p.f("anim_rand", 0.0) != 0.0).then(|| anim_rng.rand() as f32 / 32768.0);
                commands.entity(e).insert(DefaultAnim { anim, seen: false, phase });
            }
        }
        // decal receivers (Decals_ProjectOnEntity 0x00416f50): decal_receive (default 1, +0x3ec bit 8), not animating
        // (0x00418610), not skinned (Entity_DecalExcluded 0x00416e90: guess); characters are skinned
        if !invisible && !background && !animated && model.skeleton.is_none() && p.f("decal_receive", 1.0) != 0.0 {
            decal_world.add_object(e, model, to_game * world);
        }
        // the bullseye that marks the houses a mission asks to destroy (lev_3_m2's 14, lev_4_m3's 15): BuildingObject
        // 0x0040ad80 reads Bullseye and BullsEyeSize (default 10), and its first update 0x0040cb10 spawns the decal
        // "Fx_bullseye" at the model's Ref_bullseye node onto the building alone (Decals_Spawn 0x004165c0 with the
        // object), kept for good (its age -1.0: the pool never recycles it); it fades with the building's death
        if behavior == "building" && p.f("bullseye", 0.0) != 0.0 {
            if let Some(node) = ref_node(&lib_dir, &reference_anim(tpl, &p, &inst.model), "Ref_bullseye") {
                let place = to_game * world * node;
                commands.write_message(crate::decals::SpawnDecal { place, size: p.f("bullseyesize", 10.0), texture: "Fx_bullseye".into(), onto: Some(e), pinned: true });
            }
        }
        if !invisible {
            // shadow casters: objects whose property set has shadow_enable (shadows.md, object +0x3ec bits 1-2)
            let casts = !background && p.f("shadow_enable", 0.0) != 0.0;
            let hand_bone = ai::Kind::from_template(&inst.template).map_or("ref_rhand", ai::Kind::weapon_bone);
            let (d, t, hand) = attach_model(commands, e, model, casts, Some(&mut *skin), hand_bone);
            // its template's other LODs, switched by distance unless lod_max (graphics/geometry/lod.rs)
            if let Some(lods) = tpl.and_then(|t| crate::graphics::geometry::lod::LodSource::new(t, &p, casts)) {
                commands.entity(e).insert(lods);
            }
            // weapons: grunts' rifle or bow; Roman's guns (weapons/arsenal.rs: every player gun this level has a
            // template for hangs in his hand, the one in use shown by player.rs hand_weapon)
            if inst.template == "trotter" {
                if let Some(h) = hand {
                    for id in crate::weapons::WeaponId::ALL {
                        if let Some(m) = data.templates.get(id.template()).and_then(|t| t.model.clone()).and_then(|n| lib.load_model(&n, false, assets, meshes, materials)) {
                            // the weapon's ref_hand_00 = the hand joint (identity; checked by CPU skinning: the rifle
                            // points ahead in trotter_idle_1 and about 45 degrees up in the aim / run_aim poses)
                            let parts = m.parts.clone();
                            let held = attach_rest_pose(commands, h, &parts, meshes);
                            commands.entity(held).insert((HandWeapon::Gun(id), Visibility::Hidden));
                        }
                    }
                    // the Topsy Turvy bomb in the hand during its use (0x0047bbe0; shown by player.rs hand_weapon)
                    if let Some(parts) = data.templates.get("player_topsyturvy").and_then(|t| t.model.clone()).and_then(|n| lib.load_model(&n, false, assets, meshes, materials)).map(|m| m.parts.clone()) {
                        let held = attach_rest_pose(commands, h, &parts, meshes);
                        commands.entity(held).insert((HandWeapon::Topsy, Visibility::Hidden));
                    }
                    // the knockout bomb's glove: player_punchingglove attached at bof_glove_player_bone (r_wrist) by its
                    // bof_glove_child_bone (0x004788a0 case 1: 0x004539f0); here in the hand like the guns (tuning item)
                    if let Some(parts) = data.templates.get("player_punchingglove").and_then(|t| t.model.clone()).and_then(|n| lib.load_model(&n, false, assets, meshes, materials)).map(|m| m.parts.clone()) {
                        let held = attach_rest_pose(commands, h, &parts, meshes);
                        commands.entity(held).insert((HandWeapon::Glove, Visibility::Hidden));
                    }
                }
            } else if let (Some(h), Some(w)) = (hand, ai::weapon_template(&inst.template).and_then(|w| data.templates.get(w)).and_then(|t| t.model.clone())) {
                if let Some(m) = lib.load_model(&w, false, assets, meshes, materials) {
                    attach_weapon(commands, h, m, Transform::IDENTITY);
                }
            }
            info.instances += 1;
            info.draws += d;
            info.triangles += t;
        }
        if background {
            continue;
        }

        // Roman, the player: the placed trotter
        if inst.template == "trotter" {
            let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
            // PlayerObject_Load 0x00473da0 puts him on the ground (Object_ForceGround 0x00455860: from 1000 above,
            // 10000 down, the landscape only, 0.1 above the hit): he does not drop from the placement's height at the start
            let pos = physics.force_ground(pos, 1000.0, 10000.0, None, 0.0).unwrap_or(pos);
            if let Some(r) = crate::player::make_roman(physics, &p, data, pos, yaw - std::f32::consts::PI) {
                commands.entity(e).insert((Health(r.max_health), r, bounds, crate::ai::Player));
                info.player = Some(e);
            }
            continue;
        }
        // Q and Jonesy follow Roman
        if inst.template == "q111" || inst.template == "molmer" {
            if let Some(walker) = physics.add_walker(pos, p.f("collision_radius", 4.0).clamp(2.0, 5.0), 10.0, 50f32.to_radians()) {
                let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
                commands.entity(e).insert((
                    crate::ai::Companion::new(walker, &p, data, inst.template == "q111", yaw, game_to_engine()),
                    Health(p.f("max_health", 500.0)),
                    bounds,
                ));
                // (its send_trigger_on_death: Companion::death_trigger, sent by ai.rs companion_damage)
                // a trigger and a tower marker: the tower task (lev_4_m2's Jonesy, ai/tower.rs)
                if let Some(task) = crate::ai::tower::TowerTask::from_props(&p) {
                    commands.entity(e).insert(task);
                }
            }
            continue;
        }
        // characters: property sets with an AI mode (grunts). Their AI here is a stand-in driven by the game's
        // own parameters (speeds, ranges); the recovered AI (archive/src/ai) replaces it later.
        // (the grunt-class templates by ai::Kind: grunt, grunt_regular, captain, captain_sniper)
        if p.has("mode") && ai::Kind::from_template(&inst.template).is_some() {
            // window / tower grunts (AI_ResolveStartObjects 0x00425e20): placed at the generic_txt bone of the object
            // whose tag is obj_tag (the house), physics off: they stay there. Facing: away from the object's centre
            // (guess: the original takes the bone's whole matrix)
            let mode = p.s("mode").unwrap_or("").to_owned();
            let mut pos = pos;
            let mut window_yaw = None;
            // the house is his start object (+0x880): its death knocks him out (ai/ic.rs `knocks`)
            let mut start_object = None;
            if mode.contains("window") || mode.contains("tower") {
                let tag = p.f("obj_tag", 0.0) as i64;
                let house = scene.instances.iter().find(|h| h.props.get("tag").and_then(|v| v.as_i64()) == Some(tag) && tag != 0);
                start_object = house.map(|h| ai::ic::StartObject { id: h.id });
                if let (Some(h), Some(bone)) = (house, p.s("generic_txt")) {
                    // the house's reference anim: its template's default anim (ww_house_2f_b carries v_house_2f_b_0),
                    // else its model's own (0x00453c50 reads the matrices of the anim set at load)
                    let anim = data.templates.get(&h.template).and_then(|t| t.default_anim.clone()).unwrap_or_else(|| h.model.clone());
                    if let Some(b) = crate::mission::pub_bone(&lib.dir, &anim, bone).or_else(|| crate::mission::pub_bone(&lib.dir, &h.model, bone)) {
                        let m = crate::scene::game_matrix(&h.transform);
                        let at = game_to_engine() * m.transform_point3(b);
                        let centre = game_to_engine() * m.w_axis.truncate();
                        let out = (at - centre).with_y(0.0).normalize_or_zero();
                        pos = at;
                        window_yaw = (out != Vec3::ZERO).then(|| out.x.atan2(out.z));
                    }
                }
            }
            if let Some((body, character)) = ai::body::add(physics, e, ai::body::CharacterBody::new(&p), pos, p.s("material").unwrap_or("grunt")) {
                let (yaw, _, _) = (rot * game_to_engine().inverse()).to_euler(EulerRot::YXZ);
                let mut g = ai::make_grunt(&p, data, ai::Kind::of(&inst.template), pos, window_yaw.unwrap_or(yaw), &mut rng);
                g.cold = ai::cold_level(&lib_dir);
                // mode bully: the peasant whose tag is obj_tag (the peasant list +0xef8)
                if mode == "bully" {
                    let tag = p.i("obj_tag", 0);
                    let peasant = scene.instances.iter().find(|h| {
                        tag != 0 && h.props.get("tag").and_then(|v| v.as_i64()) == Some(tag) && data.template_props(&h.template).s("behavior") == Some("peasant")
                    });
                    match peasant {
                        Some(h) => {
                            g.bully = Some(ai::bully::Bully::new(h.id, &mut rng));
                            g.mind = ai::Mind::Bully;
                        }
                        None => warn!("ai: bully {} finds no peasant of tag {tag}: ready roaming", inst.id),
                    }
                    g.attention = false;
                }
                // mode turret: the gun whose tag is obj_tag (the turret list +0xea8: stig_mgun / forge_cannon)
                if mode == "turret" {
                    let tag = p.i("obj_tag", 0);
                    g.start_gun = scene
                        .instances
                        .iter()
                        .find(|h| {
                            tag != 0
                                && h.props.get("tag").and_then(|v| v.as_i64()) == Some(tag)
                                && matches!(data.template_props(&h.template).s("behavior"), Some("stig_mgun_turret" | "forge_cannon"))
                        })
                        .map(|h| h.id);
                }
                if window_yaw.is_some() {
                    g.fixed = Some(pos);
                }
                commands.entity(e).insert((
                    body,
                    character,
                    Health(p.f("max_health", 100.0)),
                    bounds,
                    g,
                ));
                if let Some(s) = start_object {
                    commands.entity(e).insert(s);
                }
                // its send_trigger_on_death (the death of any object sends +0x488, 0x00450ee0): mission.rs troop_deaths
                let on_death = p.i("send_trigger_on_death", 0);
                if on_death != 0 {
                    commands.entity(e).insert(crate::mission::DeathTrigger(on_death));
                }
                info.grunts += 1;
            }
            continue;
        }

        // everything else stands still and collides with its collision model (the render model when the
        // template names none: invisible walls, and collide_poly objects such as the trenches, p_barracade; the
        // template's +0x4c flag 1, loader_01.cpp:3446). Broadcast volumes (p_tools) are triggers, never solid.
        let poly = p.i("collide_poly", 0) != 0 && p.s("behavior") != Some("broadcast");
        let coll_name = tpl.and_then(|t| t.collision.clone()).or_else(|| (invisible || poly).then(|| inst.model.clone()));
        // a default object with no_collide (flag 8: the big red arrow) skips its whole collision set-up in
        // DefaultGameObject_Load 0x0044fd20 (no vt+0xf8 filter, no lists): no collider (guess: a body without the
        // filter collides with nothing; the crosshair's list +0xe9c and the bullets' +0xe90 never hold it)
        let default_object = is_default_object(behavior);
        let coll_name = coll_name.filter(|_| !(default_object && p.f("no_collide", 0.0) != 0.0));
        // a house that throws its people out when destroyed (toss_peasants / toss_grunts: tossed.rs); its husks inherit it
        if behavior == "building" {
            if let Some(t) = crate::tossed::TossOnDeath::of(&p) {
                commands.entity(e).insert(t);
            }
        }
        // a movable default object (boulders): its body once unpinned (movable.rs)
        if default_object {
            if let Some(m) = crate::movable::Movable::new(&p, coll_name.clone()) {
                commands.entity(e).insert(m);
            }
        }
        let triggers = if default_object { ObjectTriggers::of(&p) } else { ObjectTriggers::default() };
        // a collide barrier waiting for its start_blocking trigger has no collider until then (DormantBarrier)
        let coll_name = match coll_name {
            Some(n) if default_object && p.f("collide_barrier", 0.0) != 0.0 && triggers.start_blocking != 0 => {
                commands.entity(e).insert(DormantBarrier { collision: n, material: p.s("material").map(str::to_owned) });
                None
            }
            other => other,
        };
        let collider = match coll_name {
            Some(n) => lib.load_model(&n, false, assets, meshes, materials).and_then(|m| static_collider(physics, m, world)),
            None => None,
        };
        if let Some(c) = collider {
            collider_lists(physics, &p, c);
        }
        let husk = pick_husk(&p, &mut husk_rng);
        commands.entity(e).insert((Placed { template: inst.template.clone(), collider, husk, triggers }, bounds, crate::hud::AimClass::of(&p)));
        if in_damage_list(&p) {
            commands.entity(e).insert(DamageTaker);
        }
        // a peasant to rescue (peasant.rs, PeasantObject_Load 0x004056b0), ordered on the object list by his entry
        if behavior == "peasant" {
            let peasant = crate::peasant::Peasant::from_props(&p, &inst.template, inst.id);
            commands.entity(e).insert((peasant.health(&p), crate::fx::BlastProof, peasant));
        }
        // a door mat (a drawn trigger volume): its death slot kills it when it banks a peasant (one_shot: 0x00497620 ->
        // Object_Kill 0x00455640, its fx_death and husk); nothing damages it (TriggerObject has no damage list)
        if shown_volume {
            commands.entity(e).insert((crate::peasant::Mat, crate::peasant::mat_health(&p)));
        }
        // a trench (behavior "barracade", BarricadeObject): its cover slot for the grunts (ai.rs Barricade): ref_exit
        // (0x0040dcb0) and ref_barracade (0x0040dd10) from its reference anim; the direction is -(the node's y axis),
        // the nodes' rotations being identity in trench_0
        if behavior == "barracade" {
            let node = |n: &str| crate::mission::pub_bone(&lib_dir, &inst.model, n);
            if let (Some(s), Some(x)) = (node("ref_barracade"), node("ref_exit")) {
                commands.entity(e).insert(crate::ai::Barricade {
                    origin: world.transform_point3(Vec3::ZERO),
                    exit: world.transform_point3(x),
                    spot: world.transform_point3(s),
                    dir: (-world.transform_vector3(Vec3::Y)).with_y(0.0).normalize_or_zero(),
                    occupant: None,
                    collider,
                });
            }
        }
        // attach_audio_loop (the oil pumps' amb_oil_pump_lp): a loop on the object while it lives
        if let Some(name) = p.s("attach_audio_loop").filter(|n| !n.is_empty() && *n != "none") {
            commands.entity(e).insert(AudioLoop(name.to_owned()));
        } else if behavior == "pub" {
            // the pub's own loop (Pub_StartExteriorLoop 0x0046a790: "amb_pub_exterior_lp", attached, once)
            commands.entity(e).insert(AudioLoop("amb_pub_exterior_lp".into()));
        }
        // the building's windows (0x0040ad80): a village_window at each ref_win_0N matrix of its reference anim
        if p.f("windows", 0.0) != 0.0 {
            let anim = reference_anim(tpl, &p, &inst.model);
            let wp = data.template_props("village_window");
            let wt = data.templates.get("village_window").cloned();
            for i in 0..10 {
                let Some(at) = ref_node(&lib_dir, &anim, &format!("ref_win_{i:02}")) else { break };
                let Some(wt) = wt.as_ref() else { break };
                let ww = world * at;
                let we = commands.spawn((Transform::from_matrix(ww), Visibility::default(), Name::new("village_window"), Window { building: e, index: i as u8 })).id();
                if let Some(m) = wt.model.as_ref().and_then(|m| lib.load_model(m, false, assets, meshes, materials)) {
                    let _ = attach_model(commands, we, m, false, None, "ref_rhand");
                    if m.skeleton.is_none() && wp.f("decal_receive", 1.0) != 0.0 {
                        decal_world.add_object(we, m, to_game * ww);
                    }
                }
                let collider = match wt.collision.as_ref() {
                    Some(n) => lib.load_model(n, false, assets, meshes, materials).and_then(|m| static_collider(physics, m, ww)),
                    None => None,
                };
                if let (Some(c), Some(mt)) = (collider, wp.s("material")) {
                    physics.materials.insert(c, mt.to_owned());
                }
                // on_hit_death (p_village_window): the first body to touch the pane breaks it (movable.rs)
                if let (Some(c), true) = (collider, wp.f("on_hit_death", 0.0) != 0.0) {
                    physics.set_contact_rule(c, crate::contacts::ContactRule::default());
                    commands.entity(we).insert(crate::movable::HitDeath);
                }
                let (husk, triggers) = (pick_husk(&wp, &mut husk_rng), ObjectTriggers::of(&wp));
                commands.entity(we).insert((Placed { template: "village_window".into(), collider, husk, triggers }, Bounds { center: Vec3::ZERO, radius: 4.0 }));
                if let Some(d) = destructible(&wp) {
                    commands.entity(we).insert(d);
                }
            }
        }
        // the reward poster (0x0040ad80 "rewardposter" -> 0x0040bf40): a prop_rewardposter at that node of the building
        // (always ref_wantedposter), attached: it goes with the building's stage and fades with it (the fade list
        // World+0xed8 the camera's fade walks with its building: camfade.rs fades the descendants)
        if let Some(node) = p.s("rewardposter").filter(|_| behavior == "building") {
            let anim = reference_anim(tpl, &p, &inst.model);
            let model = data.templates.get("prop_rewardposter").and_then(|t| t.model.clone());
            if let (Some(at), Some(m)) = (ref_node(&lib_dir, &anim, node), model.as_ref().and_then(|m| lib.load_model(m, false, assets, meshes, materials))) {
                let poster = commands.spawn((Transform::from_matrix(at), Visibility::default(), Name::new("prop_rewardposter"), RewardPoster)).id();
                let _ = attach_model(commands, poster, m, false, None, "ref_rhand");
                commands.entity(e).add_child(poster);
            }
        }
        if let Some(d) = destructible(&p) {
            commands.entity(e).insert(d);
            info.destructibles += 1;
        } else if let Some(species) = Species::of(&inst.template) {
            // in the projectile and area damage lists (+0xe90, +0xe94): 1 health, no filter
            commands.entity(e).insert((
                Destructible {
                    health: 1.0,
                    max_health: 1.0,
                    fx_death: None,
                    husk: None,
                    area_damage: 0.0,
                    radius_inner: 0.0,
                    radius_outer: 0.0,
                    impact_force: 0.0,
                    chain: false,
                    start_delay: 0.0,
                    min_delay: 0.0,
                    acceleration: 1.0,
                    dying: None,
                    chain_delay: 0.0,
                    class: DClass::Open,
                    score: p.f("score_value", -10.0),
                    takes_damage: true,
                    takes_bullet: true,
                    player_damage: true,
                    teammate_damage: true,
                    badguy_damage: true,
                },
                Critter { species, roams: p.s("generic_txt").is_some_and(|g| !g.is_empty() && g != "none") && turret_start_known(scene, inst.id), state: None, dead: None },
            ));
        }
    }
    // the level's fog, as the game material takes it (gamematerial.rs level_fog: global_settings, its property set,
    // the reader's defaults)
    let (near, far, c) = crate::gamematerial::level_fog(scene, data);
    info.fog = Some((near, far, Color::srgb(c.x, c.y, c.z)));
    let (receivers, receiver_tris) = decal_world.receiver_stats();
    info!("decal receivers: {receivers} objects, {receiver_tris} triangles");
    commands.insert_resource(decal_world);
    info.models = lib.models.len();
    info.static_bodies = physics.static_bodies;
    info.static_triangles = physics.static_triangles;

    // The player's tickerbomb, for throwing.
    let parts = lib.load_model("tickerbomb_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    commands.insert_resource(BombAssets {
        parts,
        shape: Shape::new_sphere(1.4 * physics.meters_per_unit).expect("bomb shape"),
        base: game_to_engine(),
    });

    // The sticky bomb (template player_sticky_bomb).
    let parts = lib.load_model("player_stickybomb_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    let dynamite = lib.load_model("molmer_dynamite_0", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default();
    // the grunts' collision model (templates grunt / grunt_regular: collision_lod_index 3 = grunt_coll_9), for bullet hits
    commands.insert_resource(crate::hitparts::HitHulls {
        grunt: crate::hitparts::load(&lib.dir, "grunt_coll_9"),
        roman: crate::hitparts::load(&lib.dir, "trotter_coll_0"),
        molmer: crate::hitparts::load(&lib.dir, "molmer_coll_0"),
        q111: crate::hitparts::load(&lib.dir, "q111_coll_0"),
    });
    commands.insert_resource(crate::sticky::StickyAssets { parts, dynamite, base: game_to_engine() });
    // the captains' grenade (template captain_grenade: grunt_grenade_1; ai/grenade.rs)
    commands.insert_resource(crate::ai::grenade::GrenadeAssets(lib.load_model("grunt_grenade_1", false, assets, meshes, materials).map(|m| m.parts.clone()).unwrap_or_default()));
    // the paratroopers' parachute (template captain_parachute: fxo_captain_parachute; ai/fall.rs): skinned, drawn in
    // its rest pose, the joint attributes dropped
    let chute: Vec<(Handle<Mesh>, Handle<StandardMaterial>)> = lib
        .load_model("fxo_captain_parachute", false, assets, meshes, materials)
        .map(|m| m.parts.clone())
        .unwrap_or_default()
        .into_iter()
        .filter_map(|(mh, mt)| {
            let x = meshes.get(&mh).cloned()?;
            Some((meshes.add(x.with_removed_attribute(Mesh::ATTRIBUTE_JOINT_INDEX).with_removed_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT)), mt))
        })
        .collect();
    commands.insert_resource(crate::ai::fall::ParachuteAssets(chute));
    // the army's ladder (template grunt_ladder: grunt_ladder_0; ai/ladder.rs), the same way
    let ladder: Vec<(Handle<Mesh>, Handle<StandardMaterial>)> = lib
        .load_model("grunt_ladder_0", false, assets, meshes, materials)
        .map(|m| m.parts.clone())
        .unwrap_or_default()
        .into_iter()
        .filter_map(|(mh, mt)| {
            let x = meshes.get(&mh).cloned()?;
            Some((meshes.add(x.with_removed_attribute(Mesh::ATTRIBUTE_JOINT_INDEX).with_removed_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT)), mt))
        })
        .collect();
    commands.insert_resource(crate::ai::ladder::LadderAssets(ladder));

    // The level's directional lights (lighttype "dir"): each shines down its local -Z axis.
    let (lo, hi) = (Vec3::from(scene.bounds[0]), Vec3::from(scene.bounds[1]));
    info.center = game_to_engine() * ((lo + hi) * 0.5);
    info.size = hi - lo;
    let extent = (hi - lo).length();
    let mut first = true;
    for l in &scene.lights {
        let p = |k: &str| l.props.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
        if l.props.get("lighttype").and_then(|v| v.as_str()) != Some("dir") || p("lighton") == 0.0 || p("ambientonly") != 0.0 {
            continue;
        }
        let m = scene::game_matrix(&l.transform);
        let dir = game_to_engine() * -m.z_axis.truncate().normalize();
        let mut light = commands.spawn((
            DirectionalLight {
                color: Color::srgb(p("redvalue") / 255.0, p("greenvalue") / 255.0, p("bluevalue") / 255.0),
                illuminance: 9000.0 * p("intensity").max(0.1),
                shadow_maps_enabled: first,
                ..default()
            },
            Transform::default().looking_to(dir, Vec3::Y),
            CascadeShadowConfigBuilder { maximum_distance: extent * 0.5, first_cascade_far_bound: 60.0, ..default() }.build(),
        ));
        if first {
            light.insert(crate::options::ShadowLight);
        }
        first = false;
    }
    if first {
        commands.spawn((
            DirectionalLight { illuminance: 9000.0, shadow_maps_enabled: true, ..default() },
            Transform::default().looking_to(Vec3::new(-0.4, -1.0, -0.3), Vec3::Y),
            crate::options::ShadowLight,
        ));
    }
    info
}

/// An object's attach_audio_loop sound (DefaultGameObject_Load 0x0044fd20 reads it into +0x4f4 with flag 0x20000;
/// the per-frame update 0x004505a0 starts it as a positional loop on the object; its removal 0x00450490 stops it). The object's own sound handle
/// (+0x410, 0x00457aa0) is the Positional sound that follows it.
#[derive(Component)]
pub struct AudioLoop(pub String);

fn audio_loops(
    new: Query<(Entity, &AudioLoop), Added<AudioLoop>>,
    mut died: MessageReader<Died>,
    mut play: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopSoundsOn>,
) {
    for (e, l) in &new {
        play.write(crate::audio::PlaySound::on(l.0.clone(), e).looping(true));
    }
    for d in died.read() {
        stop.write(crate::audio::StopSoundsOn(d.0));
    }
}

/// The stage swap shared by a death (`husks`) and a save's restore (`restore_stages`): a placed object loses its
/// parts, collider and decals (Object_Remove 0x004547b0, once: decals, force actions, shadow, detach, lists; the
/// destructor Object_Destruct 0x004512b0 is the entity's despawn) and becomes another template's object in place
/// (Object_SpawnHusk 0x00455660), or goes.
#[derive(bevy::ecs::system::SystemParam)]
pub struct StageSwap<'w, 's> {
    commands: Commands<'w, 's>,
    data: Res<'w, GameData>,
    lib: ResMut<'w, Library>,
    physics: NonSendMut<'w, Physics>,
    assets: Res<'w, AssetServer>,
    meshes: ResMut<'w, Assets<Mesh>>,
    materials: ResMut<'w, Assets<StandardMaterial>>,
    decal_world: Option<ResMut<'w, crate::decals::DecalWorld>>,
    /// the next stage's husk pick (Object_LoadBaseProps 0x00451360 of the new object); each level starts it over
    rng: crate::levelreset::PerLevel<'w, 's, crate::fxvm::CrtRand>,
}

impl StageSwap<'_, '_> {
    /// The object's collider and decals go (0x00455640: its decals fade, decals.rs, and it takes no more).
    fn unlink(&mut self, e: Entity, p: &Placed) {
        // (a movable object's collider is its dynamic body: movable.rs)
        if let Some(c) = p.collider {
            if self.physics.bodies.contains_key(&c) {
                self.physics.remove(c);
                self.commands.entity(e).remove::<(crate::physics::Body, crate::movable::Movable)>();
            } else {
                self.physics.remove_static(c);
            }
        }
        if let Some(w) = self.decal_world.as_mut() {
            w.remove_object(e);
        }
    }

    /// Removed for good: unlinked and despawned.
    fn remove(&mut self, e: Entity, p: &Placed) {
        self.unlink(e, p);
        self.commands.entity(e).despawn();
    }

    /// A dormant barrier's collider at `world`, solid and in the bullets' list (0x00450fb0's start_blocking; the ray
    /// collector still drops invisible_material, 0x004138c0).
    fn make_solid(&mut self, b: &DormantBarrier, world: Mat4) -> Option<BodyId> {
        let model = self.lib.load_model(&b.collision, false, &self.assets, &mut self.meshes, &mut self.materials)?;
        let c = static_collider(&mut self.physics, model, world)?;
        if let Some(m) = &b.material {
            self.physics.materials.insert(c, m.clone());
            if m == "invisible_material" {
                self.physics.no_bullets.insert(c);
            }
        }
        Some(c)
    }

    /// The object becomes `template` at `world` (its own matrix), keeping its entity and InstanceId: the husk made
    /// by Object_SpawnHusk 0x00455660 through the factory (ObjectSystem_CreateObject 0x0045f020) from its template
    /// alone. Without such a template the object is despawned. The caller has unlinked it.
    fn become_template(&mut self, e: Entity, template: &str, world: Mat4, children: &[Entity]) {
        for &c in children {
            self.commands.entity(c).despawn();
        }
        let Some(ht) = self.data.templates.get(template).cloned() else {
            self.commands.entity(e).despawn();
            return;
        };
        let hp = self.data.template_props(template);
        let mut model_name = None;
        if let Some(m) = ht.model.as_ref().and_then(|m| self.lib.load_model(m, false, &self.assets, &mut self.meshes, &mut self.materials)) {
            model_name = ht.model.clone();
            let _ = attach_model(&mut self.commands, e, m, true, None, "ref_rhand");
            // the husk is a new object: a receiver like any other (decal_receive of its template)
            if m.skeleton.is_none() && hp.f("decal_receive", 1.0) != 0.0 {
                if let Some(w) = self.decal_world.as_mut() {
                    w.add_object(e, m, Mat4::from_quat(game_to_engine()).inverse() * world);
                }
            }
            // area damage measures to the husk's own bounds (a destructible husk: the houses' _da / _db stages)
            self.commands.entity(e).insert(Bounds { center: (m.min + m.max) * 0.5, radius: (m.max - m.min).length() * 0.5 });
        }
        // its collision model, or (collide_poly) its render model, as a placed object's
        let coll_name = ht.collision.clone().or_else(|| (hp.i("collide_poly", 0) != 0).then_some(model_name).flatten());
        // a movable husk (boulder_large -> boulder_medium ...): a new object, unpinned unless its template pins it
        let default_object = is_default_object(hp.s("behavior").unwrap_or(""));
        if default_object {
            if let Some(m) = crate::movable::Movable::new(&hp, coll_name.clone()) {
                self.commands.entity(e).insert(m);
            }
        }
        let collider = match coll_name {
            Some(n) => self.lib.load_model(&n, false, &self.assets, &mut self.meshes, &mut self.materials).and_then(|m| static_collider(&mut self.physics, m, world)),
            None => None,
        };
        if let Some(c) = collider {
            collider_lists(&mut self.physics, &hp, c);
        }
        let aim = crate::hud::AimClass::of(&hp);
        let triggers = if default_object { ObjectTriggers::of(&hp) } else { ObjectTriggers::default() };
        let next = pick_husk(&hp, self.rng.get());
        let mut ec = self.commands.entity(e);
        ec.insert((Placed { template: template.to_owned(), collider, husk: next, triggers }, aim, Husk));
        if in_damage_list(&hp) {
            ec.insert(DamageTaker);
        } else {
            ec.remove::<DamageTaker>();
        }
        // a husk with health and its own death fx or husk dies again (v_house_2f_a -> _da -> _db -> _dc)
        if let Some(dd) = destructible(&hp) {
            ec.insert(dd);
        }
    }
}

/// What a saved game says a placed object is now (savegame.rs).
#[derive(Clone, Debug, PartialEq)]
pub enum Stage {
    /// gone (destroyed without a husk, or removed)
    Gone,
    /// a husk stage: the template it became (v_house_2f_a_db)
    Template(String),
    /// the husk picked at load (saves written before stages were recorded)
    FirstHusk,
}

/// Put a placed object straight at a saved stage (savegame.rs). No death: no Died message, so no fx_death, no
/// score, no send_trigger_on_death and no barracks stop (the mission's saved state already holds their effects).
#[derive(Message, Clone)]
pub struct RestoreStage {
    pub entity: Entity,
    pub stage: Stage,
}

/// Applies RestoreStage: the object's windows go with it (their bursts happened before the save), its loop stops,
/// its health and death state are the stage's (none when the stage does not die again), and it becomes the saved
/// stage or goes.
fn restore_stages(
    mut events: MessageReader<RestoreStage>,
    mut swap: StageSwap,
    placed: Query<(&Placed, &Transform, &Children)>,
    windows: Query<(Entity, &Window, &Placed)>,
    mut stop: MessageWriter<crate::audio::StopSoundsOn>,
) {
    for ev in events.read() {
        let Ok((p, t, children)) = placed.get(ev.entity) else { continue };
        for (we, w, wpl) in &windows {
            if w.building == ev.entity {
                swap.remove(we, wpl);
            }
        }
        stop.write(crate::audio::StopSoundsOn(ev.entity));
        swap.commands.entity(ev.entity).remove::<(AudioLoop, Destructible)>();
        let template = match &ev.stage {
            Stage::Gone => None,
            Stage::Template(t) => Some(t.clone()),
            Stage::FirstHusk => p.husk.clone(),
        };
        match template {
            Some(h) => {
                swap.unlink(ev.entity, p);
                swap.become_template(ev.entity, &h, t.to_matrix(), children);
            }
            None => swap.remove(ev.entity, p),
        }
    }
}

/// A dead object is replaced by its husk template (felled tree, ruined barracks) or removed, with its collider.
#[allow(clippy::type_complexity)]
fn husks(
    mut died: MessageReader<Died>,
    mut swap: StageSwap,
    placed: Query<(&Placed, &Transform, &Children)>,
    mut critters: Query<&mut Critter>,
    mut windows: Query<(Entity, &Window, &Placed, Option<&mut Destructible>)>,
) {
    for d in died.read() {
        // a destroyed building's windows die with it: each window holds its building (+0x520, 0x00450980 from
        // BuildingObject 0x0040ad80) and its update 0x004505a0 calls its death slot once the building is dead, so
        // each pane bursts with its own fx_death (fx_explosion_village_window) through the usual death path
        for (we, w, wpl, wd) in &mut windows {
            if w.building == d.0 {
                if let Some(mut wd) = wd {
                    if wd.dying.is_none() {
                        wd.health = 0.0;
                        wd.chain_delay = 0.0;
                        wd.dying = Some(0.0);
                    }
                    continue;
                }
                swap.remove(we, wpl);
            }
        }
        let Ok((p, t, children)) = placed.get(d.0) else { continue };
        swap.unlink(d.0, p);
        // a critter keeps its model: its death anims play (charanim.rs) while its corpse timer runs (GoRagdoll
        // 0x00431310 counts it as a ragdoll down)
        if let Ok(mut c) = critters.get_mut(d.0) {
            c.dead = Some(0.0);
            swap.commands.entity(d.0).insert(crate::corpses::Corpse::lying(crate::charanim::CRITTER_CORPSE, 0.5, true));
            continue;
        }
        // the husk picked at load (Object_LoadBaseProps 0x00451360), at the dead object's matrix
        match p.husk.clone() {
            Some(h) => swap.become_template(d.0, &h, t.to_matrix(), children),
            None => {
                swap.commands.entity(d.0).despawn();
            }
        }
    }
}

/// SpawnHusk: a new object becomes the husk template where the body lay.
fn spawn_husks(mut events: MessageReader<SpawnHusk>, mut swap: StageSwap) {
    for ev in events.read() {
        let e = swap.commands.spawn((Transform::from_matrix(ev.world), Visibility::default(), Name::new(ev.template.clone()))).id();
        swap.become_template(e, &ev.template, ev.world, &[]);
    }
}

#[allow(clippy::too_many_arguments)]
fn spawn_grunts(
    mut commands: Commands,
    mut events: MessageReader<SpawnGrunt>,
    data: Res<GameData>,
    mut lib: ResMut<Library>,
    mut anim_lib: ResMut<crate::skin::AnimLibrary>,
    mut binds: ResMut<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
    mut physics: NonSendMut<Physics>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    player: Query<Entity, With<crate::ai::Player>>,
    // the troops' random stream: each level starts it over
    mut rng: crate::levelreset::PerLevel<crate::fxvm::CrtRand>,
) {
    let rng = rng.get();
    for ev in events.read() {
        let mut p = data.template_props(&ev.template);
        for k in &ev.switches {
            p.0.insert((*k).into(), 1.into());
        }
        if ev.dropped.as_ref().is_some_and(|d| d.rocket) {
            p.0.insert("rocket_captain".into(), 1.into());
        }
        if ev.jumper {
            p.0.insert("jumper_captain".into(), 1.into());
        }
        let Some(model_name) = data.templates.get(&ev.template).and_then(|t| t.model.clone()) else { continue };
        let Some(model) = lib.load_model(&model_name, false, &assets, &mut meshes, &mut materials).cloned_parts() else { continue };
        // a hidden grunt faces as his node (ai/ic.rs)
        let yaw = ev.ic.as_ref().map_or(0.0, |ic| ai::ic::yaw_of(game_to_engine() * Quat::from_mat4(&ic.origin)));
        let world = Mat4::from_rotation_translation(Quat::from_rotation_y(yaw) * game_to_engine(), ev.at);
        // (its GlobalTransform too: the AI's first search runs before the transforms propagate, and would look from
        // the origin)
        let e = commands.spawn((Transform::from_matrix(world), GlobalTransform::from(Transform::from_matrix(world)), Visibility::default(), Name::new(ev.template.clone()))).id();
        if ev.alarm {
            commands.entity(e).insert(crate::alarm::AlarmTroop { far_cull: ev.alarm_far_cull });
        }
        if let Some(v) = ev.thrown {
            commands.entity(e).insert(crate::tossed::Tossed(v));
        }
        if ev.smoking {
            commands.entity(e).insert(crate::tossed::SmokingTroop::default());
        }
        if let Some(a) = &ev.army {
            commands.entity(e).insert(a.clone());
        }
        // its template's send_trigger_on_death (no placed instance carries it: mission.rs DeathTrigger)
        let on_death = p.i("send_trigger_on_death", 0);
        if on_death != 0 {
            commands.entity(e).insert(crate::mission::DeathTrigger(on_death));
        }
        // what its body leaves when removed (Object_LoadBaseProps 0x00451360: fx_death, the husk picked at load)
        let remains = crate::corpses::Remains { fx_death: p.s("fx_death").filter(|s| !s.eq_ignore_ascii_case("null")).map(str::to_owned), husk: pick_husk(&p, rng) };
        if remains.fx_death.is_some() || remains.husk.is_some() {
            commands.entity(e).insert(remains);
        }
        let mut ctx = SkinCtx { lib: &mut anim_lib, binds: &mut binds };
        let hand_bone = ai::Kind::from_template(&ev.template).map_or("ref_rhand", ai::Kind::weapon_bone);
        let (_, _, hand) = attach_model(&mut commands, e, &model, true, Some(&mut ctx), hand_bone);
        if let Some(lods) = data.templates.get(&ev.template).and_then(|t| crate::graphics::geometry::lod::LodSource::new(t, &p, true)) {
            commands.entity(e).insert(lods);
        }
        if let (Some(h), Some(w)) = (hand, ai::weapon_template(&ev.template).and_then(|w| data.templates.get(w)).and_then(|t| t.model.clone())) {
            if let Some(m) = lib.load_model(&w, false, &assets, &mut meshes, &mut materials) {
                attach_weapon(&mut commands, h, m, Transform::IDENTITY);
            }
        }
        let mut character = ai::body::CharacterBody::new(&p);
        // a squad member passes through his squad mates: +0x510 = his group, the squad's -squad_tag (0x0042de60)
        if ev.squad.is_some() {
            character.group = Some(ev.alert_group as u32);
        }
        if let Some((body, character)) = ai::body::add(&mut physics, e, character, ev.at, "grunt") {
            let mut g = ai::make_grunt(&p, &data, ai::Kind::of(&ev.template), ev.at, yaw, rng);
            g.cold = ai::cold_level(&lib.dir);
            g.priority_tags = ev.priority_tags;
            if let Some(ic) = &ev.ic {
                // hidden (0x0042d000): waiting for his trigger at his node, held there, looking for targets with the
                // host's trigger_los
                g.mind = ai::Mind::WaitTrigger;
                g.trigger_los = ic.trigger_los;
                g.fixed = Some(ev.at);
                commands.entity(e).insert(ai::ic::IcEntry { spawn: ic.clone(), running: false });
            } else if let Some(d) = &ev.dropped {
                commands.entity(e).insert(Dropped);
                // a dropped troop: a rocket captain's range rolled in 50..100 (0x00434a80), then its IC anim's
                // landing orders
                if d.rocket {
                    g.range = 50.0 + rng.rand() as f32 * 50.0 / 32768.0;
                }
                let notes = d.mover.as_deref().and_then(|m| anim_lib.anim(m)).map(|a| a.notes.clone()).unwrap_or_default();
                if !ai::drop_orders(&mut g, notes.iter().map(|(_, n)| n.as_str())) {
                    g.target = player.single().ok();
                    g.mind = ai::Mind::Chase;
                }
                // down his rope by the IC anim, then the free fall into those orders (ai/paratroop.rs)
                if let Some(m) = d.mover.as_deref().filter(|m| anim_lib.anim(m).is_some()) {
                    ai::paratroop::start(&mut commands, e, &mut g, ev.at, m);
                }
            } else if let Some(link) = ev.squad {
                // a squad member (0x0042de60): his group, his bone, state 0xde
                g.squad = Some(link);
                g.alert_group = ev.alert_group;
                g.mind = ai::Mind::InSquad;
                g.attention = true;
            } else if ev.barracks.is_some() || ev.exit.is_some() {
                // a barracks troop (0x0042db50) or an alarm generator's (0x0042dd50, ai.rs: then +0x488 0x200, the
                // charge): its alert group, marker tags and the walk out through the exit
                if let Some(b) = ev.barracks {
                    commands.entity(e).insert(crate::mission::BarracksTroop(b));
                }
                g.charge = ev.alarm;
                g.alert_group = ev.alert_group;
                g.marker_tags = ev.marker_tags;
                g.exit = ev.exit;
                g.mind = ai::Mind::ToMarker;
                if let Some(x) = ev.exit {
                    let d = (x - ev.at).with_y(0.0);
                    g.yaw = d.x.atan2(d.z);
                }
            } else {
                // spawned troops charge the player (0x0042dd50 -> state 0x5b -> 0x0042ce70)
                g.target = player.single().ok();
                g.mind = ai::Mind::Chase;
                g.charged = true;
            }
            let bounds = Bounds { center: (model.min + model.max) * 0.5, radius: (model.max - model.min).length() * 0.5 };
            commands.entity(e).insert((body, character, Health(p.f("max_health", 100.0)), bounds, g));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> Props {
        Props(v.as_object().cloned().unwrap_or_default())
    }

    /// Bookmark 7 of session 2026-10-18_17-00-51 (the purple pyramid at lev_1_m1's generator 215): an alarm generator
    /// is never drawn although its set (p_grunt_generator) says invisible 0; other objects still follow their set.
    #[test]
    fn alarm_generators_are_hidden() {
        let set = props(serde_json::json!({"behavior": "alarm_generator", "invisible": 0, "force_ground": 1}));
        let behavior = set.s("behavior").unwrap_or("");
        assert!(set.f("visible", 1.0) != 0.0 && set.f("invisible", 0.0) == 0.0, "the set alone says drawn");
        assert!(hidden_by_class(behavior));
        for shown in ["", "building", "tree", "fx_explosion", "twiglet_generator"] {
            assert!(!hidden_by_class(shown), "{shown}");
        }
    }

    /// DefaultGameObject_OnTrigger 0x00450fb0: kill, deactivate, start_blocking in that order; a field fires once.
    #[test]
    fn trigger_receive_is_one_shot() {
        // v_prison_gate 248 (instance values over p_prison_gate): receive_trigger_kill 556
        let mut t = ObjectTriggers::of(&props(serde_json::json!({ "receive_trigger_kill": 556, "takes_damage": 0 })));
        assert_eq!(t.receive(900), None);
        assert_eq!(t.receive(0), None, "trigger 0 is never delivered (0x00461870)");
        assert_eq!(t.receive(556), Some(TriggerAct::Kill));
        assert_eq!(t.receive(556), None, "cleared once it fired");
        let mut t = ObjectTriggers { kill: 7, deactivate: 7, start_blocking: 8 };
        assert_eq!(t.receive(7), Some(TriggerAct::Kill), "kill is tested first");
        assert_eq!(t.receive(7), Some(TriggerAct::Deactivate));
        assert_eq!(t.receive(8), Some(TriggerAct::StartBlocking));
        assert_eq!(t, ObjectTriggers::default());
    }

    /// Object_LoadBaseProps 0x00451360: husk, husk1..husk9 while present, (rand() * n) >> 15; "null" is none.
    #[test]
    fn husk_pick() {
        let mut rng = crate::fxvm::CrtRand::default();
        assert_eq!(pick_husk(&props(serde_json::json!({})), &mut rng), None);
        assert_eq!(pick_husk(&props(serde_json::json!({ "husk": "null" })), &mut rng), None);
        assert_eq!(pick_husk(&props(serde_json::json!({ "husk": "oilpump_d" })), &mut rng).as_deref(), Some("oilpump_d"));
        // a present "null" is drawn like a name: with husk1 the object gets husk1 about half the time
        let p = props(serde_json::json!({ "husk": "null", "husk1": "b" }));
        let picks: Vec<Option<String>> = (0..200).map(|_| pick_husk(&p, &mut rng)).collect();
        assert!(picks.contains(&None) && picks.contains(&Some("b".into())));
        // husk3 is not read: husk2 is missing
        let p = props(serde_json::json!({ "husk": "a", "husk1": "b", "husk3": "d" }));
        let mut seen = std::collections::HashSet::new();
        for _ in 0..200 {
            seen.insert(pick_husk(&p, &mut rng).unwrap());
        }
        assert_eq!(seen, ["a", "b"].iter().map(|s| s.to_string()).collect());
    }

    /// The bullets' list +0xe90 for default objects (0x0044fd20): damage takers and projectile blockers, never
    /// no_collide or collide_barrier objects.
    #[test]
    fn default_objects_in_the_bullet_list() {
        let listed = |v: serde_json::Value| in_bullet_list(&props(v));
        assert!(listed(serde_json::json!({ "takes_damage": 1, "material": "metal" })), "oil pump");
        assert!(listed(serde_json::json!({ "blocks_projectiles": 1 })), "igloo");
        assert!(!listed(serde_json::json!({ "takes_damage": 0 })), "a plain rock");
        assert!(!listed(serde_json::json!({ "blocks_projectiles": 0, "collide_barrier": 1, "visible": 0 })), "invisible walls");
        assert!(!listed(serde_json::json!({ "no_collide": 1, "takes_damage": 1 })), "no_collide");
        assert!(!listed(serde_json::json!({ "takes_damage": 1, "material": "invisible_material" })));
        // other classes: buildings unless base_defend / invisible, trees and kegs always
        assert!(listed(serde_json::json!({ "behavior": "building" })));
        assert!(!listed(serde_json::json!({ "behavior": "building", "base_defend": 1 })));
        assert!(listed(serde_json::json!({ "behavior": "tree" })) && listed(serde_json::json!({ "behavior": "fx_explosion" })));
        // "defaultgameobject" by name is a default object (p_l4_trees: blocks_projectiles 1)
        assert!(listed(serde_json::json!({ "behavior": "defaultgameobject", "blocks_projectiles": 1, "takes_damage": 0 })));
        assert!(!listed(serde_json::json!({ "behavior": "defaultgameobject", "takes_damage": 0 })));
        assert!(!in_damage_list(&props(serde_json::json!({ "behavior": "defaultgameobject", "blocks_projectiles": 1, "takes_damage": 0 }))));
    }

    /// FxExplosion_Load 0x0041c180: a keg without its fuse keys gets start_delay 1.0, acceleration_rate 0.75,
    /// min_delay 0.1; a default object 2010 impact force.
    #[test]
    fn keg_fuse_defaults() {
        let d = destructible(&props(serde_json::json!({ "behavior": "fx_explosion", "max_health": 1, "fx_death": "x" }))).unwrap();
        assert_eq!((d.start_delay, d.acceleration, d.min_delay), (1.0, 0.75, 0.1));
        assert_eq!(d.impact_force, 2010.0);
        let d = destructible(&props(serde_json::json!({ "behavior": "defaultgameobject", "max_health": 1, "fx_death": "x" }))).unwrap();
        assert_eq!(d.impact_force, 2010.0);
    }
}
