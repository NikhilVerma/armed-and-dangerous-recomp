//! The rocket towers and the rocket buds (src/game/game_03.cpp:1065-1140, game_09.cpp:280-720).
//!
//! **Rocket tower** (template v_rockettower, class 82 RocketTowerBuilding, vtable 0x005a87c0): a default object (no
//! damage of its own: p_rockettower takes_damage 0) that makes its turret at load (slot 8 0x004510b0): a
//! "v_rockettower_turret" at its "ref_turret" node, given the tower's send_trigger_on_death and tag (0x0049c530, the
//! tower's tag cleared). The turret's death kills the tower (0x0049c540: husk v_rockettower_d, fx_exp_generic, area
//! damage 50 in 20..100) and the tower's death kills the turret (0x00451160).
//!
//! **Rocket tower turret** (rocket_tower_turret, class 81 RocketTowerTurret, vtable 0x005a8b88; load 0x0049c320): takes
//! every hit (Object_TakeDamage, no filter; p_rocket_tower_turret 750 health, fx_explosion_rocket_turret, score 500).
//! The update 0x0049c6a0: Roman outside active_range (450, 3D, from the turret's spawn point): the turret spins up by
//! spin_accel to spin_max_speed (the turn anim's fraction per second) with the loop "wpn_rocket_tower_turn_lp".
//! Inside: it spins down by spin_stop_accel; stopped, while ready (0x0049c5b0: never fired or recycle_time 6 s since),
//! a sight check every second (a ray from 10 above the turret to Roman, Turret_SightHit) and, when he is seen or
//! within blind_fire_duration (1.0) of the last sighting, it fires (0x0049c610): one rocket_tower_missile from each of
//! the four nozzles (ref_nozzle_00..03), each homing on Roman's "chest".
//!
//! **Rocket bud** (rocket_bud_turret, class 83 RocketBudTurret, vtable 0x005adde8; load 0x0049c9b0): takes every hit
//! (Object_TakeDamage; p_rocket_bud_turret 600 health, husk v_rocketbud_d, fx_exp_rocket_bud, score 500). The update
//! 0x0049cbe0: Roman within active_range (1000) of its spawn point, ready (recycle_time 6), the same sight check, and
//! seen or within blind_fire_duration (10) of the last sighting: fire 0x0049cab0, a rocket_bud_missile lobbed from the
//! emitter ref_nozzle_00 at Roman's position spread by +-inaccuracy_range (10) in x and y, the muzzle fx
//! fx_muzzle_flash_rocket_bud (one-shot, 0x00497fd0).

use super::nodes::Nodes;
use super::rockets::{self, Launch};
use crate::fx::{DClass, Destructible, Died, Src};
use crate::level::{InstanceId, Placed};
use bevy::prelude::*;

pub struct RocketTurretsPlugin;

impl Plugin for RocketTurretsPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, (tower_update, bud_update)).chain().after(crate::fx::FxSet).run_if(crate::playing))
            // before the husk swap removes the dead (level.rs husks runs after mission.rs deaths)
            .add_systems(Update, deaths.after(crate::fx::FxSet).before(crate::mission::deaths));
    }
}

/// A rocket tower (the building) and the turret it made.
#[derive(Component)]
pub struct RocketTower {
    pub turret: Option<Entity>,
}

/// The tower's spinning turret (RocketTowerTurret: the fields of 0x0049c320).
#[derive(Component)]
pub struct RocketTowerTurret {
    pub tower: Entity,
    /// the tower's collision: the turret sits in it, and its sight line starts inside it (the tower is the turret's
    /// linked object, 0x0049c520, passed over by the sight query with the turret)
    pub tower_body: Option<oxijolt::prelude::BodyId>,
    /// the tower's instance id (the state log's key: "<id>t")
    pub id: u32,
    /// where it was made (its range is measured from here)
    pub home: Vec3,
    pub range: f32,
    pub spin_max: f32,
    pub spin_accel: f32,
    pub spin_stop: f32,
    pub blind: f32,
    pub recycle: f32,
    pub projectile: String,
    pub nozzles: [String; 4],
    /// how far round it has turned (the turn anim's fraction) and how fast it turns (fractions a second)
    pub angle: f32,
    pub spin: f32,
    /// Roman in range last frame; the next sight check in; Roman seen at the last check; when he was last seen; when
    /// it last fired (0: not since he came in range)
    pub in_range: bool,
    pub sight: f32,
    pub seen: bool,
    pub seen_at: f32,
    pub last_fire: f32,
    /// the turn loop sound plays
    pub turning: bool,
    pub volleys: u32,
    /// the "center" node's stand-in (the parts hang off it), the turn anim the angle scrubs (its fraction: 0x00408800(4)
    /// +4 with Anim_Set 0x00408d90 flags 0x210) and the nozzles' places on "center" (the anim's first frame)
    pub holder: Entity,
    pub turn: String,
    pub nodes: Nodes,
}

/// A rocket bud (RocketBudTurret: the fields of 0x0049c9b0).
#[derive(Component)]
pub struct RocketBud {
    pub id: u32,
    /// where it stands (its range is measured from here)
    pub home: Vec3,
    pub range: f32,
    pub spread: f32,
    pub blind: f32,
    pub recycle: f32,
    pub projectile: String,
    pub emitter: String,
    pub muzzle_fx: Option<(String, String)>,
    /// the next sight check in; Roman seen at the last check; when he was last seen; when it last fired
    pub sight: f32,
    pub seen: bool,
    pub seen_at: f32,
    pub last_fire: f32,
    pub shots: u32,
}

/// Rocket towers and buds for the placed objects, and the towers' turrets.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    q: Query<(Entity, &InstanceId, &Placed, &Transform), (Without<RocketTower>, Without<RocketBud>)>,
    mut done: crate::levelreset::PerLevel<Option<std::collections::HashSet<Entity>>>,
    mut props: crate::levelreset::PerLevel<Option<std::collections::HashMap<u32, crate::gamedata::Props>>>,
    (mut lib, mut anim_lib, mut binds, assets, mut meshes, mut materials): (
        ResMut<crate::scene::Library>,
        ResMut<crate::skin::AnimLibrary>,
        ResMut<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
        Res<AssetServer>,
        ResMut<Assets<Mesh>>,
        ResMut<Assets<StandardMaterial>>,
    ),
    mut physics: NonSendMut<crate::physics::Physics>,
    (time, tuning): (Res<Time>, Res<super::VehicleTuning>),
) {
    let done = done.get().get_or_insert_with(Default::default);
    let placed = props.get().get_or_insert_with(|| super::placements(&anim_lib.dir, &data));
    for (e, id, pl, t) in &q {
        if !done.insert(e) {
            continue;
        }
        let Some(p) = placed.get(&id.0) else { continue };
        match super::class_of(p, &pl.template) {
            "v_rockettower" => {
                // slot 8 0x004510b0: the turret at the tower's "ref_turret" (its model's anim table: v_rockettower_0)
                let anim = data.templates.get(&pl.template).and_then(|t| t.default_anim.clone()).unwrap_or_else(|| pl.template.clone());
                let at = Nodes::from_anim(&anim_lib.dir, &anim).and_then(|n| n.get("ref_turret"));
                let Some(at) = at else {
                    warn!("rocket tower {}: no ref_turret in {anim}", id.0);
                    commands.entity(e).insert(RocketTower { turret: None });
                    continue;
                };
                let world = t.to_matrix() * at;
                let turret = spawn_turret(&mut commands, &data, &mut lib, &mut anim_lib, &mut binds, &assets, &mut meshes, &mut materials, &mut physics, world, (e, pl.collider), id.0, &tuning);
                commands.entity(e).insert(RocketTower { turret });
            }
            "rocket_bud_turret" => {
                let tp = p;
                commands.entity(e).insert((
                    RocketBud {
                        id: id.0,
                        home: t.translation,
                        range: tp.f("active_range", 500.0),
                        spread: tp.f("inaccuracy_range", 10.0),
                        blind: tp.f("blind_fire_duration", 10.0),
                        recycle: tp.f("recycle_time", 1.0) * tuning.fire_pause_scale,
                        projectile: tp.s("projectile_name").unwrap_or("").to_owned(),
                        emitter: tp.s("emitter_bone_name").unwrap_or("ref_nozzle_00").to_owned(),
                        muzzle_fx: tp.s("muzzle_fx_name").zip(tp.s("muzzle_bone_name")).map(|(a, b)| (a.to_owned(), b.to_owned())),
                        sight: 1.0,
                        seen: false,
                        seen_at: f32::NEG_INFINITY,
                        // its fire time starts at the load (0x0049c9b0): the first shot waits recycle_time
                        last_fire: time.elapsed_secs(),
                        shots: 0,
                    },
                    // Object_TakeDamage: every hit (the placed object's own record replaced)
                    super::unit_health(tp, DClass::Open, pl.husk.clone(), tuning.health_scale),
                ));
            }
            _ => {}
        }
    }
}

/// The factory call of 0x004510b0 for "v_rockettower_turret": its model (skinned: the turn anim drives "center"), its
/// collision model, its record (0x0049c320 / 0x00497d00) and the tower's send_trigger_on_death.
#[allow(clippy::too_many_arguments)]
fn spawn_turret(
    commands: &mut Commands,
    data: &crate::gamedata::GameData,
    lib: &mut crate::scene::Library,
    anim_lib: &mut crate::skin::AnimLibrary,
    binds: &mut Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
    physics: &mut crate::physics::Physics,
    world: Mat4,
    (tower, tower_body): (Entity, Option<oxijolt::prelude::BodyId>),
    id: u32,
    tuning: &super::VehicleTuning,
) -> Option<Entity> {
    let name = "v_rockettower_turret";
    let tpl = data.templates.get(name)?.clone();
    let p = data.template_props(name);
    let e = commands.spawn((Transform::from_matrix(world), Visibility::default(), Name::new(name))).id();
    // the model's parts hang off its "center" node (every part is bound to it), which the turn anim turns: a holder
    // entity stands for that node
    let holder = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
    commands.entity(e).add_child(holder);
    let model = tpl.model.clone()?;
    if let Some(m) = lib.load_model(&model, false, assets, meshes, materials) {
        let mut ctx = crate::level::SkinCtx { lib: anim_lib, binds };
        let _ = crate::level::attach_model(commands, holder, m, true, Some(&mut ctx), "ref_rhand");
        let bounds = crate::fx::Bounds { center: (m.min + m.max) * 0.5, radius: (m.max - m.min).length() * 0.5 };
        commands.entity(e).insert(bounds);
    }
    let turn = tpl.default_anim.clone().unwrap_or_default();
    let nodes = Nodes::from_anim(&anim_lib.dir, &turn).unwrap_or_default();
    let collider = tpl.collision.as_ref().and_then(|c| lib.load_model(c, false, assets, meshes, materials)).map(|m| (m.vertices.clone(), m.tris.clone())).and_then(|(v, t)| {
        let v: Vec<Vec3> = v.iter().map(|x| world.transform_point3(*x)).collect();
        physics.add_static_mesh(&v, &t, false)
    });
    if let Some(c) = collider {
        crate::level::collider_lists(physics, &p, c);
    }
    let health = super::unit_health(&p, DClass::Open, None, tuning.health_scale);
    let nozzle = |i: usize, d: &str| p.s(&format!("emitter_bone_name_{i}")).unwrap_or(d).to_owned();
    commands.entity(e).insert((
        Placed { template: name.to_owned(), collider, husk: None, triggers: Default::default() },
        crate::hud::AimClass::of(&p),
        health,
        RocketTowerTurret {
            tower,
            tower_body,
            id,
            home: world.w_axis.truncate(),
            range: p.f("active_range", 500.0),
            spin_max: p.f("spin_max_speed", 0.5),
            spin_accel: p.f("spin_accel", 0.5),
            spin_stop: p.f("spin_stop_accel", 0.8),
            blind: p.f("blind_fire_duration", 1.0),
            recycle: p.f("recycle_time", 1.0) * tuning.fire_pause_scale,
            projectile: p.s("projectile_name").unwrap_or("").to_owned(),
            nozzles: [nozzle(1, "ref_nozzle_00"), nozzle(2, "ref_nozzle_01"), nozzle(3, "ref_nozzle_02"), nozzle(4, "ref_nozzle_03")],
            angle: 0.0,
            spin: 0.0,
            in_range: false,
            sight: 1.0,
            seen: false,
            seen_at: f32::NEG_INFINITY,
            last_fire: 0.0,
            turning: false,
            volleys: 0,
            holder,
            turn,
            nodes,
        },
    ));
    Some(e)
}

/// The sight check of both turrets (Turret_SightHit, game_09.cpp): from 10 above the point to Roman's centre, nothing
/// in between but the shooter's own collision.
fn sees(physics: &crate::physics::Physics, blind: &super::SightBlind, from: Vec3, roman: Vec3, own: &[oxijolt::prelude::BodyId]) -> bool {
    super::sight(physics, blind, from + Vec3::Y * 5.0, roman, own)
}

/// RocketTowerTurret_Update 0x0049c6a0 and its fire 0x0049c610.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn tower_update(
    mut commands: Commands,
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    (physics, blind): (NonSend<crate::physics::Physics>, Res<super::SightBlind>),
    mut q: Query<(Entity, &mut RocketTowerTurret, &Placed, &GlobalTransform, Option<&Destructible>)>,
    mut holders: Query<&mut Transform, Without<crate::player::Roman>>,
    roman: Query<(Entity, &crate::player::Roman, &Transform)>,
    (mut fx, mut sounds, mut stop): (MessageWriter<crate::fx::StartFx>, MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSoundsOn>),
    (mut rng, mut log, mut lib): (ResMut<super::VehicleRng>, ResMut<super::VehicleLog>, ResMut<crate::skin::AnimLibrary>),
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Ok((re, r, rt)) = roman.single() else { return };
    let roman_at = rt.translation + Vec3::Y * r.walker.lift;
    for (e, mut tt, pl, gt, health) in &mut q {
        // dead (its record gone this frame; the husk swap removes it)
        let Some(health) = health else { continue };
        // the "center" node at the angle (the turn anim sampled at the fraction)
        let spin = lib.anim(&tt.turn).and_then(|a| a.rotation("center", tt.angle * a.duration)).unwrap_or(Quat::IDENTITY);
        // 0x00497ed0 (the muzzle fx step: the tower has no muzzle fx)
        if roman_at.distance_squared(tt.home) <= tt.range * tt.range {
            if !tt.in_range {
                tt.last_fire = 0.0;
            }
            tt.in_range = true;
            tt.spin -= dt * tt.spin_stop;
            if tt.spin < 0.0 {
                tt.spin = 0.0;
                if tt.turning {
                    tt.turning = false;
                    stop.write(crate::audio::StopSoundsOn(e));
                }
                // ready (0x0049c5b0)
                if tt.last_fire == 0.0 || now - tt.last_fire > tt.recycle {
                    tt.sight -= dt;
                    if tt.sight <= 0.0 {
                        tt.sight = 1.0;
                        let own: Vec<_> = pl.collider.into_iter().chain(tt.tower_body).collect();
                        tt.seen = sees(&physics, &blind, tt.home + Vec3::Y * 5.0, roman_at, &own);
                        if tt.seen {
                            tt.seen_at = now;
                        }
                    }
                    if tt.seen || now - tt.seen_at < tt.blind {
                        // 0x0049c610: a rocket from each nozzle, each homing on Roman's chest
                        for n in tt.nozzles.clone() {
                            let Some(node) = tt.nodes.get(&n) else {
                                warn!("rocket tower {}: no nozzle {n} in {}", tt.id, tt.turn);
                                continue;
                            };
                            let m = gt.to_matrix() * Mat4::from_quat(spin) * node;
                            let from = m.w_axis.truncate();
                            // the nozzle's facing (0x00453150: its -Y)
                            let along = m.transform_vector3(Vec3::NEG_Y).normalize_or(Vec3::Y);
                            rockets::launch(&mut commands, &mut fx, &data, &mut rng, Launch { template: &tt.projectile, from, to: roman_at, along: Some(along), target: Some((re, Some("chest"))), owner: Some(e), by: Src::Badguy });
                        }
                        tt.last_fire = now;
                        tt.volleys += 1;
                    }
                } else {
                    tt.sight = 0.0;
                }
            }
        } else {
            tt.in_range = false;
            tt.spin = (tt.spin + dt * tt.spin_accel).min(tt.spin_max);
            if !tt.turning {
                tt.turning = true;
                sounds.write(crate::audio::PlaySound::on("wpn_rocket_tower_turn_lp", e).looping(true));
            }
        }
        tt.angle += dt * tt.spin;
        while tt.angle > 1.0 {
            tt.angle -= 1.0;
        }
        if let Ok(mut h) = holders.get_mut(tt.holder) {
            h.rotation = spin;
        }
        log.0.insert(format!("{}t", tt.id), serde_json::json!({"kind": "rocket_tower_turret", "health": health.health, "spin": tt.spin, "in_range": tt.in_range, "seen": tt.seen, "volleys": tt.volleys}));
    }
}

/// RocketBudTurret_Update 0x0049cbe0 and its fire 0x0049cab0.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn bud_update(
    mut commands: Commands,
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    (physics, blind): (NonSend<crate::physics::Physics>, Res<super::SightBlind>),
    mut q: Query<(Entity, &mut RocketBud, &Placed, &GlobalTransform, Option<&Destructible>)>,
    roman: Query<(&crate::player::Roman, &Transform)>,
    (mut fx, mut rng, mut log, lib): (MessageWriter<crate::fx::StartFx>, ResMut<super::VehicleRng>, ResMut<super::VehicleLog>, Res<crate::skin::AnimLibrary>),
    mut nodes_cache: crate::levelreset::PerLevel<std::collections::HashMap<String, Nodes>>,
) {
    let now = time.elapsed_secs();
    let dt = time.delta_secs();
    let Ok((r, rt)) = roman.single() else { return };
    let roman_feet = rt.translation;
    let roman_at = roman_feet + Vec3::Y * r.walker.lift;
    let cache = nodes_cache.get();
    for (e, mut b, pl, gt, d) in &mut q {
        let Some(health) = d.map(|d| d.health) else { continue };
        log.0.insert(b.id.to_string(), serde_json::json!({"kind": "rocket_bud", "health": health, "shots": b.shots, "seen": b.seen}));
        if roman_at.distance_squared(b.home) > b.range * b.range {
            continue;
        }
        if !(b.last_fire == 0.0 || now - b.last_fire > b.recycle) {
            b.sight = 0.0;
            continue;
        }
        b.sight -= dt;
        if b.sight <= 0.0 {
            b.sight = 1.0;
            b.seen = sees(&physics, &blind, b.home + Vec3::Y * 5.0, roman_at, &pl.collider.into_iter().collect::<Vec<_>>());
            if b.seen {
                b.seen_at = now;
            }
        }
        if !(b.seen || now - b.seen_at < b.blind) {
            continue;
        }
        // 0x0049cab0: the emitter's matrix (the bud is rigid: its reference anim's node table)
        let nodes = cache.entry(pl.template.clone()).or_insert_with(|| {
            let anim = data.templates.get(&pl.template).and_then(|t| t.default_anim.clone()).unwrap_or_default();
            Nodes::from_anim(&lib.dir, &anim).unwrap_or_default()
        });
        let Some(m) = nodes.get(&b.emitter).map(|m| gt.to_matrix() * m) else { continue };
        let from = m.w_axis.truncate();
        // Roman's position spread in game x and y (engine x and -z) by rand in [-s, s]
        let s = b.spread;
        let (dx, dy) = (rng.range(-s, s), rng.range(-s, s));
        let aim = roman_feet + Vec3::new(dx, 0.0, -dy);
        rockets::launch(&mut commands, &mut fx, &data, &mut rng, Launch { template: &b.projectile, from, to: aim, along: None, target: None, owner: Some(e), by: Src::Badguy });
        if let Some((name, bone)) = &b.muzzle_fx {
            let at = nodes.get(bone).map(|m| (gt.to_matrix() * m).w_axis.truncate()).unwrap_or(from);
            let (_, rot, _) = gt.to_scale_rotation_translation();
            fx.write(crate::fx::StartFx { name: name.clone(), at, rotation: rot, follow: None, position_only: false });
        }
        b.last_fire = now;
        b.shots += 1;
    }
}

/// The deaths that take the other half with them: the turret's kills its tower (0x0049c540), the tower's its turret
/// (0x00451160); the removal stops the turn loop (0x0049c4c0, RocketTowerBuilding_Remove 0x00451070: the sounds on
/// the entity stop with it). A dead bud's log entry stays.
#[allow(clippy::type_complexity)]
fn deaths(
    mut died: MessageReader<Died>,
    turrets: Query<&RocketTowerTurret>,
    towers: Query<&RocketTower>,
    buds: Query<&RocketBud>,
    mut health: Query<&mut Destructible>,
    mut log: ResMut<super::VehicleLog>,
) {
    for d in died.read() {
        if let Ok(t) = turrets.get(d.0) {
            log.0.insert(format!("{}t", t.id), serde_json::json!({"kind": "rocket_tower_turret", "dead": true}));
            if let Ok(mut h) = health.get_mut(t.tower) {
                super::kill(&mut h);
            }
        }
        if let Ok(Some(t)) = towers.get(d.0).map(|t| t.turret) {
            if let Ok(mut h) = health.get_mut(t) {
                super::kill(&mut h);
            }
        }
        if let Ok(b) = buds.get(d.0) {
            log.0.insert(b.id.to_string(), serde_json::json!({"kind": "rocket_bud", "dead": true, "shots": b.shots}));
        }
    }
}
