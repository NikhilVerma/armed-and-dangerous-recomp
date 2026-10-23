//! Tessellated characters (setting `tessellation`): the skinned meshes are refined into a smooth curved surface once,
//! when they first appear, and drawn in place of the authored ones.
//!
//! Port of the old remaster's tessellation (archive/src/rhi/vulkan/vk_tessellation.cpp TessControlGlsl / TessEvalGlsl,
//! settings in vk_device.cpp `Tess()` and `TessModules()`): Phong tessellation (Boubekeur and Alexa 2008) with the
//! crease rule of 2026-10-12 ("sharp angles remain sharp but shallow angles get smooth"). Every point of the flat
//! triangle is projected onto the tangent plane of each corner, the three projections are blended by the barycentrics,
//! and the shape factor (0.75) blends between the flat triangle and that curved surface; an edge whose corner normals
//! lie within the smooth angle (25 degrees) curves fully, beyond the sharp angle (50) it stays straight.
//!
//! What Bevy forces to change: wgpu has no tessellation stage, so the same maths runs on the CPU at load, at a fixed
//! level (segments per edge) instead of the old screen-space level (up to 8, one segment per 6 pixels), and in the
//! model's bind pose; the refined vertices then skin like any other (weights blended from the corners).
//!
//! What it fixes over the old one (docs/player-feedback.md 1-3, the Phong-AEN design in docs/journal/2026-10-12.md):
//! the old surface cracked along UV seams and hard edges, where the game's meshes split a vertex in two, and some of
//! its rebuilt normals were garbage. Here the corners are first welded (same position and skin), the curving normal
//! is the welded one (averaged across the seam: PN-AEN's idea), each edge's crease is decided from welded data both
//! triangles share, and an edge point is evaluated in one canonical order, so both sides of a seam produce the same
//! bits: no cracks, and each side keeps its own UVs. Boundary and non-manifold edges and corners with bad normals stay
//! straight. A displacement is limited to a quarter of the edge length (the journal's guard).

use bevy::asset::RenderAssetUsages;
use bevy::mesh::skinning::SkinnedMesh;
use bevy::mesh::{Indices, PrimitiveTopology, VertexAttributeValues};
use bevy::prelude::*;
use std::collections::HashMap;

use crate::graphics::GraphicsSettings;

/// The refinement's parameters (from [`super::GeometrySettings`]).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TessParams {
    /// segments per edge (each triangle becomes level^2); 1 = no refinement
    pub level: u32,
    /// 0 flat .. 1 the full Phong surface (the old tess_shape, 0.75)
    pub shape: f32,
    /// edges whose corner normals are within this angle curve fully (degrees; the old tess_smooth_angle, 25)
    pub smooth_angle: f32,
    /// beyond this angle an edge stays straight (degrees; the old tess_sharp_angle, 50); both 0 = every edge curves
    pub sharp_angle: f32,
}

impl TessParams {
    /// The crease rule's cosines (vk_tessellation.cpp `CreaseGlsl`; vk_device.cpp `TessModules`: without a valid
    /// sharp > smooth pair, every edge curves).
    fn crease_cosines(&self) -> (f32, f32) {
        if self.sharp_angle > 0.0 && self.sharp_angle > self.smooth_angle {
            (self.sharp_angle.to_radians().cos(), self.smooth_angle.to_radians().cos())
        } else {
            (-3.0, -2.0)
        }
    }
}

fn smoothstep(e0: f32, e1: f32, x: f32) -> f32 {
    let t = ((x - e0) / (e1 - e0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

/// Whether a normal is unusable (vk_tessellation.cpp `BadNormal`).
fn bad_normal(n: Vec3) -> bool {
    !n.is_finite() || n.length_squared() < 1e-8
}

/// The point `p` projected onto the tangent plane through `corner` with unit normal `n` (`ToPlane`).
fn to_plane(p: Vec3, corner: Vec3, n: Vec3) -> Vec3 {
    p - (p - corner).dot(n) * n
}

/// Where an output vertex comes from: up to three source vertices and their weights (barycentrics). Edge points list
/// their two corners in the canonical order (lower welded id first) so both triangles of an edge compute the same.
#[derive(Clone, Copy)]
struct Recipe {
    src: [u32; 3],
    w: [f32; 3],
    n: usize,
}

/// The welded view of a mesh: corners that share a position and a skin are one.
struct Welded {
    /// per source vertex: its welded id
    id: Vec<u32>,
    pos: Vec<Vec3>,
    /// unit normal per welded vertex (the average of its split normals), or zero when unusable
    normal: Vec<Vec3>,
}

fn weld(positions: &[[f32; 3]], normals: &[[f32; 3]], joints: Option<&[[u16; 4]]>, weights: Option<&[[f32; 4]]>) -> Welded {
    type Key = ([u32; 3], [u16; 4], [u32; 4]);
    let mut map: HashMap<Key, u32> = HashMap::new();
    let mut w = Welded { id: Vec::with_capacity(positions.len()), pos: Vec::new(), normal: Vec::new() };
    let mut sums: Vec<Vec3> = Vec::new();
    for (i, p) in positions.iter().enumerate() {
        let key: Key = (
            p.map(f32::to_bits),
            joints.map_or([0; 4], |j| j[i]),
            weights.map_or([0; 4], |x| x[i].map(f32::to_bits)),
        );
        let id = *map.entry(key).or_insert_with(|| {
            w.pos.push(Vec3::from(*p));
            sums.push(Vec3::ZERO);
            (w.pos.len() - 1) as u32
        });
        w.id.push(id);
        let n = Vec3::from(normals[i]);
        if !bad_normal(n) {
            sums[id as usize] += n.normalize();
        }
    }
    w.normal = sums.into_iter().map(|s| if bad_normal(s) { Vec3::ZERO } else { s.normalize() }).collect();
    w
}

/// Per welded edge (lower id first): how much it curves, 0 straight .. 1 full.
fn creases(tris: &[[u32; 3]], normals: &[[f32; 3]], welded: &Welded, params: &TessParams) -> HashMap<(u32, u32), f32> {
    // the split normals each side's triangles have at the edge's two ends, and how many triangles share it
    #[derive(Default)]
    struct Uses {
        count: u32,
        at_lo: Vec<Vec3>,
        at_hi: Vec<Vec3>,
    }
    let mut uses: HashMap<(u32, u32), Uses> = HashMap::new();
    for t in tris {
        for k in 0..3 {
            let (a, b) = (t[k], t[(k + 1) % 3]);
            let (wa, wb) = (welded.id[a as usize], welded.id[b as usize]);
            if wa == wb {
                continue;
            }
            let (lo, hi) = if wa < wb { (a, b) } else { (b, a) };
            let u = uses.entry((wa.min(wb), wa.max(wb))).or_default();
            u.count += 1;
            u.at_lo.push(Vec3::from(normals[lo as usize]).normalize_or_zero());
            u.at_hi.push(Vec3::from(normals[hi as usize]).normalize_or_zero());
        }
    }
    let spread = |ns: &[Vec3]| {
        let mut m = 1.0f32;
        for (i, a) in ns.iter().enumerate() {
            for b in &ns[i + 1..] {
                m = m.min(a.dot(*b));
            }
        }
        m
    };
    let (sharp, smooth) = params.crease_cosines();
    uses.into_iter()
        .map(|((lo, hi), u)| {
            let (nl, nh) = (welded.normal[lo as usize], welded.normal[hi as usize]);
            // a boundary or non-manifold edge, or a corner without a usable normal: straight
            let c = if u.count != 2 || nl == Vec3::ZERO || nh == Vec3::ZERO {
                0.0
            } else {
                // the corners' welded normals, and a hard edge (the two sides' own normals differ at an end)
                smoothstep(sharp, smooth, nl.dot(nh).min(spread(&u.at_lo)).min(spread(&u.at_hi)))
            };
            ((lo, hi), c)
        })
        .collect()
}

/// Refines a triangle mesh (positions, normals) into `level`^2 triangles per triangle on its Phong surface. Every other
/// attribute is interpolated (skin weights blended and renormalised over the four strongest joints); tangents are
/// generated again. None when the mesh has no normals, no triangles or an attribute this cannot interpolate.
pub fn refine(mesh: &Mesh, params: &TessParams) -> Option<Mesh> {
    let level = params.level.clamp(1, 16);
    if level < 2 || mesh.primitive_topology() != PrimitiveTopology::TriangleList {
        return None;
    }
    let Some(VertexAttributeValues::Float32x3(pos)) = mesh.attribute(Mesh::ATTRIBUTE_POSITION) else { return None };
    let Some(VertexAttributeValues::Float32x3(nrm)) = mesh.attribute(Mesh::ATTRIBUTE_NORMAL) else { return None };
    let joints = match mesh.attribute(Mesh::ATTRIBUTE_JOINT_INDEX) {
        Some(VertexAttributeValues::Uint16x4(j)) => Some(j.as_slice()),
        Some(_) => return None,
        None => None,
    };
    let weights = match mesh.attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT) {
        Some(VertexAttributeValues::Float32x4(w)) => Some(w.as_slice()),
        Some(_) => return None,
        None => None,
    };
    let idx: Vec<u32> = match mesh.indices() {
        Some(i) => i.iter().map(|i| i as u32).collect(),
        None => (0..pos.len() as u32).collect(),
    };
    let tris: Vec<[u32; 3]> = idx.chunks_exact(3).map(|t| [t[0], t[1], t[2]]).collect();
    if tris.is_empty() {
        return None;
    }
    let welded = weld(pos, nrm, joints, weights);
    let crease = creases(&tris, nrm, &welded, params);
    let crease_of = |a: u32, b: u32| -> f32 {
        let (wa, wb) = (welded.id[a as usize], welded.id[b as usize]);
        crease.get(&(wa.min(wb), wa.max(wb))).copied().unwrap_or(0.0)
    };
    let shape = params.shape.clamp(0.0, 1.0);
    let wp = |v: u32| welded.pos[welded.id[v as usize] as usize];
    let wn = |v: u32| welded.normal[welded.id[v as usize] as usize];

    // the curved position of an edge point: corners in canonical order, t from the first (`TessEvalGlsl` on an edge,
    // where only that edge's weight counts)
    let edge_point = |lo: u32, hi: u32, t: f32| -> Vec3 {
        let (pa, pb) = (wp(lo), wp(hi));
        let p = pa * (1.0 - t) + pb * t;
        let c = crease_of(lo, hi);
        if c <= 0.0 {
            return p;
        }
        let q = to_plane(p, pa, wn(lo)) * (1.0 - t) + to_plane(p, pb, wn(hi)) * t;
        let d = (q - p) * shape * c;
        p + d.clamp_length_max(0.25 * pa.distance(pb))
    };
    // an inside point: the three edge weights blended (`TessEvalGlsl`)
    let inner_point = |t: &[u32; 3], b: [f32; 3]| -> Vec3 {
        let ps = [wp(t[0]), wp(t[1]), wp(t[2])];
        let p = ps[0] * b[0] + ps[1] * b[1] + ps[2] * b[2];
        let (c01, c12, c20) = (crease_of(t[0], t[1]), crease_of(t[1], t[2]), crease_of(t[2], t[0]));
        let (s01, s12, s20) = (b[0] * b[1], b[1] * b[2], b[2] * b[0]);
        let ss = s01 + s12 + s20;
        let w = if ss > 1e-8 { (s01 * c01 + s12 * c12 + s20 * c20) / ss } else { 0.0 };
        if w <= 0.0 {
            return p;
        }
        let q = (0..3).map(|k| to_plane(p, ps[k], wn(t[k])) * b[k]).sum::<Vec3>();
        let longest = ps[0].distance(ps[1]).max(ps[1].distance(ps[2])).max(ps[2].distance(ps[0]));
        p + ((q - p) * shape * w).clamp_length_max(0.25 * longest)
    };

    #[derive(Hash, PartialEq, Eq)]
    enum Key {
        Corner(u32),
        Edge(u32, u32, u32),
    }
    let mut keyed: HashMap<Key, u32> = HashMap::new();
    let mut recipes: Vec<Recipe> = Vec::new();
    let mut positions: Vec<[f32; 3]> = Vec::new();
    let mut out_idx: Vec<u32> = Vec::with_capacity(tris.len() * (level * level) as usize * 3);
    let n = level;
    let nf = n as f32;
    for t in &tris {
        if (0..3).any(|k| welded.id[t[k] as usize] == welded.id[t[(k + 1) % 3] as usize]) {
            // a degenerate triangle: kept as it is
            for &v in t {
                let i = *keyed.entry(Key::Corner(v)).or_insert_with(|| {
                    recipes.push(Recipe { src: [v, v, v], w: [1.0, 0.0, 0.0], n: 1 });
                    positions.push(pos[v as usize]);
                    (recipes.len() - 1) as u32
                });
                out_idx.push(i);
            }
            continue;
        }
        // grid point (a, b): a steps toward corner 1, b toward corner 2 (barycentrics (n - a - b, a, b) / n)
        let mut grid = vec![0u32; ((n + 1) * (n + 2) / 2) as usize];
        let at = |a: u32, b: u32| -> usize { (b * (n + 1) - b * (b.saturating_sub(1)) / 2 + a) as usize };
        for b in 0..=n {
            for a in 0..=(n - b) {
                let bary = [(n - a - b), a, b];
                let zeros = bary.iter().filter(|x| **x == 0).count();
                let index = if zeros == 2 {
                    let v = t[bary.iter().position(|x| *x == n).unwrap_or(0)];
                    *keyed.entry(Key::Corner(v)).or_insert_with(|| {
                        recipes.push(Recipe { src: [v, v, v], w: [1.0, 0.0, 0.0], n: 1 });
                        positions.push(pos[v as usize]);
                        (recipes.len() - 1) as u32
                    })
                } else if zeros == 1 {
                    // on the edge between the two non-zero corners
                    let ks: Vec<usize> = (0..3).filter(|k| bary[*k] != 0).collect();
                    let (k0, k1) = (ks[0], ks[1]);
                    let (v0, v1) = (t[k0], t[k1]);
                    let (lo, hi, steps) = if welded.id[v0 as usize] < welded.id[v1 as usize] { (v0, v1, bary[k1]) } else { (v1, v0, bary[k0]) };
                    *keyed.entry(Key::Edge(lo, hi, steps)).or_insert_with(|| {
                        let tt = steps as f32 / nf;
                        recipes.push(Recipe { src: [lo, hi, hi], w: [1.0 - tt, tt, 0.0], n: 2 });
                        positions.push(edge_point(lo, hi, tt).to_array());
                        (recipes.len() - 1) as u32
                    })
                } else {
                    let bw = bary.map(|x| x as f32 / nf);
                    recipes.push(Recipe { src: *t, w: bw, n: 3 });
                    positions.push(inner_point(t, bw).to_array());
                    (recipes.len() - 1) as u32
                };
                grid[at(a, b)] = index;
            }
        }
        for b in 0..n {
            for a in 0..(n - b) {
                out_idx.extend([grid[at(a, b)], grid[at(a + 1, b)], grid[at(a, b + 1)]]);
                if a + b + 1 < n {
                    out_idx.extend([grid[at(a + 1, b)], grid[at(a + 1, b + 1)], grid[at(a, b + 1)]]);
                }
            }
        }
    }

    let usage = mesh.asset_usage;
    let mut out = Mesh::new(PrimitiveTopology::TriangleList, usage | RenderAssetUsages::MAIN_WORLD);
    out.insert_attribute(Mesh::ATTRIBUTE_POSITION, positions);
    let had_tangents = mesh.attribute(Mesh::ATTRIBUTE_TANGENT).is_some();
    for (attr, values) in mesh.attributes() {
        let id = attr.id;
        if id == Mesh::ATTRIBUTE_POSITION.id || id == Mesh::ATTRIBUTE_TANGENT.id || id == Mesh::ATTRIBUTE_JOINT_WEIGHT.id {
            continue;
        }
        if id == Mesh::ATTRIBUTE_JOINT_INDEX.id {
            let (j, w) = blend_skin(&recipes, joints?, weights?);
            out.insert_attribute(Mesh::ATTRIBUTE_JOINT_INDEX, VertexAttributeValues::Uint16x4(j));
            out.insert_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT, w);
            continue;
        }
        let normalise = id == Mesh::ATTRIBUTE_NORMAL.id;
        out.insert_attribute(attr.clone(), interpolate(values, &recipes, normalise)?);
    }
    out.insert_indices(Indices::U32(out_idx));
    if had_tangents {
        if let Err(e) = out.generate_tangents() {
            warn!("tessellation: no tangents for the refined mesh: {e}");
        }
    }
    Some(out)
}

/// One float attribute interpolated along the recipes (normals renormalised).
fn interpolate(values: &VertexAttributeValues, recipes: &[Recipe], normalise: bool) -> Option<VertexAttributeValues> {
    fn mix<const N: usize>(src: &[[f32; N]], r: &Recipe) -> [f32; N] {
        let mut o = [0.0; N];
        for k in 0..r.n {
            for (c, v) in o.iter_mut().zip(src[r.src[k] as usize]) {
                *c += v * r.w[k];
            }
        }
        o
    }
    Some(match values {
        VertexAttributeValues::Float32x2(v) => VertexAttributeValues::Float32x2(recipes.iter().map(|r| mix(v, r)).collect()),
        VertexAttributeValues::Float32x3(v) => VertexAttributeValues::Float32x3(
            recipes
                .iter()
                .map(|r| {
                    let m = mix(v, r);
                    if normalise { Vec3::from(m).normalize_or_zero().to_array() } else { m }
                })
                .collect(),
        ),
        VertexAttributeValues::Float32x4(v) => VertexAttributeValues::Float32x4(recipes.iter().map(|r| mix(v, r)).collect()),
        _ => return None,
    })
}

/// Skin weights blended from the corners: each joint's weight summed by the barycentrics, the four strongest kept and
/// renormalised (corners keep theirs exactly).
fn blend_skin(recipes: &[Recipe], joints: &[[u16; 4]], weights: &[[f32; 4]]) -> (Vec<[u16; 4]>, Vec<[f32; 4]>) {
    let mut js = Vec::with_capacity(recipes.len());
    let mut ws = Vec::with_capacity(recipes.len());
    for r in recipes {
        if r.n == 1 {
            js.push(joints[r.src[0] as usize]);
            ws.push(weights[r.src[0] as usize]);
            continue;
        }
        let mut acc: Vec<(u16, f32)> = Vec::with_capacity(12);
        for k in 0..r.n {
            let v = r.src[k] as usize;
            for (j, w) in joints[v].iter().zip(weights[v]) {
                if w <= 0.0 {
                    continue;
                }
                match acc.iter_mut().find(|(aj, _)| aj == j) {
                    Some(e) => e.1 += w * r.w[k],
                    None => acc.push((*j, w * r.w[k])),
                }
            }
        }
        acc.sort_by(|a, b| b.1.total_cmp(&a.1));
        acc.truncate(4);
        let total: f32 = acc.iter().map(|a| a.1).sum();
        let mut j = [0u16; 4];
        let mut w = [0.0f32; 4];
        for (i, (aj, aw)) in acc.iter().enumerate() {
            j[i] = *aj;
            w[i] = if total > 0.0 { aw / total } else { 0.0 };
        }
        js.push(j);
        ws.push(w);
    }
    (js, ws)
}

/// A skinned part drawn with its refined mesh; `original` is restored when the setting goes off.
#[derive(Component)]
pub struct Tessellated {
    pub original: Handle<Mesh>,
    pub refined: Handle<Mesh>,
}

/// The refined meshes, per original mesh, for the parameters they were made with.
#[derive(Resource, Default)]
struct RefinedMeshes {
    params: Option<TessParams>,
    by_original: HashMap<AssetId<Mesh>, Option<Handle<Mesh>>>,
}

pub struct TessellationPlugin;

impl Plugin for TessellationPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<RefinedMeshes>().add_systems(PostUpdate, apply_tessellation.after(crate::graphics::sync));
    }
}

/// Swaps every skinned part's mesh for its refined one while `tessellation` is on, back when it goes off. The old
/// remaster tessellated the skinned draws only (vk_device.cpp: vertex shaders with relative constants), props with
/// tess_props=1, off by default: the same set here, the characters.
#[allow(clippy::type_complexity)]
fn apply_tessellation(
    mut commands: Commands,
    settings: Res<GraphicsSettings>,
    mut cache: ResMut<RefinedMeshes>,
    mut meshes: ResMut<Assets<Mesh>>,
    parts: Query<(Entity, Ref<Mesh3d>, Ref<SkinnedMesh>, Option<&Tessellated>)>,
) {
    if !settings.is_changed() && !parts.iter().any(|(_, m, s, _)| m.is_changed() || s.is_added()) {
        return;
    }
    let g = &settings.geometry;
    let params = TessParams { level: g.tessellation_level, shape: g.tessellation_shape, smooth_angle: g.tessellation_smooth_angle, sharp_angle: g.tessellation_sharp_angle };
    let on = g.tessellation && params.level >= 2;
    if cache.params != Some(params) {
        cache.params = Some(params);
        cache.by_original.clear();
    }
    // the meshes of a level left behind (the library is per level) let their refined meshes go too
    cache.by_original.retain(|id, _| meshes.contains(*id));
    for (e, mesh, _, tess) in &parts {
        // a part whose mesh someone else replaced since: that mesh is its original now
        let original = match tess {
            Some(t) if mesh.0 == t.refined => t.original.clone(),
            _ => mesh.0.clone(),
        };
        let refined = if on {
            cache
                .by_original
                .entry(original.id())
                .or_insert_with(|| {
                    let r = meshes.get(&original).and_then(|m| refine(m, &params));
                    if r.is_none() {
                        debug!("tessellation: mesh {:?} kept (no normals or attributes it cannot interpolate)", original.id());
                    }
                    r.map(|m| meshes.add(m))
                })
                .clone()
        } else {
            None
        };
        match refined {
            Some(r) => {
                if mesh.0 != r {
                    commands.entity(e).insert(Mesh3d(r.clone()));
                }
                if tess.is_none_or(|t| t.refined != r || t.original != original) {
                    commands.entity(e).insert(Tessellated { original, refined: r });
                }
            }
            None => {
                if tess.is_some() {
                    if mesh.0 != original {
                        commands.entity(e).insert(Mesh3d(original));
                    }
                    commands.entity(e).remove::<Tessellated>();
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const PARAMS: TessParams = TessParams { level: 4, shape: 0.75, smooth_angle: 25.0, sharp_angle: 50.0 };

    fn mesh(positions: Vec<[f32; 3]>, normals: Vec<[f32; 3]>, uvs: Vec<[f32; 2]>, idx: Vec<u32>) -> Mesh {
        Mesh::new(PrimitiveTopology::TriangleList, RenderAssetUsages::all())
            .with_inserted_attribute(Mesh::ATTRIBUTE_POSITION, positions)
            .with_inserted_attribute(Mesh::ATTRIBUTE_NORMAL, normals)
            .with_inserted_attribute(Mesh::ATTRIBUTE_UV_0, uvs)
            .with_inserted_indices(Indices::U32(idx))
    }

    fn positions(m: &Mesh) -> Vec<Vec3> {
        match m.attribute(Mesh::ATTRIBUTE_POSITION) {
            Some(VertexAttributeValues::Float32x3(p)) => p.iter().map(|v| Vec3::from(*v)).collect(),
            _ => Vec::new(),
        }
    }

    /// An icosahedron (12 corners, smooth normals); `split`: every triangle has corners of its own (as a mesh with
    /// UV seams everywhere), each with its own UVs.
    fn icosahedron(split: bool) -> Mesh {
        let t = (1.0 + 5f32.sqrt()) / 2.0;
        let v: Vec<Vec3> = [
            [-1.0, t, 0.0], [1.0, t, 0.0], [-1.0, -t, 0.0], [1.0, -t, 0.0], [0.0, -1.0, t], [0.0, 1.0, t],
            [0.0, -1.0, -t], [0.0, 1.0, -t], [t, 0.0, -1.0], [t, 0.0, 1.0], [-t, 0.0, -1.0], [-t, 0.0, 1.0],
        ]
        .iter()
        .map(|p| Vec3::from(*p).normalize())
        .collect();
        let f: [[u32; 3]; 20] = [
            [0, 11, 5], [0, 5, 1], [0, 1, 7], [0, 7, 10], [0, 10, 11], [1, 5, 9], [5, 11, 4], [11, 10, 2], [10, 7, 6],
            [7, 1, 8], [3, 9, 4], [3, 4, 2], [3, 2, 6], [3, 6, 8], [3, 8, 9], [4, 9, 5], [2, 4, 11], [6, 2, 10],
            [8, 6, 7], [9, 8, 1],
        ];
        if split {
            let mut p = Vec::new();
            let mut uv = Vec::new();
            for (i, tri) in f.iter().enumerate() {
                for (k, c) in tri.iter().enumerate() {
                    p.push(v[*c as usize].to_array());
                    uv.push([i as f32, k as f32]);
                }
            }
            let n = p.clone();
            mesh(p, n, uv, (0..60).collect())
        } else {
            let p: Vec<[f32; 3]> = v.iter().map(|x| x.to_array()).collect();
            mesh(p.clone(), p, vec![[0.0, 0.0]; 12], f.iter().flatten().copied().collect())
        }
    }

    /// Seams do not crack: the mesh split at every edge (each triangle with its own UVs) gives exactly the same
    /// surface points, to the bit, as the welded one; and the surface moved toward the sphere.
    #[test]
    fn crack_free_across_uv_seams() {
        let shared = refine(&icosahedron(false), &PARAMS).expect("refined");
        let split = refine(&icosahedron(true), &PARAMS).expect("refined");
        let bits = |m: &Mesh| {
            let mut s: Vec<[u32; 3]> = positions(m).iter().map(|p| p.to_array().map(f32::to_bits)).collect();
            s.sort();
            s.dedup();
            s
        };
        assert_eq!(bits(&shared), bits(&split));
        assert_eq!(shared.indices().map(|i| i.len()), Some(20 * 16 * 3));
        // the icosahedron's neighbouring corners are 63 degrees apart (beyond the sharp angle): with every edge allowed to
        // curve the inside points bulge out toward the unit sphere
        let all = TessParams { smooth_angle: 0.0, sharp_angle: 0.0, ..PARAMS };
        let curved = refine(&icosahedron(false), &all).expect("refined");
        let flat_min = positions(&shared).iter().map(|p| p.length()).fold(f32::MAX, f32::min);
        let curved_min = positions(&curved).iter().map(|p| p.length()).fold(f32::MAX, f32::min);
        assert!(curved_min > flat_min + 0.02, "{flat_min} -> {curved_min}");
    }

    /// Hard edges stay straight: a cube with split normals (24 corners) keeps every point on its faces.
    #[test]
    fn hard_edges_stay_sharp() {
        let mut p = Vec::new();
        let mut n = Vec::new();
        let mut idx = Vec::new();
        for axis in 0..3 {
            for sign in [-1.0f32, 1.0] {
                let mut nn = Vec3::ZERO;
                nn[axis] = sign;
                let (u, v) = (Vec3::from_array(std::array::from_fn(|i| if i == (axis + 1) % 3 { 1.0 } else { 0.0 })), Vec3::from_array(std::array::from_fn(|i| if i == (axis + 2) % 3 { 1.0 } else { 0.0 })));
                let base = p.len() as u32;
                for (a, b) in [(-1.0, -1.0), (1.0, -1.0), (1.0, 1.0), (-1.0, 1.0)] {
                    p.push((nn + u * a + v * b).to_array());
                    n.push(nn.to_array());
                }
                idx.extend([base, base + 1, base + 2, base, base + 2, base + 3]);
            }
        }
        let uv = vec![[0.0, 0.0]; p.len()];
        let out = refine(&mesh(p, n, uv, idx), &PARAMS).expect("refined");
        for q in positions(&out) {
            assert!((q.abs().max_element() - 1.0).abs() < 1e-5, "{q} left the cube");
        }
    }

    /// Skin weights: corners keep theirs, blended points renormalise over the strongest four.
    #[test]
    fn skin_blends() {
        let m = mesh(vec![[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0]], vec![[0.0, 0.0, 1.0]; 3], vec![[0.0, 0.0]; 3], vec![0, 1, 2])
            .with_inserted_attribute(Mesh::ATTRIBUTE_JOINT_INDEX, VertexAttributeValues::Uint16x4(vec![[0, 0, 0, 0], [1, 0, 0, 0], [2, 3, 4, 5]]))
            .with_inserted_attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT, vec![[1.0, 0.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0], [0.25, 0.25, 0.25, 0.25]]);
        let out = refine(&m, &PARAMS).expect("refined");
        let Some(VertexAttributeValues::Float32x4(w)) = out.attribute(Mesh::ATTRIBUTE_JOINT_WEIGHT) else { panic!("weights") };
        for x in w {
            assert!((x.iter().sum::<f32>() - 1.0).abs() < 1e-5);
        }
        assert_eq!(positions(&out).len(), 15);
    }
}
