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
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum Phase {
    /// running its script (or waiting for it)
    Scripted,
    /// standing, waiting for the timer or a target
    Stand,
    Pickup,
    Throw,
}

pub struct GoliathPlugin;

impl Plugin for GoliathPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (setup, guard, notes, log).chain().run_if(crate::playing));
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
        let k = physics.meters_per_unit;
        let (radius, half) = (14.0, 18.0);
        let lift = Vec3::Y * (half + radius);
        let Ok(shape) = Shape::new_capsule(half * k, radius * k) else { continue };
        let Some(body) = physics.add_dynamic(e, &shape, gt.translation() + lift, Quat::IDENTITY, Vec3::ZERO, BodySettings::new_dynamic().mass(2000.0)) else { continue };
        if let Some(m) = p.s("material") {
            physics.materials.insert(body, m.to_owned());
        }
        placed.collider = Some(body);
        // GenericAI damage: every shot counts (a placed object's default filter would take none)
        if let Some(mut d) = d {
            d.takes_damage = true;
            d.takes_bullet = true;
        }
        commands.entity(e).insert(Goliath { body, lift, phase: Phase::Scripted, timer: 0.0, held: None, range: p.f("scan_distance", 350.0), home: gt.translation() });
        info!("EVENT goliath: {e:?} ready");
    }
}

/// Its hit body follows it; after its script it guards: the fireball cycle at the player in range.
#[allow(clippy::too_many_arguments)]
fn guard(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Goliath, &GlobalTransform, &mut Transform, &mut crate::skin::Animator, Option<&crate::mover::Mover>, Option<&crate::fx::Destructible>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    player: Query<&GlobalTransform, (With<crate::player::Roman>, Without<Goliath>)>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let dt = time.delta_secs();
    let roman = player.single().ok().map(|p| p.translation());
    for (e, mut g, gt, mut tr, mut a, mover, d) in &mut q {
        let k = physics.meters_per_unit;
        if let Ok(mut b) = physics.world.body_mut(g.body) {
            let _ = b.set_position(crate::physics::jr((gt.translation() + g.lift) * k), Activation::Activate);
            let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
        }
        if d.is_none() {
            // dead (the husk takes its place)
            physics.remove(g.body);
            commands.entity(e).remove::<Goliath>();
            continue;
        }
        match g.phase {
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

fn log(q: Query<(&Goliath, &GlobalTransform, Option<&crate::fx::Destructible>)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let Some((g, gt, d)) = q.iter().next() else {
        state.0.remove("goliath");
        return;
    };
    let walked = (gt.translation() - g.home).with_y(0.0).length();
    state.0.insert("goliath".into(), serde_json::json!({ "phase": format!("{:?}", g.phase), "health": d.map(|d| d.health), "walked": walked }));
}
