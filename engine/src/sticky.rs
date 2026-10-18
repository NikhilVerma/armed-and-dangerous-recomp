//! The sticky bomb (StickyBombProjectile, vtable 0x005a83e0; game_07.cpp 892-1300), template player_sticky_bomb /
//! p_player_stickybomb. Fire Special queues the throw (0x004a1930: 0.4 s); the bomb leaves Roman's L_hand toward
//! the auto-aim point (Hud_UpdateCrosshair 0x00406f50: the first grunt inside the narrowed view, in range
//! sticky_bomb_range and in line of sight; else the camera ray at that range). Launch 0x004813b0: horizontal speed
//! = the horizontal distance (speed 0 in lev_1_m1: 1 s flight), the vertical speed solved for g = 64.348 to the
//! point lowered by 4 (0x00471a00), turned left by launch_angle_degrees. In flight no gravity; it homes
//! (0x00481520): k = (1 - min(d, R) / R) * 5 + 2, the velocity pulled toward unit(T - pos) * speed (as an impulse
//! once k >= 5, guess from 0x00404140). Toward a point the homing ends when the distance flown reaches R and
//! gravity applies. A hit sticks it (0x004817b0): on the ground at the hit - flight direction * 5, on an object at
//! the hit, held there; fx_stuck_*; the object and everything within 25 units are scared. After linger_lifetime it
//! explodes (0x00481ab0): area damage (excluding the stuck object), area_damage + damage to the stuck object,
//! fx_end. lifetime runs out in flight: removed without exploding.

use crate::physics::Physics;
use bevy::prelude::*;

#[derive(Resource)]
pub struct StickyAssets {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    /// Jonesy's dynamite (template molmer_dynamite, model molmer_dynamite_0)
    pub dynamite: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub base: Quat,
}

/// Jonesy's thrown dynamite (Fire_tnt note, 0x00428340 with "molmer_dynamite", attach 1): a StickyBombProjectile
/// of p_molmer_dynamite from his hand toward the target's ref_hit_00, homing on a random ref_hit_0N joint.
#[derive(Message)]
pub struct ThrowDynamite {
    pub from: Vec3,
    pub target: Entity,
    pub joint: Option<Entity>,
    pub point: Vec3,
}

#[derive(Component)]
pub struct StickyBomb {
    /// its property set (p_player_stickybomb, p_molmer_dynamite)
    props: &'static str,
    vel: Vec3,
    /// the target character and the joint the bomb homes on (ref_hit_0N)
    target: Option<(Entity, Entity)>,
    point: Vec3,
    speed: f32,
    range: f32,
    flown: f32,
    life: f32,
    gravity: bool,
    /// stuck: the object (or the ground), the offset in the object's frame, linger left
    stuck: Option<(Option<Entity>, Vec3, f32)>,
    /// the fuse +0x64c (0x00481b90): a blast lights it, a direct hit sets it to 0; at or below 0 it explodes
    pub fuse: Option<f32>,
}

impl StickyBomb {
    /// the object it is stuck to (not the ground)
    pub fn stuck_to(&self) -> Option<Entity> {
        self.stuck.and_then(|s| s.0)
    }
    pub fn is_stuck(&self) -> bool {
        self.stuck.is_some()
    }
    /// homing on a character's hit bone (the target the throw picked, still alive)
    pub fn is_homing(&self) -> bool {
        self.target.is_some()
    }
}

/// The bomb's collision radius (Object_BuildRadiusCollision 0x004527d0 from its model's bounds: not read; guess 1.5
/// for the small sticky_bomb model). Bullets (the list +0xe90) and blasts (+0xe94) reach it (0x00481320).
pub const BOMB_RADIUS: f32 = 1.5;

/// StickyBombProjectile's damage slot (0x00481b90): health 1 (max_health), so any damage takes it to 0; an area
/// damage record (flags bit 0, 0x0047f020) lights a random fuse rand() x 2.136e-5 (0 .. 0.7 s), anything else (a
/// bullet's direct hit) sets it off at once. This handles the blasts; the bullets set `fuse` in player.rs.
fn chain(mut blasts: MessageReader<crate::fx::Explode>, mut bombs: Query<(&Transform, &mut StickyBomb)>, mut rng: ResMut<crate::fx::FxStats>) {
    for x in blasts.read() {
        // AreaDamage_Apply 0x0047f020: nothing unless outer >= 0.01, outer >= inner and damage != 0; a receiver at a
        // surface distance d <= outer (its bounding sphere)
        if x.area_damage == 0.0 || x.radius_outer < 0.01 || x.radius_outer < x.radius_inner {
            continue;
        }
        for (t, mut b) in &mut bombs {
            if b.fuse.is_none() && t.translation.distance(x.at) - BOMB_RADIUS <= x.radius_outer {
                b.fuse = Some(blast_fuse(rng.rand()));
            }
        }
    }
}

/// The fuse a blast lights (0x00481b90): rand() x 2.136e-5 (0x005bcc4c), rand() in 0..32767; `u` in [0, 1).
pub fn blast_fuse(u: f32) -> f32 {
    (u * 32768.0).floor().min(32767.0) * 2.136e-5
}

pub struct StickyPlugin;

impl Plugin for StickyPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<ThrowDynamite>().add_systems(Update, (throw, throw_dynamite, chain, fly).chain().run_if(crate::playing));
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn throw(
    mut commands: Commands,
    mut thrown: MessageReader<crate::weapons::ThrowSpecial>,
    assets: Option<Res<StickyAssets>>,
    data: Res<crate::gamedata::GameData>,
    physics: NonSend<Physics>,
    mut roman: Query<(Entity, &Transform, &crate::skin::Animator, &mut crate::player::Roman)>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    proj: Query<&Projection, With<Camera3d>>,
    grunts: Query<(Entity, &crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut seed: Local<u32>,
    (mut stats, cheats): (ResMut<crate::player::PlayerStats>, Res<crate::cheats::Cheats>),
) {
    use crate::weapons::SpecialId;
    if !thrown.read().fold(false, |any, t| any || t.0 == SpecialId::StickyBomb) {
        return;
    }
    let (Some(assets), Ok((re, rt, ra, mut rm)), Ok(c)) = (assets, roman.single_mut(), cam.single()) else { return };
    if rm.arsenal.count(SpecialId::StickyBomb) == 0 {
        return;
    }
    // 0x004a1970: one less unless the infinite ammo cheat (Game+0x175)
    if !cheats.on(crate::cheats::INFINITE_AMMO) {
        rm.arsenal.spend(SpecialId::StickyBomb);
    }
    // the throw anim's note fires the queued bomb (0x00475ff0 state 5 -> 0x004a1970) and counts one more shot
    // (Game+0x2718, game_05.cpp:1592: the use already counted one, so a sticky bomb counts two)
    stats.shots += 1;
    let g = data.props("p_globalsettings");
    // its properties (StickyBombProjectile_ReadProps 0x00481110: linger_lifetime, fx_stuck_*, launch_angle_degrees
    // over Projectile_Load)
    let p = data.props("p_player_stickybomb");
    let range = g.f("sticky_bomb_range", 10.0);
    let factors = (g.f("autoaim_sticky_factor_horz", 0.4), g.f("autoaim_sticky_factor_vert", 0.6));
    let bone = |a: &crate::skin::Animator, n: &str| a.skeleton.index.get(n).and_then(|&i| joints.get(a.joints[i]).ok()).map(|t| t.translation());
    // the sticky target (Hud_UpdateCrosshair 0x00406f50's sticky pass, ui_01.cpp:455-505; weapons/targeting.rs): the
    // view volume narrowed by autoaim_sticky_factor_horz / vert in clip space from the CAMERA (0x00408390: |x_ndc| <=
    // 0.04, |y_ndc| <= 0.12 at the current FOV; aspect 1.7204), each candidate's model bounds centre with a slack R =
    // (half diagonal + smallest half extent) / 2 (grunt_1: centre ~4.9 up, R ~4.66); candidates the 21 nearest to the
    // eye (Roman + 4) by their origin, the living only (fn_00408190; targeting.rs search); the first out of sticky_bomb_range ends the
    // search; the line of sight from the eye to the target's ref_hit_00 (d + 10) is blocked only by the level and
    // props, not by other characters
    let eye = rt.translation + Vec3::Y * 4.0;
    let fwd = c.forward().as_vec3();
    let view = crate::weapons::targeting::View::new(c, proj.single().ok(), eye);
    let cands = crate::weapons::targeting::candidates(&grunts, &joints, |_, _, _| true);
    let target = crate::weapons::targeting::search(&view, cands, factors, range, 1, &physics, |e| e != re && grunts.contains(e)).first().map(|t| (t.entity, t.point, grunts.get(t.entity).ok().and_then(|g| g.3)));
    if std::env::var("AIDBG").is_ok() {
        info!("AIDBG sticky target {:?}", target.map(|t| t.0));
    }
    let from = bone(ra, "l_hand").unwrap_or(rt.translation + Vec3::Y * 8.0);
    let (point, homing) = match target {
        Some((e, centre, a)) => {
            // 0x004811e0: a random hit bone ref_hit_0N, N = rand() * 7 >> 15
            *seed = seed.wrapping_mul(214013).wrapping_add(2531011);
            let n = (((*seed >> 16) & 0x7fff) * 7) >> 15;
            let j = a.and_then(|a| a.skeleton.index.get(&format!("ref_hit_0{n}")).map(|&i| a.joints[i]));
            (centre, j.map(|j| (e, j)))
        }
        None => (c.translation() + fwd * 20.0 + fwd * range, None),
    };
    let (vel, speed) = launch(&p, from, point, fwd);
    let e = commands
        .spawn((
            Transform::from_translation(from).with_rotation(assets.base),
            Visibility::default(),
            // range: the distance to the target (0x0047e4d0 -> +0x568); the point without a target is the crosshair's
            // (vt+0x164 0x00481380: the target list's point)
            StickyBomb { props: "p_player_stickybomb", vel, target: homing, point, speed, range: from.distance(point), flown: 0.0, life: p.f("lifetime", 4.0), gravity: false, stuck: None, fuse: None },
        ))
        .id();
    for (m, mat) in &assets.parts {
        let c = commands.spawn((Mesh3d(m.clone()), MeshMaterial3d(mat.clone()))).id();
        commands.entity(e).add_child(c);
    }
    sounds.write(crate::audio::PlaySound::on("wpn_sticky_grenade_lp", e).looping(true));
    if let Some(n) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: n.to_owned(), at: from, rotation: Quat::IDENTITY, follow: Some(e), position_only: true });
    }
}

/// Launch (0x004813b0, 0x00471a00): speed = horizontal distance + speed x 3 (a 1 s flight at speed 0), the vertical
/// speed solved for g = 64.348 to the point lowered by 4, turned left (counter-clockwise from above) by
/// launch_angle_degrees.
fn launch(p: &crate::gamedata::Props, from: Vec3, point: Vec3, fwd: Vec3) -> (Vec3, f32) {
    let to = point - Vec3::Y * 4.0;
    let d = to - from;
    let h = Vec2::new(d.x, d.z).length();
    let speed = h + p.f("speed", 10.0) * 3.0;
    let vel = if h > 1e-3 && speed > 1e-3 {
        let t = h / speed;
        Vec3::new(d.x / h * speed, (d.y + 0.5 * 64.348 * t * t) / t, d.z / h * speed)
    } else {
        fwd * speed.max(1.0)
    };
    (Quat::from_rotation_y(p.f("launch_angle_degrees", 30.0).to_radians()) * vel, speed)
}

/// Jonesy's dynamite: no flight loop sound (0x004813b0 skips wpn_sticky_grenade_lp for a "dynamite" name), no trail.
fn throw_dynamite(mut commands: Commands, mut msgs: MessageReader<ThrowDynamite>, assets: Option<Res<StickyAssets>>, data: Res<crate::gamedata::GameData>) {
    let Some(assets) = assets else { return };
    let p = data.props("p_molmer_dynamite");
    for m in msgs.read() {
        let fwd = (m.point - m.from).with_y(0.0).normalize_or(Vec3::Z);
        let (vel, speed) = launch(&p, m.from, m.point, fwd);
        let e = commands
            .spawn((
                Transform::from_translation(m.from).with_rotation(assets.base),
                Visibility::default(),
                StickyBomb {
                    props: "p_molmer_dynamite",
                    vel,
                    target: m.joint.map(|j| (m.target, j)),
                    point: m.point,
                    speed,
                    range: m.from.distance(m.point),
                    flown: 0.0,
                    life: p.f("lifetime", 10.0),
                    gravity: false,
                    stuck: None,
                    fuse: None,
                },
            ))
            .id();
        for (mesh, mat) in &assets.dynamite {
            let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
            commands.entity(e).add_child(c);
        }
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    physics: NonSend<Physics>,
    mut bombs: Query<(Entity, &mut StickyBomb, &mut Transform)>,
    others: Query<&GlobalTransform, Without<StickyBomb>>,
    mut grunts: Query<(&mut crate::ai::Grunt, &mut crate::fx::Health)>,
    companions: Query<(), Or<(With<crate::ai::Companion>, With<crate::player::Roman>)>>,
    placed: Query<(Entity, &crate::level::Placed)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    mut blasts: MessageWriter<crate::fx::Explode>,
    mut scare: MessageWriter<crate::ai::StickyStuck>,
    mut interests: ResMut<crate::ai::Interests>,
    mut objects: Query<&mut crate::fx::Destructible>,
) {
    let dt = time.delta_secs();
    for (e, mut b, mut t) in &mut bombs {
        // Roman's bomb is a projectile to the grunts' ears (0x00404810 from its start effect fx_trail_sticky, following it
        // while it lives; from its end effect at the blast, 2 s: ai.rs Interests)
        let roman_bomb = b.props == "p_player_stickybomb";
        if roman_bomb {
            interests.follow(e, t.translation);
        }
        let p = data.props(b.props);
        // the fuse (0x00481520: counts down, at or below 0 StickyBombProjectile_Explode with no hit object: the stuck
        // one, if any)
        let mut boom = false;
        if let Some(f) = b.fuse.as_mut() {
            *f -= dt;
            boom = *f <= 0.0;
        }
        // stuck: held on the object, then the explosion (0x0047e9b0, 0x00481ab0)
        if let Some((obj, local, linger)) = b.stuck.as_mut() {
            if let Some(o) = obj.and_then(|o| others.get(o).ok()) {
                t.translation = o.transform_point(*local);
            }
            *linger -= dt;
            if *linger > 0.0 && !boom {
                continue;
            }
            boom = true;
        }
        if boom {
            let obj = b.stuck.and_then(|s| s.0);
            if b.stuck.is_none() && b.props == "p_player_stickybomb" {
                stop.write(crate::audio::StopSound("wpn_sticky_grenade_lp".into()));
            }
            let area = p.f("area_damage", 0.0);
            blasts.write(crate::fx::Explode {
                at: t.translation,
                area_damage: area,
                radius_inner: p.f("damage_radius_inner", 0.0),
                radius_outer: p.f("damage_radius_outer", 0.0),
                impact_force: p.f("impact_force", 2010.0),
                source: obj,
                delay: 0.0,
                // the thrower (0x0047e3a0): Roman's sticky bomb, or Jonesy's dynamite (a teammate)
                by: if b.props.contains("molmer") { crate::fx::Src::Teammate } else { crate::fx::Src::Player },
                // flags 0x80 | 1 (0x00481ab0)
                cartoon: true,
            });
            // the object it is stuck to takes the direct hit, damage + area_damage (0x0047eec0); the area damage above
            // spares it (0x0047f020 skips the object hit): a grunt, or a placed object through its damage filters (a
            // building stuck with a bomb: lev_2_m2's bullseye houses, the barracks)
            let by = if b.props.contains("molmer") { crate::fx::Src::Teammate } else { crate::fx::Src::Player };
            let direct = area + p.f("damage", 10.0);
            if let Some((mut g, mut h)) = obj.and_then(|o| grunts.get_mut(o).ok()) {
                g.last_hit = direct;
                g.cartoon = true;
                h.0 -= direct;
            } else if let Some(mut d) = obj.and_then(|o| objects.get_mut(o).ok()) {
                if d.accepts(by, false) && d.take_damage(direct) && d.dying.is_none() {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
            // the end effects (Projectile vt+0x158 0x0047ed40): fx_end at the blast, and fx_end_material too when it is
            // stuck to an object (Roman's bomb on a grunt: both, fx_imp_sticky twice, as the original). p_molmer_dynamite
            // has only fx_end_material: the original's blast (capture tntO, frame 1607, stuck on a grunt) plays
            // fx_imp_sticky's sounds (exp_fireball_med, exp_metal_small); on the ground the remaster keeps its surface's
            // variant (surface.rs). The material fx of a character is the base fx (no _grunt variant in the bank)
            let mut ends: Vec<String> = p.s("fx_end").map(str::to_owned).into_iter().collect();
            if obj.is_some() || ends.is_empty() {
                if let Some(m) = p.s("fx_end_material") {
                    let ground = if obj.is_some() { None } else { physics.surface_below(t.translation, 4.0) };
                    ends.push(ground.unwrap_or_default().variant(m, |n| data.fx.contains_key(n)));
                }
            }
            for n in ends {
                fx.write(crate::fx::StartFx { name: n, at: t.translation, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
            }
            if roman_bomb {
                interests.end_at(e, t.translation);
            }
            commands.entity(e).despawn();
            continue;
        }
        // the lifetime runs out in flight: removed (StickyBombProjectile_Remove 0x00481c20: the loop sound stopped)
        b.life -= dt;
        if b.life <= 0.0 {
            if b.props == "p_player_stickybomb" {
                stop.write(crate::audio::StopSound("wpn_sticky_grenade_lp".into()));
            }
            // removed without its end effect: no end noise
            interests.forget(e);
            commands.entity(e).despawn();
            continue;
        }
        let pos = t.translation;
        // homing toward the hit bone (the target alive) or the point
        if let Some((ch, _)) = b.target {
            if grunts.get(ch).map_or(true, |(g, _)| g.mind == crate::ai::Mind::Dead) {
                b.target = None;
            }
        }
        let goal = b.target.and_then(|(_, j)| others.get(j).ok()).map(|j| j.translation()).unwrap_or(b.point);
        if b.target.is_none() && b.flown >= b.range {
            b.gravity = true;
        } else if !b.gravity && b.range > 0.0 {
            let dist = goal.distance(pos);
            let k = (1.0 - dist.min(b.range) / b.range) * 5.0 + 2.0;
            let want = (goal - pos).normalize_or(b.vel.normalize_or(Vec3::Y)) * b.speed;
            if k >= 5.0 {
                b.vel = want;
            } else {
                let v = b.vel;
                b.vel = v + (want - v) * (1.0 - (-k * dt).exp());
            }
        }
        if b.gravity {
            b.vel.y -= 64.348 * dt;
        }
        let step = b.vel * dt;
        let len = step.length();
        if len <= 1e-6 {
            continue;
        }
        let dir = step / len;
        // the segment cast, through the ignore list (SetOwner 0x00481220: the owner, the bomb, Roman's companions and
        // Roman)
        let mut from = pos;
        let mut rest = len;
        let mut hit = None;
        // through the bodies projectiles pass (the projectile list, Projectile_Update 0x0047e580): invisible walls
        // (collide_barrier, blocks_projectiles 0) stop characters, not bombs — a long throw used to stick in mid-air
        while let Some((body, at, _)) = physics.raycast_bullet(from, dir, rest) {
            let owner = physics.bodies.get(&body).copied();
            if owner.is_some_and(|o| companions.get(o).is_ok()) {
                rest -= at.distance(from) + 0.05;
                from = at + dir * 0.05;
                if rest <= 0.0 {
                    break;
                }
                continue;
            }
            let obj = owner.or_else(|| placed.iter().find(|(_, pl)| pl.collider == Some(body)).map(|(pe, _)| pe));
            hit = Some((obj, at));
            break;
        }
        b.flown += len;
        let Some((obj, at)) = hit else {
            t.translation = pos + step;
            continue;
        };
        // stick (0x004817b0)
        if b.props == "p_player_stickybomb" {
            stop.write(crate::audio::StopSound("wpn_sticky_grenade_lp".into()));
        }
        t.translation = if obj.is_some() { at } else { at - dir * 5.0 };
        let local = obj.and_then(|o| others.get(o).ok()).map(|o| o.affine().inverse().transform_point3(t.translation)).unwrap_or(Vec3::ZERO);
        b.stuck = Some((obj, local, p.f("linger_lifetime", 5.0)));
        if std::env::var("AIDBG").is_ok() {
            info!("AIDBG sticky stuck to {:?} at {:?} target {:?} flown {:.1}", obj, t.translation, b.target.map(|x| x.0), b.flown);
        }
        let name = if obj.is_some() { p.s("fx_stuck_object") } else { p.s("fx_stuck_ground") };
        if let Some(n) = name {
            fx.write(crate::fx::StartFx { name: n.to_owned(), at: t.translation, rotation: crate::level::game_to_engine(), follow: Some(e), position_only: true });
        }
        scare.write(crate::ai::StickyStuck(t.translation, obj));
    }
}
