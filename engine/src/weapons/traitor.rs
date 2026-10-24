//! The Guy Fawkes traitor bomb (special 6: player_guyfawkes_bomb, p_player_guyfawkes_bomb, behaviour proj_guyfawkes,
//! Projectile vtable 0x005a6608; the AI's turn 0x00430140).
//!
//! - Fire Special with it in hand queues the throw, no sooner than 10 s after the last (Game+0xae0, 0x004788a0's case
//!   6); at the throw note it leaves the hand (0x004a1970: one less) toward the crosshair's point at speed x 3 (300 a
//!   second, 0x0047dbf0 / Projectile_Launch 0x0047e400), fx_trail_sticky on it, lifetime 10 s.
//! - In flight it carries the start effect's AI noise (0x0047ee30); its end posts one at the point for 2 s (0x0047ed40).
//! - Its hit (0x00481d10): at the point, fx_guy_fawkes_bomb (the fireworks and their wpn_guy_fawkes_lp, 10 s) and the
//!   area damage (1, within 50 / 200) with record flag 0x10 (0x00481d00; objects and Roman take the 1, an AI character
//!   none: `DamageKind::Traitor`), the inner radius passed along: every bad guy
//!   (vt+0xe8) alive within 200 turns traitor (GenericAIObject_Slot52 0x00430340 -> 0x00430140): the disc fx_guy_fawkes
//!   over his head (bone ref_fawkes), his two lists swapped (+0x604 / +0x608: he now hunts his old side and is hunted
//!   by it: ai.rs `search`, player.rs's team test for bullets), his target dropped; then unless he is busy (vt+0x178
//!   0x0042f8d0), a guard looks for a new target at once (+0x490 = 0, 0x0042ec20) and holds his post, the others from
//!   outside the inner radius run to the nearest point of that circle round the bomb (state 0xfa). (+0x488 bit 14.)
//! - Every live bad guy the message reaches, a traitor already or not, stamps the time (Game+0xb2c); once the mission's
//!   guy_fawkes_duration (Game+0xb30, MissionObject_Load: default 10 s; no level sets it) has passed since the last
//!   stamp, every live traitor turns back (GenericAI_Update 0x0043ee90 -> 0x0042ecf0, every AI each frame): his lists
//!   swapped back, his target dropped and the player taken as his target (vt+0x23c(player, 1)).

use super::{SpecialId, ThrowSpecial};
use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

/// A grunt turned by the Guy Fawkes bomb (+0x488 bit 14): on the player's side now.
#[derive(Component, Debug, Default)]
pub struct Traitor;

/// When the last Guy Fawkes message reached a live bad guy (Game+0xb2c, game time): the traitors turn back
/// guy_fawkes_duration after it.
#[derive(Resource, Default, Debug)]
pub struct FawkesClock {
    last: Option<f32>,
}

/// The grunts a bomb's message reaches: who, his position and skeleton, and whether he is a traitor already or held
/// by the Topsy Turvy bomb or the glove (busy: states 0xe7..0xeb, 0x19a..0x19d).
type Receivers<'w, 's> = Query<'w, 's, (Entity, &'static mut crate::ai::Grunt, &'static GlobalTransform, Option<&'static crate::skin::Animator>, Has<Traitor>, Has<crate::topsy::Thrown>, Has<super::glove::Gloved>)>;

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
        app.init_level_resource::<FawkesClock>()
            .add_systems(Update, (throw.after(super::WeaponSet::Throws), fly, turn_back).chain().after(crate::player::PlayerSet::Weapons).run_if(crate::playing));
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
    (mut fx, mut stats, cheats, mut noise): (MessageWriter<crate::fx::StartFx>, ResMut<crate::player::PlayerStats>, Res<crate::cheats::Cheats>, ResMut<crate::ai::Interests>),
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
        // the start effect's AI noise, following the bomb (0x0047ee30 -> 0x00404810: radius 200; it needs
        // fx_start_bone_name too)
        if p.s("fx_start_bone_name").is_some() {
            noise.follow(e, from);
        }
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
    mut grunts: Receivers,
    roman: Query<Entity, With<crate::player::Roman>>,
    mut clock: ResMut<FawkesClock>,
    data: Res<crate::gamedata::GameData>,
    (mut fx, mut blasts): (MessageWriter<crate::fx::StartFx>, MessageWriter<crate::fx::Explode>),
    (mut log, mut stats, mut noise): (Option<ResMut<crate::mission::Mission>>, ResMut<super::WeaponStats>, ResMut<crate::ai::Interests>),
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
                // (its lifetime out: vt+0xd4 0x00433bb0, removed without its end effect)
                noise.forget(e);
                commands.entity(e).despawn();
            }
            continue;
        };
        let p = data.props("p_player_guyfawkes_bomb");
        if let Some(n) = p.s("fx_end") {
            fx.write(crate::fx::StartFx { name: n.to_owned(), at, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
        }
        // the end effect's noise at the point for 2 s (0x0047ed40 -> 0x00404810); the set has no fx_end_material
        noise.end_at(e, at);
        // the record carries flag 0x10 (0x00481d00): the AI characters in it turn (below) and take none of its damage
        let (inner, outer) = (p.f("damage_radius_inner", 0.0), p.f("damage_radius_outer", 0.0));
        blasts.write(crate::fx::Explode { at, area_damage: p.f("area_damage", 0.0), radius_inner: inner, radius_outer: outer, impact_force: p.f("impact_force", 2010.0), source: None, delay: 0.0, by: crate::fx::Src::Player, cartoon: false, kind: crate::player::DamageKind::Traitor });
        let (turned, reached) = turn(&mut commands, &mut grunts, &mut fx, at, inner, outer);
        if reached {
            clock.last = Some(time.elapsed_secs());
        }
        if let Some(m) = log.as_deref_mut() {
            m.log.push(format!("guy fawkes: {turned} turned traitor"));
        }
        stats.turned += turned;
        info!("EVENT guyfawkes: {turned} turned traitor");
        commands.entity(e).despawn();
    }
}

/// The 0x10 message to every bad guy within `outer` (AreaDamage_Apply's receivers; 0x00430140 with the inner radius):
/// the turn. Returns how many turned and whether it reached anyone alive (the time stamp Game+0xb2c).
fn turn(commands: &mut Commands, grunts: &mut Receivers, fx: &mut MessageWriter<crate::fx::StartFx>, at: Vec3, inner: f32, outer: f32) -> (u32, bool) {
    let (mut n, mut reached) = (0, false);
    for (ge, mut g, gt, a, traitor, thrown, gloved) in grunts.iter_mut() {
        let pos = gt.translation();
        // (the receivers by their bounding sphere's surface: 4.66 for a grunt, as the blasts')
        if g.mind == crate::ai::Mind::Dead || pos.distance(at) - super::targeting::SLACK > outer {
            continue;
        }
        reached = true;
        if traitor {
            continue;
        }
        n += 1;
        commands.entity(ge).insert(Traitor);
        // the disc over his head, on his ref_fawkes
        let bone = a.and_then(|a| a.skeleton.index.get("ref_fawkes").map(|&i| a.joints[i]));
        fx.write(crate::fx::StartFx { name: "fx_guy_fawkes".into(), at: pos + Vec3::Y * 10.0, rotation: Quat::IDENTITY, follow: Some(bone.unwrap_or(ge)), position_only: true });
        // (only the target +0x588: an alert-group hand-over under way, +0x578, goes on)
        g.target = None;
        if g.busy_now() || thrown || gloved {
            continue;
        }
        // a guard holds his post and looks for a target at once (+0x490 = 0, 0x0042ec20); the others outside the inner
        // circle run to its nearest point
        if g.guard {
            g.search_timer = 0.0;
            continue;
        }
        let across = (pos - at).with_y(0.0);
        if across.length() > inner {
            let to = at + across.normalize_or(Vec3::X) * inner;
            let speed = g.move_speed;
            crate::ai::wander_to(&mut g, pos, Vec3::new(to.x, pos.y, to.z), speed);
        }
    }
    (n, reached)
}

/// The traitors turn back (0x0042ecf0's start, every AI each frame from GenericAI_Update 0x0043ee90) once the mission's
/// guy_fawkes_duration has passed since the last message's stamp: the lists swapped back (the Traitor mark off, +0x488
/// bit 14), the target dropped and the player taken (vt+0x23c(player, 1)). The dead stay as they fell. (Its states
/// 0x12c..0x132, which skip the update, have no counterpart here.)
fn turn_back(
    mut commands: Commands,
    time: Res<Time>,
    clock: Res<FawkesClock>,
    mission: Option<Res<crate::mission::Mission>>,
    roman: Query<Entity, With<crate::player::Roman>>,
    mut traitors: Query<(Entity, &mut crate::ai::Grunt), With<Traitor>>,
) {
    let duration = mission.as_deref().map_or(10.0, |m| m.guy_fawkes_duration);
    let Some(last) = clock.last.filter(|&l| time.elapsed_secs() - l > duration) else { return };
    let player = roman.single().ok();
    let mut n = 0;
    for (e, mut g) in &mut traitors {
        if g.mind == crate::ai::Mind::Dead {
            continue;
        }
        commands.entity(e).remove::<Traitor>();
        g.target = player;
        n += 1;
    }
    if n > 0 {
        info!("EVENT guyfawkes: {n} turned back ({:.1} s after the last bomb)", time.elapsed_secs() - last);
    }
}
