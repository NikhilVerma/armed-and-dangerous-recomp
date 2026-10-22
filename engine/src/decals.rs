//! Decals (fx op 0x6a: Decals_Spawn 0x004165c0, Decal_Build 0x00417260, Decals_Update 0x00416440, Decals_Draw
//! 0x00416330): a box of +-size along the fx object's three axes (its matrix columns, unnormalised) around its
//! position; the landscape's triangles inside it are clipped to the box (Sutherland-Hodgman against the six planes, no
//! normal test) and drawn with the texture: uv from the two box coordinates with the largest range over the clipped
//! vertices (u the largest), u_k = a_k . (x - p) / (2 size), stretched by the third coordinate, + 0.5
//! (Decal_ComputeTexCoords 0x004175c0: [`tex_coords`]). Texture x (1, 1, 1, fade), alpha-blended, no depth write, clamped, fogged; the
//! texture sampled as stored (premultiplied textures blended straight, as the original). 160 decals (the remaster's
//! `decal_pool` setting, pools.rs), the oldest recycled at once; no expiry.
//!
//! Placed objects receive them too (Decals_ProjectOnEntity 0x00416f50, the object callback of the world walk
//! 0x0048eb60 that Decal_Build passes unless `single`; the fx op passes 0): objects with decal_receive (default 1,
//! +0x3ec bit 8, Object_LoadBaseProps 0x00451360) that neither animate (0x00418610) nor have a mesh with +8 set
//! (Entity_DecalExcluded 0x00416e90; taken here as skinned models: guess), and whose bounding sphere is not wholly
//! outside a box plane. Their render mesh (DecalGeometry_AddEntity 0x00416bf0: every submesh but those whose material
//! has bit 0x10 of +0x34, the blended ones) is clipped like the landscape, into the same 128-vertex / 384-index
//! budget. Up to 4 receivers are recorded; a receiver's death fades its decals in 0.1 s (0x00418580 via 0x00416620).
//! Not ported: the pressure fade (0x00416440), which never runs in the original (docs/subsystems/decals-bodies.md,
//! "Measured"). The original interleaves landscape cells and objects cell by cell; here the landscape goes first.
//! docs/subsystems/decals-bodies.md section 1.1.
//!
//! The clip is Polygon_ClipPlanes 0x00416010 over Polygon_ClipPlane 0x00415eb0 (Sutherland-Hodgman, kept when the
//! distance is not below 0); the pool recycles its oldest decal (Decal_Recycle 0x00417110, Decals_Spawn); the draw is
//! Decal_Render 0x00416740 through Decals_Draw (here one mesh per decal: texture x the fade; the original's vertex
//! colour, its white record times each vertex's w, is not reproduced: unverified).

use crate::levelreset::LevelAppExt;
use bevy::asset::RenderAssetUsages;
use bevy::image::{ImageAddressMode, ImageLoaderSettings, ImageSampler};
use bevy::mesh::{Indices, PrimitiveTopology};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

const CELL: f32 = 32.0;
/// The original's pool (0x00416150 `mov ebx,0xa0`), until pools.rs sets the setting's.
const POOL: usize = 160;

fn bucket(cells: &mut HashMap<(i32, i32), Vec<u32>>, lo: Vec3, hi: Vec3, i: u32) {
    for x in (lo.x / CELL).floor() as i32..=(hi.x / CELL).floor() as i32 {
        for y in (lo.y / CELL).floor() as i32..=(hi.y / CELL).floor() as i32 {
            cells.entry((x, y)).or_default().push(i);
        }
    }
}

fn cell_range(lo: Vec3, hi: Vec3) -> impl Iterator<Item = (i32, i32)> {
    let ys = (lo.y / CELL).floor() as i32..=(hi.y / CELL).floor() as i32;
    ((lo.x / CELL).floor() as i32..=(hi.x / CELL).floor() as i32).flat_map(move |x| ys.clone().map(move |y| (x, y)))
}

/// A placed object that takes decals: its entity, its world box (game space), whether it still stands.
struct Receiver {
    entity: Entity,
    lo: Vec3,
    hi: Vec3,
    alive: bool,
}

/// The landscape's triangles and the receiving objects' triangles (game space, world-transformed once at load),
/// bucketed by x/y cells.
#[derive(Resource)]
pub struct DecalWorld {
    verts: Vec<Vec3>,
    tris: Vec<[u32; 3]>,
    cells: HashMap<(i32, i32), Vec<u32>>,
    overts: Vec<Vec3>,
    otris: Vec<[u32; 3]>,
    owner: Vec<u32>,
    ocells: HashMap<(i32, i32), Vec<u32>>,
    receivers: Vec<Receiver>,
}

impl DecalWorld {
    pub fn new(verts: &[Vec3], tris: &[[u32; 3]]) -> Self {
        let mut cells: HashMap<(i32, i32), Vec<u32>> = HashMap::default();
        for (i, t) in tris.iter().enumerate() {
            let (a, b, c) = (verts[t[0] as usize], verts[t[1] as usize], verts[t[2] as usize]);
            bucket(&mut cells, a.min(b).min(c), a.max(b).max(c), i as u32);
        }
        DecalWorld {
            verts: verts.to_vec(),
            tris: tris.to_vec(),
            cells,
            overts: Vec::new(),
            otris: Vec::new(),
            owner: Vec::new(),
            ocells: HashMap::default(),
            receivers: Vec::new(),
        }
    }

    /// A placed object's render mesh as a receiver (`place`: model space to game space).
    pub fn add_object(&mut self, entity: Entity, model: &crate::scene::Model, place: Mat4) {
        let r = self.receivers.len() as u32;
        let base = self.overts.len() as u32;
        self.overts.extend(model.vertices.iter().map(|v| place.transform_point3(*v)));
        let (mut lo, mut hi) = (Vec3::splat(f32::MAX), Vec3::splat(f32::MIN));
        for (k, t) in model.tris.iter().enumerate() {
            if model.no_decal.iter().any(|s| s.contains(&(k as u32))) {
                continue;
            }
            let t = [t[0] + base, t[1] + base, t[2] + base];
            let (a, b, c) = (self.overts[t[0] as usize], self.overts[t[1] as usize], self.overts[t[2] as usize]);
            let (tl, th) = (a.min(b).min(c), a.max(b).max(c));
            lo = lo.min(tl);
            hi = hi.max(th);
            let i = self.otris.len() as u32;
            self.otris.push(t);
            self.owner.push(r);
            bucket(&mut self.ocells, tl, th, i);
        }
        self.receivers.push(Receiver { entity, lo, hi, alive: lo.x <= hi.x });
    }

    /// The object is gone (destroyed, or replaced by its husk): it takes no more decals.
    pub fn remove_object(&mut self, entity: Entity) {
        for r in self.receivers.iter_mut().filter(|r| r.entity == entity) {
            r.alive = false;
        }
    }

    /// (standing receivers, receiver triangles)
    pub fn receiver_stats(&self) -> (usize, usize) {
        (self.receivers.iter().filter(|r| r.alive).count(), self.otris.len())
    }
}

/// A decal to make (game space placement, half size, texture).
#[derive(Message, Clone)]
pub struct SpawnDecal {
    pub place: Mat4,
    pub size: f32,
    pub texture: String,
}

#[derive(Resource)]
pub struct Decals {
    live: std::collections::VecDeque<Entity>,
    /// decals at once: one more recycles the oldest (the original's 160; pools.rs sets it from `decal_pool`)
    pub capacity: usize,
    /// made so far, and how many of them lie on placed objects (playtest state "decals")
    made: u32,
    on_objects: u32,
    /// faded out after their receiver died
    faded: u32,
}

impl Default for Decals {
    fn default() -> Self {
        Decals { live: Default::default(), capacity: POOL, made: 0, on_objects: 0, faded: 0 }
    }
}

/// A live decal: the objects it lies on (up to 4), and its fade (left, total) once one of them died.
#[derive(Component)]
struct Decal {
    receivers: Vec<Entity>,
    fade: Option<(f32, f32)>,
}

pub struct DecalPlugin;

impl Plugin for DecalPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SpawnDecal>().init_level_resource::<Decals>().add_systems(Update, (spawn, receiver_died, fade).chain());
    }
}

fn clip(poly: Vec<Vec3>, n: Vec3, c: f32) -> Vec<Vec3> {
    let mut out = Vec::with_capacity(poly.len() + 2);
    for i in 0..poly.len() {
        let (a, b) = (poly[i], poly[(i + 1) % poly.len()]);
        let (da, db) = (n.dot(a) + c, n.dot(b) + c);
        if da >= 0.0 {
            out.push(a);
        }
        if (da >= 0.0) != (db >= 0.0) {
            out.push(a + (b - a) * (da / (da - db)));
        }
    }
    out
}

/// Clips one triangle to the box and appends it as a fan. None once the build is out of room (capacity 128
/// vertices / 384 indices: DecalGeometry_AddSubmesh 0x00416930 stops there); Some(added).
fn add_clipped(tri: [Vec3; 3], planes: &[(Vec3, f32)], verts: &mut Vec<Vec3>, idx: &mut Vec<u32>) -> Option<bool> {
    let mut poly = tri.to_vec();
    for (n, c) in planes {
        if poly.is_empty() {
            break;
        }
        poly = clip(poly, *n, *c);
    }
    if poly.len() < 3 {
        return Some(false);
    }
    if verts.len() + poly.len() > 128 || idx.len() + (poly.len() - 2) * 3 > 384 {
        return None;
    }
    let base = verts.len() as u32;
    verts.extend(poly.iter());
    for k in 1..poly.len() as u32 - 1 {
        idx.extend_from_slice(&[base, base + k, base + k + 1]);
    }
    Some(true)
}

/// Decal_ComputeTexCoords 0x004175c0: each vertex's three box coordinates a_k . (x - p) / (2 size); u and v are the
/// two with the largest ranges over the vertices, ordered as the original's three compare-and-swaps leave them (u the
/// largest), stretched by |(u, v, w)| / |(u, v)| unless u = v = 0, plus 0.5.
fn tex_coords(verts: &[Vec3], p: Vec3, axes: [Vec3; 3], s: f32) -> Vec<[f32; 2]> {
    let coords: Vec<[f32; 3]> = verts.iter().map(|v| axes.map(|a| a.dot(*v - p) / (2.0 * s))).collect();
    // ranges over the vertices (the original's minima start at 1, maxima at -1)
    let range = |k: usize| {
        let (lo, hi) = coords.iter().fold((1.0f32, -1.0f32), |(lo, hi), c| (lo.min(c[k]), hi.max(c[k])));
        hi - lo
    };
    let mut order = [0usize, 1, 2];
    let (mut ru, mut rv, rw) = (range(0), range(1), range(2));
    if ru < rv {
        order.swap(0, 1);
        std::mem::swap(&mut ru, &mut rv);
    }
    if rv < rw {
        order.swap(1, 2);
        rv = rw;
    }
    if ru < rv {
        order.swap(0, 1);
    }
    let [ku, kv, kw] = order;
    coords
        .iter()
        .map(|c| {
            let (u, v, w) = (c[ku], c[kv], c[kw]);
            let s2 = u * u + v * v;
            let k = if s2 != 0.0 { (w * w + s2).sqrt() / s2.sqrt() } else { 1.0 };
            [u * k + 0.5, v * k + 0.5]
        })
        .collect()
}

#[allow(clippy::too_many_arguments)]
fn spawn(
    mut commands: Commands,
    mut events: MessageReader<SpawnDecal>,
    world: Option<Res<DecalWorld>>,
    mut decals: ResMut<Decals>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut textures: Local<HashMap<String, Handle<StandardMaterial>>>,
    mut state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(world) = world else {
        events.clear();
        return;
    };
    for ev in events.read() {
        let p = ev.place.w_axis.truncate();
        let axes = [ev.place.x_axis.truncate(), ev.place.y_axis.truncate(), ev.place.z_axis.truncate()];
        let s = ev.size;
        // the six planes: a.x + (s - a.p) >= 0 and -a.x + (s + a.p) >= 0
        let planes: Vec<(Vec3, f32)> = axes.iter().flat_map(|a| [(*a, s - a.dot(p)), (-*a, s + a.dot(p))]).collect();
        let reach = axes.iter().map(|a| a.length()).fold(0.0, f32::max) * s * 1.8;
        let mut verts: Vec<Vec3> = Vec::new();
        let mut idx: Vec<u32> = Vec::new();
        let mut full = false;
        let mut seen = std::collections::HashSet::new();
        'cells: for c in cell_range(p - Vec3::splat(reach), p + Vec3::splat(reach)) {
            for &ti in world.cells.get(&c).map(|v| v.as_slice()).unwrap_or(&[]) {
                if !seen.insert(ti) {
                    continue;
                }
                let t = world.tris[ti as usize];
                let tri = [world.verts[t[0] as usize], world.verts[t[1] as usize], world.verts[t[2] as usize]];
                if add_clipped(tri, &planes, &mut verts, &mut idx).is_none() {
                    full = true;
                    break 'cells;
                }
            }
        }
        // placed objects (Decals_ProjectOnEntity 0x00416f50): standing receivers whose box meets the decal's box
        // (the original: the bounding sphere against the six planes)
        let half = axes.iter().fold(Vec3::ZERO, |h, a| h + a.abs()) * s;
        let (blo, bhi) = (p - half, p + half);
        let mut receivers: Vec<Entity> = Vec::new();
        if !full {
            let mut cand: Vec<u32> = cell_range(blo, bhi)
                .flat_map(|c| world.ocells.get(&c).map(|v| v.as_slice()).unwrap_or(&[]).iter().copied())
                .filter(|&ti| {
                    let r = &world.receivers[world.owner[ti as usize] as usize];
                    r.alive && r.lo.cmple(bhi).all() && r.hi.cmpge(blo).all()
                })
                .collect();
            cand.sort_unstable();
            cand.dedup();
            for ti in cand {
                let t = world.otris[ti as usize];
                let tri = [world.overts[t[0] as usize], world.overts[t[1] as usize], world.overts[t[2] as usize]];
                if tri[0].min(tri[1]).min(tri[2]).cmpgt(bhi).any() || tri[0].max(tri[1]).max(tri[2]).cmplt(blo).any() {
                    continue;
                }
                match add_clipped(tri, &planes, &mut verts, &mut idx) {
                    None => break,
                    Some(true) => {
                        // the first 4 objects are recorded (DecalGeometry_AddEntity 0x00416bf0)
                        let e = world.receivers[world.owner[ti as usize] as usize].entity;
                        if !receivers.contains(&e) && receivers.len() < 4 {
                            receivers.push(e);
                        }
                    }
                    Some(false) => {}
                }
            }
        }
        if idx.is_empty() {
            // Decals_Spawn 0x004165c0 takes its decal before building: at a full pool the oldest is recycled even when
            // the build then finds no geometry
            if decals.live.len() >= decals.capacity.max(1) {
                if let Some(old) = decals.live.pop_front() {
                    commands.entity(old).despawn();
                }
            }
            continue;
        }
        let uv = tex_coords(&verts, p, axes, s);
        let normals = vec![[0.0f32, 0.0, 1.0]; verts.len()];
        let (receivers_copy, nverts) = (receivers.clone(), verts.len());
        let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::default());
        mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, verts.iter().map(|v| v.to_array()).collect::<Vec<_>>());
        mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, normals);
        mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
        mesh.insert_indices(Indices::U32(idx));
        let mat = textures
            .entry(ev.texture.to_lowercase())
            .or_insert_with(|| {
                // clamped, trilinear (anisotropic with the remaster's anisotropic_fx: graphics.rs)
                let sampler = crate::graphics::world_sampler(ImageAddressMode::ClampToEdge, true);
                let tex = assets.load_builder().with_settings(move |s: &mut ImageLoaderSettings| {
                    s.sampler = ImageSampler::Descriptor(sampler.clone());
                }).load(crate::graphics::texture_path(&ev.texture.to_lowercase()));
                materials.add(StandardMaterial {
                    base_color_texture: Some(tex),
                    unlit: true,
                    alpha_mode: AlphaMode::Blend,
                    double_sided: true,
                    cull_mode: None,
                    // coplanar with the surface: a bias (the original draws it with LESSEQUAL and no bias; with 4 the
                    // landscape won the depth test)
                    depth_bias: 1000.0,
                    ..default()
                })
            })
            .clone();
        let e = commands
            .spawn((
                Mesh3d(meshes.add(mesh)),
                MeshMaterial3d(mat),
                Transform::from_rotation(crate::level::game_to_engine()),
                bevy::light::NotShadowCaster,
                Decal { receivers, fade: None },
            ))
            .id();
        decals.made += 1;
        decals.on_objects += u32::from(!receivers_copy.is_empty());
        decals.live.push_back(e);
        // the pool recycles its oldest (Decal_Recycle 0x00417110); a pool made smaller drops the oldest down to it
        while decals.live.len() > decals.capacity.max(1) {
            if let Some(old) = decals.live.pop_front() {
                commands.entity(old).despawn();
            }
        }
        if let Some(st) = state.as_mut() {
            st.0.insert(
                "decals".into(),
                serde_json::json!({ "made": decals.made, "on_objects": decals.on_objects, "last_receivers": receivers_copy.len(), "last_vertices": nverts, "faded": decals.faded, "live": decals.live.len(), "pool": decals.capacity }),
            );
        }
    }
}

/// A receiver's death fades the decals on it over 0.1 s (0x00416620 from 0x004547b0 calls the decal's slot +0x1c,
/// 0x00418580); each gets its own material for the fade.
fn receiver_died(
    mut died: MessageReader<crate::fx::Died>,
    mut q: Query<(&mut Decal, &mut MeshMaterial3d<StandardMaterial>)>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    for d in died.read() {
        for (mut decal, mut mat) in &mut q {
            if decal.fade.is_none() && decal.receivers.contains(&d.0) {
                decal.fade = Some((0.1, 0.1));
                if let Some(m) = materials.get(&mat.0).cloned() {
                    mat.0 = materials.add(m);
                }
            }
        }
    }
}

/// Decal_Update 0x00417180 while fading: alpha = left / total; recycled at 0.
fn fade(
    mut commands: Commands,
    time: Res<Time>,
    mut decals: ResMut<Decals>,
    mut q: Query<(Entity, &mut Decal, &MeshMaterial3d<StandardMaterial>)>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let dt = time.delta_secs();
    for (e, mut decal, mat) in &mut q {
        let Some((left, total)) = decal.fade else { continue };
        let left = left - dt;
        if left <= 0.0 {
            commands.entity(e).despawn();
            decals.live.retain(|x| *x != e);
            decals.faded += 1;
            if let Some(d) = state.as_mut().and_then(|s| s.0.get_mut("decals")).and_then(|d| d.as_object_mut()) {
                d.insert("faded".into(), decals.faded.into());
            }
            continue;
        }
        decal.fade = Some((left, total));
        if let Some(mut m) = materials.get_mut(&mat.0) {
            m.base_color = Color::srgba(1.0, 1.0, 1.0, left / total);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// u follows the box axis with the largest range, also when that axis comes later (0x004175c0's swaps): a strip
    /// along y on a floor (z the projection axis) maps y to u and x to v.
    #[test]
    fn u_is_the_axis_with_the_largest_range() {
        let verts = [Vec3::new(-0.5, -1.5, 0.0), Vec3::new(0.5, -1.5, 0.0), Vec3::new(0.5, 1.5, 0.0), Vec3::new(-0.5, 1.5, 0.0)];
        let uv = tex_coords(&verts, Vec3::ZERO, [Vec3::X, Vec3::Y, Vec3::Z], 2.0);
        // y = 1.5 -> 1.5 / 4 + 0.5; x = 0.5 -> 0.5 / 4 + 0.5 (no stretch: the third coordinate is 0)
        assert!((uv[2][0] - 0.875).abs() < 1e-6 && (uv[2][1] - 0.625).abs() < 1e-6, "{uv:?}");
    }
}
