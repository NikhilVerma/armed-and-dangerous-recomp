//! A model's named nodes in model space (game axes): the original's bone-matrix lookup 0x00453c50 (an object, a node
//! name, the out matrix) on a model's node tree. A skinned model has its skeleton (the Animator's rest pose, or its
//! live joints); a rigid one (v_rocketbud_0, the turret attachments) only the node table of its reference anim, which
//! the exporter writes as the anim's first keys and parents (tools/remaster_export_anim.py: local transforms, the
//! conjugation rule already applied).

use bevy::prelude::*;
use std::collections::HashMap;

/// Model-space matrices by lower-case node name.
#[derive(Clone, Default, Debug)]
pub struct Nodes(pub HashMap<String, Mat4>);

impl Nodes {
    pub fn get(&self, name: &str) -> Option<Mat4> {
        self.0.get(&name.to_lowercase()).copied()
    }

    /// The node's model-space position.
    pub fn point(&self, name: &str) -> Option<Vec3> {
        self.get(name).map(|m| m.w_axis.truncate())
    }

    /// The nodes of an anim's first frame (its `tracks` and `parents`), every node's chain multiplied out.
    pub fn from_anim(dir: &std::path::Path, anim: &str) -> Option<Nodes> {
        let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(dir.join("anims").join(format!("{}.json", anim.to_lowercase()))).ok()?).ok()?;
        let tracks = v.get("tracks")?.as_object()?;
        let parents = v.get("parents").and_then(|p| p.as_object());
        let first = |k: &serde_json::Value, n: usize| -> Option<Vec<f32>> {
            let a = k.as_array()?.first()?.as_array()?;
            (a.len() > n).then(|| a.iter().skip(1).filter_map(|x| x.as_f64()).map(|x| x as f32).collect())
        };
        let local: HashMap<String, Mat4> = tracks
            .iter()
            .map(|(name, t)| {
                let tr = t.get("t").and_then(|k| first(k, 3)).map(|p| Vec3::new(p[0], p[1], p[2])).unwrap_or(Vec3::ZERO);
                let r = t.get("r").and_then(|k| first(k, 4)).map(|q| Quat::from_xyzw(q[0], q[1], q[2], q[3]).normalize()).unwrap_or(Quat::IDENTITY);
                (name.clone(), Mat4::from_rotation_translation(r, tr))
            })
            .collect();
        let mut out = HashMap::new();
        for name in local.keys() {
            let mut m = Mat4::IDENTITY;
            let mut at = Some(name.clone());
            let mut guard = 0;
            while let (Some(n), true) = (at, guard < 64) {
                m = local.get(&n).copied().unwrap_or(Mat4::IDENTITY) * m;
                at = parents.and_then(|p| p.get(&n)).and_then(|p| p.as_str()).map(str::to_owned);
                guard += 1;
            }
            out.insert(name.to_lowercase(), m);
        }
        Some(Nodes(out))
    }

    /// The rest pose of a skeleton (model space).
    pub fn from_skeleton(sk: &crate::skin::Skeleton) -> Nodes {
        let mut out = HashMap::new();
        for (i, name) in sk.names.iter().enumerate() {
            let mut m = Mat4::IDENTITY;
            let mut j = i as i32;
            while j >= 0 {
                m = sk.rest[j as usize].to_matrix() * m;
                j = sk.parents[j as usize];
            }
            out.insert(name.clone(), m);
        }
        Nodes(out)
    }

    /// A model's nodes: its skeleton's rest pose over its reference anim's node table (either may be missing).
    pub fn of_model(dir: &std::path::Path, model: &str, skeleton: Option<&crate::skin::Skeleton>) -> Nodes {
        let reference = std::fs::read_to_string(dir.join("skeletons").join(format!("{model}.json")))
            .ok()
            .and_then(|s| serde_json::from_str::<serde_json::Value>(&s).ok())
            .and_then(|v| v["reference_anim"].as_str().map(str::to_owned));
        let mut n = reference.as_deref().and_then(|r| Nodes::from_anim(dir, r)).unwrap_or_default();
        if let Some(sk) = skeleton {
            n.0.extend(Nodes::from_skeleton(sk).0);
        }
        n
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A child node's matrix is its parent's times its own (the anim table's chain).
    #[test]
    fn chains_multiply() {
        let dir = std::env::temp_dir().join(format!("nodes_test_{}", std::process::id()));
        std::fs::create_dir_all(dir.join("anims")).unwrap();
        let anim = serde_json::json!({
            "tracks": {
                "base": {"t": [[0.0, 0.0, 0.0, 5.0]], "r": [[0.0, 0.0, 0.0, 0.7071068, 0.7071068]]},
                "tip": {"t": [[0.0, 0.0, -2.0, 0.0]]}
            },
            "parents": {"base": null, "tip": "base"}
        });
        std::fs::write(dir.join("anims").join("t_ref.json"), anim.to_string()).unwrap();
        let n = Nodes::from_anim(&dir, "t_ref").unwrap();
        // tip: 2 along base's -y, base turned 90 degrees about z: -y -> +x
        let p = n.point("TIP").unwrap();
        assert!((p - Vec3::new(2.0, 0.0, 5.0)).length() < 1e-4, "{p:?}");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
