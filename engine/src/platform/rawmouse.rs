//! Raw mouse motion on macOS, from Apple's GameController framework (`GCMouse`): each report of the device, as the
//! device counted it, at the device's rate. The window system's motion (AppKit's `NSEvent` deltas, which winit and so
//! bevy's `MouseMotion` carry) is whole points of pointer travel after the system's acceleration, handed over in one
//! lump per display refresh: a game that reads it on its own frame clock gets one lump on some frames and two on
//! others, and the view judders under a steady hand (winit issues 1418 and 4581). SDL3 reads `GCMouse` for its
//! relative mouse mode for the same reason; this follows it.

use bevy::math::Vec2;
use block2::RcBlock;
use objc2_game_controller::{GCMouse, GCMouseInput};
use std::ptr::NonNull;
use std::sync::Mutex;

static REPORTS: Mutex<Vec<Vec2>> = Mutex::new(Vec::new());

/// The mice the framework lists get a motion handler (once each; a mouse or trackpad can come and go). Returns how
/// many there are. Main thread: the handlers run on the main queue, between frames.
fn listen() -> usize {
    // SAFETY: plain GameController calls on the main thread; the handler takes its arguments by value and only
    // pushes them into a static.
    unsafe {
        let mice = GCMouse::mice();
        for mouse in mice.iter() {
            let Some(input) = mouse.mouseInput() else { continue };
            if !input.mouseMovedHandler().is_null() {
                continue;
            }
            // the framework's y points up; the window's (and the look's) points down
            let handler = RcBlock::new(|_: NonNull<GCMouseInput>, dx: f32, dy: f32| {
                if let Ok(mut reports) = REPORTS.lock() {
                    reports.push(Vec2::new(dx, -dy));
                }
            });
            input.setMouseMovedHandler(RcBlock::as_ptr(&handler));
        }
        mice.len()
    }
}

/// The raw motion reports since the last call, or None when the framework lists no mouse.
pub fn drain() -> Option<Vec<Vec2>> {
    let mice = listen();
    let reports = REPORTS.lock().map(|mut r| std::mem::take(&mut *r)).unwrap_or_default();
    (mice > 0).then_some(reports)
}
