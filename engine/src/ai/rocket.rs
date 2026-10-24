//! The rocket captain's rocket (template captain_rocket, behaviour proj_captain_rocket: CaptainRocket, vtable 0x005a11d8):
//! its flight is vehicles/rockets.rs's (CaptainRocket_ReadProps 0x004829e0 / CaptainRocket_Update 0x00482b40: dumb_fire,
//! straight at max_speed x 3; the hit, the area damage, fx_end_material); this module adds what makes it the captain's:
//!
//! - [`fire`]: the captain's shot (the fire cycle 0x00428980 -> 0x004284e0 with its aim error -> the weapon
//!   captain_rocketlauncher, projectile captain_rocket) launches one from the weapon's emitter at the aim point.
//! - The owner (CaptainRocket_SetOwner 0x00482ab0, slot 0x52): +0x504 = the captain, and an ignore list of the owner and
//!   the rocket itself (+0x570): its path passes through its own captain (rockets.rs skips the owner).
//! - Its collision (CaptainRocket_InitCollision 0x00482d10, slot 7): a collision description 0x100 and the lists
//!   World+0xe90 (what projectiles hit) and +0xe94 (what area damage reaches): it can be shot down.
//! - Its damage (CaptainRocket_Damage 0x00482d60, slot 0x34): while its health (+0x454, max_health 1) is above 0; a hit
//!   from a source on its own side (vt+0xe4 equal: a bad guy's) other than its owner is ignored unless the source is an
//!   object (vt+0xe0: a keg, a prop); the damage taken; at 0 or below a hit record at its own position goes to its hit
//!   (vt+0x138): it explodes where it is (the area damage, fx_end_material).
//!
//! Guess: the shootable volume, a sphere of [`HULL_RADIUS`] round the rocket (the template has no collision model; the
//! description 0x100's shape is not traced).

use crate::fx::{Explode, Src};
use crate::vehicles::rockets::Rocket;
use bevy::prelude::*;

/// The shootable sphere round a captain's rocket (guess: the captain_rocket_0 model is about 4 long).
pub const HULL_RADIUS: f32 = 2.0;

/// A captain's rocket's own health (CaptainRocket_Damage 0x00482d60): +0x454 / +0x458 from max_health (1).
#[derive(Component, Clone, Copy, Debug)]
pub struct RocketHull {
    pub health: f32,
    pub max: f32,
}

/// Rockets shot down (playtest log `captain_rocket`).
#[derive(Resource, Default, Debug)]
pub struct RocketHullStats {
    pub fired: u32,
    pub shot_down: u32,
}

/// The captain's shot: a captain_rocket from `from` at `aim` (the fire cycle's aim point), owned by the captain.
pub fn fire(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, data: &crate::gamedata::GameData, rng: &mut crate::fx::FxStats, from: Vec3, aim: Vec3, owner: Entity) {
    let max = data.template_props("captain_rocket").f("max_health", 1.0);
    let mut vrng = crate::vehicles::VehicleRng(crate::fxvm::CrtRand::seeded((rng.rand() * 32767.0) as u32));
    let launch = crate::vehicles::rockets::Launch { template: "captain_rocket", from, to: aim, along: None, target: None, owner: Some(owner), by: Src::Badguy };
    if let Some(e) = crate::vehicles::rockets::launch(commands, fx, data, &mut vrng, launch) {
        commands.entity(e).insert(RocketHull { health: max, max });
        commands.queue(|w: &mut World| {
            if let Some(mut s) = w.get_resource_mut::<RocketHullStats>() {
                s.fired += 1;
            }
        });
    }
}

/// Does a hit from this source count (CaptainRocket_Damage's side test): the owner's always; a bad guy's (the rocket's
/// side) never; the player's, a teammate's and an object's always.
pub fn takes_hit_from(by: Src, source: Option<Entity>, owner: Option<Entity>) -> bool {
    source.is_some() && source == owner || by != Src::Badguy
}

/// Where a segment first passes within `r` of `c` (the shot's swept step against the rocket's sphere), if it does.
pub fn segment_hits(from: Vec3, to: Vec3, c: Vec3, r: f32) -> bool {
    let seg = to - from;
    let u = ((c - from).dot(seg) / seg.length_squared().max(1e-9)).clamp(0.0, 1.0);
    (from + seg * u).distance(c) <= r
}

/// The damage of a blast at distance `d` (AreaDamage_Apply 0x0047f020's falloff: whole inside the inner radius, falling
/// to 0 at the outer).
fn blast_damage(e: &Explode, d: f32) -> f32 {
    if d <= e.radius_inner {
        e.area_damage
    } else if d >= e.radius_outer {
        0.0
    } else {
        e.area_damage * (1.0 - (d - e.radius_inner) / (e.radius_outer - e.radius_inner).max(1e-3))
    }
}

/// Shots and blasts on the captains' rockets; a rocket whose health runs out explodes where it is.
#[allow(clippy::too_many_arguments)]
pub fn shoot_down(
    mut commands: Commands,
    time: Res<Time>,
    mut rockets: Query<(Entity, &Rocket, &mut RocketHull, &Transform)>,
    shots: Query<(Entity, &crate::player::Projectile, &Transform), Without<Rocket>>,
    companions: Query<(), With<super::Companion>>,
    mut blasts: ParamSet<(MessageReader<Explode>, MessageWriter<Explode>)>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut stats: ResMut<RocketHullStats>,
) {
    let dt = time.delta_secs();
    let incoming: Vec<Explode> = blasts.p0().read().cloned().collect();
    let mut gone: Vec<Entity> = Vec::new();
    let mut out: Vec<Explode> = Vec::new();
    for (e, r, mut hull, t) in &mut rockets {
        let c = t.translation;
        // the bullets in flight (World+0xe90): the player's and his teammates' (a bad guy's are on the rocket's side)
        for (se, p, st) in &shots {
            if gone.contains(&se) {
                continue;
            }
            let by = if p.from_player {
                Src::Player
            } else if p.owner.is_some_and(|o| companions.contains(o)) {
                Src::Teammate
            } else {
                Src::Badguy
            };
            if !takes_hit_from(by, p.owner, r.owner) {
                continue;
            }
            let step = p.vel * dt;
            if segment_hits(st.translation - step, st.translation + step, c, HULL_RADIUS) {
                hull.health -= p.damage;
                gone.push(se);
                commands.entity(se).despawn();
            }
        }
        // the area damage (World+0xe94)
        for b in &incoming {
            if b.source == Some(e) || !takes_hit_from(b.by, b.source, r.owner) {
                continue;
            }
            hull.health -= blast_damage(b, b.at.distance(c));
        }
        hull.health = hull.health.min(hull.max);
        if hull.health > 0.0 {
            continue;
        }
        // the hit record at its own position: the rocket's hit (vt+0x138), here its blast and end effects
        stats.shot_down += 1;
        info!("EVENT ai: a captain's rocket was shot down");
        if r.area != 0.0 {
            out.push(Explode { at: c, area_damage: r.area, radius_inner: r.inner, radius_outer: r.outer, impact_force: r.impact, source: Some(e), delay: 0.0, by: r.by, cartoon: false, kind: crate::player::DamageKind::Plain });
        }
        for n in [&r.fx_end, &r.fx_end_material].into_iter().flatten() {
            fx.write(crate::fx::StartFx { name: n.clone(), at: c, rotation: Quat::IDENTITY, follow: None, position_only: false });
        }
        commands.entity(e).despawn();
    }
    let mut w = blasts.p1();
    for b in out {
        w.write(b);
    }
}

/// The playtest state's `captain_rocket`.
pub fn log(stats: Res<RocketHullStats>, live: Query<(), With<RocketHull>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    // (ended: hit something, shot down or out of lifetime)
    let ended = stats.fired.saturating_sub(live.iter().count() as u32);
    state.0.insert("captain_rocket".into(), serde_json::json!({"fired": stats.fired, "ended": ended, "shot_down": stats.shot_down}));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sides() {
        let (a, b) = (Entity::from_raw_u32(1).unwrap(), Entity::from_raw_u32(2).unwrap());
        // a bad guy's shot or blast: only the owner's own counts
        assert!(!takes_hit_from(Src::Badguy, Some(b), Some(a)));
        assert!(takes_hit_from(Src::Badguy, Some(a), Some(a)));
        assert!(takes_hit_from(Src::Player, Some(b), Some(a)));
        assert!(takes_hit_from(Src::Object, None, Some(a)));
        assert!(takes_hit_from(Src::Teammate, Some(b), Some(a)));
    }

    #[test]
    fn swept_hit() {
        let c = Vec3::new(10.0, 0.0, 0.0);
        assert!(segment_hits(Vec3::ZERO, Vec3::new(20.0, 1.0, 0.0), c, HULL_RADIUS));
        assert!(!segment_hits(Vec3::ZERO, Vec3::new(20.0, 5.0, 0.0), c, HULL_RADIUS));
        assert!(!segment_hits(Vec3::ZERO, Vec3::new(5.0, 0.0, 0.0), c, HULL_RADIUS));
    }
}
