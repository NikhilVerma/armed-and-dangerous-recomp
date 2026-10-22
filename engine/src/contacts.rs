//! Per-body contact rules and contact notes, the game's side of the solver's contacts (game-neutral).
//!
//! The original hands Havok two things per pair of bodies (docs/subsystems/havok-layouts.md):
//! - whether they collide at all: CollisionFilter_IsEnabled 0x00411710 asks each owner's virtual +0xa0 about the other
//!   (GenericAIObject_Slot40 0x00432590 for the AI characters: never the one object it was told to ignore, +0x50c, and
//!   never an object of its own group, +0x510); here a [`ContactRule`]'s `ignore` and `group`;
//! - how they rub: CollisionListener_OnContactAdded 0x00413240 combines the two bodies' frictions as sqrt(fA * fB)
//!   (Jolt's own combination); a body's friction can change at run time (Body_SetFriction 0x00458250), which a Jolt body
//!   cannot, so a ruled body is created with friction 1 and its rule's `friction` scales the combination;
//! - how they bounce: the same listener combines restitutions as sqrt(rA * rB) where Jolt takes the larger. A rule with
//!   a `restitution` (fx pieces, movables: their templates' values) gets the game's combination against any body; the
//!   other side's value is its rule's, else the level material's ([`LEVEL_RESTITUTION`]).
//!
//! And it tells each owner of every contact of the step (CollisionListener_OnContact 0x004112d0 -> the owner's contact note,
//! GenericAIObject_Slot44 0x004320b0, once per step and contact): [`Contacts::touches`] keeps the last step's per ruled
//! body (`clear` before each step).

use oxijolt::prelude::*;
use oxijolt::{ContactCandidate, ContactManifold, ContactSettings, RVec3, ValidateResult};
use std::collections::HashMap;
use std::sync::{Mutex, RwLock};

/// How one body takes contacts with the others.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ContactRule {
    /// Bodies sharing a group never touch (the AI's +0x510: a squad's members, 0x0042de60).
    pub group: Option<u32>,
    /// The one body this body passes through (the AI's +0x50c: a mover it bumped into, its turret).
    pub ignore: Option<BodyId>,
    /// The body's friction, combined with the other's as sqrt(fA * fB).
    pub friction: f32,
    /// The body's restitution, combined with the other's as sqrt(rA * rB) (0x00413240); None leaves the contact's
    /// restitution to the solver.
    pub restitution: Option<f32>,
}

impl Default for ContactRule {
    fn default() -> Self {
        ContactRule { group: None, ignore: None, friction: 1.0, restitution: None }
    }
}

/// The restitution of a body that states none, chiefly the level's static geometry: ContactSide_Fill 0x00413120 takes
/// it from the triangle's collision material, which PhysMaterial_Load 0x00412140 fills from global_settings'
/// "default_restitution" (0.5 on every shipped level; wood's 1.0 etc. are not modelled, as for friction in physics.rs).
pub const LEVEL_RESTITUTION: f32 = 0.5;

/// The game's restitution of a contact (CollisionListener_OnContactAdded 0x00413240: sqrt(rB * rA)), kept within the
/// solver's 0..1 (the shipped "flesh"/"grunt" materials' 8.0 would give more).
pub fn combined_restitution(a: f32, b: f32) -> f32 {
    (a.max(0.0) * b.max(0.0)).sqrt().min(1.0)
}

impl ContactRule {
    /// Whether this body refuses `other` (the owner veto of 0x00411710: each side is asked).
    fn refuses(&self, other: BodyId, other_rule: Option<&ContactRule>) -> bool {
        self.ignore == Some(other) || (self.group.is_some() && other_rule.is_some_and(|o| o.group == self.group))
    }
}

/// One contact a ruled body made in a step: the other body, the contact normal pointing out of the other body
/// (towards this one), in engine axes, and the contact point on the other body's surface (the mean of the manifold's
/// points; the solver's world space, metres: `Physics::from_m` gives engine units).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Touch {
    pub other: BodyId,
    pub normal: bevy::math::Vec3,
    pub point: RVec3,
}

/// The rules and the contact notes, shared with the solver's worker threads.
#[derive(Default)]
pub struct Contacts {
    rules: RwLock<HashMap<BodyId, ContactRule>>,
    touches: Mutex<HashMap<BodyId, Vec<Touch>>>,
}

impl Contacts {
    /// Sets a body's rule; returns whether it changed (the caller then drops the body's cached contacts, which Jolt would
    /// otherwise keep with their old answer).
    pub fn set_rule(&self, body: BodyId, rule: ContactRule) -> bool {
        let Ok(mut rules) = self.rules.write() else { return false };
        rules.insert(body, rule) != Some(rule)
    }

    pub fn remove(&self, body: BodyId) {
        if let Ok(mut rules) = self.rules.write() {
            rules.remove(&body);
        }
        if let Ok(mut touches) = self.touches.lock() {
            touches.remove(&body);
        }
    }

    /// The contacts `body` made in the last step, by the other body's id (a stable order).
    pub fn touches(&self, body: BodyId) -> Vec<Touch> {
        let mut t = self.touches.lock().ok().and_then(|m| m.get(&body).cloned()).unwrap_or_default();
        t.sort_by_key(|t| t.other.index());
        t
    }

    /// Drops the contacts `body` made so far (a flight's take-off: only the steps after it count).
    pub fn forget(&self, body: BodyId) {
        if let Ok(mut touches) = self.touches.lock() {
            touches.remove(&body);
        }
    }

    pub fn clear(&self) {
        if let Ok(mut touches) = self.touches.lock() {
            touches.values_mut().for_each(Vec::clear);
        }
    }

    fn note(&self, manifold: &ContactManifold, settings: &mut ContactSettings) {
        let (a, b) = (manifold.pair.body1, manifold.pair.body2);
        let (ra, rb) = {
            let Ok(rules) = self.rules.read() else { return };
            (rules.get(&a).copied(), rules.get(&b).copied())
        };
        if ra.is_none() && rb.is_none() {
            return;
        }
        // sqrt(fA * fB): Jolt combined the creation frictions (1 for a ruled body) the same way
        let scale = ra.map_or(1.0, |r| r.friction.max(0.0).sqrt()) * rb.map_or(1.0, |r| r.friction.max(0.0).sqrt());
        if scale != 1.0 {
            let _ = settings.set_combined_friction(settings.combined_friction() * scale);
        }
        let (rest_a, rest_b) = (ra.and_then(|r| r.restitution), rb.and_then(|r| r.restitution));
        if rest_a.is_some() || rest_b.is_some() {
            let r = combined_restitution(rest_a.unwrap_or(LEVEL_RESTITUTION), rest_b.unwrap_or(LEVEL_RESTITUTION));
            let _ = settings.set_combined_restitution(r);
        }
        let n = bevy::math::Vec3::new(manifold.normal.x, manifold.normal.y, manifold.normal.z);
        // the contact point on each side: the mean of the manifold's points
        let mean = |on: fn(&oxijolt::ContactPoint) -> RVec3| {
            let k = manifold.points.len().max(1) as f64;
            let s = manifold.points.iter().map(on).fold((0.0, 0.0, 0.0), |s, p| (s.0 + p.x as f64, s.1 + p.y as f64, s.2 + p.z as f64));
            RVec3::new((s.0 / k) as _, (s.1 / k) as _, (s.2 / k) as _)
        };
        let (on1, on2) = (mean(|p| p.point_on1), mean(|p| p.point_on2));
        if let Ok(mut touches) = self.touches.lock() {
            // the manifold's normal is the one body 2 moves out of body 1 along
            if ra.is_some() {
                touches.entry(a).or_default().push(Touch { other: b, normal: -n, point: on2 });
            }
            if rb.is_some() {
                touches.entry(b).or_default().push(Touch { other: a, normal: n, point: on1 });
            }
        }
    }
}

/// The solver's listener over [`Contacts`].
pub struct ContactListenerImpl(pub std::sync::Arc<Contacts>);

impl ContactListener for ContactListenerImpl {
    fn contact_validate(&self, c: &ContactCandidate) -> ValidateResult {
        let Ok(rules) = self.0.rules.read() else { return ValidateResult::AcceptAllContactsForThisBodyPair };
        let (ra, rb) = (rules.get(&c.body1), rules.get(&c.body2));
        let refused = ra.is_some_and(|r| r.refuses(c.body2, rb)) || rb.is_some_and(|r| r.refuses(c.body1, ra));
        if refused {
            ValidateResult::RejectAllContactsForThisBodyPair
        } else {
            ValidateResult::AcceptAllContactsForThisBodyPair
        }
    }

    fn contact_added(&self, manifold: &ContactManifold, settings: &mut ContactSettings) {
        self.0.note(manifold, settings);
    }

    fn contact_persisted(&self, manifold: &ContactManifold, settings: &mut ContactSettings) {
        self.0.note(manifold, settings);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_group_and_an_ignored_body_are_refused() {
        let mut world = PhysicsWorld::new(WorldSettings::default()).expect("world");
        let shape = Shape::new_sphere(1.0).expect("sphere");
        let ids: Vec<BodyId> = (0..3).map(|_| world.create_body(&shape, &BodySettings::new_static()).expect("body")).collect();
        let squad = ContactRule { group: Some(7), ..Default::default() };
        assert!(squad.refuses(ids[1], Some(&squad)));
        assert!(!squad.refuses(ids[1], Some(&ContactRule::default())));
        assert!(!squad.refuses(ids[1], None));
        let bumped = ContactRule { ignore: Some(ids[2]), ..Default::default() };
        assert!(bumped.refuses(ids[2], None));
        assert!(!bumped.refuses(ids[1], None));
    }

    /// An fx piece (0.2) on the level (0.5) bounces with sqrt(0.1), not the solver's max 0.5; a dead piece (0) does
    /// not bounce at all; two flesh materials (8.0) stay within the solver's range.
    #[test]
    fn restitution_is_the_geometric_mean() {
        assert!((combined_restitution(0.2, LEVEL_RESTITUTION) - 0.1f32.sqrt()).abs() < 1e-6);
        assert_eq!(combined_restitution(0.0, LEVEL_RESTITUTION), 0.0);
        assert_eq!(combined_restitution(8.0, 8.0), 1.0);
    }
}
