//! Streamers: ribbon trails of fx objects (opcodes 0x4d..0x57; render_r4.cpp). The original's manager Game+0xa94
//! (Streamers 0x00494d90, blocks Streamers_AllocBlock 0x00494e30, Streamers_Spawn 0x00494ee0 / Streamers_Free
//! 0x00494f20, the list links StreamerList_Link 0x00494fc0, Streamers_Clear / Destruct 0x00494df0 / 0x00494de0) is a
//! Vec here; a streamer: Streamer_Init 0x00495000 (flags 3, the parameters or the defaults 0x00494d40), Streamer_Stop
//! 0x00494f10 -> 0x004952f0, Streamer_Reset 0x00495040.
//!
//! One point per frame at the object while it emits (Streamer_AddPoint 0x004958a0; the original's ring holds 63, a
//! span of time here: `RING_SPAN`), points older than the life dropped (Streamer_Update 0x00495300 via
//! Streamers_Update 0x00494f30); neither runs while the game is paused (Game+0x2684); drawn as a
//! camera-facing strip whose half-width, colour and alpha are Bezier curves of the point's age / life (4 keys each:
//! Streamer_SetWidthCurve 0x00495060, Streamer_SetAlpha / Red / Green / Blue 0x00495080 / 0x00495100 / 0x00495180 /
//! 0x00495200), Streamer_Draw 0x00495380 via Streamers_Draw 0x00494f80.

use super::FxVm;
use crate::level::game_to_engine;
use bevy::asset::RenderAssetUsages;
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::prelude::*;
use serde_json::Value;

/// How far back a trail reaches at most: the original's ring of 64 slots keeps 63 points (Streamer_AddPoint
/// 0x004958a0, mask +0x6c), one per game frame, so at its 60 Hz a trail covers 62 frames whatever its life (the
/// shipped lives run 0.3 .. 3 s). Expressed as time, the trail looks the same at any frame rate: a ring of 63
/// points at 144 Hz covered only 0.43 s (docs/engine-limits.md R13).
pub const RING_SPAN: f32 = 62.0 / 60.0;
/// A bound on the points per streamer (a frame rate of about 1000 Hz over `RING_SPAN`).
const MAX_POINTS: usize = 1024;

pub struct Streamer {
    pub owner: Entity,
    /// 0x4e: the owner's attach point it follows (model space); None: the owner's origin
    pub point: Option<Mat4>,
    /// Streamer_SetLifetime 0x00495050 (+0x24, opcode 0x54)
    pub life: f32,
    pub width: [f32; 4],
    /// per key: r, g, b, a (each quantised to a byte as the original stores them)
    pub colour: [[f32; 4]; 4],
    /// Streamer_SetBlend 0x004952d0 (opcode 0x57): render flags 0x100 additive, else 0x300 alpha blend
    pub additive: bool,
    /// 0x55 texture (Streamer_SetTexture 0x004952b0, render flags |= 0xc; untextured by default)
    pub texture: Option<String>,
    /// [u0, u1, v0, v1]: 0x56 uvs (Streamer_SetU / SetV 0x00495270 / 0x00495290: +0x48 u0, +0x50 u1, +0x4c v0, +0x54
    /// v1; defaults 0x00494d40: 0, 1, 0, 1): u = lerp(u0, u1, age / life) along the strip, v0 on the +side vertex
    pub uv: [f32; 4],
    pub time: f32,
    pub emitting: bool,
    /// (position (engine space), direction, birth time, size)
    points: std::collections::VecDeque<(Vec3, Vec3, f32, f32)>,
}

impl Streamer {
    /// Defaults 0x00494d40: life 1, width [1, 1, 1, 0], colour white with the last key's alpha 0, alpha blend.
    pub fn new(owner: Entity, point: Option<Mat4>) -> Self {
        let w = [1.0; 4];
        Streamer {
            owner,
            point,
            life: 1.0,
            width: [1.0, 1.0, 1.0, 0.0],
            colour: [w, w, w, [1.0, 1.0, 1.0, 0.0]],
            additive: false,
            texture: None,
            uv: [0.0, 1.0, 0.0, 1.0],
            time: 0.0,
            emitting: true,
            points: Default::default(),
        }
    }

    pub fn points(&self) -> usize {
        self.points.len()
    }

    /// One game frame (Streamer_Update 0x00495300, then Streamer_AddPoint 0x004958a0 from the owner's update while it
    /// emits): the clock advances by `dt`, a point is added at `at` (engine space; None: the owner is gone, the
    /// streamer stops emitting, Streamer_Stop 0x00494f10), and points older than the life or `RING_SPAN` go.
    pub fn step(&mut self, dt: f32, at: Option<Vec3>) {
        self.time += dt;
        match at {
            Some(p) if self.emitting => self.add_point(p),
            _ => self.emitting = false,
        }
        let cut = self.time - self.life.min(RING_SPAN);
        while self.points.front().is_some_and(|q| q.2 < cut) || self.points.len() > MAX_POINTS {
            self.points.pop_front();
        }
    }

    /// Streamer_AddPoint 0x004958a0: the width fades in over the first quarter of the life (3x^2 - 2x^3); the new
    /// point's direction is new - previous, and the previous point's becomes new - the point before it, which for the
    /// first point is itself (node +0x7c starts as the first point: Streamer_SetDirection 0x004959b0 gives it
    /// e2 - e1, so the trail's tail has a width from the start).
    fn add_point(&mut self, p: Vec3) {
        let x = self.time / (self.life * 0.25);
        let size = if x < 1.0 { 3.0 * x * x - 2.0 * x * x * x } else { 1.0 };
        let n = self.points.len();
        let dir = match self.points.back() {
            Some(prev) => {
                let before = if n >= 2 { self.points[n - 2].0 } else { prev.0 };
                self.points[n - 1].1 = (p - before).normalize_or_zero();
                (p - self.points[n - 1].0).normalize_or_zero()
            }
            None => Vec3::ZERO,
        };
        self.points.push_back((p, dir, self.time, size));
    }

    /// One opcode of the 0x4f..0x57 group on this streamer (Fx_SetStreamer* 0x0041f690..0x0041f810).
    pub fn set(&mut self, op: &str, arg: &Value, words: [f32; 4]) {
        let q = |x: f32| (x * 255.0).trunc() / 255.0;
        match op {
            // Fx_SetStreamerThickness 0x0041f6d0
            "0x4f" => self.width = curve_keys(arg),
            // Fx_SetStreamerLength 0x0041f810
            "0x54" => self.life = words[0].max(1e-3),
            // Fx_SetStreamerTransparencyMode 0x0041f690
            "0x57" => self.additive = words[0] != 0.0,
            // 0x55 (0x00422a8e): Fx_SetStreamerTexture 0x0041f5a0, the name from the texture bank Game+0xa78 (0x004881c0)
            "0x55" => self.texture = arg.as_str().map(|n| n.to_lowercase()),
            // 0x56 (0x00422ac1): Fx_SetStreamerUvs 0x0041f640: SetU(w0, w1), SetV(w2, w3)
            "0x56" => self.uv = words,
            // Fx_SetStreamerAlpha / Red / Green / Blue 0x0041f710 / 0x0041f750 / 0x0041f790 / 0x0041f7d0
            _ => {
                let ch = match op {
                    "0x50" => 3,
                    "0x51" => 0,
                    "0x52" => 1,
                    _ => 2,
                };
                for (i, v) in curve_keys(arg).iter().enumerate() {
                    self.colour[i][ch] = q(*v);
                }
            }
        }
    }
}

/// A curve argument (render_r2b.cpp:853-881): 1 value constant, 2 a linear ramp, 4 as given, else 1.
pub fn curve_keys(v: &Value) -> [f32; 4] {
    let n: Vec<f32> = v.as_array().map(|a| a.iter().filter_map(|x| x.as_f64()).map(|x| x as f32).collect()).unwrap_or_default();
    match n.len() {
        1 => [n[0]; 4],
        2 => {
            let k1 = n[0] + (n[1] - n[0]) / 3.0;
            [n[0], k1, (k1 + n[1]) * 0.5, n[1]]
        }
        4 => [n[0], n[1], n[2], n[3]],
        _ => [1.0; 4],
    }
}

pub fn bezier(k: [f32; 4], t: f32) -> f32 {
    let u = 1.0 - t;
    u * u * u * k[0] + 3.0 * t * u * u * k[1] + 3.0 * t * t * u * k[2] + t * t * t * k[3]
}

/// Streamer meshes per (texture, blend): untextured ones made at set-up, textured ones (0x55) on first use.
#[derive(Default)]
pub struct StreamerMeshes(pub std::collections::HashMap<(Option<String>, bool), Handle<Mesh>>);

/// One streamer mesh (all streamers of one texture and blend): vertex colours (x the texture when 0x55 set one:
/// render flags | 0xc, 0x004952b0), unlit, both sides.
pub fn streamer_mesh(commands: &mut Commands, meshes: &mut Assets<Mesh>, std_mats: &mut Assets<StandardMaterial>, assets: &AssetServer, texture: Option<&str>, additive: bool) -> Handle<Mesh> {
    let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
    mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, vec![[0.0f32; 3]; 3]);
    mesh.insert_attribute(Mesh::ATTRIBUTE_COLOR, vec![[0.0f32; 4]; 3]);
    mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 1.0, 0.0]; 3]);
    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, vec![[0.0f32; 2]; 3]);
    mesh.insert_indices(Indices::U32(vec![0, 1, 2]));
    let h = meshes.add(mesh);
    let mat = std_mats.add(StandardMaterial {
        base_color: Color::WHITE,
        // repeat addressing: 0x56 runs u over 0..5 (fx_trail_cap_mgun)
        base_color_texture: texture.map(|t| crate::gamematerial::load_tex(assets, t, true)),
        unlit: true,
        double_sided: true,
        cull_mode: None,
        alpha_mode: if additive { AlphaMode::Add } else { AlphaMode::Blend },
        fog_enabled: false,
        ..default()
    });
    commands.spawn((Mesh3d(h.clone()), MeshMaterial3d(mat), Transform::IDENTITY, bevy::camera::visibility::NoFrustumCulling, bevy::light::NotShadowCaster, Name::new("fx_streamers")));
    h
}

/// Streamers: unless the game is paused (Streamers_Update 0x00494f30 returns at once while Game+0x2684 is set, and
/// the owners' updates that add points do not run either), each streamer steps (`Streamer::step`: a point at the
/// owner or its attach point, the old ones dropped; it stops emitting when the owner is gone, FxObject_Remove ->
/// Streamer_Stop 0x00494f10) and is freed when no points remain; then, paused or not, build the
/// strips (Streamer_Draw 0x00495380: side = dir x camera axis (the view's back axis, switching to its up axis while
/// (dir . axis)^2 > 0.6), not normalised; vertices pos +- side * w, w = width(t) * size, colour per channel = curve(t),
/// t = age / life, oldest first).
#[allow(clippy::too_many_arguments)]
pub fn streamers(
    mut commands: Commands,
    time: Res<Time<Virtual>>,
    mut vm: ResMut<FxVm>,
    owners: Query<&GlobalTransform, With<super::FxEntity>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut std_mats: ResMut<Assets<StandardMaterial>>,
    assets: Res<AssetServer>,
    mut empty: Local<std::collections::HashSet<AssetId<Mesh>>>,
) {
    let dt = time.delta_secs();
    let vm = &mut *vm;
    if !time.is_paused() && dt > 0.0 {
        for st in vm.streamers.iter_mut() {
            // the object's own placement (game space): an fx object spawned this frame has no world transform yet
            let at = vm.objects.get(&st.owner).filter(|_| owners.get(st.owner).is_ok()).map(|o| {
                let at = match st.point {
                    Some(p) => super::attach::point_world(o.place, o.scale_now, Some(p)).w_axis.truncate(),
                    None => o.place.w_axis.truncate(),
                };
                game_to_engine() * at
            });
            st.step(dt, at);
        }
        vm.streamers.retain(|s| s.emitting || !s.points.is_empty());
    }
    let Ok(c) = cam.single() else { return };
    let (back, up) = (c.back().as_vec3(), c.up().as_vec3());
    let keys: Vec<(Option<String>, bool)> = vm.streamers.iter().map(|s| (s.texture.clone(), s.additive)).collect();
    for key in keys {
        if !vm.streamer_meshes.0.contains_key(&key) {
            let h = streamer_mesh(&mut commands, &mut meshes, &mut std_mats, &assets, key.0.as_deref(), key.1);
            vm.streamer_meshes.0.insert(key, h);
        }
    }
    for ((texture, additive), handle) in vm.streamer_meshes.0.iter() {
        let mut pos: Vec<[f32; 3]> = Vec::new();
        let mut col: Vec<[f32; 4]> = Vec::new();
        let mut uvs: Vec<[f32; 2]> = Vec::new();
        let mut idx: Vec<u32> = Vec::new();
        // newest first: StreamerList_Link 0x00494fc0 puts a new streamer at the head of the list Streamers_Draw walks
        for st in vm.streamers.iter().rev().filter(|s| s.additive == *additive && s.texture == *texture && s.points.len() >= 2) {
            let mut axis = back;
            let base = pos.len() as u32;
            for (i, (p, d, birth, size)) in st.points.iter().enumerate() {
                let t = ((st.time - birth) / st.life).clamp(0.0, 1.0);
                if d.dot(axis).powi(2) > 0.6 {
                    axis = if axis == back { up } else { back };
                }
                let side = d.cross(axis) * bezier(st.width, t) * size;
                let ch = |c: usize| bezier([st.colour[0][c], st.colour[1][c], st.colour[2][c], st.colour[3][c]], t);
                // vertex colours are linear in Bevy: the original's are gamma-space bytes
                let lin = |v: f32| v.clamp(0.0, 1.0).powf(2.2);
                let cl = [lin(ch(0)), lin(ch(1)), lin(ch(2)), ch(3).clamp(0.0, 1.0)];
                pos.push((*p + side).to_array());
                pos.push((*p - side).to_array());
                col.push(cl);
                col.push(cl);
                // Streamer_Draw 0x00495380: u = (u1 - u0) * t + u0, v0 on the +side vertex, v1 on the -side one
                let u = (st.uv[1] - st.uv[0]) * t + st.uv[0];
                uvs.push([u, st.uv[2]]);
                uvs.push([u, st.uv[3]]);
                if i > 0 {
                    let a = base + (i as u32 - 1) * 2;
                    idx.extend_from_slice(&[a, a + 1, a + 2, a + 1, a + 3, a + 2]);
                }
            }
        }
        if idx.is_empty() {
            // already empty: leave the mesh alone (a rewritten mesh is re-uploaded, a cost per frame and mesh)
            if !empty.insert(handle.id()) {
                continue;
            }
            pos = vec![[0.0; 3]; 3];
            col = vec![[0.0; 4]; 3];
            uvs = vec![[0.0; 2]; 3];
            idx = vec![0, 1, 2];
        } else {
            empty.remove(&handle.id());
        }
        if let Some(mut m) = meshes.get_mut(handle) {
            let n = pos.len();
            m.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
            m.insert_attribute(Mesh::ATTRIBUTE_NORMAL, vec![[0.0f32, 1.0, 0.0]; n]);
            m.insert_attribute(Mesh::ATTRIBUTE_COLOR, col);
            m.insert_attribute(Mesh::ATTRIBUTE_UV_0, uvs);
            m.insert_indices(Indices::U32(idx));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn curve_keys_forms() {
        let v: Value = serde_json::from_str("[0.5]").unwrap();
        assert_eq!(curve_keys(&v), [0.5; 4]);
        let v: Value = serde_json::from_str("[0, 3]").unwrap();
        assert_eq!(curve_keys(&v), [0.0, 1.0, 2.0, 3.0]);
        let v: Value = serde_json::from_str("[1, 2, 3]").unwrap();
        assert_eq!(curve_keys(&v), [1.0; 4]);
        // a linear ramp stays linear through the Bezier
        let k = curve_keys(&serde_json::from_str("[0, 3]").unwrap());
        assert!((bezier(k, 0.5) - 1.5).abs() < 1e-6);
    }

    fn moving(st: &mut Streamer, hz: f32, secs: f32) {
        let frames = (hz * secs) as usize;
        for i in 0..frames {
            st.step(1.0 / hz, Some(Vec3::X * i as f32));
        }
    }

    #[test]
    fn first_point_gets_the_forward_direction() {
        let mut st = Streamer::new(Entity::PLACEHOLDER, None);
        st.step(1.0 / 60.0, Some(Vec3::ZERO));
        assert_eq!(st.points[0].1, Vec3::ZERO);
        st.step(1.0 / 60.0, Some(Vec3::X));
        // 0x004958a0: the second point sets the first one's direction to e2 - e1
        assert_eq!(st.points[0].1, Vec3::X);
        assert_eq!(st.points[1].1, Vec3::X);
    }

    #[test]
    fn trail_length_does_not_depend_on_the_frame_rate() {
        let span = |hz: f32, life: f32| {
            let mut st = Streamer::new(Entity::PLACEHOLDER, None);
            st.life = life;
            moving(&mut st, hz, 3.0);
            st.time - st.points.front().unwrap().2
        };
        for hz in [60.0, 144.0, 240.0] {
            // the life when it is shorter than the original's ring, else the ring's 62 frames at 60 Hz
            assert!((span(hz, 0.5) - 0.5).abs() <= 1.0 / hz + 1e-3, "{hz}");
            assert!((span(hz, 2.0) - RING_SPAN).abs() <= 1.0 / hz + 1e-3, "{hz}");
        }
    }

    #[test]
    fn owner_gone_stops_emitting() {
        let mut st = Streamer::new(Entity::PLACEHOLDER, None);
        moving(&mut st, 60.0, 0.5);
        st.step(1.0 / 60.0, None);
        assert!(!st.emitting);
        let n = st.points();
        st.step(1.0 / 60.0, Some(Vec3::ZERO));
        assert_eq!(st.points(), n);
    }
}
