//! The playtest bot (debug tooling, not game logic): plays Roman for scripted runs (`--play`) from the script's
//! orders (playtest.rs: goto / fight / use). Each frame `think` decides where to walk and where to aim and leaves it
//! in `Bot` (walk / face / aim); the player's control and weapon systems read it as they read the keys and the mouse.
//! It never changes a game rule: everything it does a player could do with the controls.
//!
//! Movement: a route over the ground (nav.rs A*), replanned when stuck; a trail of where it has been to back out of
//! dead ends. Tactics: engage the nearest grunt in range with line of sight, hold position while it is far, back off
//! when badly hurt, a sticky bomb for a group, shoot explosives beside grunts (never one beside Roman), detour to
//! health pickups when hurt.

use crate::physics::Physics;
use bevy::prelude::*;

/// The playtest bot's orders (from the script) and its state; `walk` / `face` / `aim` are this frame's decisions.
#[derive(Resource, Default)]
pub struct Bot {
    pub goto: Option<Vec3>,
    pub fight: bool,
    pub arrived: bool,
    pub use_key: bool,
    pub stuck_timer: f32,
    pub last_pos: Vec3,
    pub veer: f32,
    /// planned waypoints toward `goto` (nav.rs)
    pub route: Vec<Vec3>,
    pub planned_for: Option<Vec3>,
    pub replan_timer: f32,
    /// places the bot got stuck at (avoided by later plans)
    pub blocked: Vec<Vec3>,
    pub no_progress: f32,
    pub last_dist: f32,
    /// seconds spent holding position on the current engagement
    pub hold: f32,
    /// seconds without a target (the hold resets after 5)
    pub no_target: f32,
    /// the bot has something worth shooting and no explosive next to it
    pub fire_ok: bool,
    /// the current goto's arrival distance (0: the default 12)
    pub near: f32,
    /// seconds on the current pickup detour; pickups given up on (unreachable)
    pub detour_t: f32,
    pub skip_pickups: Vec<Vec3>,
    /// throw a sticky bomb this frame; seconds until the next may be thrown
    pub throw: bool,
    /// where the bot has been (one point a second, the last 20): the way back out of a dead end
    pub trail: Vec<Vec3>,
    pub trail_t: f32,
    /// progress check: the best distance to the goal and how long since it improved
    pub best_goal_dist: f32,
    pub no_gain: f32,
    pub throw_timer: f32,
    /// this frame: the direction to walk (engine space, unit or zero to stand), None: no bot input
    pub walk: Option<Vec3>,
    /// this frame: the yaw to face while walking without a fight
    pub face: Option<f32>,
    /// this frame: the yaw and pitch to aim at a target
    pub aim: Option<(f32, f32)>,
}

pub struct BotPlugin;

impl Plugin for BotPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Bot>().add_systems(Update, think.before(crate::player::control).run_if(crate::playing));
    }
}

/// The yaw (Roman's convention: 0 faces -z) that faces along `dir`.
fn yaw_of(dir: Vec3) -> f32 {
    (-dir.x).atan2(-dir.z)
}

/// Plans a route when the goal changes or a partial route ran out short of it, tracks progress (retracing the trail
/// after 15 s without any) and drops the waypoints reached.
fn follow_route(bot: &mut Bot, physics: &Physics, me: Vec3, dt: f32) {
    // (re)plan: a new goal, or the partial route ran out short of the goal
    let route_done = bot.route.is_empty() && bot.goto.is_some_and(|g| (g - me).with_y(0.0).length() > 30.0);
    bot.replan_timer -= dt;
    if bot.goto.is_some() && (bot.planned_for != bot.goto || (route_done && bot.replan_timer <= 0.0)) {
        bot.planned_for = bot.goto;
        bot.replan_timer = 1.5;
        bot.route = crate::nav::path(physics, me, bot.goto.unwrap(), &bot.blocked).unwrap_or_default();
        // no route from here (a dead end the planner cannot see out of): back along the way it came, then plan
        // again from there
        if bot.route.is_empty() && bot.trail.len() > 3 {
            bot.route = bot.trail.iter().rev().skip(1).take(8).copied().collect();
        }
        info!("bot: route to {:?}: {} waypoints {:?}", bot.goto, bot.route.len(), bot.route.iter().map(|p| [p.x as i32, p.y as i32, p.z as i32]).collect::<Vec<_>>());
    }
    // no progress toward the goal for 15 s (routes that lead nowhere in a walled yard): retrace the trail out,
    // then plan afresh from there
    if let Some(g) = bot.goto {
        let d = (g - me).with_y(0.0).length();
        if d < bot.best_goal_dist - 10.0 || bot.best_goal_dist == 0.0 {
            bot.best_goal_dist = d;
            bot.no_gain = 0.0;
        } else {
            bot.no_gain += dt;
        }
        if bot.no_gain > 15.0 && bot.trail.len() > 3 {
            info!("bot: no progress for 15 s, retracing its trail");
            bot.route = bot.trail.iter().rev().skip(1).copied().collect();
            bot.trail.clear();
            bot.no_gain = 0.0;
            bot.best_goal_dist = 0.0;
            bot.replan_timer = 12.0;
        }
    } else {
        bot.best_goal_dist = 0.0;
        bot.no_gain = 0.0;
    }
    bot.trail_t += dt;
    if bot.trail_t >= 1.0 {
        bot.trail_t = 0.0;
        if bot.trail.last().is_none_or(|l| l.distance(me) > 6.0) {
            bot.trail.push(me);
            if bot.trail.len() > 20 {
                bot.trail.remove(0);
            }
        }
    }
    while bot.route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < 8.0) {
        bot.route.remove(0);
    }
}

/// The step toward the goal along the route: None on arrival (the goal is cleared). Stuck (under 4 units of progress
/// in a second): jump and veer for a moment; stuck 3 s: block the waypoint ahead and plan again around it.
fn step_to_goal(bot: &mut Bot, me: Vec3, dt: f32) -> Option<Vec3> {
    let g = bot.goto?;
    let to = (bot.route.first().copied().unwrap_or(g) - me).with_y(0.0);
    let near = if bot.near > 0.0 { bot.near } else { 12.0 };
    let to = if (g - me).with_y(0.0).length() < 12.0 { (g - me).with_y(0.0) } else { to };
    if (g - me).with_y(0.0).length() < near {
        bot.goto = None;
        bot.arrived = true;
        return None;
    }
    bot.stuck_timer += dt;
    if bot.stuck_timer > 1.0 {
        let moved = me.with_y(0.0).distance(bot.last_pos.with_y(0.0));
        bot.veer = if moved < 4.0 { 1.2 } else { (bot.veer - 1.0).max(0.0) };
        // progress = the distance to the next waypoint (or the goal) shrinking
        let target = bot.route.first().copied().unwrap_or(g);
        let d_now = (target - me).with_y(0.0).length();
        bot.no_progress = if d_now > bot.last_dist - 5.0 { bot.no_progress + 1.0 } else { 0.0 };
        bot.last_dist = d_now;
        bot.last_pos = me;
        bot.stuck_timer = 0.0;
        if bot.no_progress >= 3.0 {
            let spot = bot.route.first().copied().unwrap_or(me);
            bot.blocked.push(spot);
            bot.route.clear();
            bot.replan_timer = 0.0;
            bot.planned_for = None;
            bot.no_progress = 0.0;
            bot.last_dist = f32::MAX;
            info!("bot: stuck near {:?}, replanning", [spot.x as i32, spot.z as i32]);
        }
    }
    let dir = to.normalize();
    Some(if bot.veer > 0.0 { Quat::from_rotation_y(0.9 * if (bot.veer * 10.0) as i32 % 2 == 0 { 1.0 } else { -1.0 }) * dir } else { dir })
}

/// Where a player sees each live grunt: his position, except a mounted gun's gunner, whose entity sits at the gun's
/// origin (ref_grunt_08) while his body is at the seat 7.5 behind it (ai.rs GunPhase::On): aiming at the origin puts
/// every shot into the gun's own collision (stig_mgun_coll_0); aimed (+5 below) at his head above the gun's shield,
/// 8.5 over the origin.
fn live_grunts(grunts: &Query<(&crate::ai::Grunt, &GlobalTransform)>) -> Vec<Vec3> {
    grunts
        .iter()
        .filter(|(g, _)| g.mind != crate::ai::Mind::Dead)
        .map(|(g, gt)| match g.gun {
            Some(gn) if gn.phase == crate::ai::GunPhase::On => gn.origin + Quat::from_rotation_y(gn.yaw) * crate::level::game_to_engine() * Vec3::new(0.3, 7.5, 0.0) + Vec3::Y * 3.5,
            _ => gt.translation(),
        })
        .collect()
}

/// The bot's decisions for this frame (debug tooling): route, target, tactics, pickups; left in `Bot`.
#[allow(clippy::too_many_arguments)]
pub fn think(
    time: Res<Time>,
    mut bot: ResMut<Bot>,
    grunts: Query<(&crate::ai::Grunt, &GlobalTransform)>,
    pickups: Query<&GlobalTransform, With<crate::pickup::Pickup>>,
    explosives: Query<(&crate::fx::Destructible, &GlobalTransform)>,
    physics: NonSend<Physics>,
    roman: Query<(&crate::player::Roman, &Transform, &crate::fx::Health)>,
    mounted: Res<crate::turret::Mounted>,
) {
    let dt = time.delta_secs();
    bot.walk = None;
    bot.face = None;
    bot.aim = None;
    // on a mounted gun the player update takes no input (turret.rs holds him)
    let Ok((r, t, h)) = roman.single() else { return };
    if mounted.0.is_some() {
        return;
    }
    let me = t.translation;
    follow_route(&mut bot, &physics, me, dt);
    // the bot's tactics, as a player would play: engage the nearest grunt in rifle range with line of sight; hold
    // position while it is far, back off when badly hurt, a sticky bomb for a group
    let live = live_grunts(&grunts);
    let target = if bot.fight {
        live.iter()
            .copied()
            .filter(|p| p.distance(me) < 450.0 && crate::ai::line_of_sight(&physics, me, *p))
            .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
    } else {
        None
    };
    // explosives (kegs): never shoot with one next to Roman; shoot one from a distance when grunts stand by it
    let kegs: Vec<(Vec3, f32)> = explosives
        .iter()
        .filter(|(d, _)| d.area_damage > 0.0 && d.health > 0.0 && d.dying.is_none())
        .map(|(d, gt)| (gt.translation() + Vec3::Y * 2.0, d.radius_outer))
        .collect();
    let danger = kegs.iter().any(|(k, r)| k.distance(me) < r + 25.0);
    let keg_target = if bot.fight {
        kegs.iter()
            .filter(|(k, r)| {
                let d = k.distance(me);
                d > r + 50.0 && d < 400.0 && live.iter().any(|g| g.distance(*k) < *r) && {
                    let eye = me + Vec3::Y * 8.0;
                    let v = *k - eye;
                    physics.raycast(eye, v.normalize_or(Vec3::Y), v.length() + 2.0).is_none_or(|h| h.1.distance(*k) < 6.0)
                }
            })
            .map(|(k, _)| *k)
            .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
    } else {
        None
    };
    let aim_at = keg_target.or(target.map(|p| p + Vec3::Y * 5.0));
    bot.fire_ok = aim_at.is_some() && !danger;
    let low = h.0 < r.max_health * 0.35;
    bot.throw_timer -= dt;
    // an engagement: hold up to 20 s, then push on for 15 s; it ends after 5 s without a target
    bot.no_target = if target.is_some() { 0.0 } else { bot.no_target + dt };
    bot.hold = if bot.no_target > 5.0 || bot.hold > 35.0 { 0.0 } else { bot.hold + dt };
    if let Some(p) = target {
        let d = p.distance(me);
        let group = live.iter().filter(|q| q.distance(p) < 35.0).count();
        if d < 150.0 && group >= 2 && r.stickies > 0 && bot.throw_timer <= 0.0 {
            bot.throw = true;
            bot.throw_timer = 3.0;
        }
    }
    // an explosive beside Roman while under fire: step away from it (the bot holds its fire there)
    let near_keg = kegs.iter().filter(|(k, r)| k.distance(me) < r + 25.0).map(|(k, _)| *k).min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)));
    let tactic: Option<Vec3> = match target {
        Some(_) if near_keg.is_some() => near_keg.map(|k| (me - k).with_y(0.0).normalize_or_zero()),
        Some(p) if low => Some((me - p).with_y(0.0).normalize_or_zero()),
        Some(p) if p.distance(me) > 120.0 && bot.hold < 20.0 => Some(Vec3::ZERO),
        _ => None,
    };
    // a hurt bot detours to a pickup nearby (farther when badly hurt), as a player would
    let reach = if low { 200.0 } else { 100.0 };
    let detour = if bot.goto.is_some() && h.0 < r.max_health * 0.6 {
        pickups
            .iter()
            .map(|p| p.translation())
            .filter(|p| p.distance(me) < reach && !bot.skip_pickups.iter().any(|s| s.distance(*p) < 6.0))
            .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
    } else {
        None
    };
    // a pickup not reached in 8 s is given up on
    bot.detour_t = if detour.is_some() { bot.detour_t + dt } else { 0.0 };
    let detour = match detour {
        Some(p) if bot.detour_t > 8.0 => {
            info!("bot: pickup at {:?} not reached (Roman at {:?}), skipping", [p.x as i32, p.y as i32, p.z as i32], [me.x as i32, me.y as i32, me.z as i32]);
            bot.skip_pickups.push(p);
            bot.detour_t = 0.0;
            None
        }
        d => d,
    };
    if let (Some(dir), None) = (tactic, detour) {
        // hold (zero) or back off; the route is kept for later
        bot.walk = Some(dir);
    } else if let Some(p) = detour {
        let dir = (p - me).with_y(0.0).normalize_or_zero();
        if !bot.fight {
            bot.face = Some(yaw_of(dir));
        }
        bot.walk = Some(dir);
    } else if let Some(dir) = step_to_goal(&mut bot, me, dt) {
        if !bot.fight {
            bot.face = Some(yaw_of(dir));
        }
        bot.walk = Some(dir);
    }
    if bot.fight {
        if let Some(p) = aim_at {
            let to = p - (me + Vec3::Y * 8.5);
            bot.aim = Some((yaw_of(to), (to.y / to.with_y(0.0).length().max(1.0)).atan() - 0.08));
        }
    }
}
