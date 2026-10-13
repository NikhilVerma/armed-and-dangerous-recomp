//! Area damage and object death, from the game's property sets: `max_health`, `area_damage`,
//! `damage_radius_inner/outer`, `impact_force`, `fx_death`, `husk`, and the chain delays of exploding props
//! (`start_delay`, `min_delay`, `acceleration_rate`). The death fx itself runs in the fx VM (fxvm.rs).
//! Formulas marked "guess" wait for docs/subsystems/fx-vm.md.

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
}

/// Start an fx script by name at a place (run by fxvm.rs).
#[derive(Message, Clone)]
pub struct StartFx {
    pub name: String,
    pub at: Vec3,
    pub rotation: Quat,
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
    /// Chain timing of exploding props (guess at the meaning: the first death in a chain waits start_delay,
    /// each later link waits acceleration_rate times the previous delay, never below min_delay).
    pub start_delay: f32,
    pub min_delay: f32,
    pub acceleration: f32,
    /// Seconds until it dies, once its health ran out; chain link delay it was killed with.
    pub dying: Option<f32>,
    pub chain_delay: f32,
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
            d.dying = Some(0.0);
            commands.entity(e).remove::<Fuse>();
        }
    }
}

fn damage(
    mut blasts: MessageReader<Explode>,
    mut physics: NonSendMut<Physics>,
    mut objects: Query<(Entity, &GlobalTransform, &mut Destructible)>,
    mut chars: Query<(&GlobalTransform, &mut Health)>,
) {
    for b in blasts.read() {
        // the source's own chain delay sets its neighbours' (guess, see Destructible)
        let src_delay = b.source.and_then(|s| objects.get(s).ok().map(|(_, _, d)| d.chain_delay));
        for (e, t, mut d) in &mut objects {
            if Some(e) == b.source || d.dying.is_some() {
                continue;
            }
            let f = falloff(t.translation().distance(b.at), b.radius_inner, b.radius_outer);
            if f <= 0.0 {
                continue;
            }
            d.health -= b.area_damage * f;
            if d.health <= 0.0 {
                let delay = match src_delay {
                    Some(prev) if prev > 0.0 => (prev * d.acceleration).max(d.min_delay),
                    _ => d.start_delay,
                };
                d.chain_delay = delay;
                d.dying = Some(delay);
            }
        }
        for (t, mut h) in &mut chars {
            h.0 -= b.area_damage * falloff(t.translation().distance(b.at), b.radius_inner, b.radius_outer);
        }
        // impact force on everything that moves (guess: impulse = impact_force x falloff, game mass units)
        let k = physics.meters_per_unit;
        let ids: Vec<_> = physics.bodies.keys().copied().collect();
        for id in ids {
            let Ok(body) = physics.world.body(id) else { continue };
            let p = crate::physics::br(body.position()) / k;
            drop(body);
            let f = falloff(p.distance(b.at), b.radius_inner, b.radius_outer);
            if f <= 0.0 {
                continue;
            }
            let dir = (p - b.at).normalize_or(Vec3::Y);
            if let Ok(mut m) = physics.world.body_mut(id) {
                m.activate();
                let _ = m.add_impulse_at_point(jv(dir * b.impact_force * f * k * k), jr(b.at * k));
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
            blasts.write(Explode {
                at: t.translation(),
                area_damage: d.area_damage,
                radius_inner: d.radius_inner,
                radius_outer: d.radius_outer,
                impact_force: d.impact_force,
                source: Some(e),
            });
        }
        if let Some(name) = &d.fx_death {
            let (_, rot, _) = t.to_scale_rotation_translation();
            fx.write(StartFx { name: name.clone(), at: t.translation(), rotation: rot });
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
