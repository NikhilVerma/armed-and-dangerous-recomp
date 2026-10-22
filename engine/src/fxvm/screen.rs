//! Screen effects: the post-process effect manager (PostEffects, Game+0xa9c; render_r3.cpp:1085..), started by fx
//! opcodes 0x5b..0x61 / 0x70 / 0x71 / 0x77 on an fx object (the effect's owner) and by the pause (Game_PauseEffect
//! 0x00403150: the blurred game under every paused screen). Each effect runs three phases (in, hold, out; defaults
//! 1, -1 = hold forever, 1: PostEffect_Start's [0x00588da8..b0]), weighted t / in, 1, 1 - t / out, optionally by
//! distance (0x5f) and by the distance to the view's edges (0x61), and folds into the frame's accumulators
//! (PostEffect_Apply 0x0047c200): colour (glow), blur, screen texture, their ring variants, fade.
//!
//! The draw (PostProcess_Pass 0x0047c600) is `screen_post`: the blur (Post_DrawBlur 0x0047d0f0) and the fade
//! (Post_DrawFade 0x0047cff0) on the 3D view, under the HUD as in the original. Not drawn yet (debt:
//! docs/remaster-engine.md): the glow (Post_DrawColour 0x0047d8a0: its fixed-function stage and blend states are
//! not decoded; fx_lev_1 / fx_lev_4 / fx_lev3_m3 hold one all level, fx_explosion_mountain / turret / multi_kill
//! flash one), the screen-texture overlays (Post_DrawType3 0x0047d5c0 / Post_DrawType3Rings 0x0047d6d0: the
//! renderer texture of its virtual +0xe0 is not identified; fx_explosion_black_hole starts one) and the ring blur
//! (type 2, Post_DrawBlurRings 0x0047d230: started only by the object update 0x00467a20, class vtable 0x005a5818,
//! not ported).

use crate::levelreset::LevelAppExt;
use bevy::prelude::*;

/// What an effect adds (the node's type +8 and its parameters +0x44..).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ScreenKind {
    /// type 0 (0x0047be00): glow p0 (scene into the glow), p1 (decay per level), p2 (glow strength), p3 (persistence)
    Colour([f32; 4]),
    /// type 1 (0x0047be60): blur amount
    Blur(f32),
    // type 2 (0x0047bf20, blur between two circles): started only by 0x00467a20 (not ported; see the header)
    /// type 3 (0x0047bec0): screen texture overlay amount
    Overlay(f32),
    /// type 4 (0x0047bf80): overlay between two circles
    OverlayRings([f32; 8]),
    /// type 5 (0x0047bda0): fade to a colour (game colour 0..1) with alpha
    Fade(Vec3, f32),
}

/// Who an effect belongs to (the node's id +0xc): an fx object (the opcodes use the object as the id) or the pause
/// (id 1 in Game_PauseEffect 0x00403150).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum EffectOwner {
    Object(Entity),
    Pause,
}

/// A request to the manager (sent by the fx VM and the pause).
#[derive(Clone, Debug)]
pub enum ScreenRequest {
    Start(EffectOwner, ScreenKind),
    /// PostEffects_SetTimes 0x0047c1a0 (first only): in, hold, out of the owner's newest effect
    Phases(EffectOwner, [f32; 3]),
    /// 0x0047c0a0 (first only): near, far (a negative distance switches it off)
    DistanceFade(EffectOwner, f32, f32),
    /// 0x0047c140 (first only): near, far
    EdgeFade(EffectOwner, f32, f32),
    /// PostEffects_SetIgnorePause 0x0047c1d0 (first only): the effect's clock keeps running while the game is paused
    IgnorePause(EffectOwner, bool),
    /// PostEffects_Stop 0x0047bfe0 on every effect of the owner: freed at once (`now`), else to the fade-out phase
    Stop { owner: EffectOwner, now: bool },
    /// fx opcode 0x60 (render_r2b.cpp:1151): the object lets its effects go (obj+0x634 = 0): they run on by their
    /// phases, no longer follow it nor stop with it
    Release(EffectOwner),
    /// 0x0047c060 via 0x0041bd30 (all of the owner's effects): the effect's position (engine space)
    Position(EffectOwner, Vec3),
}

#[derive(Clone, Debug)]
pub struct Effect {
    pub owner: EffectOwner,
    pub kind: ScreenKind,
    /// 0 in, 1 hold, 2 out
    pub phase: usize,
    pub time: f32,
    pub durations: [f32; 3],
    pub position: Vec3,
    /// flag 1: weight x clamp(distance * scale + offset)
    pub distance: Option<(f32, f32)>,
    /// flag 2: weight x clamp(offset - scale * the distance to the nearest view plane)
    pub edge: Option<(f32, f32)>,
    /// flag 4: the clock runs while the game is paused
    pub ignore_pause: bool,
    /// its object still holds it (obj+0x634): the object's removal stops it (FxObject_Remove 0x00418db0)
    pub attached: bool,
}

/// The frame's accumulators (+0x690..+0x6ef).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Accum {
    /// min of the weighted first two glow parameters (start 1), max of the last two (start 0)
    pub colour: [f32; 4],
    pub blur: f32,
    pub overlay: f32,
    pub overlay_rings: Option<[f32; 8]>,
    pub fade: (Vec3, f32),
}

impl Default for Accum {
    fn default() -> Self {
        Accum { colour: [1.0, 1.0, 0.0, 0.0], blur: 0.0, overlay: 0.0, overlay_rings: None, fade: (Vec3::ZERO, 0.0) }
    }
}

/// The live effects, newest first (PostEffect_MoveToList 0x0047c030 puts a started node at the head of the live
/// list: "first only" requests and the accumulation order go by that). 16 nodes in the original
/// (PostEffects_Construct 0x0047bd10); the remaster keeps 64.
#[derive(Resource, Default)]
pub struct ScreenEffects {
    pub effects: Vec<Effect>,
    pub accum: Accum,
}

const CAPACITY: usize = 64;

/// The degenerate near == far fade nudge ([0x005bcc64] = 1e-5).
const FADE_NUDGE: f32 = 1e-5;

/// Game_PauseEffect 0x00403150: the blur amount ([0x005bc15c] = 0.75) and its phases (0.1 in, held, 0.5 out).
const PAUSE_BLUR: f32 = 0.75;
const PAUSE_PHASES: [f32; 3] = [0.1, -1.0, 0.5];

pub(crate) fn clamp01(v: f32) -> f32 {
    if v.is_nan() { 0.0 } else { v.clamp(0.0, 1.0) }
}

/// A fade's (scale, offset) from near / far: full at near, none at far.
fn fade_ramp(near: f32, far: f32) -> (f32, f32) {
    let far = if far == near { near + FADE_NUDGE } else { far };
    let s = 1.0 / (near - far);
    (s, -s * far)
}

impl ScreenEffects {
    pub fn live(&self) -> usize {
        self.effects.len()
    }

    fn newest(&mut self, owner: EffectOwner) -> Option<&mut Effect> {
        self.effects.iter_mut().find(|e| e.owner == owner)
    }

    fn all(&mut self, owner: EffectOwner) -> impl Iterator<Item = &mut Effect> {
        self.effects.iter_mut().filter(move |e| e.owner == owner)
    }

    pub fn apply(&mut self, r: ScreenRequest) {
        match r {
            ScreenRequest::Start(owner, kind) => {
                // the opcodes set obj+0x634 again: every effect with the id goes with the object once more
                self.all(owner).for_each(|e| e.attached = true);
                if self.effects.len() < CAPACITY {
                    let e = Effect {
                        owner,
                        kind,
                        phase: 0,
                        time: 0.0,
                        durations: [1.0, -1.0, 1.0],
                        position: Vec3::ZERO,
                        distance: None,
                        edge: None,
                        ignore_pause: false,
                        attached: matches!(owner, EffectOwner::Object(_)),
                    };
                    self.effects.insert(0, e);
                }
            }
            ScreenRequest::Phases(owner, d) => {
                if let Some(e) = self.newest(owner) {
                    e.durations = d;
                }
            }
            ScreenRequest::DistanceFade(owner, near, far) => {
                if let Some(e) = self.newest(owner) {
                    e.distance = (near >= 0.0 && far >= 0.0).then(|| fade_ramp(near, far));
                }
            }
            ScreenRequest::EdgeFade(owner, near, far) => {
                if let Some(e) = self.newest(owner) {
                    e.edge = Some(fade_ramp(near, far));
                }
            }
            ScreenRequest::IgnorePause(owner, on) => {
                if let Some(e) = self.newest(owner) {
                    e.ignore_pause = on;
                }
            }
            ScreenRequest::Stop { owner, now: true } => self.effects.retain(|e| e.owner != owner),
            ScreenRequest::Stop { owner, now: false } => self.all(owner).for_each(|e| {
                if e.phase != 2 {
                    e.phase = 2;
                    e.time = 0.0;
                }
            }),
            ScreenRequest::Release(owner) => self.all(owner).for_each(|e| e.attached = false),
            ScreenRequest::Position(owner, p) => self.all(owner).for_each(|e| e.position = p),
        }
    }

    /// FxObject_Remove 0x00418db0: an object removed while it holds its effects stops them (PostEffects_Stop, now 0:
    /// to the fade-out phase). `alive`: whether the fx object still exists.
    pub fn stop_orphans(&mut self, alive: impl Fn(Entity) -> bool) {
        let gone: Vec<EffectOwner> = self
            .effects
            .iter()
            .filter(|e| e.attached && matches!(e.owner, EffectOwner::Object(o) if !alive(o)))
            .map(|e| e.owner)
            .collect();
        for owner in gone {
            self.apply(ScreenRequest::Stop { owner, now: false });
            self.apply(ScreenRequest::Release(owner));
        }
    }

    /// PostProcess_Pass 0x0047c600's accumulation: every effect advanced and folded in (PostEffect_Apply 0x0047c200;
    /// newest first). `game_dt`: the game's step (0 while paused); `real_dt`: the frame's, for the effects that run
    /// while paused. `cam`: the camera's position and the view planes' distance function for the fades.
    pub fn update(&mut self, game_dt: f32, real_dt: f32, cam: Vec3, edge_distance: &dyn Fn(Vec3) -> f32) {
        let mut acc = Accum::default();
        self.effects.retain_mut(|e| {
            e.time += if e.ignore_pause { real_dt } else { game_dt };
            if e.time >= e.durations[e.phase] && e.durations[e.phase] >= 0.0 {
                loop {
                    if e.durations[e.phase] < 0.0 {
                        break;
                    }
                    e.time = 0.0;
                    e.phase += 1;
                    if e.phase == 3 {
                        return false;
                    }
                    if !(e.time >= e.durations[e.phase]) || e.durations[e.phase] < 0.0 {
                        break;
                    }
                }
            }
            let mut w = match e.phase {
                0 => e.time / e.durations[0],
                2 => 1.0 - e.time / e.durations[2],
                _ => 1.0,
            };
            if let Some((s, o)) = e.distance {
                w *= clamp01(e.position.distance(cam) * s + o);
            }
            if let Some((s, o)) = e.edge {
                w *= clamp01(o - s * edge_distance(e.position));
            }
            match e.kind {
                ScreenKind::Colour(p) => {
                    let c = (p[0] - 1.0) * w + 1.0;
                    if c != 1.0 {
                        acc.colour[0] = acc.colour[0].min(c);
                        acc.colour[1] = acc.colour[1].min((p[1] - 1.0) * w + 1.0);
                        acc.colour[2] = acc.colour[2].max(p[2] * w);
                        acc.colour[3] = acc.colour[3].max(p[3] * w);
                    }
                }
                ScreenKind::Blur(a) => acc.blur = acc.blur.max(a * w),
                ScreenKind::Overlay(a) => acc.overlay = acc.overlay.max(a * w),
                ScreenKind::OverlayRings(mut r) => {
                    r[3] *= w;
                    r[7] *= w;
                    if r[3] != 0.0 || r[7] != 0.0 {
                        acc.overlay_rings = Some(r);
                    }
                }
                ScreenKind::Fade(c, a) => {
                    if a * w > acc.fade.1 {
                        acc.fade = (c, a * w);
                    }
                }
            }
            true
        });
        self.accum = acc;
    }
}

pub struct ScreenFxPlugin;

impl Plugin for ScreenFxPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<ScreenEffects>()
            .add_systems(Update, (pause_blur, update, super::screen_post::follow_effects).chain().after(super::FxVmSet))
            .add_plugins(super::screen_post::ScreenPostPlugin);
    }
}

/// Game_PauseEffect 0x00403150, from Game_SetPaused 0x004031e0 on the first pause and the last unpause (every
/// pausing screen asks for it: the pause menu, mission messages, the pub and the stats): on pause the blur (id 1)
/// restarts at once, fades in over 0.1 s and holds, its clock running while paused; on resume it fades out over
/// 0.5 s.
fn pause_blur(vtime: Res<Time<Virtual>>, mut fx: ResMut<ScreenEffects>, mut was_paused: Local<bool>) {
    let paused = vtime.is_paused();
    if paused == *was_paused {
        return;
    }
    *was_paused = paused;
    let owner = EffectOwner::Pause;
    if paused {
        fx.apply(ScreenRequest::Stop { owner, now: true });
        fx.apply(ScreenRequest::Start(owner, ScreenKind::Blur(PAUSE_BLUR)));
        fx.apply(ScreenRequest::Phases(owner, PAUSE_PHASES));
        fx.apply(ScreenRequest::IgnorePause(owner, true));
    } else {
        fx.apply(ScreenRequest::Stop { owner, now: false });
    }
}

fn update(
    time: Res<Time>,
    real: Res<Time<Real>>,
    mut fx: ResMut<ScreenEffects>,
    cam: Query<(&GlobalTransform, &Projection), (With<Camera3d>, With<crate::display::GameView>)>,
) {
    let Some((c, proj)) = cam.iter().next() else { return };
    let (cp, fwd, right, up) = (c.translation(), c.forward().as_vec3(), c.right().as_vec3(), c.up().as_vec3());
    let half_v = match proj {
        Projection::Perspective(p) => p.fov * 0.5,
        _ => 0.5,
    };
    let aspect = match proj {
        Projection::Perspective(p) => p.aspect_ratio,
        _ => 1.0,
    };
    let half_h = (half_v.tan() * aspect).atan();
    // signed distance to the nearest side plane of the view (positive inside); the original takes the scene pass's
    // frustum planes (0x00488990; no shipped script uses the edge fade, 0x61)
    let edge = move |p: Vec3| {
        let d = p - cp;
        let (x, y, z) = (d.dot(right), d.dot(up), d.dot(fwd));
        let side = |a: f32, half: f32| z * half.sin() - a.abs() * half.cos();
        side(x, half_h).min(side(y, half_v)).min(z)
    };
    fx.update(time.delta_secs(), real.delta_secs(), cp, &edge);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(fx: &mut ScreenEffects, secs: f32) {
        run_paused(fx, secs, false);
    }

    fn run_paused(fx: &mut ScreenEffects, secs: f32, paused: bool) {
        let mut t = 0.0;
        while t < secs {
            fx.update(if paused { 0.0 } else { 0.01 }, 0.01, Vec3::ZERO, &|_| 100.0);
            t += 0.01;
        }
    }

    fn obj(i: u32) -> EffectOwner {
        EffectOwner::Object(Entity::from_raw_u32(i).unwrap())
    }

    #[test]
    fn phases_in_hold_out() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(obj(7), ScreenKind::Fade(Vec3::ONE, 0.8)));
        fx.apply(ScreenRequest::Phases(obj(7), [0.5, 1.0, 0.5]));
        run(&mut fx, 0.25);
        assert!((fx.accum.fade.1 - 0.4).abs() < 0.03, "{:?}", fx.accum.fade);
        run(&mut fx, 0.5);
        assert!((fx.accum.fade.1 - 0.8).abs() < 1e-4);
        run(&mut fx, 1.0);
        assert!(fx.accum.fade.1 < 0.8 && fx.accum.fade.1 > 0.0, "{:?}", fx.accum.fade);
        run(&mut fx, 0.5);
        assert_eq!(fx.live(), 0);
        assert_eq!(fx.accum.fade.1, 0.0);
    }

    #[test]
    fn hold_forever_and_stop() {
        let mut fx = ScreenEffects::default();
        // fx_lev_1: colour (0.9, 0.5, 2, 0.15), phases (0, -1, 0): at full weight at once, held
        fx.apply(ScreenRequest::Start(obj(1), ScreenKind::Colour([0.9, 0.5, 2.0, 0.15])));
        fx.apply(ScreenRequest::Phases(obj(1), [0.0, -1.0, 0.0]));
        run(&mut fx, 2.0);
        assert_eq!(fx.live(), 1);
        assert!((fx.accum.colour[0] - 0.9).abs() < 1e-5 && (fx.accum.colour[2] - 2.0).abs() < 1e-5);
        fx.apply(ScreenRequest::Stop { owner: obj(1), now: false });
        run(&mut fx, 0.05);
        assert_eq!(fx.live(), 0);
    }

    /// fx_explosion_black_hole: white flash (phases 0.01 / 0.1 / 0.01), then a black fade with its own phases: each
    /// 0x5e sets only the newest effect (render_r2b.cpp:1124 first only), so the flash stays a flash.
    #[test]
    fn phases_set_the_newest_only() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(obj(3), ScreenKind::Fade(Vec3::ONE, 0.8)));
        fx.apply(ScreenRequest::Phases(obj(3), [0.01, 0.1, 0.01]));
        fx.apply(ScreenRequest::Start(obj(3), ScreenKind::Fade(Vec3::ZERO, 0.3)));
        fx.apply(ScreenRequest::Phases(obj(3), [5.0, 0.2, 2.8]));
        let flash = fx.effects.iter().find(|e| e.kind == ScreenKind::Fade(Vec3::ONE, 0.8)).unwrap();
        assert_eq!(flash.durations, [0.01, 0.1, 0.01]);
        run(&mut fx, 0.2);
        assert_eq!(fx.live(), 1);
    }

    /// FxObject_Remove: the object's effects go to their fade-out; released ones (0x60) run on.
    #[test]
    fn removal_stops_held_effects() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(obj(4), ScreenKind::Fade(Vec3::ONE, 0.5)));
        fx.apply(ScreenRequest::Phases(obj(4), [0.0, -1.0, 0.5]));
        fx.apply(ScreenRequest::Start(obj(5), ScreenKind::Fade(Vec3::ONE, 0.5)));
        fx.apply(ScreenRequest::Phases(obj(5), [0.0, -1.0, 0.5]));
        fx.apply(ScreenRequest::Release(obj(5)));
        run(&mut fx, 0.1);
        fx.stop_orphans(|_| false);
        run(&mut fx, 0.6);
        assert_eq!(fx.live(), 1);
        assert_eq!(fx.effects[0].owner, obj(5));
    }

    /// Game_PauseEffect: the pause blur runs on the real clock while game effects stand still.
    #[test]
    fn ignore_pause() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(EffectOwner::Pause, ScreenKind::Blur(PAUSE_BLUR)));
        fx.apply(ScreenRequest::Phases(EffectOwner::Pause, PAUSE_PHASES));
        fx.apply(ScreenRequest::IgnorePause(EffectOwner::Pause, true));
        fx.apply(ScreenRequest::Start(obj(6), ScreenKind::Fade(Vec3::ONE, 1.0)));
        run_paused(&mut fx, 0.2, true);
        assert!((fx.accum.blur - PAUSE_BLUR).abs() < 1e-5, "{}", fx.accum.blur);
        assert_eq!(fx.accum.fade.1, 0.0);
        fx.apply(ScreenRequest::Stop { owner: EffectOwner::Pause, now: false });
        run(&mut fx, 0.6);
        assert_eq!(fx.accum.blur, 0.0);
    }

    #[test]
    fn distance_fade() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(obj(2), ScreenKind::Blur(1.0)));
        fx.apply(ScreenRequest::Phases(obj(2), [0.0, -1.0, 1.0]));
        // full at 100, none at 300
        fx.apply(ScreenRequest::DistanceFade(obj(2), 100.0, 300.0));
        fx.apply(ScreenRequest::Position(obj(2), Vec3::new(200.0, 0.0, 0.0)));
        run(&mut fx, 0.02);
        assert!((fx.accum.blur - 0.5).abs() < 1e-4, "{}", fx.accum.blur);
    }
}
