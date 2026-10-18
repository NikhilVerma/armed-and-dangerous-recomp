//! Mouse look: the pointer input the game window receives in play, turned into one look delta per frame (window
//! pixels; player.rs `grab_cursor` publishes it as `LookDelta`, the player / turret / defence-gun updates turn by it).
//!
//! The pointer reaches the game three ways:
//! - the cursor's offset from the window centre, where it is put back every frame (winit also clips a hidden, confined
//!   cursor to the centre, so this is mostly zero; it catches a cursor the clip has not caught yet);
//! - relative raw device motion (a mouse, a local trackpad; streamed input that leaves the cursor still);
//! - absolute raw reports: devices that report positions over the desktop (0..=65535 per axis) instead of motion, which
//!   winit passes on as motion (it tests MOUSE_MOVE_RELATIVE, which is 0, so every report counts as relative). A Mac
//!   trackpad streamed over Moonlight in its remote-desktop mouse mode does this. The look is the change between
//!   reports; the cursor offset is ignored meanwhile (the host cursor follows the injected positions, so its offset is
//!   where the pointer is, not how it moved: session 2026-10-18_17-00-51 jumped to the pitch limit, -1.71, on a report
//!   that moved 34 units).
//!
//! An absolute pointer stops at the desktop's edges: the remote machine's pointer is never warped back. Session
//! 2026-10-18_17-00-51 (bookmarks 4 to 6: "can't look down", "can't look left or right") had y held at 65535 and then
//! x at 0 while reports kept arriving. A report with the pointer held at an edge and little movement along the free
//! axis is taken as movement into that edge, at the pointer's usual speed (`AbsolutePointer::report`).
//!
//! Focus: losing it, or leaving play, forgets the last absolute position and makes the next frames only centre the
//! cursor (`CursorSettle`), so nothing that happened meanwhile (another program's pointer, a click on a menu row) turns
//! the view; nothing else is held across, so no axis can stay stuck.

use bevy::prelude::*;

/// One frame's pointer input.
pub struct PointerFrame<'a> {
    /// in play (not a menu)
    pub play: bool,
    /// the game window has the focus
    pub focused: bool,
    /// window size (logical pixels)
    pub window: Vec2,
    /// the cursor's position in the window, if it is over it
    pub cursor: Option<Vec2>,
    /// the raw motion reports of this frame, one per device report (not their sum: two absolute reports in one frame
    /// add up to a position twice as far)
    pub reports: &'a [Vec2],
}

/// The look's state between frames.
#[derive(Default)]
pub struct MouseLook {
    settle: CursorSettle,
    abs: AbsolutePointer,
    focused: Option<bool>,
}

impl MouseLook {
    /// Window pixels per absolute unit, as a fraction of the window: the desktop's 65535 units span this many windows
    /// (kept from 98e7700, which the player's sessions have used since)
    const ABS_GAIN: f32 = 4.0;

    /// Forget everything about the pointer: the next frames only centre the cursor.
    pub fn reset(&mut self) {
        self.settle.start();
        self.abs.forget();
    }

    /// Whether the focus changed since the last frame (`Some(focused)` when it did).
    pub fn focus_change(&mut self, focused: bool) -> Option<bool> {
        let changed = self.focused.is_some_and(|f| f != focused);
        self.focused = Some(focused);
        changed.then_some(focused)
    }

    /// Whether the pointer has been sending absolute positions.
    pub fn absolute(&self) -> bool {
        self.abs.latched
    }

    /// This frame's look delta (window pixels; +x right, +y down).
    pub fn frame(&mut self, f: &PointerFrame) -> Vec2 {
        if !f.play || !f.focused {
            self.reset();
            // the reports still say which kind of device this is
            for r in f.reports {
                self.abs.classify(*r);
            }
            return Vec2::ZERO;
        }
        let offset = f.cursor.map(|p| p - f.window / 2.0);
        let take = self.settle.look(offset);
        let mut relative = Vec2::ZERO;
        let mut absolute = Vec2::ZERO;
        let mut any_absolute = false;
        for r in f.reports {
            match self.abs.classify(*r) {
                Report::Relative(d) => relative += d,
                Report::Absolute(p) => {
                    any_absolute = true;
                    absolute += self.abs.report(p);
                }
            }
        }
        if !take {
            return Vec2::ZERO;
        }
        if any_absolute || self.abs.latched {
            return absolute * f.window / AbsolutePointer::RANGE * Self::ABS_GAIN;
        }
        match offset {
            Some(o) if o != Vec2::ZERO => o,
            _ => relative,
        }
    }
}

/// A raw motion report.
#[derive(Debug, PartialEq)]
pub enum Report {
    /// motion (counts)
    Relative(Vec2),
    /// a position over the desktop (0..=65535 per axis)
    Absolute(Vec2),
}

/// An absolute pointer's reports turned into motion.
#[derive(Debug)]
pub struct AbsolutePointer {
    /// the last position
    last: Option<Vec2>,
    /// the pointer's usual movement per report (units), from the reports away from the edges
    step: f32,
    /// absolute reports have been seen (until a report that can only be motion: a negative component)
    latched: bool,
}

impl Default for AbsolutePointer {
    fn default() -> Self {
        AbsolutePointer { last: None, step: Self::STEP_START, latched: false }
    }
}

impl AbsolutePointer {
    /// the desktop's extent in absolute units
    pub const RANGE: f32 = 65535.0;
    /// a component this large is a position, never a motion (a mouse report is a few tens of counts)
    const ABSOLUTE_FROM: f32 = 2000.0;
    /// how near the range's end a position is held at the edge (the session's are exactly 0 and 65535)
    const EDGE: f32 = 64.0;
    /// a move longer than this between two reports is a jump (another device, the pointer entering the stream), no look
    const JUMP: f32 = Self::RANGE / 4.0;
    /// the usual step: at the start, and its limits (the session's trackpad: 50 to 600 units a report while moving)
    const STEP_START: f32 = 100.0;
    const STEP_MIN: f32 = 16.0;
    const STEP_MAX: f32 = 800.0;
    /// at an edge, a report that moves the free axis by less than this part of the usual step is movement into the edge
    const INTO_EDGE: f32 = 0.35;

    /// Whether a report is a position or motion. Positions are never negative; once they have been seen, small
    /// non-negative reports are positions too (near the desktop's top left corner).
    pub fn classify(&mut self, r: Vec2) -> Report {
        if r.x.abs() > Self::ABSOLUTE_FROM || r.y.abs() > Self::ABSOLUTE_FROM {
            self.latched = true;
        } else if r.x < 0.0 || r.y < 0.0 {
            if self.latched {
                self.latched = false;
                self.last = None;
            }
        }
        if self.latched { Report::Absolute(r) } else { Report::Relative(r) }
    }

    pub fn forget(&mut self) {
        self.last = None;
    }

    /// The movement (units, +x right, +y down) a position report stands for.
    pub fn report(&mut self, p: Vec2) -> Vec2 {
        let Some(prev) = self.last.replace(p) else { return Vec2::ZERO };
        let d = p - prev;
        if d.length() > Self::JUMP {
            return Vec2::ZERO;
        }
        // -1 / +1: held at the low / high edge of an axis
        let edge = |v: f32| {
            if v <= Self::EDGE {
                -1.0
            } else if v >= Self::RANGE - Self::EDGE {
                1.0
            } else {
                0.0
            }
        };
        let held = Vec2::new(edge(p.x), edge(p.y));
        if held == Vec2::ZERO {
            if d != Vec2::ZERO {
                self.step = (self.step * 0.9 + d.length() * 0.1).clamp(Self::STEP_MIN, Self::STEP_MAX);
            }
            return d;
        }
        let mut out = d;
        for axis in 0..2 {
            let dir = held[axis];
            // not held, or moving away from the edge: as reported
            if dir == 0.0 || d[axis] * dir < 0.0 {
                continue;
            }
            let free = d[1 - axis].abs();
            if free < self.step * Self::INTO_EDGE {
                out[axis] = dir * self.step.max(d[axis].abs());
            }
        }
        out
    }
}

/// Entering play (or the window regaining focus) puts the cursor at the window centre; its offset from the centre is
/// look input only once the cursor has been seen there. The warp lands a frame or more later, later still at a low
/// frame rate or through a streamed desktop: a fixed two frames read the way from the clicked menu row (the yes / no
/// box's Yes, below the centre) to the centre as one big look down (feedback 28, "Roman looking down" after Restart).
#[derive(Default)]
pub struct CursorSettle {
    settling: bool,
    frames: u32,
}

impl CursorSettle {
    /// frames always skipped, as before
    const MIN_FRAMES: u32 = 2;
    /// a cursor that never shows at the centre is taken as it is after this many frames
    const MAX_FRAMES: u32 = 30;
    /// how near the centre (window pixels) counts as there
    const CENTRED: f32 = 1.5;

    pub fn start(&mut self) {
        *self = CursorSettle { settling: true, frames: 0 };
    }

    /// Whether this frame's cursor offset from the centre (None: no cursor in the window) is look input.
    pub fn look(&mut self, offset: Option<Vec2>) -> bool {
        if !self.settling {
            return true;
        }
        self.frames += 1;
        let centred = offset.is_some_and(|o| o.length() <= Self::CENTRED);
        if (self.frames > Self::MIN_FRAMES && centred) || self.frames >= Self::MAX_FRAMES {
            self.settling = false;
        }
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const WINDOW: Vec2 = Vec2::new(1920.0, 1080.0);

    fn frame(look: &mut MouseLook, focused: bool, cursor: Option<Vec2>, reports: &[Vec2]) -> Vec2 {
        look.frame(&PointerFrame { play: true, focused, window: WINDOW, cursor, reports })
    }

    /// Settled in play with the cursor at the centre.
    fn settled() -> MouseLook {
        let mut l = MouseLook::default();
        l.reset();
        for _ in 0..4 {
            frame(&mut l, true, Some(WINDOW / 2.0), &[]);
        }
        l
    }

    const DIRS: [Vec2; 4] = [Vec2::new(1.0, 0.0), Vec2::new(-1.0, 0.0), Vec2::new(0.0, 1.0), Vec2::new(0.0, -1.0)];

    /// Feedback 28: the way from the clicked Yes (below the centre) to the centre is never a look, however many frames
    /// the warp takes; once the cursor shows at the centre, its moves are; a cursor that never gets there is taken after
    /// MAX_FRAMES.
    #[test]
    fn cursor_settles_at_the_centre() {
        let mut s = CursorSettle::default();
        assert!(s.look(Some(Vec2::new(3.0, 4.0))), "not settling: every offset is look input");
        s.start();
        let yes = Some(Vec2::new(0.0, 180.0));
        for f in 0..10 {
            assert!(!s.look(yes), "frame {f}: the warp has not landed");
        }
        assert!(!s.look(Some(Vec2::new(0.5, 0.0))), "the frame the cursor shows at the centre");
        assert!(s.look(Some(Vec2::new(0.0, 6.0))), "then its moves are look input");
        s.start();
        assert!(!s.look(Some(Vec2::ZERO)) && !s.look(Some(Vec2::ZERO)), "the first two frames are always skipped");
        assert!(!s.look(Some(Vec2::ZERO)));
        assert!(s.look(Some(Vec2::ZERO)));
        s.start();
        let mut n = 0;
        while !s.look(yes) {
            n += 1;
            assert!(n <= CursorSettle::MAX_FRAMES, "a cursor that never centres is taken eventually");
        }
    }

    /// Losing the focus and getting it back: a mouse turns the view in all four directions again once the cursor is
    /// back at the centre, and nothing done while away is a look.
    #[test]
    fn focus_loss_and_regain_restore_the_look() {
        let mut l = settled();
        assert_eq!(l.focus_change(true), None);
        for d in DIRS {
            assert_eq!(frame(&mut l, true, Some(WINDOW / 2.0), &[d * 5.0]), d * 5.0, "relative motion {d}");
        }
        assert_eq!(l.focus_change(false), Some(false));
        // away: another program's pointer, the cursor wherever it is
        for _ in 0..20 {
            assert_eq!(frame(&mut l, false, Some(Vec2::new(5.0, 900.0)), &[Vec2::new(-40.0, 30.0)]), Vec2::ZERO);
        }
        assert_eq!(l.focus_change(true), Some(true));
        // back: the cursor still where it was until the warp lands
        assert_eq!(frame(&mut l, true, Some(Vec2::new(5.0, 900.0)), &[]), Vec2::ZERO, "the way back to the centre is no look");
        for _ in 0..3 {
            frame(&mut l, true, Some(WINDOW / 2.0), &[]);
        }
        for d in DIRS {
            assert_eq!(frame(&mut l, true, Some(WINDOW / 2.0), &[d * 5.0]), d * 5.0, "relative motion {d} after the focus came back");
            assert_eq!(frame(&mut l, true, Some(WINDOW / 2.0 + d * 7.0), &[]), d * 7.0, "cursor offset {d} after the focus came back");
        }
    }

    /// Absolute reports: the change between them is the look; two in one frame do not add up to a jump; the cursor's
    /// offset (where the injected position put it) is not a look.
    #[test]
    fn absolute_reports_are_their_change() {
        let mut l = settled();
        let p = Vec2::new(30000.0, 30000.0);
        assert_eq!(frame(&mut l, true, Some(WINDOW / 2.0), &[p]), Vec2::ZERO, "the first position only sets the start");
        assert!(l.absolute());
        let scale = WINDOW / AbsolutePointer::RANGE * MouseLook::ABS_GAIN;
        for d in DIRS {
            let at = p + d * 100.0;
            let look = frame(&mut l, true, Some(Vec2::new(400.0, 1000.0)), &[at]);
            assert!((look - d * 100.0 * scale).length() < 1e-3, "{d}: {look}");
            frame(&mut l, true, Some(WINDOW / 2.0), &[p]);
        }
        let two = frame(&mut l, true, Some(WINDOW / 2.0), &[p + Vec2::X * 50.0, p + Vec2::X * 100.0]);
        assert!((two - Vec2::X * 100.0 * scale).length() < 1e-3, "two reports in a frame: {two}");
    }

    /// Session 2026-10-18_17-00-51: the pointer held at the bottom (y 65535, bookmark 4) and at the left (x 0, bookmark 6)
    /// edge; moving on into the edge still turns the view that way, and moving out of it works as before.
    #[test]
    fn a_pointer_held_at_an_edge_still_turns() {
        for (start, into) in [
            (Vec2::new(15000.0, 65535.0), Vec2::Y),
            (Vec2::new(0.0, 45000.0), Vec2::NEG_X),
            (Vec2::new(65535.0, 30000.0), Vec2::X),
            (Vec2::new(20000.0, 0.0), Vec2::NEG_Y),
        ] {
            let mut l = settled();
            frame(&mut l, true, None, &[start - into * 300.0]);
            frame(&mut l, true, None, &[start]);
            // pushing on: the same position again, or with a little drift along the edge
            let side = Vec2::new(into.y, into.x) * 10.0;
            let mut total = Vec2::ZERO;
            for i in 0..20 {
                total += frame(&mut l, true, None, &[start + if i % 2 == 0 { side } else { Vec2::ZERO }]);
            }
            assert!(total.dot(into) > 0.0 && total.dot(into) > total.dot(Vec2::new(into.y, into.x)).abs() * 5.0, "{start} into {into}: {total}");
            // along the edge (a deliberate move on the free axis): no push
            let along = frame(&mut l, true, None, &[start + side * 30.0]);
            assert!(along.dot(into).abs() < 1e-3 && along.length() > 0.0, "{start}: along {along}");
            // out of the edge: as reported
            let back = frame(&mut l, true, None, &[start + side * 30.0 - into * 200.0]);
            assert!(back.dot(into) < 0.0, "{start}: away from the edge {back}");
        }
    }

    /// A mouse after a streamed session (Moonlight switched to relative): its negative reports end the absolute mode.
    #[test]
    fn relative_motion_ends_the_absolute_mode() {
        let mut l = settled();
        frame(&mut l, true, None, &[Vec2::new(30000.0, 30000.0)]);
        assert!(l.absolute());
        frame(&mut l, true, None, &[Vec2::new(-3.0, 2.0)]);
        assert!(!l.absolute());
        assert_eq!(frame(&mut l, true, Some(WINDOW / 2.0), &[Vec2::new(4.0, 2.0)]), Vec2::new(4.0, 2.0));
    }
}
