//! The frame rate limit (old remaster hook/enhance/display.cpp `fps_limit`, applied in `DisplayBeforePresent`): each
//! frame waits until 1 / limit seconds have passed since the last one, sleeping 1 ms at a time while more than 2 ms
//! remain, then spinning (the old code's QueryPerformanceCounter loop with Sleep(1)). It also keeps the original's
//! frame-rate bugs away (docs/original-bugs.md: the lev_1_m1 opening stalls above about 85 fps); the engine's own
//! rules do not depend on it.

use bevy::prelude::*;
use std::collections::VecDeque;
use std::time::{Duration, Instant};

pub struct FrameLimitPlugin;

impl Plugin for FrameLimitPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<FrameTimes>().add_systems(Last, limit);
    }
}

/// The intervals between the last frames, for the state log (`display.frame_ms`: playtests check the cap).
#[derive(Resource, Default)]
pub struct FrameTimes {
    last: Option<Instant>,
    /// the last 31 frame intervals, in milliseconds
    recent: VecDeque<f32>,
}

impl FrameTimes {
    /// The median of the recent frame intervals (ms): the frame rate held, without the odd long frame (an off-screen
    /// test window stalls ~0.4 s every 15 frames whatever the cap).
    pub fn median_ms(&self) -> f32 {
        let mut v: Vec<f32> = self.recent.iter().copied().collect();
        if v.is_empty() {
            return 0.0;
        }
        v.sort_by(f32::total_cmp);
        v[v.len() / 2]
    }
}

/// The wait still needed before a frame may end: None when the period has passed (or there is no limit).
pub fn remaining(since_last: Duration, limit: u32) -> Option<Duration> {
    if limit == 0 {
        return None;
    }
    let period = Duration::from_secs_f64(1.0 / limit as f64);
    period.checked_sub(since_last).filter(|d| !d.is_zero())
}

fn limit(settings: Res<crate::graphics::GraphicsSettings>, mut times: ResMut<FrameTimes>) {
    let cap = settings.display.fps_limit;
    if let Some(last) = times.last {
        while let Some(left) = remaining(last.elapsed(), cap) {
            if left > Duration::from_millis(2) {
                std::thread::sleep(Duration::from_millis(1));
            } else {
                std::hint::spin_loop();
            }
        }
    }
    let now = Instant::now();
    if let Some(last) = times.last {
        if times.recent.len() == 31 {
            times.recent.pop_front();
        }
        times.recent.push_back((now - last).as_secs_f32() * 1000.0);
    }
    times.last = Some(now);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn period() {
        assert_eq!(remaining(Duration::from_millis(5), 0), None);
        let left = remaining(Duration::from_millis(5), 60).unwrap();
        assert!((left.as_secs_f64() - (1.0 / 60.0 - 0.005)).abs() < 1e-6);
        assert_eq!(remaining(Duration::from_millis(40), 30), None);
    }

    #[test]
    fn median() {
        let mut t = FrameTimes::default();
        t.recent.extend([50.0, 50.2, 430.0, 49.9, 50.1]);
        assert_eq!(t.median_ms(), 50.1);
    }
}
