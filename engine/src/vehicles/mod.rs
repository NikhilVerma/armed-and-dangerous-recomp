//! Vehicles and big units of the later levels: the rocket towers and rocket buds (RocketTowerBuilding /
//! RocketTowerTurret / RocketBudTurret, src/game/game_03.cpp and game_09.cpp), the turret bosses (SaucerObject,
//! ZeppelinObject, ColossusObject: src/ai/ai_04.cpp, ai_07.cpp), the projectiles they fire (game_07.cpp) and the
//! scripted flight paths they follow (the IC "mover" anims, src/ai/ai_03.cpp / ai_06.cpp). The forge cannon, a
//! mountable gun, is turret.rs's (it shares the StigTurret base).
//!
//! Each unit is a plugin of its own over the shared parts here:
//! - [`nodes`]: a model's named nodes (bones, refs) in model space, from its skeleton or its reference anim;
//! - [`MovingCollider`]: a placed object's static collision mesh carried along when the object moves;
//! - [`StartLockFx`]: the effects an object starts at load ("fx_origin_start_lock", Object_LoadProperties 0x004517b0);
//! - [`VehicleRng`]: the vehicles' own rand() stream (so the grunts' rolls stay as they were);
//! - the state log's `vehicles` (playtest expects).
//!
//! Docs: docs/subsystems/vehicles.md (what is ported, with addresses; open questions).

pub mod boss;
pub mod gunnery;
pub mod mover;
pub mod nodes;
pub mod rocket_turrets;
pub mod rockets;

use crate::levelreset::LevelAppExt;
use crate::physics::{Physics, jq, jr};
use bevy::prelude::*;
use bevy::transform::TransformSystems;
use oxijolt::prelude::*;

pub struct VehiclesPlugin;

impl Plugin for VehiclesPlugin {
    fn build(&self, app: &mut App) {
        app.insert_level_resource(|| VehicleRng(crate::fxvm::CrtRand::seeded(0x5a5e)))
            .init_resource::<VehicleTuning>()
            .init_level_resource::<VehicleLog>()
            .init_level_resource::<SightBlind>()
            .add_systems(Update, (start_lock_fx, sight_blind))
            .add_systems(PostUpdate, move_colliders.after(TransformSystems::Propagate))
            .add_systems(Last, publish_log)
            .add_plugins((rockets::RocketsPlugin, mover::MoverPlugin, rocket_turrets::RocketTurretsPlugin, boss::BossPlugin));
    }
}

/// The vehicles' numbers that are not in the property sets: the original's code constants (with the function that
/// holds each), and scales for later settings (difficulty, mods). Every unit's own numbers stay in its property set;
/// this resource scales them. Defaults = the original.
#[derive(Resource, Clone, Debug)]
pub struct VehicleTuning {
    /// the turret bosses' weapon turns toward its aim by this fraction per second of what is left (0x0042ecf0: 4)
    pub weapon_turn_rate: f32,
    /// a boss looks for a target every `retarget_every` + up to `retarget_jitter` seconds (0x00435b50: 1 + rand)
    pub retarget_every: f32,
    pub retarget_jitter: f32,
    /// the colossus's fire range (ColossusObject_Slot6 0x00436390: 400, over its fire_range_*)
    pub colossus_range: f32,
    /// how fast a walking boss turns, radians a second (360 degrees: 0x00436390, 0x0042df70)
    pub boss_turn_speed: f32,
    /// a marker counts as reached within this distance (the remaster's straight walk; the original's route follower
    /// 0x00470c30 decides on its grid)
    pub marker_reached: f32,
    /// scales: every unit's health (on top of the difficulty's bad-guy factor for the AI bosses), the damage of every
    /// vehicle's projectile, every unit's pauses between shots (below 1: faster fire)
    pub health_scale: f32,
    pub damage_scale: f32,
    pub fire_pause_scale: f32,
}

impl Default for VehicleTuning {
    fn default() -> Self {
        VehicleTuning {
            weapon_turn_rate: 4.0,
            retarget_every: 1.0,
            retarget_jitter: 1.0,
            colossus_range: 400.0,
            boss_turn_speed: 360f32.to_radians(),
            marker_reached: 15.0,
            health_scale: 1.0,
            damage_scale: 1.0,
            fire_pause_scale: 1.0,
        }
    }
}

/// The vehicles' rand() (the CRT's, 0x0055d5b4 semantics): its own stream, seeded per level.
#[derive(Resource)]
pub struct VehicleRng(pub crate::fxvm::CrtRand);

impl VehicleRng {
    /// rand() / 32768 (0..1)
    pub fn unit(&mut self) -> f32 {
        self.0.rand() as f32 / 32768.0
    }
    /// 0x0046e2c0 Rand_FloatRange: lo + rand() * (hi - lo) / 32768
    pub fn range(&mut self, lo: f32, hi: f32) -> f32 {
        lo + self.unit() * (hi - lo)
    }
    /// The counts' roll ((rand() * (hi - lo + 1)) >> 15) + lo (0x00435420, 0x00435940)
    pub fn count(&mut self, lo: i64, hi: i64) -> i64 {
        ((self.0.rand() as i64 * (hi - lo + 1)) >> 15) + lo
    }
}

/// A placed object's static collision mesh that follows the object: level.rs builds it from the vertices in world
/// space at the placement (`spawn`, the world matrix then); each frame the body is put at the object's current matrix
/// times the placement's inverse. The original moves the object's body with it (Object_SetTransform 0x00457990).
#[derive(Component)]
pub struct MovingCollider {
    pub body: oxijolt::prelude::BodyId,
    pub spawn_inverse: Mat4,
}

impl MovingCollider {
    pub fn new(body: oxijolt::prelude::BodyId, spawn: Mat4) -> Self {
        MovingCollider { body, spawn_inverse: spawn.inverse() }
    }
}

fn move_colliders(mut physics: NonSendMut<Physics>, q: Query<(&MovingCollider, &GlobalTransform)>) {
    let k = physics.meters_per_unit;
    for (c, gt) in &q {
        let (_, rot, pos) = (gt.to_matrix() * c.spawn_inverse).to_scale_rotation_translation();
        if let Ok(mut b) = physics.world.body_mut(c.body) {
            let _ = b.set_position_and_rotation(jr(pos * k), jq(rot.normalize()), Activation::DontActivate);
        }
    }
}

/// "fx_origin_start_lock" (Object_LoadProperties 0x004517b0, physics_02.cpp:620-630): an fx started at load and kept
/// on the object (Fx_Create 0x0041cc60, flag 4), at the "fx_origin_start_lock_parent" node unless "null". The
/// zeppelin's propeller and engine hum (fx_zeppelin_prop at ref_prop), the saucer's loop (fx_saucer_lp).
#[derive(Component)]
pub struct StartLockFx {
    pub name: String,
    /// model-space node position (game axes) or the origin
    pub at: Vec3,
}

fn start_lock_fx(mut commands: Commands, q: Query<(Entity, &StartLockFx, &GlobalTransform)>, mut fx: MessageWriter<crate::fx::StartFx>) {
    for (e, s, gt) in &q {
        // an anchor at the node, carried by the object (the fx objects follow it)
        let anchor = commands.spawn((Transform::from_translation(s.at), Visibility::default())).id();
        commands.entity(e).add_child(anchor);
        let (_, rot, _) = gt.to_scale_rotation_translation();
        fx.write(crate::fx::StartFx { name: s.name.clone(), at: gt.transform_point(s.at), rotation: rot, follow: Some(anchor), position_only: false });
        commands.entity(e).remove::<StartLockFx>();
    }
}

/// The state log's `vehicles`: one entry per unit by instance id (or a name for spawned parts), kept by the units'
/// systems (a dead unit's last entry stays) and copied to the playtest state (expect steps:
/// `"vehicles.149.health": [0, 1999]`).
#[derive(Resource, Default)]
pub struct VehicleLog(pub serde_json::Map<String, serde_json::Value>);

fn publish_log(log: Res<VehicleLog>, state: Option<ResMut<crate::playtest::PlayState>>) {
    if let (Some(mut s), false) = (state, log.0.is_empty()) {
        s.0.insert("vehicles".into(), serde_json::Value::Object(log.0.clone()));
    }
}

/// Every placed instance's property values (the template's set with the instance's own over it), by instance id:
/// the units read their placement values (receive_trigger_activate, generic_txt, target tags, send_trigger_on_death)
/// from here, as the original's Load reads the instance block before the template's set (0x00453400 ...).
pub fn placements(dir: &std::path::Path, data: &crate::gamedata::GameData) -> std::collections::HashMap<u32, crate::gamedata::Props> {
    let Ok(text) = std::fs::read_to_string(dir.join("scene.json")) else { return Default::default() };
    let Ok(scene) = serde_json::from_str::<crate::scene::SceneFile>(&text) else { return Default::default() };
    scene.instances.iter().map(|i| (i.id, data.instance_props(&i.template, &i.props))).collect()
}

/// The class an object is made as: its behaviour, else its template name (ObjectSystem_CreateObject 0x0045f020 hashes
/// that name into the behaviour table 0x0045df60: "v_rockettower" is a class of its own, id 82).
pub fn class_of<'a>(p: &'a crate::gamedata::Props, template: &'a str) -> &'a str {
    p.s("behavior").filter(|b| !b.is_empty()).unwrap_or(template)
}

/// A unit's health and death record (the Object base: health and max_health, fx_death, husk,
/// score_value; Object_LoadBaseProps 0x00451360), with the damage rule of its class: `Open` for the turrets that take
/// every hit (Object_TakeDamage 0x004555e0), `Building` with only badguy_damage off for the AI bosses (a bad guy's
/// shot does nothing to a bad guy: the same-team rule, docs/subsystems/damage.md). `scale` multiplies max_health (the
/// difficulty's factor for AI characters, Ai_InitGeneric 0x004247f0, and VehicleTuning.health_scale).
pub fn unit_health(p: &crate::gamedata::Props, class: crate::fx::DClass, husk: Option<String>, scale: f32) -> crate::fx::Destructible {
    let max = p.f("max_health", 100.0) * scale;
    crate::fx::Destructible {
        health: max,
        max_health: max,
        fx_death: p.s("fx_death").filter(|s| !s.eq_ignore_ascii_case("null")).map(str::to_owned),
        husk,
        area_damage: p.f("area_damage", 0.0),
        radius_inner: p.f("damage_radius_inner", 0.0),
        radius_outer: p.f("damage_radius_outer", 0.0),
        impact_force: p.f("impact_force", 0.0),
        chain: false,
        start_delay: 0.0,
        min_delay: 0.0,
        acceleration: 1.0,
        dying: None,
        chain_delay: 0.0,
        class,
        score: p.f("score_value", 0.0),
        takes_damage: true,
        takes_bullet: true,
        player_damage: true,
        teammate_damage: true,
        badguy_damage: false,
    }
}

/// Kills an object through its death path now (vt+0xd4: a lethal record): no-op when it is already dying or dead.
pub fn kill(d: &mut crate::fx::Destructible) {
    if d.dying.is_none() && d.health > 0.0 {
        d.health = 0.0;
        d.chain_delay = 0.0;
        d.dying = Some(0.0);
    }
}

/// The bodies sight rays pass through: the collide barriers (invisible walls) and invisible_material, which are in
/// none of the lists a sight query takes (DefaultGameObject_Load 0x0044fd20: a collide barrier joins no list; the
/// sight queries 0x00426fd0 / Turret_SightHit use the collidable list +0xe9c; 0x004138c0 drops invisible_material).
#[derive(Resource, Default)]
pub struct SightBlind {
    pub bodies: std::collections::HashSet<oxijolt::prelude::BodyId>,
    built: bool,
}

fn sight_blind(
    mut blind: ResMut<SightBlind>,
    q: Query<(&crate::level::InstanceId, &crate::level::Placed)>,
    data: Res<crate::gamedata::GameData>,
    lib: Res<crate::skin::AnimLibrary>,
) {
    if blind.built || q.is_empty() {
        return;
    }
    blind.built = true;
    let props = placements(&lib.dir, &data);
    for (id, pl) in &q {
        let (Some(c), Some(p)) = (pl.collider, props.get(&id.0)) else { continue };
        if p.f("collide_barrier", 0.0) != 0.0 || p.s("material") == Some("invisible_material") {
            blind.bodies.insert(c);
        }
    }
}

/// A sight line (0x00426fd0 / Turret_SightHit): from `from` to `to`, nothing in between but the bodies in `skip`
/// (the looker's and the target's own) and the ones sight passes (SightBlind).
pub fn sight(physics: &Physics, blind: &SightBlind, from: Vec3, to: Vec3, skip: &[oxijolt::prelude::BodyId]) -> bool {
    blocker(physics, blind, from, to, skip).is_none()
}

/// What stops a sight line first (its body and the point), if anything.
pub fn blocker(physics: &Physics, blind: &SightBlind, from: Vec3, to: Vec3, skip: &[oxijolt::prelude::BodyId]) -> Option<(oxijolt::prelude::BodyId, Vec3)> {
    let dir = (to - from).normalize_or(Vec3::Y);
    let (mut at, mut left) = (from, (to - from).length());
    for _ in 0..32 {
        let (b, hit, _) = physics.raycast(at, dir, left)?;
        if !skip.contains(&b) && !blind.bodies.contains(&b) {
            return Some((b, hit));
        }
        left -= hit.distance(at) + 0.05;
        if left <= 0.0 {
            return None;
        }
        at = hit + dir * 0.05;
    }
    None
}

/// The game-space position of an engine-space point (the state log speaks game coordinates, as the scene files).
pub fn game_pos(p: Vec3) -> serde_json::Value {
    let g = crate::level::game_to_engine().inverse() * p;
    let r = |v: f32| (v as f64 * 10.0).round() / 10.0;
    serde_json::json!({"x": r(g.x), "y": r(g.y), "z": r(g.z)})
}
