//! Screen effects: the post-process effect manager (PostEffects, Game+0xa9c; render_r3.cpp:1085..), started by fx
//! opcodes 0x5b..0x61 / 0x70 / 0x71 / 0x77 on an fx object (the effect's id). Each effect runs three phases (in,
//! hold, out; defaults 1, -1 = hold forever, 1: PostEffect_Start's [0x00588da8..b0]), weighted t / in, 1, 1 - t / out,
//! optionally by distance (0x5f) and by the distance to the view's edges (0x61), and folds into the frame's
//! accumulators (PostEffect_Apply 0x0047c200): colour (glow), blur, screen texture, their ring variants, fade.
//! The draw (PostProcess_Pass 0x0047c600): the fade is a full-screen quad (Post_DrawFade 0x0047cff0); here a UI
//! overlay. The glow (Post_DrawColour 0x0047d8a0), the blurs (0x0047d0f0, 0x0047d230) and the screen-texture overlays
//! (0x0047d5c0, 0x0047d6d0) are computed but not drawn yet (debt: docs/remaster-engine.md); lev_1_m1 uses them only in
//! the end explosions (fx_explosion_mountain, fx_explosion_black_hole).

use bevy::prelude::*;

/// What an effect adds (the node's type +8 and its parameters +0x44..).
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum ScreenKind {
    /// type 0 (0x0047be00): glow p0 (scene into the glow), p1 (decay per level), p2 (glow strength), p3 (persistence)
    Colour([f32; 4]),
    /// type 1 (0x0047be60): blur amount
    Blur(f32),
    // type 2 (0x0047bf20, blur between two circles) is started by no opcode
    /// type 3 (0x0047bec0): screen texture overlay amount
    Overlay(f32),
    /// type 4 (0x0047bf80): overlay between two circles
    OverlayRings([f32; 8]),
    /// type 5 (0x0047bda0): fade to a colour (rgb 0..1) with alpha
    Fade(Vec3, f32),
}

/// A request from an fx object (sent by the fx VM).
#[derive(Clone, Debug)]
pub enum ScreenRequest {
    Start(u64, ScreenKind),
    /// PostEffects_SetPhases 0x0047c1a0: in, hold, out of the effects with this id
    Phases(u64, f32, f32, f32),
    /// 0x0047c0a0: near, far (a negative distance switches it off)
    DistanceFade(u64, f32, f32),
    /// 0x0047c140: near, far
    EdgeFade(u64, f32, f32),
    /// PostEffects_Stop 0x0047bfe0 (now = 0: to the fade-out phase)
    Stop(u64),
    /// 0x0047c060 via 0x0041bd30: the effect's position (engine space)
    Position(u64, Vec3),
}

#[derive(Clone, Debug)]
pub struct Effect {
    pub id: u64,
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

/// The live effects (16 nodes in the original: PostEffects_Construct 0x0047bd10; the remaster keeps 64).
#[derive(Resource, Default)]
pub struct ScreenEffects {
    pub effects: Vec<Effect>,
    pub accum: Accum,
}

const CAPACITY: usize = 64;

fn clamp01(v: f32) -> f32 {
    if v.is_nan() { 0.0 } else { v.clamp(0.0, 1.0) }
}

impl ScreenEffects {
    pub fn live(&self) -> usize {
        self.effects.len()
    }

    pub fn apply(&mut self, r: ScreenRequest) {
        let each = |s: &mut Self, id: u64, f: &mut dyn FnMut(&mut Effect)| {
            for e in s.effects.iter_mut().filter(|e| e.id == id) {
                f(e);
            }
        };
        match r {
            ScreenRequest::Start(id, kind) => {
                if self.effects.len() < CAPACITY {
                    self.effects.push(Effect { id, kind, phase: 0, time: 0.0, durations: [1.0, -1.0, 1.0], position: Vec3::ZERO, distance: None, edge: None });
                }
            }
            ScreenRequest::Phases(id, i, h, o) => each(self, id, &mut |e| e.durations = [i, h, o]),
            ScreenRequest::DistanceFade(id, near, far) => each(self, id, &mut |e| {
                e.distance = if near < 0.0 || far < 0.0 {
                    None
                } else {
                    let far = if far == near { far + 1e-3 } else { far };
                    let s = 1.0 / (near - far);
                    Some((s, -s * far))
                }
            }),
            ScreenRequest::EdgeFade(id, near, far) => each(self, id, &mut |e| {
                let far = if far == near { far + 1e-3 } else { far };
                let s = 1.0 / (near - far);
                e.edge = Some((s, -s * far));
            }),
            ScreenRequest::Stop(id) => each(self, id, &mut |e| {
                if e.phase != 2 {
                    e.phase = 2;
                    e.time = 0.0;
                }
            }),
            ScreenRequest::Position(id, p) => each(self, id, &mut |e| e.position = p),
        }
    }

    /// PostProcess_Pass 0x0047c600's accumulation: every effect advanced by dt and folded in (PostEffect_Apply
    /// 0x0047c200); `cam`: the camera's position and the view planes' distances function for the fades.
    pub fn update(&mut self, dt: f32, cam: Vec3, edge_distance: &dyn Fn(Vec3) -> f32) {
        let mut acc = Accum::default();
        self.effects.retain_mut(|e| {
            e.time += dt;
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

/// The fade overlay (Post_DrawFade 0x0047cff0: a full-screen quad of the colour at the alpha).
#[derive(Component)]
struct FadeOverlay;

pub struct ScreenFxPlugin;

impl Plugin for ScreenFxPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ScreenEffects>().add_systems(Update, update.after(super::FxVmSet));
    }
}

fn update(
    mut commands: Commands,
    time: Res<Time>,
    mut fx: ResMut<ScreenEffects>,
    cam: Query<(&GlobalTransform, &Projection), With<Camera3d>>,
    mut overlay: Query<(Entity, &mut BackgroundColor), With<FadeOverlay>>,
) {
    let Ok((c, proj)) = cam.single() else { return };
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
    // signed distance to the nearest side plane of the view (positive inside)
    let edge = move |p: Vec3| {
        let d = p - cp;
        let (x, y, z) = (d.dot(right), d.dot(up), d.dot(fwd));
        let side = |a: f32, half: f32| z * half.sin() - a.abs() * half.cos();
        side(x, half_h).min(side(y, half_v)).min(z)
    };
    fx.update(time.delta_secs(), cp, &edge);
    let (colour, alpha) = fx.accum.fade;
    match (overlay.single_mut(), alpha > 0.0) {
        (Ok((_, mut bg)), true) => bg.0 = Color::srgba(colour.x, colour.y, colour.z, clamp01(alpha)),
        (Ok((e, _)), false) => commands.entity(e).despawn(),
        (Err(_), true) => {
            commands.spawn((
                Node { position_type: PositionType::Absolute, width: Val::Percent(100.0), height: Val::Percent(100.0), ..default() },
                BackgroundColor(Color::srgba(colour.x, colour.y, colour.z, clamp01(alpha))),
                GlobalZIndex(i32::MAX - 16),
                FadeOverlay,
                Name::new("fx_screen_fade"),
            ));
        }
        (Err(_), false) => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(fx: &mut ScreenEffects, secs: f32) {
        let mut t = 0.0;
        while t < secs {
            fx.update(0.01, Vec3::ZERO, &|_| 100.0);
            t += 0.01;
        }
    }

    #[test]
    fn phases_in_hold_out() {
        let mut fx = ScreenEffects::default();
        // fx_explosion_black_hole's white flash: fade (1, 1, 1, 0.8), phases 0.01 / 0.1 / 0.01... here 0.5 / 1 / 0.5
        fx.apply(ScreenRequest::Start(7, ScreenKind::Fade(Vec3::ONE, 0.8)));
        fx.apply(ScreenRequest::Phases(7, 0.5, 1.0, 0.5));
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
        fx.apply(ScreenRequest::Start(1, ScreenKind::Colour([0.9, 0.5, 2.0, 0.15])));
        fx.apply(ScreenRequest::Phases(1, 0.0, -1.0, 0.0));
        run(&mut fx, 2.0);
        assert_eq!(fx.live(), 1);
        assert!((fx.accum.colour[0] - 0.9).abs() < 1e-5 && (fx.accum.colour[2] - 2.0).abs() < 1e-5);
        fx.apply(ScreenRequest::Stop(1));
        run(&mut fx, 0.05);
        assert_eq!(fx.live(), 0);
    }

    #[test]
    fn distance_fade() {
        let mut fx = ScreenEffects::default();
        fx.apply(ScreenRequest::Start(2, ScreenKind::Blur(1.0)));
        fx.apply(ScreenRequest::Phases(2, 0.0, -1.0, 1.0));
        // full at 100, none at 300
        fx.apply(ScreenRequest::DistanceFade(2, 100.0, 300.0));
        fx.apply(ScreenRequest::Position(2, Vec3::new(200.0, 0.0, 0.0)));
        run(&mut fx, 0.02);
        assert!((fx.accum.blur - 0.5).abs() < 1e-4, "{}", fx.accum.blur);
    }
}
