//! The goliath (GoliathObject, vtable 0x005af298, behaviour goliath; lev_3_m4_bd, lev_4_m5, lev_5_m5): a giant that
//! walks its script to the base's gate and pushes it open (mover_goliath_*: "anim_loop goliath_walk", then
//! "anim_full" with "trigger 8", the army_breach_open_trigger and the gate's activation), then stands guard throwing
//! fireballs at the player (GoliathObject_Update 0x00447040 state 0x79: pick up -> throw -> stand 1..3 s while a target
//! is in range; GoliathObject_HandleEvent 0x0043cef0: the throw anim's notes "create_fireball" (goliath_fireball held
//! at ref_fireball), "fire" (lobbed at the target, 0x00471a00), "turn"). It dies as an object (max_health 3000,
//! fx_death fx_explosion_goliath, husk goliath_d, send_trigger_on_death: 9 closes the gate and the breach).
//!
//! Its script starts through icobject.rs (mode trigger_ic); here: its hit volume (a body that follows it, the placed
//! collider staying behind otherwise), its damage filter (it takes the player's bullets), and the guard after the
//! script.
//!
//! Squad goliaths (lev_3_m1, lev_4_m1: squad_goliath_*): a squad makes one at its ref_goliath_00 (Squad_Init
//! 0x00433220 -> `SpawnGoliath`), with a goliath_cage holding a peasant_man / peasant_woman at its chest when the squad
//! has "goliath_cage". It joins the squad as a member (0x0042de60: goliath_stand, state 0xde) and walks with it once
//! the squad moves (state 0xdc: GenericAI_Update steering to its bone), never fighting (GoliathObject_Slot96 0x00434290
//! / Slot109 0x004342b0 refuse targets in a squad). It carries the squad's receive_trigger_kill (killed by it,
//! 0x0042f870), send_trigger_on_death and tag; its death makes the squad charge (ai/squad.rs).

use bevy::prelude::*;
use oxijolt::prelude::{Activation, BodySettings, Shape};

/// Seconds a held fireball takes to land (guess: the throw solver's speed, the fireball's vt+0x8c * K 0x005bcd64, is
/// not traced; a lob of 1.5 s).
const FIREBALL_FLIGHT: f32 = 1.5;

#[derive(Component)]
pub struct Goliath {
    /// the body bullets hit (the collision capsule round its middle) and its lift above the feet
    body: oxijolt::prelude::BodyId,
    lift: Vec3,
    /// the fireball cycle +0xb58 and its stand timer +0x494
    phase: Phase,
    timer: f32,
    /// the fireball held in its hand (+0xb68)
    held: Option<Entity>,
    range: f32,
    /// where it was placed (the state log's distance walked)
    home: Vec3,
    /// its health last frame (a hit's size: the damage slot 0x00434180) and hit_level_2 (+0x9ec, default 25)
    last_health: Option<f32>,
    hit_level: f32,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Phase {
    /// a squad's member (`SquadGoliath`): standing or following its bone
    InSquad,
    /// running its script (or waiting for it)
    Scripted,
    /// standing, waiting for the timer or a target
    Stand,
    Pickup,
    Throw,
    /// 0x66: a hit on its scripted walk in a base defence (goliath_hit_med, its walk held; GoliathObject_Update
    /// 0x00447040 walks on at the anim's end)
    Hurt,
}

/// A goliath for a squad (ai/squad.rs): made on `entity` (reserved by the squad, which keeps it as its +0xb7c).
#[derive(Message, Clone)]
pub struct SpawnGoliath {
    pub entity: Entity,
    /// its bone's point and the squad's heading (engine space)
    pub at: Vec3,
    pub yaw: f32,
    pub squad: Option<crate::ai::squad::SquadLink>,
    /// the squad's group (+0x510 = its -squad_tag, 0x0042de60): the goliath passes through its squad mates
    pub group: Option<u32>,
    /// a goliath_cage with a peasant at its chest
    pub cage: bool,
    /// the squad's receive_trigger_kill (+0x530), send_trigger_on_death (+0x528), tag (+0x390)
    pub kill_trigger: i64,
    pub death_trigger: i64,
    pub tag: i64,
}

/// A squad's goliath: its place in the squad and the triggers it took over.
#[derive(Component, Clone, Copy, Debug)]
pub struct SquadGoliath {
    pub link: crate::ai::squad::SquadLink,
    pub kill_trigger: i64,
    pub death_trigger: i64,
    pub tag: i64,
    /// walking to its bone (the walk anim) or standing
    pub walking: bool,
}

/// The goliath's collision model (its template's collision, goliath_coll: collision_lod_index 3) as bodies: one convex
/// hull per bone, hit-only (no_collide: bullets and sight rays meet them, nothing collides with them), posed on their
/// bones every frame, so its legs and hulls stop shots and Roman stands between its legs. The first is its main body
/// (`Placed::collider`); the rest are its parts (`Physics::main_body`). Each with the joint it rides.
#[derive(Component)]
struct Hulls {
    parts: Vec<(oxijolt::prelude::BodyId, Entity)>,
}

/// A cage to hang on the goliath's chest once its rig exists: the peasant inside is a woman (else a man).
#[derive(Component)]
struct CagePending {
    woman: bool,
}

pub struct GoliathPlugin;

impl Plugin for GoliathPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnGoliath>().add_systems(
            Update,
            (spawn_for_squads, setup, hulls, follow_squads, guard, hurt, cages, squad_triggers, notes, log).chain().run_if(crate::playing),
        );
    }
}

/// GenericAI damage: every shot and blast counts but its own side's (0x00430340: the source's and the victim's team,
/// vt+0xe4, agree: no damage), as the grunts' (player.rs projectiles) and the big units' (vehicles/boss.rs: the
/// source filter of `DClass::Building`). Oracle ticker_l3m1: the original's squad goliaths keep 3000 health with their
/// squads firing at Roman beside them.
fn badguy_filter(d: &mut crate::fx::Destructible) {
    d.class = crate::fx::DClass::Building;
    d.takes_damage = true;
    d.takes_bullet = true;
    d.player_damage = true;
    d.teammate_damage = true;
    d.badguy_damage = false;
}

/// The capsule bullets hit (round its middle) and its lift above the feet. p_goliath has no_collide 1: the original's
/// goliath pushes nothing (Roman stands between its legs, under its hulls), so the capsule is hit-only
/// (physics::HIT_ONLY: rays meet it, no body or walker collides with it), moved by `guard` each frame.
fn hit_body(physics: &mut crate::physics::Physics, e: Entity, feet: Vec3, material: Option<&str>) -> Option<(oxijolt::prelude::BodyId, Vec3)> {
    let k = physics.meters_per_unit;
    let (radius, half) = (14.0, 18.0);
    let lift = Vec3::Y * (half + radius);
    let shape = Shape::new_capsule(half * k, radius * k).ok()?;
    let settings = BodySettings::new_dynamic().mass(2000.0).gravity_factor(0.0).object_layer(crate::physics::HIT_ONLY);
    let body = physics.add_dynamic(e, &shape, feet + lift, Quat::IDENTITY, Vec3::ZERO, settings)?;
    if let Some(m) = material {
        physics.materials.insert(body, m.to_owned());
    }
    // a GenericAI (GoliathObject over GenericAIObject): in the AI list World+0xe7c the AI's sight passes through
    // (ai.rs sight_ray); bullets still hit it
    physics.ai_bodies.insert(body);
    Some((body, lift))
}

/// The squads' goliaths (Squad_Init 0x00433220: ObjectSystem_CreateObject "goliath" at ref_goliath_00, then
/// 0x0042de60 with the squad's kill / death triggers and tag; GoliathObject_Slot6 0x00433fe0: goliath_stand).
#[allow(clippy::too_many_arguments)]
fn spawn_for_squads(
    mut commands: Commands,
    mut events: MessageReader<SpawnGoliath>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut husk_rng: crate::levelreset::PerLevel<crate::fxvm::CrtRand>,
) {
    for ev in events.read() {
        let p = data.template_props("goliath");
        let Some(model) = data.templates.get("goliath").and_then(|t| t.model.clone()) else { continue };
        let world = Transform::from_translation(ev.at).with_rotation(Quat::from_rotation_y(ev.yaw) * crate::level::game_to_engine());
        commands.entity(ev.entity).insert((world, GlobalTransform::from(world), Visibility::default(), Name::new("goliath")));
        let Some(bounds) = kit.attach(&mut commands, ev.entity, &model, p.f("shadow_enable", 0.0) != 0.0) else { continue };
        let Some((body, lift)) = hit_body(&mut physics, ev.entity, ev.at, p.s("material")) else { continue };
        if ev.group.is_some() {
            physics.set_contact_rule(body, crate::contacts::ContactRule { group: ev.group, ..Default::default() });
        }
        let husk = crate::level::pick_husk(&p, husk_rng.get());
        let mut d = crate::level::destructible(&p).unwrap_or_else(|| crate::vehicles::unit_health(&p, crate::fx::DClass::Open, None, 1.0));
        badguy_filter(&mut d);
        // a squad's goliath ignores every damage message (0x00434180: +0x9bc set, not a base defence): only its squad's
        // kill trigger ends it (`squad_triggers`)
        if ev.squad.is_some() {
            d.takes_damage = false;
            d.takes_bullet = false;
        }
        commands.entity(ev.entity).insert((
            crate::level::Placed { template: "goliath".into(), collider: Some(body), husk, triggers: Default::default() },
            d,
            bounds,
            crate::hud::AimClass::of(&p),
            crate::level::DamageTaker,
            Goliath { body, lift, phase: Phase::InSquad, timer: 0.0, held: None, range: p.f("scan_distance", 350.0), home: ev.at, last_health: None, hit_level: p.f("hit_level_2", 25.0) },
        ));
        if let Some(link) = ev.squad {
            commands.entity(ev.entity).insert(SquadGoliath { link, kill_trigger: ev.kill_trigger, death_trigger: ev.death_trigger, tag: ev.tag, walking: false });
        }
        if ev.tag != 0 {
            commands.entity(ev.entity).insert(crate::level::ObjectTag(ev.tag));
        }
        if ev.cage {
            // the peasant: (rand() * 100) & ~0x7fff < 0x190000, i.e. rand() < 16384 (half): a man (Squad_Init)
            commands.entity(ev.entity).insert(CagePending { woman: rng.rand() >= 0.5 });
        }
        info!("EVENT goliath: {:?} made for a squad", ev.entity);
    }
}

/// The goliaths' hit bodies and damage filters, once each.
fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    mut q: Query<(Entity, &Name, &GlobalTransform, &mut crate::level::Placed, Option<&mut crate::fx::Destructible>), (With<crate::level::InstanceId>, Without<Goliath>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut seen: crate::levelreset::PerLevel<std::collections::HashSet<Entity>>,
) {
    let seen = seen.get();
    for (e, name, gt, mut placed, d) in &mut q {
        // once: a dead goliath keeps its entity as its husk
        if !seen.insert(e) {
            continue;
        }
        let p = data.template_props(name.as_str());
        if p.s("behavior") != Some("goliath") {
            continue;
        }
        // its placed collider stays where it was placed: a body that walks with it instead
        if let Some(c) = placed.collider.take() {
            physics.remove_static(c);
        }
        let Some((body, lift)) = hit_body(&mut physics, e, gt.translation(), p.s("material")) else { continue };
        placed.collider = Some(body);
        // (a placed object's default filter would take none)
        if let Some(mut d) = d {
            badguy_filter(&mut d);
        }
        commands.entity(e).insert(Goliath { body, lift, phase: Phase::Scripted, timer: 0.0, held: None, range: p.f("scan_distance", 350.0), home: gt.translation(), last_health: None, hit_level: p.f("hit_level_2", 25.0) });
        info!("EVENT goliath: {e:?} ready");
    }
}

/// The bone hulls (`Hulls`) of each goliath, once its rig has been posed: they take the place of the capsule.
#[allow(clippy::too_many_arguments)]
fn hulls(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    lib: Res<crate::skin::AnimLibrary>,
    mut q: Query<(Entity, &Name, &mut Goliath, &crate::skin::Animator, &mut crate::level::Placed), Without<Hulls>>,
    joints: Query<&GlobalTransform>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut models: Local<std::collections::HashMap<String, Option<std::sync::Arc<Vec<crate::scene::BonePart>>>>>,
) {
    for (e, name, mut g, a, mut placed) in &mut q {
        let template = data.templates.get(name.as_str());
        let Some(coll) = template.and_then(|t| t.collision.clone()) else { continue };
        let Some(parts) = models.entry(coll.clone()).or_insert_with(|| crate::scene::bone_parts(&lib.dir, &coll).map(std::sync::Arc::new)).clone() else { continue };
        // the rig is posed once its joints have left the origin (a frame after it was made)
        let joint = |bone: &str| a.skeleton.index.get(bone).and_then(|&i| a.joints.get(i)).and_then(|&j| Some((j, joints.get(j).ok()?)));
        if a.joints.first().and_then(|&j| joints.get(j).ok()).is_none_or(|t| *t == GlobalTransform::IDENTITY) {
            continue;
        }
        let k = physics.meters_per_unit;
        let material = data.template_props(name.as_str()).s("material").map(str::to_owned);
        let mut bodies = Vec::new();
        let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
        for part in parts.iter() {
            let Some((je, jt)) = joint(&part.bone) else { continue };
            let (_, rot, at) = jt.to_scale_rotation_translation();
            let m = jt.to_matrix();
            for p in &part.points {
                let w = m.transform_point3(*p);
                (lo, hi) = (lo.min(w), hi.max(w));
            }
            let points: Vec<_> = part.points.iter().map(|p| crate::physics::jv(rot.inverse() * (m.transform_point3(*p) - at) * k)).collect();
            let Ok(shape) = Shape::new_convex_hull(&points) else { continue };
            let settings = BodySettings::new_dynamic().mass(200.0).gravity_factor(0.0).object_layer(crate::physics::HIT_ONLY);
            let Some(body) = physics.add_dynamic(e, &shape, at, rot, Vec3::ZERO, settings) else { continue };
            physics.ai_bodies.insert(body);
            if let Some(m) = &material {
                physics.materials.insert(body, m.clone());
            }
            bodies.push((body, je));
        }
        let Some(&(main, _)) = bodies.first() else { continue };
        for &(b, _) in &bodies[1..] {
            physics.part_of.insert(b, main);
        }
        physics.remove(g.body);
        if placed.collider == Some(g.body) {
            placed.collider = Some(main);
        }
        g.body = main;
        let feet = joints.get(e).map(|t| t.translation()).unwrap_or_default();
        info!("EVENT goliath: {e:?} has {} bone hulls ({coll}), {:.0} wide, {:.0} .. {:.0} above its feet", bodies.len(), (hi - lo).with_y(0.0).length(), lo.y - feet.y, hi.y - feet.y);
        commands.entity(e).insert(Hulls { parts: bodies });
    }
}

/// The goliath's damage slot 0x00434180. Outside a base defence (0x0044e600: the level starts Roman in a turret) a squad's
/// goliath takes no damage at all (+0x9bc set: `badguy_filter` with `in_squad`) and the others take the AI's
/// (GenericAIObject_Slot52). In a base defence it takes the damage, and a hit above hit_level_2 on its scripted walk
/// (state 100), while the breach is shut (Game+0xcbe) and no full anim plays (+0xa18 bit 2: the gate push), makes it
/// say GOL50005_Hurt and play goliath_hit_med (0.25 blend), its walk held (state 0x66) until the anim ends; then
/// goliath_walk and the walk go on (GoliathObject_Update 0x00447040).
#[allow(clippy::type_complexity)]
fn hurt(
    mut q: Query<(Entity, &mut Goliath, &mut crate::skin::Animator, Option<&mut crate::mover::Mover>, &crate::fx::Destructible), Without<SquadGoliath>>,
    mission: Option<Res<crate::mission::Mission>>,
    lib: Res<crate::skin::AnimLibrary>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let Some(mission) = mission else { return };
    for (e, mut g, mut a, mover, d) in &mut q {
        let lost = g.last_health.map_or(0.0, |h| h - d.health);
        g.last_health = Some(d.health);
        let Some(mut mover) = mover else { continue };
        match g.phase {
            Phase::Scripted => {
                let full = a.layers.first().is_some_and(|l| l.anim == mover.anim);
                if mission.base_turret_mounted && !mission.breach_open && !full && !mover.ended && d.health > 0.0 && lost > g.hit_level {
                    sounds.write(crate::audio::PlaySound::on("GOL50005_Hurt", e));
                    let blend = std::mem::replace(&mut a.blend, 0.25);
                    a.play(0, "goliath_hit_med", false, crate::skin::Mask::All);
                    a.blend = blend;
                    mover.paused = true;
                    g.phase = Phase::Hurt;
                    info!("EVENT goliath: {e:?} hurt on its walk ({lost:.0})");
                }
            }
            Phase::Hurt => {
                if a.finished(0, &lib) {
                    a.play(0, "goliath_walk", true, crate::skin::Mask::All);
                    mover.paused = false;
                    g.phase = Phase::Scripted;
                    info!("EVENT goliath: {e:?} walks on");
                }
            }
            _ => {}
        }
    }
}

/// Its hit body follows it; after its script it guards: the fireball cycle at the player in range.
#[allow(clippy::too_many_arguments)]
fn guard(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Goliath, &GlobalTransform, &mut Transform, &mut crate::skin::Animator, Option<&crate::mover::Mover>, Option<&crate::fx::Destructible>, Option<&Hulls>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    player: Query<&GlobalTransform, (With<crate::player::Roman>, Without<Goliath>)>,
    joints: Query<&GlobalTransform, Without<Goliath>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let roman = player.single().ok().map(|p| p.translation());
    for (e, mut g, gt, mut tr, mut a, mover, d, hulls) in &mut q {
        let k = physics.meters_per_unit;
        // the hulls on their bones (as posed last frame), else the capsule at its feet
        let poses: Vec<_> = match hulls {
            Some(h) => h.parts.iter().filter_map(|&(b, j)| joints.get(j).ok().map(|t| (b, t.translation(), t.rotation()))).collect(),
            None => vec![(g.body, gt.translation() + g.lift, Quat::IDENTITY)],
        };
        for (body, at, rot) in poses {
            if let Ok(mut b) = physics.world.body_mut(body) {
                let _ = b.set_position_and_rotation(crate::physics::jr(at * k), crate::physics::jq(rot), Activation::Activate);
                let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
            }
        }
        if d.is_none() {
            // dead (the husk takes its place); its death line where it fell (Character_Die's DeadAtPosition, the goliath's
            // slot 0x00439f30: "GOL50001_Dead" at its position, not kept)
            sounds.write(crate::audio::PlaySound::at("GOL50001_Dead", gt.translation()));
            info!("EVENT voice: goliath {e:?} says GOL50001_Dead");
            match hulls {
                Some(h) => h.parts.iter().for_each(|&(b, _)| physics.remove(b)),
                None => physics.remove(g.body),
            }
            commands.entity(e).remove::<(Goliath, Hulls)>();
            continue;
        }
        match g.phase {
            // (the hit reaction is `hurt`'s)
            Phase::InSquad | Phase::Hurt => {}
            Phase::Scripted => {
                if mover.is_some_and(|m| m.ended) {
                    commands.entity(e).remove::<crate::mover::Mover>();
                    a.play(0, "goliath_stand", true, crate::skin::Mask::All);
                    g.phase = Phase::Stand;
                    g.timer = 0.0;
                    info!("EVENT goliath: {e:?} stands guard");
                }
            }
            Phase::Stand => {
                g.timer -= dt;
                let target = roman.filter(|r| (*r - gt.translation()).with_y(0.0).length() <= g.range);
                if g.timer <= 0.0 {
                    if let Some(r) = target {
                        // turned to the target (0x00454e60)
                        let to = (r - gt.translation()).with_y(0.0);
                        if to.length() > 1.0 {
                            let yaw = to.x.atan2(to.z);
                            tr.rotation = Quat::from_rotation_y(yaw) * crate::level::game_to_engine();
                        }
                        a.play(0, "goliath_fireball_pickup", false, crate::skin::Mask::All);
                        g.phase = Phase::Pickup;
                    }
                }
            }
            Phase::Pickup | Phase::Throw => {
                let anim = if g.phase == Phase::Pickup { "goliath_fireball_pickup" } else { "goliath_fireball_throw" };
                if a.layers.first().is_some_and(|l| l.anim == anim && l.time >= l.duration - 1e-3 && l.duration > 0.0) {
                    if g.phase == Phase::Pickup {
                        a.play(0, "goliath_fireball_throw", false, crate::skin::Mask::All);
                        g.phase = Phase::Throw;
                    } else {
                        a.play(0, "goliath_stand", true, crate::skin::Mask::All);
                        g.phase = Phase::Stand;
                        // 0x0046e2c0(1, 3)
                        g.timer = rng.range(1.0, 3.0);
                    }
                }
            }
        }
    }
}

/// The throw anim's notes: "create_fireball" (one held at ref_fireball), "fire" (lobbed at the player).
#[allow(clippy::too_many_arguments)]
fn notes(
    mut commands: Commands,
    mut notes: MessageReader<crate::skin::AnimNote>,
    mut q: Query<(&mut Goliath, &GlobalTransform, &crate::skin::Animator)>,
    joints: Query<&GlobalTransform, Without<Goliath>>,
    player: Query<&GlobalTransform, (With<crate::player::Roman>, Without<Goliath>)>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    for n in notes.read() {
        let Ok((mut g, gt, a)) = q.get_mut(n.entity) else { continue };
        let hand = a.skeleton.index.get("ref_fireball").and_then(|&i| a.joints.get(i).copied());
        match n.text.to_lowercase().as_str() {
            "create_fireball" if g.held.is_none() => {
                let model = data.templates.get("goliath_fireball").and_then(|t| t.model.clone());
                if let (Some(h), Some(m)) = (hand, model) {
                    if let Some(f) = kit.spawn(&mut commands, &m, Transform::IDENTITY, true) {
                        commands.entity(h).add_child(f);
                        g.held = Some(f);
                    }
                }
            }
            "fire" => {
                if let Some(f) = g.held.take() {
                    commands.entity(f).despawn();
                }
                let from = hand.and_then(|h| joints.get(h).ok()).map(|j| j.translation()).unwrap_or(gt.translation() + Vec3::Y * 40.0);
                // at the target, else ahead (own heading * range)
                let target = player.single().map(|p| p.translation()).unwrap_or(from + gt.forward() * g.range);
                crate::shell::launch(&mut commands, &data, &mut kit, &mut fx, "goliath_fireball", from, target, crate::shell::Lob { by: crate::fx::Src::Badguy, flight: Some(FIREBALL_FLIGHT) });
                info!("EVENT goliath: fireball thrown");
            }
            _ => {}
        }
    }
}

/// The first goliath in the playtest state log: its phase, health, how far it walked, its feet (game space) and
/// Roman's horizontal distance from them (G1: he stands between its legs without being pushed out).
fn log(
    q: Query<(&Goliath, &GlobalTransform, Option<&crate::fx::Destructible>)>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mut state) = state else { return };
    let Some((g, gt, d)) = q.iter().next() else {
        state.0.remove("goliath");
        return;
    };
    let p = gt.translation();
    let walked = (p - g.home).with_y(0.0).length();
    let roman = roman.single().ok().map(|r| r.translation() - p);
    state.0.insert("goliath".into(), serde_json::json!({
        "phase": format!("{:?}", g.phase), "health": d.map(|d| d.health), "walked": walked,
        "pos": [p.x, -p.z, p.y], "roman_dist": roman.map(|r| r.with_y(0.0).length()), "roman_dz": roman.map(|r| r.y),
    }));
}

/// A squad goliath's walk (member states 0xde / 0xdc: GenericAI_Update ai_06.cpp:3062-3089 steering to its bone, at
/// most twice its move_speed, facing the way it goes): goliath_walk while it moves, goliath_stand once there.
fn follow_squads(
    time: Res<Time>,
    squads: Query<&crate::ai::squad::Squad>,
    mut q: Query<(&mut SquadGoliath, &mut Transform, Option<&mut crate::skin::Animator>), With<Goliath>>,
    data: Res<crate::gamedata::GameData>,
) {
    let dt = time.delta_secs();
    // (p_goliath has no move_speed: the AI default, as the grunts' 46: guess)
    let speed = data.template_props("goliath").f("move_speed", 46.0);
    for (mut sg, mut tr, anim) in &mut q {
        let Ok(sq) = squads.get(sg.link.squad) else { continue };
        let (goal, max) = crate::ai::squad::member_goal(sq, sg.link, tr.translation, speed);
        let flat = (goal - tr.translation).with_y(0.0);
        let walking = sq.phase == crate::ai::squad::SquadPhase::Moving && flat.length() > 1.0;
        if walking {
            tr.translation += flat.normalize_or_zero() * (max * dt).min(flat.length());
            tr.translation.y = goal.y;
            tr.rotation = Quat::from_rotation_y(flat.x.atan2(flat.z)) * crate::level::game_to_engine();
        }
        let Some(mut a) = anim else { continue };
        if sg.walking != walking || a.layers.first().is_none_or(|l| l.anim.is_empty()) {
            a.play(0, if walking { "goliath_walk" } else { "goliath_stand" }, true, crate::skin::Mask::All);
            sg.walking = walking;
        }
    }
}

/// The cage on a squad goliath's chest (Object_Attach 0x004539f0: the cage's ref_chest at the goliath's "chest"), the
/// peasant in it at ref_peasant_1 by its "center", playing peasant_man_stand (Squad_Init 0x00433220). The peasant is a
/// PeasantObject held by the cage (peasant.rs `Peasant::held_by`: no pick-up while attached); when the goliath dies the
/// cage goes and he falls free (0x00453750: the cage without flag 4 is removed, the peasant with it is detached).
fn cages(mut commands: Commands, q: Query<(Entity, &CagePending, &crate::skin::Animator, Option<&SquadGoliath>)>, ids: Query<&crate::level::InstanceId>, mut kit: crate::army::ModelKit, data: Res<crate::gamedata::GameData>) {
    for (e, c, a, sg) in &q {
        // the squad's entry orders its peasant on the object list
        let id = sg.and_then(|s| ids.get(s.link.squad).ok()).map_or(0, |i| i.0);
        commands.entity(e).remove::<CagePending>();
        let Some(chest) = a.skeleton.index.get("chest").and_then(|&i| a.joints.get(i).copied()) else { continue };
        let dir = kit.dir();
        let node = |anim: &str, n: &str| crate::mission::pub_bone(&dir, anim, n).unwrap_or(Vec3::ZERO);
        let Some(cage_model) = data.templates.get("goliath_cage").and_then(|t| t.model.clone()) else { continue };
        let Some(cage) = kit.spawn(&mut commands, &cage_model, Transform::from_translation(-node("goliath_cage_ref", "ref_chest")), true) else { continue };
        commands.entity(chest).add_child(cage);
        let template = if c.woman { "peasant_woman" } else { "peasant_man" };
        let Some(pm) = data.templates.get(template).and_then(|t| t.model.clone()) else { continue };
        let at = node("goliath_cage_ref", "ref_peasant_1") - node("peasant_ref", "center");
        if let Some(peasant) = kit.spawn(&mut commands, &pm, Transform::from_translation(at), true) {
            // made after the placed objects (the squad's load), in the squads' order
            let props = data.template_props(template);
            let mut p = crate::peasant::Peasant::from_props(&props, template, crate::peasant::CAGED_ORDER + id);
            p.held_by = Some(e);
            commands.entity(peasant).insert((crate::level::DefaultAnim::new("peasant_man_stand"), p.health(&props), crate::fx::BlastProof, p, Name::new(template)));
            commands.entity(cage).add_child(peasant);
        }
    }
}

/// A squad goliath's handed-over triggers: the squad's receive_trigger_kill kills it (fn_0042f870: vt+0xd4), its
/// send_trigger_on_death goes out when it dies (DefaultGameObject_OnDeath 0x00450ee0 sends +0x528).
fn squad_triggers(
    mut commands: Commands,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut died: MessageReader<crate::fx::Died>,
    mut q: Query<(Entity, &SquadGoliath, Option<&mut crate::fx::Destructible>)>,
) {
    let got: Vec<i64> = triggers.read().map(|t| t.0).collect();
    for (e, sg, d) in &mut q {
        if let (Some(mut d), true) = (d, sg.kill_trigger != 0 && got.contains(&sg.kill_trigger)) {
            info!("EVENT goliath: {e:?} killed by trigger {}", sg.kill_trigger);
            crate::vehicles::kill(&mut d);
        }
    }
    for crate::fx::Died(e) in died.read() {
        if let Ok((_, sg, _)) = q.get(*e) {
            if sg.death_trigger != 0 {
                // (through the commands: this system reads the same messages)
                commands.write_message(crate::mission::SendTrigger(sg.death_trigger));
            }
        }
    }
}
