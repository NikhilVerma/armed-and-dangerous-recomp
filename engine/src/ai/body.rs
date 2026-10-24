//! The AI character's body (Object_BuildRadiusCollision 0x004527d0 on an AI object): a dynamic sphere of the
//! property `collision_radius` sitting on the feet (centre (0, 0, radius)), moving without turning, with the
//! character's contact rules (contacts.rs):
//!
//! - friction: the material's (Object_ApplyMaterialFriction 0x0042cb80: the property `friction`, else the material's,
//!   default 1.0) against the statics' 1.0, combined sqrt(fA * fB) (0x00413240); 0 while the character is in a squad
//!   (joining one, 0x0042de60 / 0x0042de30 / state 0x79/0xdd's way back, ai_06.cpp:2650: Body_SetFriction 0), the
//!   material's again when it leaves it (Object_ReleaseHolder 0x0042cc10: a charge, a death);
//! - group: a squad's members never touch each other (0x0042de60 sets +0x510 to the squad tag; GenericAIObject_Slot40
//!   0x00432590 refuses an object of the same +0x510); kept after a charge (0x0042cc10 does not clear it);
//! - ignore: a character on its way somewhere (a route, +0x428) that bumps into a mover (0x004531e0: a dynamic body) passes
//!   through it from then on and leaves its group (GenericAIObject_Slot44 0x004320b0 -> 0x004324d0 (0, self, other,
//!   1): +0x510 = 0, +0x50c = the other); a marker walker (0x96 / 0x97) re-plans instead (`bumps`).
//!
//! The landing of a flight reads the body's contacts (`touched`): the original's contact note (Slot44) records any
//! contact of the step, and a falling jumper with one lands (0x0042e1c0).
//!
//! Bullets do not hit this sphere: the original's projectile ray meets a character's collision model (Projectile_Update
//! 0x0047e580, hitparts.rs), so the body carries a hit volume round the model (`Physics::hit_volumes`) and the sphere is
//! left out of the bullets' rays.

use super::{Grunt, Mind};
use crate::contacts::ContactRule;
use crate::physics::{Body, HitVolume, Physics};
use bevy::prelude::*;
use oxijolt::prelude::*;

/// The game's default `collision_radius` for a character without one (the grunt's).
pub const DEFAULT_RADIUS: f32 = 4.0;

/// The character's body: its sphere and the contact rule parts the AI changes.
#[derive(Component, Clone, Copy, Debug)]
pub struct CharacterBody {
    pub radius: f32,
    /// the material's friction (0x0042cb80), the body's own outside a squad
    pub friction: f32,
    /// +0x510: the squad whose members it passes through
    pub group: Option<u32>,
    /// +0x50c: the one body it passes through
    pub ignore: Option<BodyId>,
}

impl CharacterBody {
    pub fn new(p: &crate::gamedata::Props) -> Self {
        CharacterBody { radius: p.f("collision_radius", DEFAULT_RADIUS).max(0.5), friction: p.f("friction", 1.0), group: None, ignore: None }
    }
}

/// Height of the sphere's centre above the feet: the sphere sits on them (0x004527d0: centre (0, 0, radius)).
pub fn lift(radius: f32) -> f32 {
    radius.max(0.5)
}

/// The hit volume of a character's collision model (grunt_coll_9: from the shins to the head, about 10 units tall) round
/// the body's centre: a capsule of 0.55 x the radius (at most 4.5) from 0.3 to 10.1 above the feet.
fn hit_volume(radius: f32) -> HitVolume {
    let r = radius.min(4.5) * 0.55;
    let l = lift(radius);
    HitVolume { radius: r, bottom: 0.3 + r - l, top: 10.1 - r - l }
}

/// A character's body for entity `e` with its feet at `feet`: the dynamic sphere (translation only, 80 kg), its game
/// material, listed with the AI bodies the AI's sight passes through (`Physics::ai_bodies`), with its hit volume and its
/// contact rule. The caller inserts the returned components.
pub fn add(physics: &mut Physics, e: Entity, character: CharacterBody, feet: Vec3, material: &str) -> Option<(Body, CharacterBody)> {
    let k = physics.meters_per_unit;
    let shape = Shape::new_sphere(character.radius * k).ok()?;
    // friction 1: the rule's friction scales the combination (contacts.rs)
    let s = BodySettings::new_dynamic()
        .mass(80.0)
        .friction(1.0)
        .allowed_dofs(AllowedDofs::TRANSLATION_X | AllowedDofs::TRANSLATION_Y | AllowedDofs::TRANSLATION_Z);
    let offset = Vec3::Y * lift(character.radius);
    let id = physics.add_dynamic(e, &shape, feet + offset, Quat::IDENTITY, Vec3::ZERO, s)?;
    physics.materials.insert(id, material.to_owned());
    physics.ai_bodies.insert(id);
    physics.no_bullets.insert(id);
    physics.hit_volumes.insert(id, hit_volume(character.radius));
    physics.set_contact_rule(id, ContactRule { friction: character.friction, ..Default::default() });
    Some((Body { id, offset, sync_rotation: false }, character))
}

/// The contact rule of each character from its state: friction 0 in a squad, its group and the body it ignores.
pub fn apply_rules(mut physics: NonSendMut<Physics>, q: Query<(&Body, &CharacterBody, Option<&Grunt>)>) {
    for (body, c, g) in &q {
        let in_squad = g.is_some_and(|g| g.squad.is_some());
        let rule = ContactRule { group: c.group, ignore: c.ignore, friction: if in_squad { 0.0 } else { c.friction }, ..Default::default() };
        physics.set_contact_rule(body.id, rule);
    }
}

/// GenericAIObject_Slot44 0x004320b0's bump (ai_04.cpp:742-760): a character walking somewhere that touched a mover
/// passes through that one from now on and leaves its group. A mover (0x004531e0: +0x424 bit 0x40, or a mass from
/// vt+0x80) is any dynamic body: another character's, a ragdoll, a vehicle, a loose prop; the level and the static
/// props are not. A marker walker (states 0x96 / 0x97) re-plans instead: within 10 of his route's end the route is
/// dropped (0x0046f440: the straight walk), else it is planned again (0x0046f480, at most every 0.5 s).
pub fn bumps(physics: NonSend<Physics>, mut q: Query<(&Body, &mut CharacterBody, &mut Grunt, &GlobalTransform)>) {
    for (body, mut c, mut g, gt) in &mut q {
        if !g.walking || g.mind == Mind::Dead || g.fixed.is_some() {
            continue;
        }
        let mover = |id: BodyId| physics.ai_bodies.contains(&id) || physics.world.body(id).is_ok_and(|b| b.motion_type() == MotionType::Dynamic);
        let Some(t) = physics.touches(body.id).into_iter().find(|t| mover(t.other)) else { continue };
        if g.mind == Mind::ToMarker {
            let pos = gt.translation();
            match g.route.last() {
                Some(end) if (*end - pos).with_y(0.0).length_squared() < 100.0 => g.route.clear(),
                // (a plan sets the clock to 1 s: half of it gone)
                Some(_) if g.replan <= 0.5 => g.replan = 0.0,
                _ => {}
            }
            continue;
        }
        if c.ignore != Some(t.other) || c.group.is_some() {
            c.ignore = Some(t.other);
            c.group = None;
        }
    }
}

/// The turn about the vertical toward a heading `off` radians away (0x00454e60, ai_07.cpp:1830-1873): the wanted yaw
/// rate is `rate` x the cross product of heading and direction (sin off), or `rate` whole when the direction is behind
/// (dot < -0.98, K 0x005bcd38); the original hands that rate to a body constraint, here it is the turn itself, never
/// past the heading. The callers' rates (rad/s): 16 for the characters' turns, 8 for a companion turning to Roman's
/// heading, 4 for the goliath. Returns the yaw step and whether the object faces the heading (dot >= 0.98, K
/// 0x005bcd30: within about 11.5 degrees), which the original's callers wait for.
pub fn turn_toward(off: f32, rate: f32, dt: f32) -> (f32, bool) {
    let wanted = if off.cos() < -0.98 { rate * off.signum() } else { rate * off.sin() };
    let step = wanted * dt;
    let step = if step.abs() > off.abs() { off } else { step };
    (step, off.cos() >= 0.98)
}

/// Whether the body touched anything in the last physics step (the contact note a flight's landing reads, 0x004320b0).
pub fn touched(physics: &Physics, body: BodyId) -> bool {
    !physics.touches(body).is_empty()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x00454e60: proportional to sin(off), never past the heading; faced within acos(0.98).
    #[test]
    fn turn_eases_in_and_reports_facing() {
        let (s, faced) = turn_toward(1.0, 16.0, 1.0 / 60.0);
        assert!((s - 16.0 * 1f32.sin() / 60.0).abs() < 1e-6 && !faced);
        let (s, faced) = turn_toward(0.01, 16.0, 1.0);
        assert_eq!(s, 0.01);
        assert!(faced);
        assert!(!turn_toward(0.25, 4.0, 0.0).1 && turn_toward(0.19, 4.0, 0.0).1);
        // behind: the whole rate
        let (s, _) = turn_toward(-3.1, 4.0, 0.1);
        assert!((s + 0.4).abs() < 1e-6);
    }
}
