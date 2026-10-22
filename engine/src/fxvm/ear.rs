//! Fx ears (behaviour `fx_ear`, the FxOwner class: FxOwner_OnMessage 0x0041c050, FxOwner_Update 0x0041c110,
//! FxOwner_Toggle 0x0041bf60, render_r2a.cpp:95-207): a placed object that starts its fx when its trigger arrives.
//!
//! Public API: build an `FxEar` from the placement (`FxEar::new`), send `TriggerFxEar` when its trigger arrives
//! (mission.rs does); `update` (in the fx plugin) keeps each ear's state (once only, the random first delay, the
//! repeat cool-down) and starts the fx (`crate::fx::StartFx`) at the ear's position, unrotated (Fx_Create flags 8).
//!
//! Not ported: `looping_fx` (flag bit 0: the fx handle kept, a second trigger releasing it, a trigger during the
//! cool-down queued): no shipped ear sets it (all 24 levels' mission.json, 2026-10-22). `lock_fx` (bit 3: the fx
//! owned by the ear, Fx_Create flags 4) is not distinguished: ears do not move, and the ears' placement matrices (a
//! quarter turn about x on every lev_1_m4 ear) would turn lev_1_m4's arrow volley (fx_launch_arrows) into the ground
//! instead of onto the army, so the owner's runtime matrix evidently lacks that turn (open question: how the game's
//! object loader treats a helper's pivot).

use super::{CrtRand, FxVm};
use crate::fx::StartFx;
use bevy::prelude::*;
use serde_json::{Map, Value};
use std::collections::HashMap;

/// One ear's placement and properties.
#[derive(Clone, Debug)]
pub struct FxEar {
    /// the placement's instance id
    pub id: u32,
    pub fx: String,
    /// engine space
    pub at: Vec3,
    /// `one_shot` (bit 1): the trigger id is cleared after the first toggle, so it fires once
    pub one_shot: bool,
    /// `delay_initial_min` / `_max` (+0x468 / +0x46c): a random delay (Rand_FloatRange 0x0046e2c0) before the fx starts
    pub delay_initial: (f32, f32),
    /// `delay_repeat` (+0x464): after a toggle, triggers are ignored for this long (+0x490)
    pub delay_repeat: f32,
}

impl FxEar {
    /// From the placement's resolved properties.
    pub fn new(id: u32, fx: String, at: Vec3, props: &Map<String, Value>) -> Self {
        let n = |k: &str| props.get(k).and_then(Value::as_f64).unwrap_or(0.0) as f32;
        FxEar {
            id,
            fx,
            at,
            one_shot: n("one_shot") != 0.0,
            delay_initial: (n("delay_initial_min"), n("delay_initial_max")),
            delay_repeat: n("delay_repeat"),
        }
    }

    fn start(&self) -> StartFx {
        StartFx { name: self.fx.clone(), at: self.at, rotation: Quat::IDENTITY, follow: None, position_only: false }
    }
}

/// The ear's trigger arrived (FxOwner_OnMessage 0x0041c050 with message == its trigger id).
#[derive(Message, Clone)]
pub struct TriggerFxEar(pub FxEar);

#[derive(Default)]
struct EarState {
    /// one-shot and fired: its trigger id cleared (+0x460 = 0)
    done: bool,
    /// +0x494: the first delay left
    delay: f32,
    /// +0x490: the cool-down left
    cooldown: f32,
}

impl EarState {
    /// FxOwner_OnMessage 0x0041c050: whether the trigger toggles the fx now. A running delay is cancelled by a second
    /// trigger; with no cool-down left the delay is drawn and the fx toggles at once when it is <= 0; a trigger during
    /// the cool-down is ignored.
    fn trigger(&mut self, ear: &FxEar, rng: &mut CrtRand) -> bool {
        if self.done {
            return false;
        }
        if self.delay > 0.0 {
            self.delay = 0.0;
            return false;
        }
        if self.cooldown > 0.0 {
            return false;
        }
        self.delay = rng.r(ear.delay_initial.0, ear.delay_initial.1);
        self.delay <= 0.0
    }

    /// FxOwner_Update 0x0041c110: whether the delay ran out (it toggles when it falls below 0); the cool-down counts
    /// down while positive.
    fn tick(&mut self, dt: f32) -> bool {
        let mut due = false;
        if self.delay > 0.0 {
            self.delay -= dt;
            due = self.delay < 0.0;
        }
        if self.cooldown > 0.0 {
            self.cooldown -= dt;
        }
        due
    }

    /// After FxOwner_Toggle 0x0041bf60 started the fx: a one-shot ear is done, any other starts its cool-down.
    fn toggled(&mut self, ear: &FxEar) {
        if ear.one_shot {
            self.done = true;
        } else {
            self.cooldown = ear.delay_repeat;
        }
    }
}

/// Every triggered ear's state, by instance id (reset with the level).
#[derive(Resource, Default)]
pub struct FxEars {
    ears: HashMap<u32, (FxEar, EarState)>,
}

/// The ears' triggers, then their timers; each toggle starts the ear's fx.
pub fn update(time: Res<Time>, mut ears: ResMut<FxEars>, mut vm: ResMut<FxVm>, mut triggers: MessageReader<TriggerFxEar>, mut out: MessageWriter<StartFx>) {
    for TriggerFxEar(ear) in triggers.read() {
        let (ear, s) = ears.ears.entry(ear.id).or_insert_with(|| (ear.clone(), EarState::default()));
        if s.trigger(ear, &mut vm.rng) {
            out.write(ear.start());
            s.toggled(ear);
        }
    }
    let dt = time.delta_secs();
    for (ear, s) in ears.ears.values_mut() {
        if s.tick(dt) {
            out.write(ear.start());
            s.toggled(ear);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(json: Value) -> Map<String, Value> {
        json.as_object().cloned().unwrap()
    }

    #[test]
    fn properties() {
        let ear = FxEar::new(1, "fx".into(), Vec3::ZERO, &props(serde_json::json!({"lock_fx": 1, "one_shot": 1, "delay_repeat": 3.0})));
        assert!(ear.one_shot);
        assert_eq!(ear.delay_repeat, 3.0);
        assert_eq!(ear.delay_initial, (0.0, 0.0));
    }

    #[test]
    fn one_shot_fires_once_and_repeat_waits_out_the_cooldown() {
        let mut rng = CrtRand::seeded(1);
        let once = FxEar::new(1, "fx".into(), Vec3::ZERO, &props(serde_json::json!({"one_shot": 1})));
        let mut s = EarState::default();
        assert!(s.trigger(&once, &mut rng));
        s.toggled(&once);
        assert!(!s.trigger(&once, &mut rng));
        // fx_launch_arrows: delay_repeat 3
        let arrows = FxEar::new(2, "fx".into(), Vec3::ZERO, &props(serde_json::json!({"delay_repeat": 3.0})));
        let mut s = EarState::default();
        assert!(s.trigger(&arrows, &mut rng));
        s.toggled(&arrows);
        s.tick(1.0);
        assert!(!s.trigger(&arrows, &mut rng), "ignored during the cool-down");
        s.tick(2.5);
        assert!(s.trigger(&arrows, &mut rng));
    }

    #[test]
    fn initial_delay_runs_out_or_is_cancelled() {
        let mut rng = CrtRand::seeded(1);
        let ear = FxEar::new(3, "fx".into(), Vec3::ZERO, &props(serde_json::json!({"delay_initial_min": 1.0, "delay_initial_max": 1.0})));
        let mut s = EarState::default();
        assert!(!s.trigger(&ear, &mut rng));
        assert!(!s.tick(0.5));
        assert!(s.tick(0.6));
        let mut s = EarState::default();
        assert!(!s.trigger(&ear, &mut rng));
        assert!(!s.trigger(&ear, &mut rng), "a second trigger cancels the delay");
        assert!(!s.tick(2.0));
    }
}
