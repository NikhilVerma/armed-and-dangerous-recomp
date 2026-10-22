//! Movable default objects: boulders and loose rocks (DefaultGameObject, no behaviour, `movable` 1; boulder_large /
//! _medium / _small / _tiny on lev_1_m1_b, lev_2_m1, lev_2_m2, lev_2_m3, lev_3_m1; the l5 rocks of lev_5_m3) and the
//! default objects' collision reactions. docs/subsystems/objects.md "Movable objects".
//!
//! - Load (DefaultGameObject_Load 0x0044fd20): `pinned` (default 1) holds the object still (0x004577b0(1)); a
//!   `gravity` other than 1 joins the custom-gravity list World+0xf1c and unpins it; `unpin_health_pct` > 0 pins it
//!   until its health falls below that share of max_health (0x00450da0: then collision vt+0xf8(0x70001) and
//!   0x004577b0(0): a dynamic body). A husk (boulder_large -> _medium -> _small -> _tiny) is a fresh object of its
//!   template: unpinned. The gravity getter (DefaultGameObject_GetGravity 0x00450fa0 = +0x464; the Object base
//!   0x004585f0 answers 0) is the body's gravity factor.
//! - Collide (0x00450af0): with another object, `crush` (0x200) queues it when |v - v_other| >= crush_speed and
//!   |v| >= crush_speed, and either |v_other| < crush_speed or the relative direction against its own is below -0.2;
//!   `on_hit_damage` (0x40) queues every object it touches. On the landscape: `on_hit_fx` "name/count/interval" fires at
//!   the contact (at most count times, interval s apart); `play_boulder_sound` marks an impact (fx_boulder_impact)
//!   when its vertical speed is below -50. `on_hit_death` kills it at its first contact.
//! - Update (0x004505a0): "imp_boulder_roll_lp" while its horizontal speed squared is above 16 and it touches
//!   something (0.5 s tail); the queued objects take a damage record from it: a crush (type 0x4000) of their whole
//!   health, else direct_damage, with direct_damage_force as the impulse along its velocity (only Roman's hit handler
//!   uses it: his knockback). A dead object (0x80) skips all of it; death_timeout (+0x4c0) is the dropped objects'
//!   (icobject.rs: no placed object in the shipped levels sets it). The per-frame nav-grid mark of a moving blocking
//!   object (vt+0xfc(1, 2), DefaultGameObject_MarkGrid 0x004509a0) is replaced by nav.rs, whose A* plans over the Jolt
//!   world the body moves in (docs/port/ai.csv).
//! - The speed trap (Object slot 0x12 0x00452790: `speed_trap`, the Havok anti-tunnelling agent 0x004a88a0): Jolt's
//!   linear-cast motion quality.

use bevy::prelude::*;
use oxijolt::prelude::{BodySettings, MotionQuality, Shape};
use serde::Deserialize;

use crate::physics::{Physics, jv};

/// A movable object's property values (DefaultGameObject_Load 0x0044fd20), read once from its property set.
#[derive(Deserialize, Clone, Debug)]
#[serde(default)]
pub struct MovableProps {
    pub movable: i32,
    pub pinned: i32,
    pub unpin_health_pct: f32,
    pub gravity: f32,
    pub crush: i32,
    pub crush_speed: f32,
    pub takes_crushing_damage: i32,
    pub on_hit_damage: i32,
    pub direct_damage: f32,
    pub direct_damage_force: f32,
    /// "name/count/interval"
    pub on_hit_fx: Option<String>,
    pub on_hit_death: i32,
    pub play_boulder_sound: i32,
    pub speed_trap: i32,
    pub friction: f32,
    pub restitution: f32,
    pub density: f32,
}

impl Default for MovableProps {
    fn default() -> Self {
        MovableProps {
            movable: 0,
            pinned: 1,
            unpin_health_pct: 0.0,
            gravity: 1.0,
            crush: 0,
            crush_speed: 10.0,
            takes_crushing_damage: 1,
            on_hit_damage: 0,
            direct_damage: 0.0,
            direct_damage_force: 0.0,
            on_hit_fx: None,
            on_hit_death: 0,
            play_boulder_sound: 0,
            speed_trap: 0,
            friction: 0.5,
            restitution: 0.0,
            density: 1.0,
        }
    }
}

impl MovableProps {
    pub fn of(p: &crate::gamedata::Props) -> Self {
        serde_json::from_value(serde_json::Value::Object(p.0.clone())).unwrap_or_default()
    }

    /// Whether it starts held still (0x0044fd20: a gravity other than 1 unpins, unpin_health_pct > 0 pins).
    pub fn starts_pinned(&self) -> bool {
        if self.unpin_health_pct > 0.0 {
            return true;
        }
        self.pinned != 0 && self.gravity == 1.0
    }

    /// on_hit_fx's name, count and interval.
    fn hit_fx(&self) -> Option<(String, u32, f32)> {
        let s = self.on_hit_fx.as_deref()?;
        let mut w = s.split('/');
        let name = w.next()?.trim().to_lowercase();
        let count = w.next().and_then(|c| c.trim().parse().ok()).unwrap_or(1);
        let interval = w.next().and_then(|c| c.trim().parse().ok()).unwrap_or(0.0);
        (!name.is_empty() && name != "none").then_some((name, count, interval))
    }
}

/// A movable default object: its properties, its collision model, and its body once it moves.
#[derive(Component)]
pub struct Movable {
    pub props: MovableProps,
    /// its property set, for the body's mass (density, collide_poly, mass: fxvm::body::BodyDef)
    body_props: crate::gamedata::Props,
    collision: String,
    /// the dynamic body (None while pinned)
    pub body: Option<oxijolt::prelude::BodyId>,
    /// on_hit_fx: its name, the fires left (+0x498) and the time of the last (+0x490)
    hit_fx: Option<(String, u32, f32, f32)>,
    /// the roll loop (+0x518) and its tail (+0x51c)
    roll: Option<crate::audio::SoundId>,
    roll_tail: f32,
    /// the crushes and hits it dealt (the state log)
    pub crushed: u32,
    /// a playtest's shove ([`Push`], engine space) waiting for the body
    shove: Option<Vec3>,
}

/// Debug (playtest `push`): the movable object with instance id `id` gets this velocity (game space), unpinned first.
#[derive(Message, Clone, Copy, Debug)]
pub struct Push {
    pub id: u32,
    pub velocity: Vec3,
}

impl Movable {
    /// A movable object with the collision model `collision`, or None when it is not movable.
    pub fn new(p: &crate::gamedata::Props, collision: Option<String>) -> Option<Self> {
        let props = MovableProps::of(p);
        if props.movable == 0 && props.gravity == 1.0 {
            return None;
        }
        let hit_fx = props.hit_fx().map(|(n, c, i)| (n, c, i, f32::NEG_INFINITY));
        Some(Movable { props, body_props: p.clone(), collision: collision?, body: None, hit_fx, roll: None, roll_tail: 0.0, crushed: 0, shove: None })
    }
}

/// A placed object with on_hit_death (0x00450af0: flag 0x10 sets the kill flag 0x80 at any contact, and the update
/// 0x004505a0 kills it): the village windows. A pane any body touches breaks.
#[derive(Component)]
pub struct HitDeath;

pub struct MovablePlugin;

impl Plugin for MovablePlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<Push>().add_systems(Update, (pushes, unpin, collide, hit_death, log).chain().after(crate::fx::FxSet).run_if(crate::playing));
    }
}

/// The playtest's shoves: kept on the object until its body exists.
fn pushes(mut events: MessageReader<Push>, mut q: Query<(&mut Movable, &crate::level::InstanceId)>) {
    for ev in events.read() {
        for (mut m, id) in &mut q {
            if id.0 == ev.id {
                m.shove = Some(Vec3::new(ev.velocity.x, ev.velocity.z, -ev.velocity.y));
            }
        }
    }
}

/// Unpinned objects get their body: at load (not pinned), or when their health falls below unpin_health_pct of their
/// max_health (0x00450da0). The static collider goes; a convex hull of the collision model (its scale applied) moves
/// the entity from then on (physics::Body), with the property set's gravity (of the game's: Physics::game_gravity_factor),
/// friction and restitution, and the mass Object_BuildRadiusCollision 0x004527d0 gives it (fxvm::body::BodyDef: the
/// bounding sphere's or, collide_poly, the hull's volume x density; at scale 1, as the fx bodies).
#[allow(clippy::type_complexity)]
fn unpin(
    mut commands: Commands,
    mut q: Query<(Entity, &mut Movable, &mut crate::level::Placed, &Transform, Option<&crate::fx::Destructible>, Option<&crate::level::InstanceId>)>,
    mut physics: NonSendMut<Physics>,
    mut kit: crate::army::ModelKit,
    mut decals: Option<ResMut<crate::decals::DecalWorld>>,
) {
    for (e, mut m, mut placed, t, d, id) in &mut q {
        if let (Some(body), Some(v)) = (m.body, m.shove) {
            m.shove = None;
            let k = physics.meters_per_unit;
            if let Ok(mut b) = physics.world.body_mut(body) {
                let _ = b.set_linear_velocity(jv(v * k));
                b.activate();
            }
            info!("EVENT movable: {} pushed at {:.0}", placed.template, v.length());
        }
        if m.body.is_some() {
            continue;
        }
        let pinned = if m.props.unpin_health_pct > 0.0 {
            d.is_none_or(|d| d.health >= m.props.unpin_health_pct * d.max_health)
        } else {
            m.props.starts_pinned()
        };
        if pinned && m.shove.is_none() {
            continue;
        }
        let Some(points) = kit.model_points(&m.collision) else { continue };
        let k = physics.meters_per_unit;
        let pts: Vec<_> = points.iter().map(|p| jv(*p * t.scale * k)).collect();
        let Ok(shape) = Shape::new_convex_hull(&pts) else { continue };
        let mass = {
            let (lo, hi) = points.iter().fold((Vec3::MAX, Vec3::MIN), |(a, b), p| (a.min(*p), b.max(*p)));
            let centre = (lo + hi) * 0.5;
            let radius = points.iter().map(|p| p.distance(centre)).fold(0.0, f32::max);
            crate::fxvm::body::BodyDef::new(lo, hi, crate::fxvm::body::hull_volume(&points), radius, &m.body_props).mass
        };
        let gravity = physics.game_gravity_factor(m.props.gravity);
        let mut settings = BodySettings::new_dynamic().mass(mass).friction(m.props.friction).restitution(m.props.restitution).gravity_factor(gravity);
        if m.props.speed_trap != 0 {
            settings = settings.motion_quality(MotionQuality::LinearCast);
        }
        let Some(body) = physics.add_dynamic(e, &shape, t.translation, t.rotation, Vec3::ZERO, settings) else { continue };
        if let Some(c) = placed.collider.replace(body) {
            physics.remove_static(c);
        }
        // its contacts are noted (contacts.rs): the crush and the landscape hits read them; it bounces by the game's
        // combination (0x00413240)
        physics.set_contact_rule(body, crate::contacts::ContactRule { restitution: Some(m.props.restitution), ..Default::default() });
        if let Some(mat) = physics.materials.get(&body).cloned().or(Some("stone".into())) {
            physics.materials.insert(body, mat);
        }
        if let Some(w) = decals.as_mut() {
            w.remove_object(e);
        }
        m.body = Some(body);
        commands.entity(e).insert(crate::physics::Body::rigid(body));
        info!("EVENT movable: {} {} unpinned", placed.template, id.map_or(0, |i| i.0));
    }
}

/// The contacts of the step (0x00450af0) and their effects (0x004505a0): crushes and hits on characters and other
/// movable objects, the landscape fx, the roll loop.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn collide(
    time: Res<Time>,
    mut movers: Query<(Entity, &mut Movable, &GlobalTransform, Option<&mut crate::fx::Destructible>)>,
    mut grunts: Query<(&crate::physics::Body, &mut crate::fx::Health, &mut crate::ai::Grunt)>,
    mut roman: Query<(&mut crate::player::Roman, &mut crate::fx::Health, &GlobalTransform, Has<crate::mission::HitsIgnored>), Without<crate::ai::Grunt>>,
    physics: NonSend<Physics>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopVoice>,
    mut before: Local<std::collections::HashMap<oxijolt::prelude::BodyId, Vec3>>,
) {
    let now = time.elapsed_secs();
    let k = physics.meters_per_unit;
    let speed_of = |b| physics.world.body(b).ok().map(|b| crate::physics::bv(b.linear_velocity()) / k);
    // the contact callback reads the bodies' velocities before the solver parts them (0x00450af0 runs inside the
    // step): here the velocities of the frame before the step, kept for the movers and the bodies near them
    let pre = |b| before.get(&b).copied().or_else(|| speed_of(b));
    let mut victims: Vec<Record> = Vec::new();
    for (e, mut m, gt, mut d) in &mut movers {
        let Some(body) = m.body else { continue };
        let Some(v) = pre(body) else { continue };
        let touches = physics.touches(body);
        // on_hit_death: its first contact kills it (0x00450af0 sets the kill flag 0x80 inside the step)
        if m.props.on_hit_death != 0 && !touches.is_empty() {
            if let Some(d) = d.as_mut() {
                d.health = 0.0;
            }
        }
        // a dead object (0x80) leaves the update before the roll loop, the impact fx and the damage records
        // (0x004505a0 returns through its death vt+0xd4): the roll loop ends with it (guess: the death removes the
        // object and the sound attached to it)
        if d.as_ref().is_some_and(|d| d.health <= 0.0) {
            if let Some(id) = m.roll.take() {
                stop.write(crate::audio::StopVoice { id, immediate: false });
            }
            continue;
        }
        let cs = m.props.crush_speed;
        // the crush test of 0x00450af0 against a body moving at `vo`
        let crushes = |vo: Vec3| {
            let rel = v - vo;
            if m.props.crush == 0 || rel.length() < cs || v.length() < cs {
                return false;
            }
            vo.length() < cs || rel.normalize().dot(v.normalize()) < -0.2
        };
        let mut hit: Vec<Victim> = Vec::new();
        // the landscape contacts of the step (in order): on_hit_fx takes the first one its interval lets through
        // (+0x490 is the step's time, so the step's later ones fail `>`), the impact mark the last below -50 (each
        // contact call overwrites +0x49c)
        let mut ground: Vec<Vec3> = Vec::new();
        for touch in &touches {
            match physics.bodies.get(&touch.other).copied() {
                Some(other) => {
                    let vo = pre(touch.other).unwrap_or(Vec3::ZERO);
                    if std::env::var("MOVDBG").is_ok() {
                        info!("MOVDBG {e:?} v {:.0} touches {other:?} v {:.0}", v.length(), vo.length());
                    }
                    if m.props.on_hit_damage != 0 || crushes(vo) {
                        hit.push(Victim::Object(other));
                    }
                }
                // a static body: the landscape or a placed object's mesh, at the contact point (0x00450af0: pos ->
                // +0x49c, the fx position 0x004505a0 hands Fx_Create)
                None => ground.push(physics.from_m(touch.point)),
            }
        }
        // Roman's walker is no body: he is touched when the boulder's surface is within his capsule's reach
        if let Ok((r, _, rt, _)) = roman.single() {
            let chest = rt.translation() + Vec3::Y * r.walker.lift;
            let to = gt.translation() - chest;
            if to.length() < 60.0 {
                let reach = physics.raycast(chest, to.normalize_or(Vec3::Y), to.length()).filter(|h| h.0 == body).map(|h| h.1.distance(chest));
                if reach.is_some_and(|d| d <= r.walker.lift + 1.0) && (m.props.on_hit_damage != 0 || crushes(r.vel)) {
                    hit.push(Victim::Roman);
                }
            }
        }
        // the landscape: on_hit_fx (once an interval, count times) and the impact mark below -50
        if let (Some(&first), Some(&last)) = (ground.first(), ground.last()) {
            if let Some((name, left, interval, at_time)) = m.hit_fx.as_mut() {
                if *left > 0 && now - *at_time > *interval {
                    *at_time = now;
                    *left -= 1;
                    fx.write(crate::fx::StartFx { name: name.clone(), at: first, rotation: Quat::IDENTITY, follow: None, position_only: false });
                }
            }
            if m.props.play_boulder_sound != 0 && v.y < -50.0 {
                fx.write(crate::fx::StartFx { name: "fx_boulder_impact".into(), at: last, rotation: Quat::IDENTITY, follow: None, position_only: false });
            }
        }
        // the roll loop: horizontal speed squared above 16 while touching, a 0.5 s tail
        if m.props.play_boulder_sound != 0 {
            if v.with_y(0.0).length_squared() > 16.0 && !touches.is_empty() {
                if m.roll.is_none() {
                    let id = crate::audio::SoundId::new();
                    sounds.write(crate::audio::PlaySound::on("imp_boulder_roll_lp", e).looping(true).with_id(id));
                    m.roll = Some(id);
                }
                m.roll_tail = 0.5;
            } else {
                m.roll_tail -= time.delta_secs();
                if m.roll_tail <= 0.0 {
                    if let Some(id) = m.roll.take() {
                        stop.write(crate::audio::StopVoice { id, immediate: false });
                    }
                }
            }
        }
        // a crush is a record of the victim's whole health (type 0x4000), else direct_damage; its direction is the
        // mover's velocity at the update, normalised (+0x18), its impulse direct_damage_force (+0x24)
        let amount = if m.props.crush != 0 { f32::INFINITY } else { m.props.direct_damage };
        let dir = speed_of(body).unwrap_or(v).normalize_or_zero();
        victims.extend(hit.into_iter().map(|victim| Record { from: e, victim, amount, speed: v.length(), dir, force: m.props.direct_damage_force }));
    }
    // this frame's velocities for the next step's contacts: the moving movers and the characters near them
    let moving: Vec<Vec3> = movers.iter().filter(|(_, m, ..)| m.body.is_some()).map(|(_, _, gt, _)| gt.translation()).collect();
    before.clear();
    for (_, m, ..) in &movers {
        if let Some(b) = m.body {
            if let Some(v) = speed_of(b) {
                before.insert(b, v);
            }
        }
    }
    for (gb, ..) in &grunts {
        if let Ok(b) = physics.world.body(gb.id) {
            let at = physics.from_m(b.position());
            if moving.iter().any(|p| p.distance(at) < 150.0) {
                before.insert(gb.id, crate::physics::bv(b.linear_velocity()) / k);
            }
        }
    }
    // the records reach their victims (vt+0xd0): characters take crushing damage (their default); a movable takes it
    // only with takes_crushing_damage (0x00450da0 ignores 0x4000 records otherwise). Only Roman's handler reads the
    // impulse: alive after the damage, he is knocked back along the record's direction (PlayerObject_OnHit
    // 0x0047afa0, Roman::knock); the AI's (GenericAIObject_Slot52 0x00430340) and the objects' (0x00450da0) ignore it
    for Record { from, victim, amount, speed, dir, force } in victims {
        let (dealt, what) = match victim {
            Victim::Roman => {
                // (none after the mission's pre-success: mission.rs HitsIgnored)
                let dealt = roman.single_mut().ok().filter(|(.., ignored)| !ignored).map_or(0.0, |(mut r, mut h, _, _)| {
                    let dealt = take(&mut h.0, amount);
                    if h.0 > 0.0 && force != 0.0 {
                        r.knock(dealt, force, dir);
                    }
                    dealt
                });
                (dealt, "Roman")
            }
            Victim::Object(e) => {
                if let Ok((_, mut h, mut g)) = grunts.get_mut(e) {
                    let d = take(&mut h.0, amount);
                    if d > 0.0 {
                        g.last_hit = d;
                    }
                    (d, "a character")
                } else if let Ok((_, m, _, Some(mut d))) = movers.get_mut(e) {
                    (if amount.is_finite() || m.props.takes_crushing_damage != 0 { take(&mut d.health, amount) } else { 0.0 }, "a movable")
                } else {
                    (0.0, "")
                }
            }
        };
        if dealt > 0.0 {
            if let Ok((_, mut m, ..)) = movers.get_mut(from) {
                m.crushed += 1;
            }
            info!("EVENT movable: {from:?} {} {what} {victim:?} at {speed:.0} ({dealt:.0} damage)", if amount.is_infinite() { "crushes" } else { "hits" });
        }
    }
}

/// `amount` off a health that is still above 0 (a crush's infinite amount takes all of it): the damage dealt.
fn take(health: &mut f32, amount: f32) -> f32 {
    let d = amount.min(health.max(0.0));
    *health -= d;
    d
}

/// The damage record a movable sends a victim it queued (0x004505a0: the 0x34-byte record through vt+0xd0).
struct Record {
    from: Entity,
    victim: Victim,
    /// +0x14: direct_damage, or the victim's whole health for a crush (infinite here, type 0x4000)
    amount: f32,
    /// the mover's speed at the contact (the log)
    speed: f32,
    /// +0x18: the mover's normalised velocity (engine axes)
    dir: Vec3,
    /// +0x24: direct_damage_force
    force: f32,
}

/// Who a movable's contact reaches: Roman (his walker is no body) or an object with a body.
#[derive(Clone, Copy, Debug)]
enum Victim {
    Roman,
    Object(Entity),
}

/// on_hit_death objects (the windows) touched by a body this step die.
fn hit_death(mut q: Query<(&crate::level::Placed, &mut crate::fx::Destructible), With<HitDeath>>, physics: NonSend<Physics>) {
    for (p, mut d) in &mut q {
        let Some(c) = p.collider else { continue };
        if d.dying.is_none() && d.health > 0.0 && !physics.touches(c).is_empty() {
            d.health = 0.0;
            d.dying = Some(0.0);
            info!("EVENT movable: {} broken by a contact", p.template);
        }
    }
}

/// The movable objects in the playtest state log: how many move, and the crushes they dealt.
fn log(q: Query<(&Movable, &GlobalTransform, Option<&crate::level::InstanceId>)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    if q.is_empty() {
        return;
    }
    let moving = q.iter().filter(|(m, ..)| m.body.is_some()).count();
    let crushed: u32 = q.iter().map(|(m, ..)| m.crushed).sum();
    // the moving ones by instance id: where they are (game space)
    let at: serde_json::Map<String, serde_json::Value> = q
        .iter()
        .filter(|(m, ..)| m.body.is_some())
        .filter_map(|(_, gt, id)| {
            let p = gt.translation();
            Some((id?.0.to_string(), serde_json::json!([p.x.round(), (-p.z).round(), p.y.round()])))
        })
        .collect();
    state.0.insert("movable".into(), serde_json::json!({ "count": q.iter().count(), "moving": moving, "crushed": crushed, "at": at }));
}
