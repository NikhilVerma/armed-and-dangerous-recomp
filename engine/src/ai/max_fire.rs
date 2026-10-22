//! The mission's shooter limit (ai_max_fire, Game+0xc50): with it set, only that many bad guys fire at once; the rest
//! wait their turn facing the target. docs/subsystems/grunt-ai.md section 15. Levels: the base defences (lev_1_m4,
//! lev_2_m4, lev_3_m4_bd, lev_4_m5: 1; lev_5_m5: 5); every other level sets 0 (no limit).
//!
//! - Engage (0x00427910 / its variant 0x00427c80, ai_02.cpp:1219-1240): in range with the counters rolled, a bad guy that
//!   is not yet a counted shooter (+0x488 0x20000) and not a teammate (vt+0xe4) joins the counted list (World+0xf18) when
//!   it holds fewer than the limit (0x20000 set, 0x100000 cleared, state 0xa); otherwise even odds ((rand() x 100) >> 15
//!   < 50, the constant 0x190000) fire anyway as an extra (+0x488 0x100000, state 0xa) or wait (state 0x11 with one of
//!   the idle bases 0x74..0x76, 0x004271c0).
//! - Waiting (state 0x11, ai_06.cpp:849-867): facing the target (0x00454e60 at 16), the next idle base as each one ends
//!   (0x004271c0: 0x74 -> 0x75 -> 0x76 -> 0x74). Nothing ends it but a hand-over, a hit or the target's loss.
//! - Hand-over (0x0042bb10, ai_03.cpp:888-926): a counted shooter that stops (the fire state's target lost, ai_06.cpp:878;
//!   its line of sight lost or its clip's end, the fire cycle ai_02.cpp:1897 / 1950; death, ai_04.cpp:1018) leaves the
//!   list and picks one of the bad guys that wait (state 0x11) or fire as extras with their counters rolled
//!   (0x0042b9e0), with a target (0x0042c6f0), at n = (rand() x count) >> 15, which then engages again (vt+0x230(0)).
//!   Original bug, kept: the pick counts down from n with `--n == 0`, so n = 0 picks nobody (docs/original-bugs.md).
//!
//! The engine asks the limit whenever a bad guy enters the fire state (`may_fire`, ai.rs's fire state: any of the
//! engine's ways in, which all pass the engage in the original) and counts a shooter while it holds the fire state with its counters rolled; leaving it or
//! its clip's end (the original's stop events above) releases the place at the end of the frame (`upkeep`). Deviation:
//! the engine's fire state keeps a shooter whose line of sight is lost (the original re-pursues and frees the place).

use super::{Grunt, Mind};
use crate::fx::FxStats;
use bevy::prelude::*;

/// A bad guy's place under the limit (+0x488 0x20000 counted, 0x100000 extra).
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Shooter {
    #[default]
    None,
    /// in the counted list (World+0xf18)
    Counted,
    /// firing beyond the limit (the even-odds roll)
    Extra,
}

/// What the engage decides under the limit.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Admission {
    /// the fire state (as a counted shooter or an extra, `Grunt::shooter` says which)
    Fire,
    /// state 0x11: wait with the idle bases
    Wait,
}

/// The counted list and the limit (Game+0xc50: the mission's ai_max_fire).
#[derive(Resource, Default, Debug)]
pub struct ShooterLimit {
    /// 0: no limit (the engage takes the fire state directly)
    pub max: u32,
    pub shooters: Vec<Entity>,
    /// counted shooters released this frame, each a hand-over to make
    pending: u32,
    /// places released, hand-overs made, waits started (playtest log)
    pub releases: u32,
    pub handovers: u32,
    pub waits: u32,
}

impl ShooterLimit {
    /// The engage's choice for `me` in range with its counters rolled (0x00427910 ai_02.cpp:1219-1240).
    pub fn admit(&mut self, me: Entity, shooter: &mut Shooter, rng: &mut FxStats) -> Admission {
        if self.max == 0 || *shooter == Shooter::Counted {
            return Admission::Fire;
        }
        if (self.shooters.len() as u32) < self.max {
            self.shooters.push(me);
            *shooter = Shooter::Counted;
            return Admission::Fire;
        }
        if rng.rand() * 100.0 < 50.0 {
            *shooter = Shooter::Extra;
            return Admission::Fire;
        }
        self.waits += 1;
        Admission::Wait
    }

    /// Entering the fire state (the engage's question, asked once per entry): may `me` fire now? Already counted or an
    /// extra: yes; no limit: yes; else `admit`.
    pub fn may_fire(&mut self, me: Entity, shooter: &mut Shooter, rng: &mut FxStats) -> bool {
        if self.max == 0 || *shooter != Shooter::None {
            return true;
        }
        self.admit(me, shooter, rng) == Admission::Fire
    }

    /// A counted shooter stops (0x0042bb10's first half): out of the list, a hand-over owed.
    pub fn release(&mut self, me: Entity, shooter: &mut Shooter) {
        if *shooter == Shooter::Counted {
            self.shooters.retain(|&e| e != me);
            self.pending += 1;
            self.releases += 1;
        }
        *shooter = Shooter::None;
    }

    /// Takes the hand-overs owed.
    fn take_pending(&mut self) -> u32 {
        std::mem::take(&mut self.pending)
    }
}

/// The limit from the mission (MissionObject_Load 0x0044c170 reads ai_max_fire into Game+0xc50).
pub fn sync_limit(mission: Option<Res<crate::mission::Mission>>, mut limit: ResMut<ShooterLimit>) {
    let max = mission.map_or(0, |m| m.ai_max_fire);
    if limit.max != max {
        limit.max = max;
    }
}

/// The idle base after `id` while waiting (0x004271c0): the next of 0x74..0x76 in turn, a random one to start.
pub fn next_wait_anim(id: u32, rng: &mut FxStats) -> u32 {
    if (0x74..=0x76).contains(&id) {
        if id == 0x76 { 0x74 } else { id + 1 }
    } else {
        0x74 + ((rng.rand() * 3.0) as u32).min(2)
    }
}

/// Which of `count` candidates takes the turn for a uniform draw `r` in [0, 1) (0x0042bb10): n = (rand() x count) >> 15,
/// the n-th counted from 1; n = 0 picks nobody (original bug: the countdown starts at 0 and wraps, so a single candidate
/// never gets the turn).
pub fn handover_pick(count: usize, r: f32) -> Option<usize> {
    let n = ((r * count as f32) as usize).min(count.saturating_sub(1));
    n.checked_sub(1)
}

/// May this bad guy be handed the turn (0x0042b9e0 with 0x0042c6f0): alive, with a target, waiting or firing as an
/// extra with its counters rolled.
fn takes_turn(g: &Grunt) -> bool {
    g.mind != Mind::Dead && g.target.is_some() && (g.mind == Mind::MaxFireWait || (g.shooter == Shooter::Extra && g.fire_ready))
}

/// After the think: releases the counted shooters that stopped firing (or died), then makes the hand-overs owed
/// (0x0042bb10): the one picked engages again on its next frame (`Grunt::retry_engage`).
pub fn upkeep(mut limit: ResMut<ShooterLimit>, mut grunts: Query<(Entity, &mut Grunt)>, mut rng: ResMut<FxStats>) {
    if limit.max == 0 && limit.shooters.is_empty() {
        return;
    }
    for (e, mut g) in &mut grunts {
        let firing = matches!(g.mind, Mind::Fire) || (g.mind == Mind::Guard && g.guard_firing);
        // the reload between two clips (state 0x32, counters still rolled) keeps a counted place: only the last clip's
        // end frees it (ai_02.cpp:1953); an extra is asked again at the reload's end (its engage)
        let between_clips = g.mind == Mind::Reload && g.fire_ready;
        if g.shooter == Shooter::Counted && (!(firing || between_clips) || !g.fire_ready || g.mind == Mind::Dead) {
            let mut s = g.shooter;
            limit.release(e, &mut s);
            g.shooter = s;
        } else if g.shooter == Shooter::Extra && !firing {
            g.shooter = Shooter::None;
        }
    }
    // a shooter despawned without dying (removed from the level) leaves the list too
    let live: Vec<Entity> = limit.shooters.iter().copied().filter(|&e| grunts.get(e).is_ok()).collect();
    let gone = limit.shooters.len() - live.len();
    limit.shooters = live;
    limit.pending += gone as u32;
    for _ in 0..limit.take_pending() {
        let count = grunts.iter().filter(|(_, g)| takes_turn(g)).count();
        if count == 0 {
            continue;
        }
        let Some(i) = handover_pick(count, rng.rand()) else { continue };
        if let Some((e, mut g)) = grunts.iter_mut().filter(|(_, g)| takes_turn(g)).nth(i) {
            g.retry_engage = true;
            // (an extra handed the turn is asked again: counted now that there is room)
            g.shooter = Shooter::None;
            limit.handovers += 1;
            info!("EVENT ai: max fire hands the turn to {e:?}");
        }
    }
}

/// The playtest state's `max_fire`: the limit, the counted shooters, the bad guys firing (all, extras) and waiting, the
/// hand-overs made and the waits started.
pub fn log(limit: Res<ShooterLimit>, grunts: Query<&Grunt>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let live = || grunts.iter().filter(|g| g.mind != Mind::Dead);
    let firing = live().filter(|g| g.mind == Mind::Fire || (g.mind == Mind::Guard && g.guard_firing)).count();
    state.0.insert(
        "max_fire".into(),
        serde_json::json!({
            "max": limit.max,
            "counted": limit.shooters.len(),
            "firing": firing,
            "extras": live().filter(|g| g.shooter == Shooter::Extra).count(),
            "waiting": live().filter(|g| g.mind == Mind::MaxFireWait).count(),
            "releases": limit.releases,
            "handovers": limit.handovers,
            "waits": limit.waits,
        }),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wait_anims_cycle() {
        let mut rng = FxStats::default();
        assert_eq!(next_wait_anim(0x74, &mut rng), 0x75);
        assert_eq!(next_wait_anim(0x76, &mut rng), 0x74);
        let first = next_wait_anim(0x23, &mut rng);
        assert!((0x74..=0x76).contains(&first));
    }

    #[test]
    fn handover_skips_the_first_roll() {
        // one candidate: never (n is always 0)
        assert_eq!(handover_pick(1, 0.0), None);
        assert_eq!(handover_pick(1, 0.99), None);
        // three: n = 0 none, 1 the first, 2 the second; the third is never picked
        assert_eq!(handover_pick(3, 0.1), None);
        assert_eq!(handover_pick(3, 0.4), Some(0));
        assert_eq!(handover_pick(3, 0.9), Some(1));
        assert_eq!(handover_pick(0, 0.5), None);
    }

    #[test]
    fn limit_counts_and_releases() {
        let mut l = ShooterLimit { max: 1, ..default() };
        let mut rng = FxStats::default();
        let (a, b) = (Entity::from_raw_u32(1).unwrap(), Entity::from_raw_u32(2).unwrap());
        let (mut sa, mut sb) = (Shooter::None, Shooter::None);
        assert_eq!(l.admit(a, &mut sa, &mut rng), Admission::Fire);
        assert_eq!(sa, Shooter::Counted);
        // the second is an extra or waits, never counted
        let _ = l.admit(b, &mut sb, &mut rng);
        assert_ne!(sb, Shooter::Counted);
        assert_eq!(l.shooters, vec![a]);
        l.release(a, &mut sa);
        assert!(l.shooters.is_empty());
        assert_eq!(l.take_pending(), 1);
        // no limit: always fire, never counted
        let mut free = ShooterLimit::default();
        let mut s = Shooter::None;
        assert_eq!(free.admit(a, &mut s, &mut rng), Admission::Fire);
        assert_eq!(s, Shooter::None);
    }
}
