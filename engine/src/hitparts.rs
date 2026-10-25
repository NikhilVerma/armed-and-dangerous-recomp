//! Characters' collision parts for bullet hits. The original's bullets (Projectile_Update 0x0047e580: a
//! detail-level-2 ray) hit a character's collision model, the template's LOD at collision_lod_index (grunt:
//! grunt_coll_9, 13 convex boxes: head, chest, belly, shoulders, arms, thighs, shins), each part carried by a bone.
//! Each part is a mesh of the collision skin bound to one bone (the skin chunk's bone list: grunt_coll_9's m_chest
//! -> chest, m_neck -> head, m_center -> center ...; exported as the submesh's "bone"), posed every hit test by that
//! bone's skinning matrix. Without a bone name, the bone whose bind-pose segment or joint lies nearest the part.

use bevy::mesh::skinning::SkinnedMeshInverseBindposes;
use bevy::prelude::*;
use std::path::Path;

/// One part: its bone's name (lower case, when the export has it) and its triangles in model space (the game's
/// axes, as the render mesh's).
pub struct Hull {
    pub bone: Option<String>,
    pub tris: Vec<[Vec3; 3]>,
}

#[derive(Resource, Default)]
pub struct HitHulls {
    pub grunt: Vec<Hull>,
    /// Roman (template trotter: collision_lod_index 2 = trotter_coll_0)
    pub roman: Vec<Hull>,
    /// Jonesy (molmer: molmer_coll_0) and Q (q111: q111_coll_0)
    pub molmer: Vec<Hull>,
    pub q111: Vec<Hull>,
}

/// Reads a collision model's parts (models/<name>.json / .bin: float3 positions, u32 indices).
pub fn load(dir: &Path, name: &str) -> Vec<Hull> {
    let (Ok(json), Ok(bin)) = (
        std::fs::read_to_string(dir.join("models").join(format!("{name}.json"))),
        std::fs::read(dir.join("models").join(format!("{name}.bin"))),
    ) else {
        return Vec::new();
    };
    let Ok(v) = serde_json::from_str::<serde_json::Value>(&json) else { return Vec::new() };
    let f32_at = |o: usize| f32::from_le_bytes(bin[o..o + 4].try_into().unwrap());
    let u32_at = |o: usize| u32::from_le_bytes(bin[o..o + 4].try_into().unwrap()) as usize;
    let mut hulls = Vec::new();
    for s in v["submeshes"].as_array().into_iter().flatten() {
        let (Some(n), Some(pos), Some(ni), Some(io)) = (s["vertices"].as_u64(), s["positions"].as_u64(), s["indices"].as_u64(), s["index_offset"].as_u64()) else {
            continue;
        };
        let (n, pos, ni, io) = (n as usize, pos as usize, ni as usize, io as usize);
        if pos + n * 12 > bin.len() || io + ni * 4 > bin.len() {
            continue;
        }
        let p: Vec<Vec3> = (0..n).map(|i| Vec3::new(f32_at(pos + i * 12), f32_at(pos + i * 12 + 4), f32_at(pos + i * 12 + 8))).collect();
        let tris = (0..ni / 3)
            .filter_map(|t| {
                let (a, b, c) = (u32_at(io + t * 12), u32_at(io + t * 12 + 4), u32_at(io + t * 12 + 8));
                (a < n && b < n && c < n).then(|| [p[a], p[b], p[c]])
            })
            .collect();
        hulls.push(Hull { bone: s["bone"].as_str().map(|b| b.to_lowercase()), tris });
    }
    hulls
}

/// The bone carrying each part: the nearest bind-pose bone segment (parent joint to joint, carried by the parent) or
/// joint, over the body's bones (no ref_* helpers, cameras, fingers or face bones: the head part goes to the head).
fn bones_of(hulls: &[Hull], a: &crate::skin::Animator, ib: &[Mat4]) -> Vec<usize> {
    let bind = |i: usize| ib.get(i).map(|m| m.inverse().w_axis.truncate());
    let body = |i: usize| {
        let n = a.skeleton.names[i].trim().to_lowercase();
        !(n.starts_with("ref") || ["camera", "finger", "thumb", "jaw", "eye", "brow", "lip"].iter().any(|w| n.contains(w)))
    };
    hulls
        .iter()
        .map(|h| {
            if let Some(&i) = h.bone.as_ref().and_then(|b| a.skeleton.index.get(b)) {
                return i;
            }
            let h = &h.tris;
            let c = h.iter().flat_map(|t| t.iter()).copied().sum::<Vec3>() / (h.len() * 3).max(1) as f32;
            let mut best = (f32::MAX, 0usize);
            for i in 0..a.skeleton.names.len() {
                if !body(i) {
                    continue;
                }
                let Some(bi) = bind(i) else { continue };
                let d = c.distance(bi);
                if d < best.0 {
                    best = (d, i);
                }
                let p = a.skeleton.parents.get(i).copied().unwrap_or(-1);
                if p >= 0 && body(p as usize) {
                    if let Some(bp) = bind(p as usize) {
                        let s = bi - bp;
                        let t = ((c - bp).dot(s) / s.length_squared().max(1e-6)).clamp(0.0, 1.0);
                        let d = c.distance(bp + s * t);
                        if d < best.0 {
                            best = (d, p as usize);
                        }
                    }
                }
            }
            best.1
        })
        .collect()
}

/// Where the segment from -> to first enters a part posed by the skeleton: (fraction along the segment, bone name).
pub fn hit(
    hulls: &[Hull],
    a: &crate::skin::Animator,
    joints: &Query<&GlobalTransform>,
    binds: &Assets<SkinnedMeshInverseBindposes>,
    from: Vec3,
    to: Vec3,
) -> Option<(f32, String)> {
    let ib: &[Mat4] = binds.get(&a.skeleton.inverse_bind)?;
    let owners = bones_of(hulls, a, ib);
    let d = to - from;
    let mut best: Option<(f32, usize)> = None;
    for (h, &bone) in hulls.iter().zip(&owners) {
        let (Some(j), Some(inv)) = (a.joints.get(bone).and_then(|e| joints.get(*e).ok()), ib.get(bone)) else { continue };
        let m = j.to_matrix() * *inv;
        for tri in &h.tris {
            let [v0, v1, v2] = tri.map(|v| m.transform_point3(v));
            // Moller-Trumbore, both faces
            let (e1, e2) = (v1 - v0, v2 - v0);
            let pv = d.cross(e2);
            let det = e1.dot(pv);
            if det.abs() < 1e-9 {
                continue;
            }
            let tv = from - v0;
            let u = tv.dot(pv) / det;
            if !(0.0..=1.0).contains(&u) {
                continue;
            }
            let qv = tv.cross(e1);
            let v = d.dot(qv) / det;
            if v < 0.0 || u + v > 1.0 {
                continue;
            }
            let t = e2.dot(qv) / det;
            if (0.0..=1.0).contains(&t) && best.is_none_or(|b| t < b.0) {
                best = Some((t, bone));
            }
        }
    }
    best.map(|(t, b)| (t, a.skeleton.names[b].trim().to_lowercase()))
}

/// Whether the segment from -> to passes near enough a character standing at `feet` for its parts to be worth testing
/// (within 12 units of its middle, 5 above the feet: every character's collision model fits in that).
pub fn near_segment(feet: Vec3, from: Vec3, to: Vec3) -> bool {
    let (c, seg) = (feet + Vec3::Y * 5.0, to - from);
    let u = ((c - from).dot(seg) / seg.length_squared().max(1e-6)).clamp(0.0, 1.0);
    (from + seg * u).distance(c) <= 12.0
}
