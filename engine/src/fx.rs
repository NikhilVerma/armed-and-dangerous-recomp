//! Area damage and object death, from the game's property sets: `max_health`, `area_damage`,
//! `damage_radius_inner/outer`, `impact_force`, `fx_death`, `husk`, and the chain delays of exploding props
//! (`start_delay`, `min_delay`, `acceleration_rate`). The death fx itself runs in the fx VM (fxvm.rs).
//! Rules from the recovered code: docs/subsystems/fx-vm.md section 5 (AreaDamage_Apply 0x0047f020,
//! FxExplosion_Die 0x0041c570).

use crate::physics::{Physics, jr, jv};
use bevy::prelude::*;

/// An explosion: area damage at a point, with the source's parameters (game units).
#[derive(Message, Clone)]
pub struct Explode {
    pub at: Vec3,
    pub area_damage: f32,
    pub radius_inner: f32,
    pub radius_outer: f32,
    pub impact_force: f32,
    pub source: Option<Entity>,
    /// The chain delay this blast passes to exploding props it kills (record +0x30).
    pub delay: f32,
    /// who the damage record names as its source (+0x2c): the thrower, or the exploding object itself
    pub by: Src,
}

/// The kind of a damage record's source (+0x2c), as the damage filters ask it: an object (its vt+0xe0 true: kegs,
/// default objects, a dead grunt) passes every source filter; the player (vt+0xe4 true, vt+0xe8 false), a teammate
/// (vt+0xe4 true) and a bad guy (grunts: vt+0xe8 true) need player_ / teammate_ / badguy_damage.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Src {
    #[default]
    Object,
    Player,
    Teammate,
    Badguy,
}

/// The damage handler class of a destructible (by its behaviour; vt+0xd0):
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DClass {
    /// no behaviour, DefaultGameObject_OnDamage 0x00450da0: takes_damage, takes_bullet_damage (both default 0)
    Default,
    /// BuildingObject 0x0040ce40: takes_damage (default 1), takes_bullet_damage (0), player / teammate (1) /
    /// badguy_damage (0)
    Building,
    /// FxExplosion_OnDamage 0x0041c4e0: player_damage (1), teammate / badguy_damage (0); no bullet test
    Keg,
    /// trees, rocket tower turrets: Object_TakeDamage 0x004555e0 with no filter
    Open,
}

/// Bounding sphere of a damage target (model space centre, radius in game units): area damage is measured to
/// its surface.
#[derive(Component, Clone, Copy)]
pub struct Bounds {
    pub center: Vec3,
    pub radius: f32,
}

/// Start an fx script by name at a place (run by fxvm.rs).
#[derive(Message, Clone)]
pub struct StartFx {
    pub name: String,
    pub at: Vec3,
    pub rotation: Quat,
    /// objects of this fx follow this entity (fx started on a moving object: projectile trails)
    pub follow: Option<Entity>,
    /// follow only its position (camera_fx position_only: the snow domes around the camera)
    pub position_only: bool,
}

/// An object that takes damage and dies with its property set's death fx (kegs, pipes, buildings, trees).
#[derive(Component, Clone)]
pub struct Destructible {
    pub health: f32,
    pub fx_death: Option<String>,
    /// Template that replaces the object when it dies (destroyed building, felled tree).
    pub husk: Option<String>,
    pub area_damage: f32,
    pub radius_inner: f32,
    pub radius_outer: f32,
    pub impact_force: f32,
    /// behavior fx_explosion: a blast that kills it only lights its fuse (max(passed delay, min_delay));
    /// when the fuse runs out it explodes and passes acceleration_rate x its fuse on. Killed any other way
    /// (shot, fuse of a bomb) it explodes at once and passes start_delay.
    pub chain: bool,
    pub start_delay: f32,
    pub min_delay: f32,
    pub acceleration: f32,
    /// Seconds until it dies once its health ran out, and the fuse it was lit with (0: killed directly).
    pub dying: Option<f32>,
    pub chain_delay: f32,
    pub class: DClass,
    pub takes_damage: bool,
    pub takes_bullet: bool,
    pub player_damage: bool,
    pub teammate_damage: bool,
    pub badguy_damage: bool,
}

impl Destructible {
    /// Whether a damage record applies: `no_buildings` is the record's 0x400 bit (a projectile without
    /// damages_buildings: the rifle's and grunts' bullets), `by` its source.
    pub fn accepts(&self, by: Src, no_buildings: bool) -> bool {
        let source = || match by {
            Src::Object => true,
            Src::Player => self.player_damage,
            Src::Teammate => self.teammate_damage,
            Src::Badguy => self.badguy_damage,
        };
        match self.class {
            DClass::Default => self.takes_damage && (!no_buildings || self.takes_bullet),
            DClass::Building => self.takes_damage && (!no_buildings || self.takes_bullet) && source(),
            DClass::Keg => source(),
            DClass::Open => true,
        }
    }
}

/// A thrown or placed bomb that goes off when its fuse runs out.
#[derive(Component)]
pub struct Fuse(pub f32);

/// Health of characters (grunts): area damage takes it, `max_health` from their property set.
#[derive(Component)]
pub struct Health(pub f32);

#[derive(Resource, Default)]
pub struct FxStats {
    pub explosions: usize,
    pub deaths: usize,
    rng: u64,
}

impl FxStats {
    pub fn rand(&mut self) -> f32 {
        self.rng = self.rng.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
        ((self.rng >> 33) as f32) / (u32::MAX >> 1) as f32
    }
    pub fn range(&mut self, lo: f32, hi: f32) -> f32 {
        lo + (hi - lo) * self.rand()
    }
}

/// The explosion systems, so other systems can run after them.
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct FxSet;

/// A death to handle: the dead object's entity (for the husk swap in level.rs).
#[derive(Message, Clone, Copy)]
pub struct Died(pub Entity);

pub struct FxPlugin;

impl Plugin for FxPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<Explode>()
            .add_message::<StartFx>()
            .add_message::<Died>()
            .insert_resource(FxStats { rng: 0x5eed, ..default() })
            .add_systems(Update, (fuses, damage, dying, tracers).chain().in_set(FxSet));
    }
}

/// Damage falloff (guess until the fx-vm doc): full inside the inner radius, linear to 0 at the outer one.
pub fn falloff(dist: f32, inner: f32, outer: f32) -> f32 {
    if dist <= inner {
        1.0
    } else if dist >= outer {
        0.0
    } else {
        1.0 - (dist - inner) / (outer - inner).max(1e-3)
    }
}

fn fuses(mut commands: Commands, time: Res<Time>, mut q: Query<(Entity, &mut Fuse, &mut Destructible)>) {
    for (e, mut f, mut d) in &mut q {
        f.0 -= time.delta_secs();
        if f.0 <= 0.0 && d.dying.is_none() {
            d.health = 0.0;
            d.chain_delay = 0.0;
            d.dying = Some(0.0);
            commands.entity(e).remove::<Fuse>();
        }
    }
}

fn damage(
    mut blasts: MessageReader<Explode>,
    mut physics: NonSendMut<Physics>,
    mut objects: Query<(Entity, &GlobalTransform, &mut Destructible, Option<&Bounds>)>,
    mut chars: Query<(Entity, &GlobalTransform, &mut Health, Option<&Bounds>)>,
) {
    // surface distance: |C - centre| - R, and the direction n
    let surface = |t: &GlobalTransform, b: Option<&Bounds>, at: Vec3| {
        let (c, r) = b.map(|b| (t.transform_point(b.center), b.radius)).unwrap_or((t.translation(), 0.0));
        ((c - at).length() - r, (c - at).normalize_or(Vec3::Y))
    };
    for b in blasts.read() {
        if b.radius_outer < 0.01 || b.radius_outer < b.radius_inner || b.area_damage == 0.0 {
            continue;
        }
        for (e, t, mut d, bounds) in &mut objects {
            if Some(e) == b.source || d.health <= 0.0 {
                continue; // a keg already at health <= 0 ignores further damage
            }
            let (dist, _) = surface(t, bounds, b.at);
            if dist > b.radius_outer || !d.accepts(b.by, false) {
                continue;
            }
            d.health -= b.area_damage * falloff(dist, b.radius_inner, b.radius_outer);
            if d.health <= 0.0 {
                if d.chain {
                    let fuse = b.delay.max(d.min_delay);
                    d.chain_delay = fuse;
                    d.dying = Some(fuse);
                } else {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
        }
        for (ce, t, mut h, bounds) in &mut chars {
            // the excluded object (a sticky bomb's stuck character takes its direct damage instead)
            if Some(ce) == b.source {
                continue;
            }
            let (dist, _) = surface(t, bounds, b.at);
            if dist <= b.radius_outer {
                h.0 -= b.area_damage * falloff(dist, b.radius_inner, b.radius_outer);
            }
        }
        // impact force: passed without falloff, direction normalise(n.x, n.y, 0.7) in game axes (z up). How
        // non-character objects turn it into an impulse is not traced: impulse = impact_force (game units)
        // scaled like the fx impulses (guess).
        let k = physics.meters_per_unit;
        let ids: Vec<_> = physics.bodies.keys().copied().collect();
        for id in ids {
            let Ok(body) = physics.world.body(id) else { continue };
            let p = crate::physics::br(body.position()) / k;
            drop(body);
            let dist = p.distance(b.at);
            if dist > b.radius_outer {
                continue;
            }
            let n = (p - b.at).normalize_or(Vec3::Y);
            let dir = Vec3::new(n.x, 0.7, n.z);
            if let Ok(mut m) = physics.world.body_mut(id) {
                m.activate();
                let _ = m.add_impulse_at_point(jv(dir.normalize() * b.impact_force * k * crate::fxvm::IMPULSE_SCALE), jr(b.at * k));
            }
        }
    }
}

#[allow(clippy::too_many_arguments)]
fn dying(
    mut commands: Commands,
    time: Res<Time>,
    mut stats: ResMut<FxStats>,
    mut q: Query<(Entity, &GlobalTransform, &mut Destructible)>,
    mut blasts: MessageWriter<Explode>,
    mut fx: MessageWriter<StartFx>,
    mut died: MessageWriter<Died>,
) {
    for (e, t, mut d) in &mut q {
        let Some(left) = d.dying.as_mut() else { continue };
        *left -= time.delta_secs();
        if *left > 0.0 {
            continue;
        }
        stats.deaths += 1;
        if d.area_damage > 0.0 {
            stats.explosions += 1;
            // killed by its fuse: acceleration_rate x the fuse; killed directly: start_delay
            let delay = if d.chain_delay > 0.0 { d.acceleration * d.chain_delay } else { d.start_delay };
            blasts.write(Explode {
                at: t.translation(),
                area_damage: d.area_damage,
                radius_inner: d.radius_inner,
                radius_outer: d.radius_outer,
                impact_force: d.impact_force,
                source: Some(e),
                delay,
                // the keg / object itself is the record's source (FxExplosion_Die 0x0041c570, 0x00450ee0)
                by: Src::Object,
            });
        }
        if let Some(name) = &d.fx_death {
            let (_, rot, _) = t.to_scale_rotation_translation();
            fx.write(StartFx { name: name.clone(), at: t.translation(), rotation: rot, follow: None, position_only: false });
        }
        d.dying = None;
        d.health = f32::INFINITY;
        commands.entity(e).remove::<Destructible>();
        died.write(Died(e));
    }
}

#[derive(Component)]
pub struct Tracer {
    pub from: Vec3,
    pub to: Vec3,
    pub age: f32,
}

fn tracers(mut commands: Commands, time: Res<Time>, mut gizmos: Gizmos, mut q: Query<(Entity, &mut Tracer)>) {
    for (e, mut t) in &mut q {
        t.age += time.delta_secs();
        let a = 1.0 - t.age / 0.12;
        if a <= 0.0 {
            commands.entity(e).despawn();
            continue;
        }
        gizmos.line(t.from, t.to, Color::srgba(1.0, 0.9, 0.5, a));
    }
}
