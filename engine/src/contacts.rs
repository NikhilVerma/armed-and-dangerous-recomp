//! Per-body contact rules and contact notes, the game's side of the solver's contacts (game-neutral).
//!
//! The original hands Havok two things per pair of bodies (docs/subsystems/havok-layouts.md):
//! - whether they collide at all: CollisionFilter_IsEnabled 0x00411710 asks each owner's virtual +0xa0 about the other
//!   (GenericAIObject_Slot40 0x00432590 for the AI characters: never the one object it was told to ignore, +0x50c, and
//!   never an object of its own group, +0x510); here a [`ContactRule`]'s `ignore` and `group`;
//! - how they rub: CollisionListener_OnContactAdded 0x00413240 combines the two bodies' frictions as sqrt(fA * fB)
//!   (Jolt's own combination); a body's friction can change at run time (Body_SetFriction 0x00458250), which a Jolt body
//!   cannot, so a ruled body is created with friction 1 and its rule's `friction` scales the combination.
//!
//! And it tells each owner of every contact of the step (CollisionListener_OnContact 0x004112d0 -> the owner's contact note,
//! GenericAIObject_Slot44 0x004320b0, once per step and contact): [`Contacts::touches`] keeps the last step's per ruled
//! body (`clear` before each step).

use oxijolt::prelude::*;
use oxijolt::{ContactCandidate, ContactManifold, ContactSettings, RVec3, ValidateResult};
use std::collections::{HashMap, HashSet};
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
}

impl Default for ContactRule {
    fn default() -> Self {
        ContactRule { group: None, ignore: None, friction: 1.0 }
    }
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
    /// bodies whose contacts are noted but never resolved: the owner's collide callback answers 0
    /// (DefaultGameObject_Collide 0x00450af0 with on_hit_death: the pane a body passes through as it breaks it)
    pass_through: RwLock<HashSet<BodyId>>,
}

impl Contacts {
    /// Every contact of `body` is noted (it needs a rule for that) and none is resolved: the other body passes through
    /// (Jolt's sensor contact).
    pub fn set_pass_through(&self, body: BodyId) {
        if let Ok(mut set) = self.pass_through.write() {
            set.insert(body);
        }
    }

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
        if let Ok(mut set) = self.pass_through.write() {
            set.remove(&body);
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
        if self.pass_through.read().is_ok_and(|s| s.contains(&a) || s.contains(&b)) {
            let _ = settings.set_is_sensor(true);
        }
        // sqrt(fA * fB): Jolt combined the creation frictions (1 for a ruled body) the same way
        let scale = ra.map_or(1.0, |r| r.friction.max(0.0).sqrt()) * rb.map_or(1.0, |r| r.friction.max(0.0).sqrt());
        if scale != 1.0 {
            let _ = settings.set_combined_friction(settings.combined_friction() * scale);
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
}
