//! The Guy Fawkes traitor bomb (special 6: player_guyfawkes_bomb, p_player_guyfawkes_bomb, behaviour proj_guyfawkes,
//! Projectile vtable 0x005a6608; the AI's turn 0x00430140).
//!
//! - Fire Special with it in hand queues the throw, no sooner than 10 s after the last (Game+0xae0, 0x004788a0's case
//!   6); at the throw note it leaves the hand (0x004a1970: one less) toward the crosshair's point at speed x 3 (300 a
//!   second, 0x0047dbf0 / Projectile_Launch 0x0047e400), fx_trail_sticky on it, lifetime 10 s.
//! - Its hit (0x00481d10): at the point, fx_guy_fawkes_bomb (the fireworks and their wpn_guy_fawkes_lp, 10 s) and the
//!   area damage (1, within 50 / 200) with record flag 0x10 (0x00481d00), the inner radius passed along: every bad guy
//!   (vt+0xe8) alive within 200 turns traitor (GenericAIObject_Slot52 0x00430340 -> 0x00430140): the disc fx_guy_fawkes
//!   over his head (bone ref_fawkes), his two lists swapped (+0x604 / +0x608: he now hunts his old side and is hunted
//!   by it: ai.rs `search`, player.rs's team test for bullets), his target dropped, and unless he is busy, from
//!   outside the inner radius he runs to the nearest point of that circle round the bomb (state 0xfa; a guard holds his
//!   post). Once a traitor, always (+0x488 bit 14).

use super::{SpecialId, ThrowSpecial};
use bevy::prelude::*;

/// A grunt turned by the Guy Fawkes bomb (+0x488 bit 14): on the player's side now.
#[derive(Component, Debug, Default)]
pub struct Traitor;

/// The thrown bomb (a Projectile of the 0x005a6608 class): flies at its target point until it hits something.
#[derive(Component, Debug)]
pub struct FawkesBomb {
    vel: Vec3,
    life: f32,
    owner: Entity,
}

pub struct TraitorPlugin;

impl Plugin for TraitorPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (throw.after(super::WeaponSet::Throws), fly).chain().after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

/// The bomb leaves the hand (0x004a1970 with 6).
#[allow(clippy::too_many_arguments)]
fn throw(
    mut commands: Commands,
    mut thrown: MessageReader<ThrowSpecial>,
    mut roman: Query<(Entity, &Transform, Option<&crate::skin::Animator>, &mut crate::player::Roman)>,
    joints: Query<&GlobalTransform>,
    xhair: Res<crate::hud::Crosshair>,
    cam: Query<&GlobalTransform, With<crate::camera::FollowCam>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    (mut fx, mut stats, cheats): (MessageWriter<crate::fx::StartFx>, ResMut<crate::player::PlayerStats>, Res<crate::cheats::Cheats>),
) {
    if !thrown.read().fold(false, |any, t| any || t.0 == SpecialId::GuyFawkes) {
        return;
    }
    let (Ok((re, rt, ra, mut r)), Ok(c)) = (roman.single_mut(), cam.single()) else { return };
    if r.arsenal.count(SpecialId::GuyFawkes) == 0 {
        return;
    }
    if !cheats.on(crate::cheats::INFINITE_AMMO) {
        r.arsenal.spend(SpecialId::GuyFawkes);
    }
    stats.shots += 1;
    let p = data.props("p_player_guyfawkes_bomb");
    let from = ra.and_then(|a| a.skeleton.index.get("l_hand").and_then(|&i| joints.get(a.joints[i]).ok())).map_or(rt.translation + Vec3::Y * 8.0, |j| j.translation());
    // the target point: the crosshair's (vt+0x164: the target list's point), else far along the view
    let to = xhair.point.unwrap_or(c.translation() + c.forward().as_vec3() * 500.0);
    let vel = (to - from).normalize_or(c.forward().as_vec3()) * p.f("speed", 10.0) * 3.0;
    let tr = Transform::from_translation(from).with_rotation(crate::vehicles::rockets::facing(vel));
    let model = data.templates.get("player_guyfawkes_bomb").and_then(|t| t.model.clone());
    let e = model.and_then(|m| kit.spawn(&mut commands, &m, tr, true)).unwrap_or_else(|| commands.spawn((tr, Visibility::default())).id());
    commands.entity(e).insert(FawkesBomb { vel, life: p.f("lifetime", 4.0), owner: re });
    if let Some(n) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: n.to_owned(), at: from, rotation: Quat::IDENTITY, follow: Some(e), position_only: true });
    }
    info!("EVENT guyfawkes: thrown");
}

/// The bomb's flight and hit (Projectile_Update 0x0047e580 -> 0x00481d10).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<crate::physics::Physics>,
    mut bombs: Query<(Entity, &mut FawkesBomb, &mut Transform)>,
    mut grunts: Query<(Entity, &mut crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>, Has<Traitor>)>,
    roman: Query<Entity, With<crate::player::Roman>>,
    data: Res<crate::gamedata::GameData>,
    (mut fx, mut blasts): (MessageWriter<crate::fx::StartFx>, MessageWriter<crate::fx::Explode>),
    (mut log, mut stats): (Option<ResMut<crate::mission::Mission>>, ResMut<super::WeaponStats>),
) {
    let dt = time.delta_secs();
    for (e, mut b, mut t) in &mut bombs {
        b.life -= dt;
        let step = b.vel * dt;
        let from = t.translation;
        // the first thing on its path but its thrower (characters by their capsules)
        let hit = physics.raycast(from, step.normalize_or(Vec3::NEG_Z), step.length().max(0.01)).filter(|h| physics.bodies.get(&h.0).copied() != Some(b.owner) && !roman.contains(physics.bodies.get(&h.0).copied().unwrap_or(Entity::PLACEHOLDER)));
        let Some((_, at, _)) = hit else {
            t.translation = from + step;
            if b.life <= 0.0 {
                commands.entity(e).despawn();
            }
            continue;
        };
        let p = data.props("p_player_guyfawkes_bomb");
        if let Some(n) = p.s("fx_end") {
            fx.write(crate::fx::StartFx { name: n.to_owned(), at, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
        }
        let (inner, outer) = (p.f("damage_radius_inner", 0.0), p.f("damage_radius_outer", 0.0));
        blasts.write(crate::fx::Explode { at, area_damage: p.f("area_damage", 0.0), radius_inner: inner, radius_outer: outer, impact_force: p.f("impact_force", 2010.0), source: None, delay: 0.0, by: crate::fx::Src::Player, cartoon: false });
        let turned = turn(&mut commands, &mut grunts, &mut fx, at, inner, outer);
        if let Some(m) = log.as_deref_mut() {
            m.log.push(format!("guy fawkes: {turned} turned traitor"));
        }
        stats.turned += turned;
        info!("EVENT guyfawkes: {turned} turned traitor");
        commands.entity(e).despawn();
    }
}

/// The 0x10 message to every bad guy within `outer` (AreaDamage_Apply's receivers; 0x00430140 with the inner radius):
/// the turn. Returns how many turned.
fn turn(commands: &mut Commands, grunts: &mut Query<(Entity, &mut crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>, Has<Traitor>)>, fx: &mut MessageWriter<crate::fx::StartFx>, at: Vec3, inner: f32, outer: f32) -> u32 {
    let mut n = 0;
    for (ge, mut g, gt, a, traitor) in grunts.iter_mut() {
        let pos = gt.translation();
        // (the receivers by their bounding sphere's surface: 4.66 for a grunt, as the blasts')
        if traitor || g.mind == crate::ai::Mind::Dead || pos.distance(at) - super::targeting::SLACK > outer {
            continue;
        }
        n += 1;
        commands.entity(ge).insert(Traitor);
        // the disc over his head, on his ref_fawkes
        let bone = a.and_then(|a| a.skeleton.index.get("ref_fawkes").map(|&i| a.joints[i]));
        fx.write(crate::fx::StartFx { name: "fx_guy_fawkes".into(), at: pos + Vec3::Y * 10.0, rotation: Quat::IDENTITY, follow: Some(bone.unwrap_or(ge)), position_only: true });
        g.target = None;
        g.pending = None;
        // a guard holds his post; the others outside the inner circle run to its nearest point
        let across = (pos - at).with_y(0.0);
        if !g.guard && across.length() > inner {
            let to = at + across.normalize_or(Vec3::X) * inner;
            let speed = g.move_speed;
            crate::ai::wander_to(&mut g, pos, Vec3::new(to.x, pos.y, to.z), speed);
        }
    }
    n
}
