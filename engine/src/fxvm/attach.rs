//! Attach points: the named nodes of an object's animation tree (its template's default anim), which the fx
//! scripts' name lists (opcode 0x104) pick from: debris placed at `ref_debri_07`, shells ejected at `ref_shell`, a
//! light at a point. The original evaluates the bone with 0x00453c50 (object, key, out, a3 = 1, .., a6 = 1, .., a8 = 1):
//! the world matrix of the node, its translation scaled by the object's scale (anim_01.cpp:2241); an unknown name or
//! "null" is the object's own matrix (FxNameList_PickRandom 0x0041cba0 returns null for "null").
//!
//! The nodes are taken at the anim's first key (fx reference objects do not animate their points). Data:
//! `anims/<name>.json` of the level export (tracks per node: t [time, x, y, z], r [time, x, y, z, w], s [time, x, y, z];
//! parents per node), game space.

use bevy::prelude::*;
use serde_json::Value;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

/// Node name (lower case) -> model-space matrix of the node (its rest pose).
pub type PointTable = HashMap<String, Mat4>;

/// The attach points of every anim asked so far (loaded on first use, kept for the level).
#[derive(Default)]
pub struct AttachPoints {
    dir: PathBuf,
    cache: HashMap<String, Arc<PointTable>>,
}

impl AttachPoints {
    pub fn new(scene_dir: PathBuf) -> Self {
        AttachPoints { dir: scene_dir, cache: HashMap::new() }
    }

    /// The points of one anim (empty when the anim is not exported).
    pub fn table(&mut self, anim: &str) -> Arc<PointTable> {
        let key = anim.to_lowercase();
        if let Some(t) = self.cache.get(&key) {
            return t.clone();
        }
        let path = self.dir.join("anims").join(format!("{key}.json"));
        let table = std::fs::read_to_string(&path).ok().and_then(|s| serde_json::from_str::<Value>(&s).ok()).map(|v| rest_pose(&v)).unwrap_or_default();
        let t = Arc::new(table);
        self.cache.insert(key, t.clone());
        t
    }

    /// One point's model-space matrix; None for "null" or a name the anim lacks (the object's own matrix).
    pub fn point(&mut self, anim: Option<&str>, name: &str) -> Option<Mat4> {
        if name.eq_ignore_ascii_case("null") {
            return None;
        }
        self.table(anim?).get(&name.to_lowercase()).copied()
    }
}

fn key(track: &Value, k: &str) -> Option<Vec<f32>> {
    track.get(k)?.as_array()?.first()?.as_array().map(|a| a.iter().filter_map(|x| x.as_f64()).map(|x| x as f32).collect())
}

/// The nodes' model-space matrices at the first key: local = T R S, world = parent world x local.
pub fn rest_pose(anim: &Value) -> PointTable {
    let mut local: HashMap<String, Mat4> = HashMap::new();
    if let Some(tracks) = anim.get("tracks").and_then(|t| t.as_object()) {
        for (name, tr) in tracks {
            let t = key(tr, "t").filter(|v| v.len() >= 4).map(|v| Vec3::new(v[1], v[2], v[3])).unwrap_or(Vec3::ZERO);
            let r = key(tr, "r").filter(|v| v.len() >= 5).map(|v| Quat::from_xyzw(v[1], v[2], v[3], v[4]).normalize()).unwrap_or(Quat::IDENTITY);
            let s = key(tr, "s").filter(|v| v.len() >= 4).map(|v| Vec3::new(v[1], v[2], v[3])).unwrap_or(Vec3::ONE);
            local.insert(name.to_lowercase(), Mat4::from_scale_rotation_translation(s, r, t));
        }
    }
    let parents: HashMap<String, String> = anim
        .get("parents")
        .and_then(|p| p.as_object())
        .map(|p| p.iter().filter_map(|(k, v)| v.as_str().map(|v| (k.to_lowercase(), v.to_lowercase()))).collect())
        .unwrap_or_default();
    let mut world = PointTable::new();
    for name in local.keys() {
        let mut m = local[name];
        let mut at = name.clone();
        // walk up (bounded: a malformed tree cannot loop forever)
        for _ in 0..64 {
            let Some(p) = parents.get(&at) else { break };
            let Some(pm) = local.get(p) else { break };
            m = *pm * m;
            at = p.clone();
        }
        world.insert(name.clone(), m);
    }
    world
}

/// The world matrix of a point on an object placed at `place` (rotation + position, game space) and drawn at
/// uniform `scale`: the node's translation scaled by the object's scale, its rotation without scale (0x00453c50
/// with a3 and a8). None: the object's own placement.
pub fn point_world(place: Mat4, scale: f32, point: Option<Mat4>) -> Mat4 {
    match point {
        None => place,
        Some(p) => {
            let (_, r, t) = p.to_scale_rotation_translation();
            place * Mat4::from_rotation_translation(r, t * scale)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rest_pose_chains_parents_and_scale() {
        let v: Value = serde_json::from_str(
            r#"{"tracks": {"center": {"t": [[0, 0, 0, 1]], "r": [[0, 0, 0, 0, 1]], "s": [[0, 2, 2, 2]]},
                           "ref_a": {"t": [[0, 1, 0, 0]], "r": [[0, 0, 0, 0.7071068, 0.7071068]]}},
                "parents": {"center": null, "ref_a": "center"}}"#,
        )
        .unwrap();
        let t = rest_pose(&v);
        // ref_a: 1 unit along x, scaled by the parent's 2, raised by the parent's 1
        let p = t["ref_a"].transform_point3(Vec3::ZERO);
        assert!((p - Vec3::new(2.0, 0.0, 1.0)).length() < 1e-5, "{p}");
        // its rotation: 90 degrees about z
        let x = t["ref_a"].transform_vector3(Vec3::X).normalize();
        assert!((x - Vec3::Y).length() < 1e-5, "{x}");
    }

    #[test]
    fn point_world_scales_translation_only() {
        let place = Mat4::from_translation(Vec3::new(10.0, 0.0, 0.0));
        let p = Mat4::from_rotation_translation(Quat::from_rotation_z(1.0), Vec3::new(0.0, 1.0, 0.0));
        let w = point_world(place, 3.0, Some(p));
        assert!((w.transform_point3(Vec3::ZERO) - Vec3::new(10.0, 3.0, 0.0)).length() < 1e-5);
        assert!((w.transform_vector3(Vec3::X).length() - 1.0).abs() < 1e-5);
        assert_eq!(point_world(place, 3.0, None), place);
    }
}
