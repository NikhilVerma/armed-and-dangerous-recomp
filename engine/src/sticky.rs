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

use crate::levelreset::LevelAppExt;
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

/// A throw waiting for the weapon queue's delay.
#[derive(Resource, Default)]
pub struct StickyThrow {
    pub pending: Option<f32>,
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
        app.init_level_resource::<StickyThrow>()
            .add_message::<ThrowDynamite>()
            .add_systems(Update, (throw, throw_dynamite, chain, fly).chain().run_if(crate::playing));
    }
}

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn throw(
    mut commands: Commands,
    time: Res<Time>,
    mut pending: ResMut<StickyThrow>,
    assets: Option<Res<StickyAssets>>,
    data: Res<crate::gamedata::GameData>,
    physics: NonSend<Physics>,
    mut roman: Query<(&Transform, &crate::skin::Animator, &mut crate::player::Roman)>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    proj: Query<&Projection, With<Camera3d>>,
    grunts: Query<(Entity, &crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut seed: Local<u32>,
    mut stats: ResMut<crate::player::PlayerStats>,
) {
    let Some(left) = pending.pending.as_mut() else { return };
    *left -= time.delta_secs();
    if *left > 0.0 {
        return;
    }
    pending.pending = None;
    let (Some(assets), Ok((rt, ra, mut rm)), Ok(c)) = (assets, roman.single_mut(), cam.single()) else { return };
    if rm.stickies == 0 {
        return;
    }
    rm.stickies -= 1;
    // the throw anim's note fires the queued bomb (0x00475ff0 state 5 -> 0x004a1970) and counts one more shot
    // (Game+0x2718, game_05.cpp:1592: the use already counted one, so a sticky bomb counts two)
    stats.shots += 1;
    let g = data.props("p_globalsettings");
    // its properties (StickyBombProjectile_ReadProps 0x00481110: linger_lifetime, fx_stuck_*, launch_angle_degrees
    // over Projectile_Load)
    let p = data.props("p_player_stickybomb");
    let range = g.f("sticky_bomb_range", 10.0);
    let (fh, fv) = (g.f("autoaim_sticky_factor_horz", 0.4), g.f("autoaim_sticky_factor_vert", 0.6));
    let bone = |a: &crate::skin::Animator, n: &str| a.skeleton.index.get(n).and_then(|&i| joints.get(a.joints[i]).ok()).map(|t| t.translation());
    // auto-aim (0x00406f50 / 0x00408390): nearest candidates first (guess at the sort), the first inside the view
    // narrowed by the factors (guess: the factor scales the half-angle tangents; plus the bounds' radius), in front,
    // within range, and the line of sight hitting it first
    let eye = rt.translation + Vec3::Y * 4.0;
    let (fwd, right, up) = (c.forward().as_vec3(), c.right().as_vec3(), c.up().as_vec3());
    // the sticky target (Hud_UpdateCrosshair 0x00406f50's sticky pass, ui_01.cpp:455-505): the view volume narrowed
    // by autoaim_sticky_factor_horz / vert in clip space from the CAMERA (0x00408390: |x_ndc| <= 0.04, |y_ndc| <= 0.12 at
    // the current FOV; aspect 1.7204), each candidate's model bounds centre with a slack R = (half diagonal + smallest
    // half extent) / 2 (grunt_1: centre ~4.9 up, R ~4.66); candidates the 21 nearest to the eye (Roman + 4) by their
    // origin, dead ones counted (fn_00408190); the first out of sticky_bomb_range ends the search; the line of sight
    // from the eye to the target's ref_hit_00 (d + 10) is blocked only by the level and props, not by other characters
    let half_v = match proj.single() {
        Ok(Projection::Perspective(pp)) => (pp.fov * 0.5).tan(),
        _ => (0.654f32 * 0.5).tan(),
    };
    let half_h = half_v * 1.7204;
    let cam_pos = c.translation();
    let mut cands: Vec<(f32, Entity, bool, Vec3, Vec3, Option<&crate::skin::Animator>)> = grunts
        .iter()
        .map(|(e, g, t, a)| {
            let origin = t.translation();
            let hit = a.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(origin + Vec3::Y * 6.0);
            (origin.distance(eye), e, g.mind == crate::ai::Mind::Dead, origin + Vec3::Y * 4.9, hit, a)
        })
        .collect();
    cands.sort_by(|a, b| a.0.total_cmp(&b.0));
    let mut target = None;
    for (_, e, dead, centre, hit, a) in cands.into_iter().take(21) {
        if dead {
            continue;
        }
        // the view volume's side planes from the camera, the slack in world units (normalised planes)
        let v = centre - cam_pos;
        let z = v.dot(fwd);
        let r = 4.66;
        let (x, y) = (v.dot(right), v.dot(up));
        if x.abs() - half_h * fh * z > r * (1.0 + (half_h * fh).powi(2)).sqrt() || y.abs() - half_v * fv * z > r * (1.0 + (half_v * fv).powi(2)).sqrt() {
            continue;
        }
        let to = hit - eye;
        let dist = to.length();
        if to.dot(fwd) < 0.0 {
            continue;
        }
        // the range test 0x00408330 (weapon 10: the weapon table's range, sticky_bomb_range; a range of 0 fails)
        if dist > range {
            break;
        }
        // the ray: passes other characters, stops at the level / props; it must reach the candidate
        let dir = to / dist.max(1e-3);
        let (mut from, mut left, mut seen) = (eye, dist + 10.0, false);
        while let Some((b, at, _)) = physics.raycast(from, dir, left) {
            let owner = physics.bodies.get(&b).copied();
            if owner == Some(e) {
                seen = true;
                break;
            }
            if owner.is_some_and(|o| grunts.get(o).is_ok()) {
                let step = at.distance(from) + 0.05;
                from = at + dir * 0.05;
                left -= step;
                if left <= 0.0 {
                    break;
                }
                continue;
            }
            break;
        }
        if !seen {
            continue;
        }
        target = Some((e, hit, a));
        break;
    }
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
) {
    let dt = time.delta_secs();
    for (e, mut b, mut t) in &mut bombs {
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
            if let Some((mut g, mut h)) = obj.and_then(|o| grunts.get_mut(o).ok()) {
                let d = area + p.f("damage", 10.0);
                g.last_hit = d;
                g.cartoon = true;
                h.0 -= d;
            }
            // p_molmer_dynamite has no fx_end, only fx_end_material: the original's blast (capture tntO, frame 1607,
            // stuck on a grunt) plays fx_imp_sticky's sounds (exp_fireball_med, exp_metal_small) and no surface layer:
            // the material fx of what it is stuck to (a character: the base fx; the ground: _snow, as the bullets')
            let end = p.s("fx_end").map(str::to_owned).or_else(|| p.s("fx_end_material").map(|m| if obj.is_some() { m.to_owned() } else { format!("{m}_snow") }));
            if let Some(n) = end.as_deref() {
                fx.write(crate::fx::StartFx { name: n.to_owned(), at: t.translation, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
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
        while let Some((body, at, _)) = physics.raycast(from, dir, rest) {
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
