//! The HUD's target passes (Hud_UpdateCrosshair 0x00406f50, ui_01.cpp:297-510, with the narrowed view volumes of
//! HudFrustum_Build 0x00408390): which characters a weapon picks by itself.
//!
//! Each pass walks the candidates nearest the eye first (the list 0x00408190, sorted from the player's position + 4
//! (0x004067e0); at most 21 looked at), keeps those inside the view volume narrowed by the pass's factors (clip-space
//! scales of the side planes: |x_ndc| <= horz, |y_ndc| <= vert, with the object's bounding-sphere slack: 0x00408050),
//! in front of the eye, within the weapon's range (0x00408330: the first one out of range ends the walk) and in line
//! of sight (0x00414110 from the eye, [`in_hud_sight`]): the first `max` of them.
//!
//! | pass | when | factors (p_globalsettings) | max | range |
//! |---|---|---|---|---|
//! | auto-aim | rifle / machine gun with the Auto-Aim option, not zoomed, difficulty <= normal (0x004a2a40) | autoaim_factor_horz / _vert | 1 | the gun's |
//! | shark gun | always with the shark gun (vt+0x16c 0x00433fa0; hud +0x150): AIs that can be bitten only | autoaim_sharkgun_factor_* (0.4 / 0.6) | 1 | sharkgun_range |
//! | multi-lock | always with the vindaloo (hud +0x152): up to four lock-ons | autoaim_multitarget_factor_* (x 1.2 upgraded) | 4 | vindaloo_range |
//! | sticky | the sticky bomb selected (hud +0x151; sticky.rs at the throw) | autoaim_sticky_factor_* | 1 | sticky_bomb_range |
//!
//! In the base turret (Game+0xac8 = 1) the gun in hand's pass becomes a multi-lock (+0x152 forced, ui_01.cpp:324-325)
//! through autoaim_base_factor_* (0.2 / 0.3, x 1.2 with the vindaloo upgrade Game+0xdd0, HudFrustum_Build
//! 0x00408390) with no range limit (0x00408330 answers true in any turret mode); on a mounted gun (mode 4) there is
//! no pass.
//!
//! [`Targets`] holds this frame's result for the gun in hand (hud +0 / +0x154); the HUD draws the lock-on markers from
//! it, the shark gun fires only with a target, the vindaloo's rockets home on the lock-ons and the auto-aimed bullets
//! fly to the target's point.

use super::{VindalooUpgrade, WeaponId};
use crate::physics::Physics;
use bevy::prelude::*;

/// A picked target: the character and the point a shot aims at (its ref_hit_00, else above its origin), with the joint
/// when it has one (homing follows the joint).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Target {
    pub entity: Entity,
    pub point: Vec3,
    pub joint: Option<Entity>,
}

/// This frame's targets of the gun in hand (hud +0: one; +0x154: up to four).
#[derive(Resource, Default, Debug)]
pub struct Targets {
    pub list: Vec<Target>,
    /// the pass that made them (for the HUD and the log)
    pub pass: Option<Pass>,
}

impl Targets {
    pub fn first(&self) -> Option<Target> {
        self.list.first().copied()
    }
}

/// Which pass ran.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Pass {
    AutoAim,
    Shark,
    MultiLock,
}

/// The view a pass looks through: the camera and the eye the rays start from.
#[derive(Clone, Copy, Debug)]
pub struct View {
    pub cam: Vec3,
    pub fwd: Vec3,
    pub right: Vec3,
    pub up: Vec3,
    /// tan of the half field of view, vertical and horizontal
    pub half_v: f32,
    pub half_h: f32,
    /// the player's position + 4 (0x004067e0): the sort origin and the line of sight's start
    pub eye: Vec3,
}

impl View {
    /// From the camera's transform and projection (the horizontal half-angle from the original's 4:3 view at its
    /// 1.7204 aspect factor, as sticky.rs measured).
    pub fn new(cam: &GlobalTransform, proj: Option<&Projection>, eye: Vec3) -> Self {
        let half_v = match proj {
            Some(Projection::Perspective(pp)) => (pp.fov * 0.5).tan(),
            _ => (0.654f32 * 0.5).tan(),
        };
        View { cam: cam.translation(), fwd: cam.forward().as_vec3(), right: cam.right().as_vec3(), up: cam.up().as_vec3(), half_v, half_h: half_v * 1.7204, eye }
    }

    /// Inside the narrowed view volume (0x00408050 against the planes of 0x00408390): the centre within the side
    /// planes scaled by (horz, vert), with `r` of slack.
    pub fn contains(&self, centre: Vec3, r: f32, (fh, fv): (f32, f32)) -> bool {
        let v = centre - self.cam;
        let z = v.dot(self.fwd);
        let (x, y) = (v.dot(self.right), v.dot(self.up));
        let (th, tv) = (self.half_h * fh, self.half_v * fv);
        x.abs() - th * z <= r * (1.0 + th * th).sqrt() && y.abs() - tv * z <= r * (1.0 + tv * tv).sqrt()
    }
}

/// One candidate: a live character, its bounds' centre (origin + 4.9 for a grunt) and its hit point.
#[derive(Clone, Copy, Debug)]
pub struct Candidate {
    pub entity: Entity,
    pub dead: bool,
    /// its origin (the list's sort key, 0x00408100)
    pub origin: Vec3,
    pub centre: Vec3,
    pub hit: Vec3,
    pub joint: Option<Entity>,
}

/// The bounding-sphere slack of a grunt (grunt_1's model: (half diagonal + smallest half extent) / 2, sticky.rs).
pub const SLACK: f32 = 4.66;

/// The characters: what the HUD passes' line of sight goes through (none is in the sight list, [`in_hud_sight`]).
pub type Characters<'w, 's> = Query<'w, 's, (), Or<(With<crate::player::Roman>, With<crate::ai::Grunt>, With<crate::ai::Companion>, With<crate::peasant::Peasant>)>>;

/// A pass over the candidates (nearest the eye first): see the module docs. `passable` says which bodies the line
/// of sight passes (the characters).
pub fn search(view: &View, mut cands: Vec<Candidate>, factors: (f32, f32), range: f32, max: usize, physics: &Physics, passable: impl Fn(Entity) -> bool) -> Vec<Target> {
    // the dead are left out before the 21 are counted: the original's list (+0xe8c) loses a character when he goes
    // down (GoRagdoll 0x00431310 unlinks him) and its bodies are gone in 5 s, while the remaster keeps up to 48 bodies
    // (corpses.rs): after a fight the nearest 21 were all bodies and the sticky bomb found no one to home on (the
    // player's report, 2026-10-18; playtest sticky_homing_bodies)
    cands.retain(|c| !c.dead);
    cands.sort_by(|a, b| a.origin.distance(view.eye).total_cmp(&b.origin.distance(view.eye)));
    let mut out = Vec::new();
    for c in cands.into_iter().take(21) {
        if !view.contains(c.centre, SLACK, factors) {
            continue;
        }
        let to = c.hit - view.eye;
        let dist = to.length();
        if to.dot(view.fwd) < 0.0 {
            continue;
        }
        // the range test 0x00408330: the first out of range ends the walk (a range of 0 fails every one)
        if dist > range || range <= 0.0 {
            break;
        }
        if !in_hud_sight(physics, view.eye, c.hit, c.entity, &passable) {
            continue;
        }
        out.push(Target { entity: c.entity, point: c.hit, joint: c.joint });
        if out.len() >= max {
            break;
        }
    }
    out
}

/// The line of sight 0x00414110 from `from` to the target (distance + 10): it must reach the target's body first;
/// the bodies `passable` names are passed through, anything else blocks.
pub fn in_sight(physics: &Physics, from: Vec3, to: Vec3, target: Entity, passable: &impl Fn(Entity) -> bool) -> bool {
    sight(physics, from, to, target, passable, false)
}

/// The HUD passes' line of sight (Hud_UpdateCrosshair 0x00406f50, ui_01.cpp:406-411): 0x00414110 with the sight list
/// World+0xea0 as its include list, the tested character added for the cast (vt+0x14 / vt+0x28), no level list and
/// flag +0x3c set, so the ray collector (0x00413a90) takes only the landscape (no owner), the sight list's objects
/// (buildings, trees, kegs and the default objects that take damage or block projectiles: the bullets' list, level.rs
/// `collider_lists`) and the target. Characters (`passable`) and the objects outside the list (invisible walls, plain
/// rocks) let it through.
pub fn in_hud_sight(physics: &Physics, from: Vec3, to: Vec3, target: Entity, passable: &impl Fn(Entity) -> bool) -> bool {
    sight(physics, from, to, target, passable, true)
}

fn sight(physics: &Physics, from: Vec3, to: Vec3, target: Entity, passable: &impl Fn(Entity) -> bool, sight_list: bool) -> bool {
    let d = to - from;
    let dist = d.length();
    let dir = d / dist.max(1e-3);
    let (mut at, mut left) = (from, dist + 10.0);
    while let Some((b, hit, _)) = physics.raycast(at, dir, left) {
        let owner = physics.bodies.get(&b).copied();
        if owner == Some(target) {
            return true;
        }
        let unlisted = sight_list && physics.landscape != Some(b) && physics.no_bullets.contains(&physics.main_body(b));
        if owner.is_some_and(&passable) || unlisted {
            left -= hit.distance(at) + 0.05;
            at = hit + dir * 0.05;
            if left <= 0.0 {
                return false;
            }
            continue;
        }
        return false;
    }
    false
}

/// The candidates: the grunt-class characters `keep` takes (by entity, character and feet), with their ref_hit_00.
pub fn candidates(grunts: &Query<(Entity, &crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>, joints: &Query<&GlobalTransform>, keep: impl Fn(Entity, &crate::ai::Grunt, Vec3) -> bool) -> Vec<Candidate> {
    grunts
        .iter()
        .filter(|(e, g, t, _)| keep(*e, g, t.translation()))
        .map(|(e, g, t, a)| {
            let origin = t.translation();
            let joint = a.and_then(|a| a.skeleton.index.get("ref_hit_00").map(|&i| a.joints[i]));
            let hit = joint.and_then(|j| joints.get(j).ok()).map_or(origin + Vec3::Y * 6.0, |j| j.translation());
            Candidate { entity: e, dead: g.mind == crate::ai::Mind::Dead, origin, centre: origin + Vec3::Y * 4.9, hit, joint }
        })
        .collect()
}

/// GruntObject_Slot147 0x00434e10, vt+0x24c of the grunt, the archer, the captain and the sniper captain (the other
/// classes' slot answers false, 0x00435250): whether the land shark can take this character now. The shark gun's pass
/// (Hud_UpdateCrosshair 0x00406f50) and the shark's own search (0x0047fa70) ask it. Not when can_be_sharked is clear
/// (+0x48c 0x100) or he is picked up (+0x48c 2); not held by a companion for a close kill (state 0xf), thrown by the
/// Topsy Turvy bomb (0xe6..0xeb, its hang timer +0x878), in the knockout bomb's glove (0x19a / 0x19b), held by anything
/// (+0x870: a black hole's carry-off), in an IC move (0x00426630); and, unless he is on a mounted gun (+0x484 0x8000),
/// only standing on the landscape (0x00454be0: not on a building, a roof or a window's floor). `held` is the caller's
/// answer for the components that hold him (black hole, Topsy Turvy, glove).
pub fn can_be_bitten_now(g: &crate::ai::Grunt, held: bool, physics: &Physics, feet: Vec3) -> bool {
    can_be_bitten(g) && !held && (g.gun.is_some_and(|gn| gn.phase == crate::ai::GunPhase::On) || on_landscape(physics, feet))
}

/// The character's own part of [`can_be_bitten_now`]: its class, can_be_sharked, and not dead, held or scripted.
pub fn can_be_bitten(g: &crate::ai::Grunt) -> bool {
    use crate::ai::{Kind, Mind};
    matches!(g.kind, Kind::Grunt | Kind::Bow | Kind::Captain | Kind::Sniper) && g.can_be_sharked && !matches!(g.mind, Mind::Dead | Mind::Held | Mind::Scripted)
}

/// Standing on the landscape (0x00454be0: vt+0xb8's contact with no object, GenericAIObject_Slot46 0x00431f70): the
/// first static thing under his feet (past the dynamic bodies, his own among them) is the level's ground, not an
/// object (a building, a roof, a prop).
pub fn on_landscape(physics: &Physics, feet: Vec3) -> bool {
    physics.static_below(feet, 4.0).is_some_and(|(b, _)| physics.landscape.is_none_or(|l| l == b))
}

/// The characters the black hole's carry-off takes (AreaDamage_Apply 0x0047f020 with flag 0x800 -> vt+0x254: not
/// traced; guess: the ones on their feet, as the land shark's prey was guessed before 0x00434e10 was read): grunts and
/// captains, not in a window or tower, not on a gun.
pub fn on_their_feet(g: &crate::ai::Grunt) -> bool {
    use crate::ai::Kind;
    matches!(g.kind, Kind::Grunt | Kind::Bow | Kind::Captain | Kind::Sniper) && !g.window && g.fixed.is_none() && g.gun.is_none()
}

pub struct TargetingPlugin;

impl Plugin for TargetingPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, update.before(crate::player::PlayerSet::Weapons).run_if(crate::playing));
    }
}

/// The pass of the gun in hand, every frame (Hud_UpdateCrosshair's target part).
#[allow(clippy::too_many_arguments)]
fn update(
    mut targets: ResMut<Targets>,
    roman: Query<(Entity, &crate::player::Roman, &GlobalTransform)>,
    cam: Query<(&GlobalTransform, &Projection), With<crate::camera::FollowCam>>,
    grunts: Query<(Entity, &crate::ai::Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    physics: NonSend<Physics>,
    (data, opts, difficulty, upgrade): (Res<crate::gamedata::GameData>, Res<crate::options::GameOptions>, Option<Res<crate::gamedata::Difficulty>>, Res<VindalooUpgrade>),
    (mounted, carried, held): (Res<crate::turret::Mounted>, Query<(), With<super::blackhole::Carried>>, Query<(), Or<(With<crate::topsy::Thrown>, With<super::glove::Gloved>)>>),
    (bases, characters): (Query<(), With<crate::defence::BaseTurret>>, Characters),
) {
    targets.list.clear();
    targets.pass = None;
    let (Ok((_, r, rt)), Ok((c, proj))) = (roman.single(), cam.single()) else { return };
    let Some(gun) = r.arsenal.in_hand() else { return };
    // Game+0xac8: 1 in the base turret (its passes run), 4 on a mounted gun (none)
    let base = mounted.0.is_some_and(|m| bases.contains(m));
    if r.dead || mounted.0.is_some() && !base {
        return;
    }
    let g = data.props("p_globalsettings");
    let pair = |k: &str, h: f32, v: f32| (g.f(&format!("autoaim_{k}factor_horz"), h), g.f(&format!("autoaim_{k}factor_vert"), v));
    let (pass, factors, max, prey) = match gun.id {
        // 0x004a2a40: the option (Game+0x4c2), not while zooming (0x0040fba0), not above normal difficulty (Game+0x550)
        WeaponId::Machinegun | WeaponId::Rifle => {
            if !opts.auto_aim || r.zoom > 0.0 || difficulty.is_some_and(|d| d.0 > 1) {
                return;
            }
            (Pass::AutoAim, pair("", 0.18, 0.4), 1, false)
        }
        WeaponId::Sharkgun => (Pass::Shark, pair("sharkgun_", 0.4, 0.6), 1, true),
        WeaponId::Vindaloo => {
            let (h, v) = pair("multitarget_", 0.4, 0.6);
            // HudFrustum_Build 0x00408390: x 1.2 (0x005bcd70) with the upgrade
            let k = if upgrade.0 { 1.2 } else { 1.0 };
            (Pass::MultiLock, (h * k, v * k), 4, false)
        }
        _ => return,
    };
    let (pass, factors, max, range) = if base {
        let (h, v) = pair("base_", 0.2, 0.3);
        let k = if upgrade.0 { 1.2 } else { 1.0 };
        (Pass::MultiLock, (h * k, v * k), 4, f32::INFINITY)
    } else {
        (pass, factors, max, gun.spec.range)
    };
    let view = View::new(c, Some(proj), rt.translation() + Vec3::Y * 4.0);
    let passable = |e: Entity| characters.contains(e);
    // a character a black hole carries off is left out by the shark gun's test alone (vt+0x24c: held); the other
    // passes can pick him (ui_01.cpp:371-379)
    let cands = candidates(&grunts, &joints, |e, g, feet| !prey || can_be_bitten_now(g, carried.contains(e) || held.contains(e), &physics, feet));
    targets.list = search(&view, cands, factors, range, max, &physics, passable);
    targets.pass = Some(pass);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn view() -> View {
        View { cam: Vec3::ZERO, fwd: Vec3::NEG_Z, right: Vec3::X, up: Vec3::Y, half_v: 0.3, half_h: 0.52, eye: Vec3::ZERO }
    }

    /// The narrowed volume: a point straight ahead is in, one off to the side beyond the factor (and the slack) is
    /// out; the slack grows the volume by the sphere.
    #[test]
    fn narrowed_view() {
        let v = view();
        assert!(v.contains(Vec3::new(0.0, 0.0, -100.0), 0.0, (0.04, 0.12)));
        // half width at 100: 0.52 x 0.04 x 100 = 2.08
        assert!(v.contains(Vec3::new(2.0, 0.0, -100.0), 0.0, (0.04, 0.12)));
        assert!(!v.contains(Vec3::new(3.0, 0.0, -100.0), 0.0, (0.04, 0.12)));
        assert!(v.contains(Vec3::new(3.0, 0.0, -100.0), SLACK, (0.04, 0.12)));
        assert!(!v.contains(Vec3::new(0.0, 0.0, 100.0), 0.0, (0.4, 0.6)), "behind");
    }
}
