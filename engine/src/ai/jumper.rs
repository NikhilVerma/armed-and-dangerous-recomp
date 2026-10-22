//! Jumper captains (CaptainObject with jumper_captain: the turbo pack +0xb08, 0x00425d30) and the jump markers they
//! fly to (JumpMarker, vtable 0x005ac628). docs/subsystems/infantry.md section 3.
//! - `Jumper::read`: the turbo parameters (Ai_InitGeneric ai_01.cpp:2354-2390).
//! - `pick_marker`: the marker choice 0x0042b480 (ai_03.cpp:589-720).
//! - `launch`: the ballistic solver 0x00471a00 (horizontal speed and gravity given).
//! - A marker is held from the launch to it until its jumper leaves it (the next launch, `Jumper::leave`) or dies
//!   (`free_markers`): the landing keeps it (0x0042e1c0 frees nothing; JumpMarker slots 0x56 / 0x57 0x0040dfa0 /
//!   0x0040dfb0 take and free it). Its slot point (slot 0x53 0x0040de50; 0x54 0x0040df90 jumps to it) is the marker's
//!   position.
//! - `Flight`: states 0xc4 (to a marker nearer the target: re-aimed into a glide once falling), 0xc5 (a plain jump),
//!   0xbd (the glide, firing; or the hop, firing without the wings), and the landing (0x0042e1c0 -> anim
//!   captain_jumper_land, state 0xbf).
//! - Not modelled: 0x0042a2c0's launch outside state 0xbc (+0x948 under the jet's gravity, state 0xb8). Its only
//!   caller that reaches it is the hiding-spot search taking a kind-4 cover while in the air (ai_03.cpp:432-440);
//!   ai.rs's `find_hiding_spot` has no such cover kind yet.

use bevy::prelude::*;

/// K(0x005bcd64): the AI's gravity (game units / s^2).
pub const GRAVITY: f32 = 64.348;

/// A marker launch rises at least this fast (0x0042b480: 5.0 when the solved upward speed is 0.0 or less).
const MIN_LAUNCH_RISE: f32 = 5.0;

/// A jump marker's slot (JumpMarker_Slot6 0x0040dd70: marker_tag +0x464, the occupant +0x468). Its point is the
/// marker's position (0x0040de50 zeroes marker_range first: original quirk).
#[derive(Component, Clone, Copy, Debug)]
pub struct JumpMarker {
    pub tag: i64,
    pub occupant: Option<Entity>,
}

/// The phases of a jumper's flight (+0x460).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Flight {
    /// 0xc4: jetting to a marker nearer the target; becomes the glide once falling
    ToGlide,
    /// 0xc5: a plain jump to a marker
    Jump,
    /// 0xbd: gliding with the wings out, firing
    Glide,
    /// 0xbd entered by the hop (0x0042a2c0 within 50 of the point): firing as in the glide, but the wings stay in
    /// (+0x484 0x20000000 not set) so the jet's gravity holds (Slot35 0x004326b0 reads the flag, not the state)
    Hop,
}

impl Flight {
    /// State 0xbd, where he fires with flag 4 (vt+0x198) whatever the line of sight.
    pub fn fires(self) -> bool {
        matches!(self, Flight::Glide | Flight::Hop)
    }
}

/// The turbo pack's parameters and a flight in progress.
#[derive(Clone, Copy, Debug)]
pub struct Jumper {
    /// turbo_anti_gravity x 0.5 (+0xaec): the gravity scale of a jet flight
    pub anti_gravity: f32,
    /// turbo_glide_anti_gravity x 0.5 (+0xb04, default +0xaec x 0.25): the glide's gravity scale
    pub glide_anti_gravity: f32,
    /// turbo_force +0xae8 (175): the hop's kick
    pub force: f32,
    /// turbo_glide_xyvel +0xaf8, turbo_glide_zvel +0xafc, turbo_glide_fwd_max_speed +0xaf4
    pub glide_xyvel: f32,
    pub glide_zvel: f32,
    pub glide_fwd_max: f32,
    /// jumper_tag +0xb14: only markers of this marker_tag (when both are non-zero)
    pub tag: i64,
    /// jump_distance: +0xb1c min, +0xb18 max (one key for both: original quirk)
    pub min: f32,
    pub max: f32,
    /// the flight now: its phase, the velocity (game space, y up as the engine's), the marker flown to
    pub flight: Option<Flight>,
    pub vel: Vec3,
    pub marker: Option<Entity>,
    pub goal: Vec3,
    /// the markers left last (+0x918 / +0x91c): skipped by the next choice
    pub history: [Option<Entity>; 2],
    /// the pursuit's walk before a glide launch (state 0xbc, +0x490): its time left
    pub walk: Option<f32>,
    /// the body has been through a physics step: the ground contact the pursuit reads (0x00454c20, vt+0xb8: the
    /// contacts of the last step) exists. Before it (the level's first AI update) a jumper counts as in the air and
    /// flies to a jump marker (lev_bonus_m1: every jumper captain leaves on the load frame, states 0xc4 / 0xc5)
    pub stepped: bool,
}

impl Jumper {
    pub fn read(p: &crate::gamedata::Props) -> Jumper {
        let anti = p.f("turbo_anti_gravity", 0.25) * 0.5;
        let jump = p.has("jump_distance").then(|| p.f("jump_distance", 0.0));
        Jumper {
            anti_gravity: anti,
            glide_anti_gravity: p.f("turbo_glide_anti_gravity", anti * 0.25) * 0.5,
            force: p.f("turbo_force", 175.0),
            glide_xyvel: p.f("turbo_glide_xyvel", 30.0),
            glide_zvel: p.f("turbo_glide_zvel", 10.0),
            glide_fwd_max: p.f("turbo_glide_fwd_max_speed", 4.0),
            tag: p.i("jumper_tag", 0),
            min: jump.unwrap_or(125.0),
            max: jump.unwrap_or(300.0),
            flight: None,
            vel: Vec3::ZERO,
            marker: None,
            goal: Vec3::ZERO,
            history: [None; 2],
            walk: None,
            stepped: false,
        }
    }

    /// The gravity of the flight now (GenericAIObject_Slot35 0x004326b0: with the wings out, +0x484 0x20000000, +0xb04,
    /// else +0xaec). Only a flight reads it: on the ground the character controller (fall.rs) falls under the world's
    /// gravity (Slot35's 0 outside a flight cancels the extra force of the World+0xf1c list, which only flights join).
    pub fn gravity(&self) -> f32 {
        match self.flight {
            Some(Flight::Glide) => self.glide_anti_gravity * GRAVITY,
            Some(_) => self.anti_gravity * GRAVITY,
            None => GRAVITY,
        }
    }

    /// Leaving the marker he holds (vt+0x188 0x004273a0 with +0x484 0x10, set by a launch: the history shifted, +0x918
    /// -> +0x91c, then the marker freed, JumpMarker slot 0x57 0x0040dfb0): only when 0x0042b480 has found the next marker
    /// (ai_03.cpp:688-691); the glide launch 0x0042a2c0 keeps it, and so does a search that finds none. Returns the
    /// marker to free.
    pub fn leave(&mut self) -> Option<Entity> {
        let m = self.marker.take()?;
        self.history = [Some(m), self.history[0]];
        Some(m)
    }

    /// The launch to a marker (0x0042b480's success): the ballistic velocity at twice the move speed under the jet's
    /// gravity, its upward part at least 5 (a marker below him: ai_03.cpp:700-701, 0.0 or less becomes 5.0),
    /// +0x484 |= 0x10000010, state 0xc4 (`closer`) or 0xc5.
    pub fn start(&mut self, from: Vec3, marker: Entity, to: Vec3, closer: bool, move_speed: f32) {
        self.flight = Some(if closer { Flight::ToGlide } else { Flight::Jump });
        let v = launch(from, to, 2.0 * move_speed, self.anti_gravity * GRAVITY);
        self.vel = if v.y <= 0.0 { v.with_y(MIN_LAUNCH_RISE) } else { v };
        self.marker = Some(marker);
        self.goal = to;
    }

    /// The glide launch toward a point (0x0042a2c0 from state 0xbc): within 50 (2D) a hop (the velocity cleared and
    /// an upward kick of turbo_force x 0.25 per unit mass, state 0xbd without the wings), else the ballistic launch at
    /// turbo_glide_xyvel under the glide's gravity with the wings out (+0x484 0x30000000, state 0xbd). The marker he
    /// holds stays his (no vt+0x188 here).
    pub fn glide(&mut self, from: Vec3, to: Vec3) {
        self.goal = to;
        if (to - from).with_y(0.0).length_squared() < 2500.0 {
            self.flight = Some(Flight::Hop);
            self.vel = Vec3::Y * self.force * 0.25;
        } else {
            self.flight = Some(Flight::Glide);
            self.vel = launch(from, to, self.glide_xyvel, self.glide_anti_gravity * GRAVITY);
        }
    }

    /// One frame of the flight: gravity by phase; 0xc4 once falling re-aims into the glide (ai_06.cpp:2393-2430);
    /// falling onto something (`grounded`: the contact of GenericAIObject_Slot44 0x004320b0 with vz <= 0) lands.
    pub fn step(&mut self, pos: Vec3, dt: f32, grounded: bool) -> FlightStep {
        self.vel.y -= self.gravity() * dt;
        if self.flight == Some(Flight::ToGlide) && self.vel.y < 0.0 {
            let h = glide_speed(pos, self.goal, self.glide_anti_gravity * GRAVITY);
            let d = (self.goal - pos).with_y(0.0).normalize_or_zero() * h;
            self.vel = Vec3::new(d.x, self.vel.y, d.z);
            self.flight = Some(Flight::Glide);
            return FlightStep::WingsOut;
        }
        if self.vel.y <= 0.0 && grounded {
            // 0x0042e1c0: the turbo stopped; the marker stays his (JumpMarker slot 0x56's occupant) until he leaves it
            self.flight = None;
            self.vel = Vec3::ZERO;
            return FlightStep::Landed;
        }
        FlightStep::Flying
    }
}

/// A marker whose jumper died or is gone is free again (the teardown 0x00432730 -> vt+0x18c 0x004274e0 frees the cover
/// held, +0x904; a removed marker, JumpMarker_Slot55, takes its occupant with it).
pub fn free_markers(mut markers: Query<&mut JumpMarker>, grunts: Query<&super::Grunt>) {
    for mut m in &mut markers {
        if m.occupant.is_some_and(|o| grunts.get(o).map_or(true, |g| g.mind == super::Mind::Dead)) {
            m.occupant = None;
        }
    }
}

/// The playtest state's `jump_markers`: markers held now.
pub fn log(markers: Query<&JumpMarker>, grunts: Query<&super::Grunt>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let held = markers.iter().filter(|m| m.occupant.is_some()).count();
    // held by a jumper standing on it (landed, not flying)
    let landed = markers.iter().filter_map(|m| m.occupant).filter(|&o| grunts.get(o).is_ok_and(|g| g.jumper.is_some_and(|j| j.flight.is_none()))).count();
    state.0.insert("jump_markers".into(), serde_json::json!({"count": markers.iter().count(), "held": held, "landed": landed}));
}

/// What a flight's frame did.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum FlightStep {
    Flying,
    /// 0xc4 -> 0xbd: the wings out (Jumper_PlayWingSounds 0x0047bb30)
    WingsOut,
    /// on the ground: the landing anim (0x6c captain_jumper_land, state 0xbf)
    Landed,
}

/// A marker as the choice sees it.
pub struct MarkerView {
    pub e: Entity,
    pub pos: Vec3,
    pub tag: i64,
    pub free: bool,
}

/// 0x0042b480: the marker to fly to, and whether it is nearer the target than the jumper (`closer`: state 0xc4, else
/// 0xc5). 2D squared distances throughout; `range` is +0x5d8.
pub fn pick_marker(j: &Jumper, me: Vec3, target: Option<Vec3>, range: f32, markers: &[MarkerView]) -> Option<(usize, bool)> {
    let d2 = |a: Vec3, b: Vec3| (a - b).with_y(0.0).length_squared();
    let ds = target.map(|t| d2(me, t)).unwrap_or(0.0);
    let mut best: Option<(usize, f32, bool)> = None;
    let mut closer = false;
    for (i, m) in markers.iter().enumerate() {
        if !m.free || j.history.contains(&Some(m.e)) || (j.tag != 0 && m.tag != 0 && m.tag != j.tag) {
            continue;
        }
        let dm = d2(me, m.pos);
        if dm < j.min * j.min {
            continue;
        }
        let far = dm > j.max * j.max;
        let dt = target.map(|t| d2(m.pos, t)).unwrap_or(0.0);
        if !closer {
            if best.is_some_and(|b| far && dt >= b.1) {
                continue;
            }
            best = Some((i, dt, far));
            if !far && dt < ds {
                closer = true;
            }
        } else if best.is_some_and(|b| dt < b.1) && dt < range * range && dt < ds && !far {
            best = Some((i, dt, far));
        }
    }
    best.map(|(i, _, _)| (i, closer))
}

/// The ballistic solver 0x00471a00: the launch velocity reaching `to` from `from` at horizontal speed `h` under gravity
/// `g` (y up).
pub fn launch(from: Vec3, to: Vec3, h: f32, g: f32) -> Vec3 {
    let flat = (to - from).with_y(0.0);
    let d = flat.length();
    let t = (d / h.max(1.0)).max(0.1);
    let vy = ((to.y - from.y) + 0.5 * g * t * t) / t;
    flat.normalize_or_zero() * h + Vec3::Y * vy
}

/// 0xc4 once falling: the horizontal speed that lands on `to` under the glide's gravity (ai_06.cpp:2393-2430).
pub fn glide_speed(from: Vec3, to: Vec3, glide_gravity: f32) -> f32 {
    let d = (to - from).with_y(0.0).length();
    let t = (2.0 * (from.y - to.y).abs() / glide_gravity.max(0.01)).sqrt().max(0.1);
    d / t
}

#[cfg(test)]
mod tests {
    use super::*;

    fn m(w: &mut World, x: f32) -> MarkerView {
        MarkerView { e: w.spawn_empty().id(), pos: Vec3::new(x, 0.0, 0.0), tag: 0, free: true }
    }

    fn jumper() -> Jumper {
        Jumper::read(&crate::gamedata::Props::default())
    }

    #[test]
    fn marker_nearer_the_target_wins() {
        // jumper at 0, target at 400: markers at 50 (too near), 200 and 280 (both within 125..300 and nearer the target)
        let w = &mut World::new();
        let ms = [m(w, 50.0), m(w, 200.0), m(w, 280.0)];
        let r = pick_marker(&jumper(), Vec3::ZERO, Some(Vec3::new(400.0, 0.0, 0.0)), 450.0, &ms);
        assert_eq!(r, Some((2, true)), "the one nearest the target, closer");
    }

    #[test]
    fn occupied_and_recent_markers_are_skipped() {
        let w = &mut World::new();
        let mut ms = [m(w, 200.0), m(w, 250.0)];
        ms[1].free = false;
        let mut j = jumper();
        j.history[0] = Some(ms[0].e);
        assert_eq!(pick_marker(&j, Vec3::ZERO, Some(Vec3::new(400.0, 0.0, 0.0)), 450.0, &ms), None);
    }

    #[test]
    fn launch_lands_on_the_point() {
        let (from, to, g) = (Vec3::ZERO, Vec3::new(200.0, 30.0, 0.0), 32.0);
        let v = launch(from, to, 92.0, g);
        let t = 200.0 / 92.0;
        let y = v.y * t - 0.5 * g * t * t;
        assert!((y - 30.0).abs() < 0.01, "{y}");
    }

    /// A marker below him still launches upward (0x0042b480's floor of 5); the hop fires without the wings' gravity;
    /// the glide keeps the marker held.
    #[test]
    fn launch_floor_hop_and_held_marker() {
        let w = &mut World::new();
        let marker = w.spawn_empty().id();
        let mut j = jumper();
        j.start(Vec3::new(0.0, 200.0, 0.0), marker, Vec3::new(150.0, 0.0, 0.0), false, 35.0);
        assert_eq!(j.vel.y, MIN_LAUNCH_RISE);
        j.flight = None;
        j.glide(Vec3::ZERO, Vec3::new(30.0, 0.0, 0.0));
        assert_eq!(j.flight, Some(Flight::Hop));
        assert!(Flight::Hop.fires() && !Flight::Jump.fires());
        assert_eq!(j.gravity(), j.anti_gravity * GRAVITY);
        assert_eq!(j.marker, Some(marker), "the glide launch keeps the marker");
    }
}
