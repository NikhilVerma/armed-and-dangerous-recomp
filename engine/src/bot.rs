//! The playtest bot (debug tooling, not game logic): plays Roman for scripted runs (`--play`) from the script's
//! orders (playtest.rs: goto / fight / use). Each frame `think` decides where to walk and where to aim and leaves it
//! in `Bot` (walk / face / aim); the player's control and weapon systems read it as they read the keys and the mouse.
//! It never changes a game rule: everything it does a player could do with the controls.
//!
//! Movement: a route over the ground (nav.rs A*), replanned when stuck; a trail of where it has been to back out of
//! dead ends. Tactics: engage the nearest grunt in range with line of sight; hold position for a duel at range while
//! at most two grunts see Roman; under fire from more, take the nearby spot the fewest of them can see and fight from
//! there; back off when badly hurt; sticky bombs at groups; machine-gun bursts at range; shoot explosives beside grunts
//! (never one beside Roman); detour to health pickups when hurt.

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
    /// what the bot did last frame (the state log's "bot"): route, duel, cover, to_cover, back_off, keg, pickup
    pub mode: &'static str,
    /// where the bot has stood for pin_t seconds (within 8 units); push_t: seconds left of pushing on regardless
    pub pin_pos: Vec3,
    pub pin_t: f32,
    pub push_t: f32,
    /// last frame the bot walked its route (not holding, in cover or on a pickup detour)
    pub advancing: bool,
    pub throw_timer: f32,
    /// the grunts that can see Roman (refreshed every 0.25 s by scan_t)
    pub threats: Vec<Vec3>,
    pub scan_t: f32,
    /// the damage taken lately (decaying) and the health last frame
    pub hurt: f32,
    pub last_health: f32,
    /// the cover spot being taken or held, for cover_t more seconds; the next search after cover_eval_t
    pub cover: Option<Vec3>,
    pub cover_t: f32,
    pub cover_eval_t: f32,
    /// the side-step: seconds left on this leg and its direction (+1 / -1)
    pub strafe_t: f32,
    pub strafe_sign: f32,
    /// the machine gun burst clock
    pub burst_t: f32,
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
        } else if bot.advancing {
            // (only while walking the route: holding, cover and pickups are not a lack of progress)
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
    pickups: Query<(&crate::pickup::Pickup, &GlobalTransform)>,
    explosives: Query<(&crate::fx::Destructible, &GlobalTransform)>,
    physics: NonSend<Physics>,
    roman: Query<(&crate::player::Roman, &Transform, &crate::fx::Health)>,
    mounted: Res<crate::turret::Mounted>,
    (cam, xhair): (Query<&GlobalTransform, With<crate::camera::FollowCam>>, Res<crate::hud::Crosshair>),
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
    let t0 = std::time::Instant::now();
    follow_route(&mut bot, &physics, me, dt);
    let route_ms = t0.elapsed().as_secs_f32() * 1000.0;
    if route_ms > 50.0 {
        info!("bot: route planning took {route_ms:.0} ms");
    }
    // the bot's tactics, as a player would play (docs/remaster-engine.md, the playtest bot): engage the nearest grunt in range with line of sight;
    // under fire from several, take cover where fewer see it and fight them from there; sticky bombs for groups;
    // bursts with the machine gun at range; health pickups when hurt
    let live = live_grunts(&grunts);
    // the grunts that can see Roman (refreshed four times a second: the cover search needs them too)
    bot.scan_t -= dt;
    if bot.scan_t <= 0.0 {
        bot.scan_t = 0.25;
        bot.threats = live.iter().copied().filter(|p| p.distance(me) < THREAT_RANGE && bullet_line(&physics, *p, me)).collect();
    }
    // the damage taken lately (decays by half a second): the sign of being shot at
    let lost = (bot.last_health - h.0).max(0.0);
    bot.last_health = h.0;
    bot.hurt = bot.hurt * (-dt / 2.0f32).exp() + lost;
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
    // fire in bursts at range: the machine gun's scatter grows over the first second held (player.rs mg_scatter),
    // so a short release every half second keeps it on target, as a player taps the trigger
    bot.burst_t += dt;
    let far = target.is_some_and(|p| p.distance(me) > 80.0) || keg_target.is_some();
    let release = far && bot.burst_t % BURST_CYCLE > BURST_CYCLE - BURST_GAP;
    // hold fire when a keg Roman would catch in its blast is on the line of fire (or the machine gun's scatter, 8)
    let danger = aim_at.is_some_and(|a| {
        let eye = me + Vec3::Y * 8.5;
        kegs.iter().any(|(k, r)| {
            let u = ((*k - eye).dot(a - eye) / (a - eye).length_squared().max(1e-3)).clamp(0.0, 1.0);
            k.distance(me) < r + 10.0 && (eye + (a - eye) * u).distance(*k) < 8.0
        })
    });
    bot.fire_ok = aim_at.is_some() && !danger && !release;
    let low = h.0 < r.max_health * 0.35;
    bot.throw_timer -= dt;
    // an engagement: hold up to 20 s, then push on for 15 s; it ends after 5 s without a target
    bot.no_target = if target.is_some() { 0.0 } else { bot.no_target + dt };
    bot.hold = if bot.no_target > 5.0 || bot.hold > 35.0 { 0.0 } else { bot.hold + dt };
    // a sticky bomb (area 100 within 30: a grunt_regular's whole health) at a group, or at anyone while several shoot
    if let Some(p) = target {
        let d = p.distance(me);
        let group = live.iter().filter(|q| q.distance(p) < 30.0).count();
        // (only along a clear line: a bomb that meets a wall close by sticks there and hurts Roman)
        let eye = me + Vec3::Y * 8.0;
        let v = p + Vec3::Y * 5.0 - eye;
        let clear = physics.raycast(eye, v.normalize_or(Vec3::Y), v.length()).is_none_or(|h| h.1.distance(p) < 8.0);
        // and with the crosshair on him, as the sticky auto-aim wants (sticky.rs: the camera's narrowed view, else
        // the bomb flies along the camera ray and may land at Roman's feet)
        // (the same test as sticky.rs, a little tighter: |x| within the view's half-width x 0.04 at that depth plus the
        // grunt's slack 4.66, |y| within the half-height x 0.12 plus the slack; the follow camera's FOV 0.654)
        let on_him = cam.single().is_ok_and(|c| {
            let v = p + Vec3::Y * 4.9 - c.translation();
            let z = v.dot(c.forward().as_vec3());
            let half_v = (0.654f32 * 0.5).tan();
            z > 0.0 && v.dot(c.right().as_vec3()).abs() < 0.8 * (half_v * 1.7204 * 0.04 * z + 4.66) && v.dot(c.up().as_vec3()).abs() < 0.8 * (half_v * 0.12 * z + 4.66)
        });
        // (and the crosshair closed on a grunt: hud.rs Crosshair, what the player sees)
        let on_him = on_him && xhair.on && !xhair.friendly && xhair.object.is_some_and(|o| grunts.get(o).is_ok_and(|(g, _)| g.mind != crate::ai::Mind::Dead));
        if d > 80.0 && d < STICKY_REACH && clear && on_him && (group >= 2 || bot.threats.len() >= 3) && r.stickies > 0 && bot.throw_timer <= 0.0 {
            bot.throw = true;
            bot.throw_timer = 4.0;
        }
    }
    // cover: under fire from several grunts (or hurt and seen), a nearby spot fewer of them can see
    bot.cover_eval_t -= dt;
    let seen = bot.threats.len();
    let pressed = bot.fight && (seen >= 3 || (seen >= 2 && bot.hurt > 25.0) || (low && seen >= 1));
    if pressed && bot.cover.is_none() && bot.cover_eval_t <= 0.0 {
        bot.cover_eval_t = 0.5;
        if let Some((spot, n)) = find_cover(&physics, me, &bot.threats, low) {
            info!("bot: cover at {:?} ({} of {} grunts see it)", [spot.x as i32, spot.y as i32, spot.z as i32], n, seen);
            bot.cover = Some(spot);
            bot.cover_t = COVER_HOLD;
        }
    }
    bot.cover_t -= dt;
    // in cover with nothing to shoot for 2 s: move on (and no new cover for a while, so it does not hide forever)
    if bot.cover.is_some() && bot.no_target > 2.0 && bot.cover_t < COVER_HOLD - 2.0 {
        bot.cover_t = 0.0;
        bot.cover_eval_t = 4.0;
    }
    if bot.cover_t <= 0.0 || !bot.fight {
        bot.cover = None;
    }
    // an explosive beside Roman while under fire: step away from it (the bot holds its fire there)
    // (inside its blast, and only where the way out is open: against a wall it would stand there for good)
    let near_keg = kegs
        .iter()
        .filter(|(k, r)| k.distance(me) < r + 10.0)
        .map(|(k, _)| *k)
        .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
        .filter(|k| physics.raycast(me + Vec3::Y * 5.0, (me - *k).with_y(0.0).normalize_or(Vec3::X), 10.0).is_none());
    let (mode, tactic): (&'static str, Option<Vec3>) = match (bot.cover, target) {
        (None, Some(_)) if near_keg.is_some() && seen <= 2 => ("keg", near_keg.map(|k| (me - k).with_y(0.0).normalize_or_zero())),
        // to the cover spot, then about it (fighting what it sees, never standing still in a grunt's sight)
        (Some(c), _) if (c - me).with_y(0.0).length() > 12.0 => ("to_cover", Some((c - me).with_y(0.0).normalize_or_zero())),
        (Some(_), Some(p)) => ("cover", Some(strafe(&mut bot, &physics, me, p, dt))),
        (Some(_), None) => ("cover", Some(Vec3::ZERO)),
        (None, Some(p)) if low => ("back_off", Some((me - p).with_y(0.0).normalize_or_zero())),
        // losing health fast to several grunts with no cover in reach: back the way it came (cleared ground)
        (None, _) if bot.hurt > 45.0 && seen >= 2 && bot.trail.len() >= 4 => {
            let back = bot.trail[bot.trail.len() - 4];
            ("retreat", Some((back - me).with_y(0.0).normalize_or_zero()))
        }
        // a duel at range: stay and shoot while at most two grunts can see Roman, side-stepping: their bullets
        // (450 units/s) fly at where he stood
        (None, Some(p)) if p.distance(me) > 120.0 && bot.hold < 20.0 && seen <= 2 => ("duel", Some(strafe(&mut bot, &physics, me, p, dt))),
        _ => ("route", None),
    };
    // pinned: 12 s within 8 units of one spot while a tactic holds it there (a keg against a wall, cover it cannot
    // leave): push on along the route for 6 s
    if me.distance(bot.pin_pos) > 8.0 {
        bot.pin_pos = me;
        bot.pin_t = 0.0;
    } else {
        bot.pin_t += dt;
    }
    bot.push_t -= dt;
    if bot.pin_t > 12.0 && tactic.is_some() && bot.goto.is_some() {
        info!("bot: pinned at {:?} ({mode}), pushing on", [me.x as i32, me.z as i32]);
        bot.pin_t = 0.0;
        bot.push_t = 6.0;
        bot.cover = None;
    }
    let (mode, tactic) = if bot.push_t > 0.0 { ("push", None) } else { (mode, tactic) };
    bot.mode = mode;
    // a hurt bot detours to a pickup nearby (farther when badly hurt, not into a crowd), as a player would
    let reach = if low { 250.0 } else { 150.0 };
    let detour = if bot.goto.is_some() && h.0 < r.max_health * 0.7 && (seen <= 1 || low) {
        pickups
            .iter()
            .filter(|(k, _)| k.kind == crate::pickup::Kind::Health)
            .map(|(_, p)| p.translation())
            .filter(|p| p.distance(me) < reach && !bot.skip_pickups.iter().any(|s| s.distance(*p) < 6.0))
            .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
    } else {
        None
    };
    // a pickup not reached in 10 s is given up on
    bot.detour_t = if detour.is_some() { bot.detour_t + dt } else { 0.0 };
    let detour = match detour {
        Some(p) if bot.detour_t > 10.0 => {
            info!("bot: pickup at {:?} not reached (Roman at {:?}), skipping", [p.x as i32, p.y as i32, p.z as i32], [me.x as i32, me.y as i32, me.z as i32]);
            bot.skip_pickups.push(p);
            bot.detour_t = 0.0;
            None
        }
        d => d,
    };
    bot.advancing = false;
    if let (Some(dir), None) = (tactic, detour) {
        // hold (zero), take cover or back off; the route is kept for later
        bot.walk = Some(dir);
    } else if let Some(p) = detour {
        bot.mode = "pickup";
        let dir = (p - me).with_y(0.0).normalize_or_zero();
        if !bot.fight {
            bot.face = Some(yaw_of(dir));
        }
        bot.walk = Some(dir);
    } else if let Some(dir) = step_to_goal(&mut bot, me, dt) {
        bot.advancing = true;
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

/// Side-steps across the line to `foe`, turning back every STRAFE_LEG seconds or at a wall: a grunt's bullet flies
/// at where Roman was when it left the gun (0x004284e0 aims at the target's ref_hit_00; no lead), so a moving Roman is
/// missed at range, as a player dodges.
fn strafe(bot: &mut Bot, physics: &Physics, me: Vec3, foe: Vec3, dt: f32) -> Vec3 {
    bot.strafe_t -= dt;
    let side = Vec3::new(-(foe - me).z, 0.0, (foe - me).x).normalize_or_zero();
    let blocked = |dir: Vec3| physics.raycast(me + Vec3::Y * 5.0, dir, 10.0).is_some();
    if bot.strafe_t <= 0.0 || blocked(side * bot.strafe_sign) {
        bot.strafe_t = STRAFE_LEG;
        bot.strafe_sign = if bot.strafe_sign > 0.0 { -1.0 } else { 1.0 };
    }
    side * bot.strafe_sign
}

/// Whether a grunt standing at `shooter` (feet) has a clear shot at `target` (feet): from his muzzle height (8.4,
/// ai.rs shoot) to the chest, through what bullets pass (Physics::raycast_bullet)
fn bullet_line(physics: &Physics, shooter: Vec3, target: Vec3) -> bool {
    let (a, b) = (shooter + Vec3::Y * 8.4, target + Vec3::Y * 6.0);
    let v = b - a;
    let dir = v.normalize_or(Vec3::Y);
    physics.raycast_bullet(a + dir * 4.0, dir, (v.length() - 4.0).max(0.1)).is_none()
}

/// how long a side-step lasts before it turns back
const STRAFE_LEG: f32 = 1.2;
/// A grunt this far away counts as a threat when he can see Roman (the grunts' fire_range_max: 500 for the
/// regulars, 350 for the captains; their aim stays tight out there, grunt-ai.md 0x004284e0)
const THREAT_RANGE: f32 = 500.0;
/// how far the bot throws a sticky bomb (sticky_bomb_range 250, p_globalsettings, less a margin)
const STICKY_REACH: f32 = 230.0;
/// the machine gun bursts: a release of BURST_GAP every BURST_CYCLE seconds
const BURST_CYCLE: f32 = 0.6;
const BURST_GAP: f32 = 0.05;
/// how long the bot stays in a cover spot before it moves on (or looks for a better one)
const COVER_HOLD: f32 = 6.0;

/// The best cover near Roman from the grunts that see him: spots on rings round him (10 to 80 units) he can walk to
/// in a straight line on about his level, scored by how many of the grunts would see him there, then by the way
/// to it: one grunt in sight is best (a duel), then none. None unless it at least halves the grunts that see him.
/// With `hide` (badly hurt) the fewer the better.
fn find_cover(physics: &Physics, me: Vec3, threats: &[Vec3], hide: bool) -> Option<(Vec3, usize)> {
    let here = threats.len();
    let mut best: Option<(f32, Vec3, usize)> = None;
    for ring in 1..=8 {
        let rad = ring as f32 * 10.0;
        for k in 0..16 {
            let a = k as f32 * std::f32::consts::TAU / 16.0 + ring as f32 * 0.2;
            let p = me + Vec3::new(a.cos(), 0.0, a.sin()) * rad;
            // the ground there, within a step or two of Roman's level
            // the ground there (the landscape, not a roof or a wall's top it would have to jump onto), within a step of
            // Roman's level
            let Some((b, g, n)) = physics.raycast(p + Vec3::Y * 20.0, -Vec3::Y, 60.0) else { continue };
            if n.y < 0.7 || (g.y - me.y).abs() > 6.0 || physics.landscape != Some(b) {
                continue;
            }
            // walkable in a straight line (a knee-high ray, as nav.rs)
            let (a3, b3) = (me + Vec3::Y * 5.0, g + Vec3::Y * 5.0);
            if physics.raycast(a3, (b3 - a3).normalize_or(Vec3::X), (b3 - a3).length()).is_some() {
                continue;
            }
            let seen = threats.iter().filter(|t| bullet_line(physics, **t, g)).count();
            // one grunt in sight is a duel worth having (none: nothing to shoot), unless hiding
            let score = match (hide, seen) {
                (true, n) => n as f32 * 1000.0,
                (false, 0) => 400.0,
                (false, n) => (n - 1) as f32 * 1000.0,
            } + rad;
            if best.is_none_or(|(s, _, _)| score < s) {
                best = Some((score, g, seen));
            }
        }
    }
    // worth the move only when it halves the grunts in sight (or leaves one)
    best.filter(|&(_, _, n)| n <= 1 || n * 2 <= here).map(|(_, p, n)| (p, n))
}
