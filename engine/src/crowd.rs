//! Crowds: many copies of one skinned model playing one looping anim, each drawn as a single plain mesh instead of a
//! rigged character. The anim is baked once per (model, anim) into a flipbook of meshes (the model skinned on the CPU
//! at evenly spaced times, parts that share a material merged); a member shows the frame of its own phase. Members
//! of a crowd share their meshes and materials, so the renderer instances them (one draw per frame mesh in view),
//! and a member is one entity, not the ~70 of a rigged character (its joints, parts and animator).
//!
//! Game-neutral. Used by the defence levels' marching platoons (army.rs): the original draws those members as
//! one sprite each (FakeGrunt_Update 0x00446460: a particle of pool 2 with the grunt / captain picture of
//! n_fx_bank_2); the remaster shows the running grunt model at the cost of a sprite.

use std::collections::HashMap;
use std::sync::Arc;

use bevy::light::NotShadowCaster;
use bevy::mesh::{Indices, PrimitiveTopology, VertexAttributeValues as V};
use bevy::prelude::*;

use crate::levelreset::LevelAppExt;
use crate::scene::PartBind;

/// The baked frames per second of anim time (a run cycle of 0.7 s gets 21 frames).
const FRAMES_PER_SECOND: f32 = 30.0;
const MIN_FRAMES: usize = 8;
const MAX_FRAMES: usize = 48;

/// Distance bands of the flipbook's rate: members nearer than the first show every baked frame, then every second
/// frame, then every third (a run cycle at 30 / 15 / 10 frames a second). Each frame change swaps the member's
/// meshes, which the renderer re-extracts and re-specialises (lev_2_m4's ~1,400 members at 4K: ~4 ms of frame time
/// at one rate for all); far members are a few dozen pixels tall. The original's members do not animate at all
/// (one still sprite each).
const RATE_BANDS: [f32; 2] = [150.0, 400.0];

/// Every how many baked frames a member `distance` units from the camera changes its pose.
pub fn frame_step(distance: f32) -> usize {
    1 + RATE_BANDS.iter().filter(|b| distance >= **b).count()
}

/// One baked anim: `frames[i]` are the parts (mesh, material) at time i / frames.len() of the anim.
pub struct CrowdClip {
    pub duration: f32,
    pub frames: Vec<Vec<(Handle<Mesh>, Handle<StandardMaterial>)>>,
}

impl CrowdClip {
    /// The frame shown `t` seconds into the (looping) anim.
    pub fn frame_at(&self, t: f32) -> usize {
        let n = self.frames.len().max(1);
        ((t.rem_euclid(self.duration) / self.duration * n as f32) as usize).min(n - 1)
    }
}

/// The baked clips of the level, by (model, anim).
#[derive(Resource, Default)]
pub struct CrowdLibrary {
    clips: HashMap<(String, String), Option<Arc<CrowdClip>>>,
}

/// A crowd member: its clip, where it is in it, and the entities showing its parts (itself for the first).
#[derive(Component)]
pub struct CrowdMember {
    clip: Arc<CrowdClip>,
    /// seconds into the anim
    pub time: f32,
    /// playback rate
    pub speed: f32,
    frame: usize,
    parts: Vec<Entity>,
}

pub struct CrowdPlugin;

impl Plugin for CrowdPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<CrowdLibrary>().add_systems(PostUpdate, animate.before(bevy::camera::visibility::VisibilitySystems::CalculateBounds));
    }
}

/// What baking needs (the model and anim libraries and the asset stores).
pub struct BakeInput<'a> {
    pub models: &'a mut crate::scene::Library,
    pub anims: &'a mut crate::skin::AnimLibrary,
    pub binds: &'a mut Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>,
    pub assets: &'a AssetServer,
    pub meshes: &'a mut Assets<Mesh>,
    pub materials: &'a mut Assets<StandardMaterial>,
}

impl CrowdLibrary {
    /// The clip of `model` playing `anim` (baked on first use); None when the model, its skeleton or the anim is
    /// missing.
    pub fn clip(&mut self, input: BakeInput, model: &str, anim: &str) -> Option<Arc<CrowdClip>> {
        let key = (model.to_owned(), anim.to_lowercase());
        if let Some(c) = self.clips.get(&key) {
            return c.clone();
        }
        let clip = bake(input, model, anim).map(Arc::new);
        if clip.is_none() {
            warn!("crowd: cannot bake {model} / {anim}");
        }
        self.clips.insert(key, clip.clone());
        clip
    }

    /// Spawns a member showing `clip` at `tr`, `time` seconds into it, playing at `speed`.
    pub fn spawn(commands: &mut Commands, clip: &Arc<CrowdClip>, tr: Transform, time: f32, speed: f32, shadows: bool) -> Entity {
        let frame = clip.frame_at(time);
        let parts_of = |i: usize| clip.frames[frame][i].clone();
        let n = clip.frames.first().map_or(0, Vec::len);
        let e = commands.spawn((tr, Visibility::default())).id();
        let mut parts = Vec::with_capacity(n);
        for i in 0..n {
            let (mesh, mat) = parts_of(i);
            let p = if i == 0 {
                commands.entity(e).insert((Mesh3d(mesh), MeshMaterial3d(mat)));
                e
            } else {
                let c = commands.spawn((Transform::IDENTITY, Visibility::default(), Mesh3d(mesh), MeshMaterial3d(mat))).id();
                commands.entity(e).add_child(c);
                c
            };
            if !shadows {
                commands.entity(p).insert(NotShadowCaster);
            }
            parts.push(p);
        }
        commands.entity(e).insert(CrowdMember { clip: clip.clone(), time, speed, frame, parts });
        e
    }
}

/// Members advance through their clip; a member whose shown frame changed (at its distance's rate, `frame_step`)
/// shows the new frame's meshes. One out of view (last frame) keeps its meshes until it is seen.
fn animate(
    time: Res<Time>,
    mut q: Query<(&mut CrowdMember, &GlobalTransform, &ViewVisibility)>,
    mut meshes: Query<&mut Mesh3d>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
) {
    let dt = time.delta_secs();
    let eye = cam.single().ok().map(|c| c.translation());
    for (mut m, gt, seen) in &mut q {
        let m = &mut *m;
        m.time = (m.time + dt * m.speed).rem_euclid(m.clip.duration);
        if !seen.get() {
            continue;
        }
        let step = eye.map_or(1, |e| frame_step(e.distance(gt.translation())));
        let f = m.clip.frame_at(m.time) / step * step;
        if f == m.frame {
            continue;
        }
        m.frame = f;
        for (p, (mesh, _)) in m.parts.iter().zip(&m.clip.frames[f]) {
            if let Ok(mut h) = meshes.get_mut(*p) {
                h.0 = mesh.clone();
            }
        }
    }
}

/// A source part: its mesh data and how its vertices follow the skeleton.
struct Source {
    positions: Vec<Vec3>,
    normals: Vec<Vec3>,
    uvs: Vec<[f32; 2]>,
    /// per vertex up to 4 (joint, weight); one joint at weight 1 for a part on a bone, none for a fixed part
    skin: Vec<[(u16, f32); 4]>,
    indices: Vec<u32>,
    material: Handle<StandardMaterial>,
}

fn bake(input: BakeInput, model: &str, anim_name: &str) -> Option<CrowdClip> {
    let m = input.models.load_model(model, false, input.assets, input.meshes, input.materials)?.light_clone();
    let skeleton = input.anims.skeleton(m.skeleton.as_deref()?, input.binds)?;
    let inverse_bind: Vec<Mat4> = input.binds.get(&skeleton.inverse_bind).map(|b| b.to_vec())?;
    let anim = input.anims.anim(anim_name)?;
    // the parts' vertex data (skinned meshes keep a main-world copy: scene.rs load_model)
    let mut sources = Vec::new();
    for (i, (mesh, mat)) in m.parts.iter().enumerate() {
        let Some(mesh) = input.meshes.get(mesh) else {
            warn!("crowd: {model} part {i} has no CPU copy; left out");
            continue;
        };
        let attr = |id| mesh.try_attribute_option(id).ok().flatten();
        let (Some(V::Float32x3(pos)), Some(V::Float32x3(nrm)), Some(V::Float32x2(uv))) = (attr(Mesh::ATTRIBUTE_POSITION), attr(Mesh::ATTRIBUTE_NORMAL), attr(Mesh::ATTRIBUTE_UV_0)) else { continue };
        let Some(idx) = mesh.try_indices_option().ok().flatten() else { continue };
        let none = [(0u16, 0.0f32); 4];
        let skin: Vec<[(u16, f32); 4]> = match m.binds.get(i) {
            Some(PartBind::Skinned) | None => match (attr(Mesh::ATTRIBUTE_JOINT_INDEX), attr(Mesh::ATTRIBUTE_JOINT_WEIGHT)) {
                (Some(V::Uint16x4(j)), Some(V::Float32x4(w))) => j.iter().zip(w).map(|(j, w)| std::array::from_fn(|k| (j[k], w[k]))).collect(),
                _ => vec![none; pos.len()],
            },
            Some(PartBind::Bone(b)) => match skeleton.index.get(b) {
                Some(&j) => vec![[(j as u16, 1.0), (0, 0.0), (0, 0.0), (0, 0.0)]; pos.len()],
                None => vec![none; pos.len()],
            },
            Some(PartBind::None) => vec![none; pos.len()],
        };
        sources.push(Source {
            positions: pos.iter().map(|p| Vec3::from(*p)).collect(),
            normals: nrm.iter().map(|n| Vec3::from(*n)).collect(),
            uvs: uv.clone(),
            skin,
            indices: idx.iter().map(|i| i as u32).collect(),
            material: mat.clone(),
        });
    }
    if sources.is_empty() {
        return None;
    }
    // parts that share a material become one mesh
    let mut groups: Vec<(Handle<StandardMaterial>, Vec<usize>)> = Vec::new();
    for (i, s) in sources.iter().enumerate() {
        match groups.iter_mut().find(|g| g.0 == s.material) {
            Some(g) => g.1.push(i),
            None => groups.push((s.material.clone(), vec![i])),
        }
    }
    let n = ((anim.duration * FRAMES_PER_SECOND).ceil() as usize).clamp(MIN_FRAMES, MAX_FRAMES);
    let mut frames = Vec::with_capacity(n);
    for f in 0..n {
        let t = anim.duration * f as f32 / n as f32;
        let joints = skeleton.model_matrices(&anim.pose(&skeleton, t));
        let skinning: Vec<Mat4> = joints.iter().zip(&inverse_bind).map(|(j, ib)| *j * *ib).collect();
        let parts = groups.iter().map(|(mat, members)| (input.meshes.add(skin_group(&sources, members, &skinning)), mat.clone())).collect();
        frames.push(parts);
    }
    info!("crowd: baked {model} / {anim_name}: {n} frames of {} meshes", groups.len());
    Some(CrowdClip { duration: anim.duration, frames })
}

/// One merged mesh of the parts `members` skinned by the matrices (joint model-space x inverse bind).
fn skin_group(sources: &[Source], members: &[usize], skinning: &[Mat4]) -> Mesh {
    let (mut pos, mut nrm, mut uv, mut idx) = (Vec::new(), Vec::new(), Vec::new(), Vec::new());
    for &i in members {
        let s = &sources[i];
        let base = pos.len() as u32;
        for (v, (p, (n, w))) in s.positions.iter().zip(s.normals.iter().zip(&s.skin)).enumerate() {
            let (sp, sn) = skin_vertex(*p, *n, w, skinning);
            pos.push(sp.to_array());
            nrm.push(sn.to_array());
            uv.push(s.uvs[v]);
        }
        idx.extend(s.indices.iter().map(|i| i + base));
    }
    let mut mesh = Mesh::new(PrimitiveTopology::TriangleList, bevy::asset::RenderAssetUsages::RENDER_WORLD);
    mesh.insert_attribute(Mesh::ATTRIBUTE_POSITION, pos);
    mesh.insert_attribute(Mesh::ATTRIBUTE_NORMAL, nrm);
    mesh.insert_attribute(Mesh::ATTRIBUTE_UV_0, uv);
    mesh.insert_indices(Indices::U32(idx));
    mesh
}

/// Linear blend skinning of one vertex (as the GPU does: the weighted sum of the joint matrices' transforms); a vertex
/// without weights stays where it is.
fn skin_vertex(p: Vec3, n: Vec3, w: &[(u16, f32); 4], skinning: &[Mat4]) -> (Vec3, Vec3) {
    let total: f32 = w.iter().map(|x| x.1).sum();
    if total <= 0.0 {
        return (p, n);
    }
    let (mut sp, mut sn) = (Vec3::ZERO, Vec3::ZERO);
    for &(j, wt) in w.iter().filter(|x| x.1 > 0.0) {
        let m = skinning.get(j as usize).copied().unwrap_or(Mat4::IDENTITY);
        sp += m.transform_point3(p) * wt;
        sn += m.transform_vector3(n) * wt;
    }
    (sp, sn.normalize_or(n))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frames_wrap_with_the_loop() {
        let clip = CrowdClip { duration: 1.0, frames: vec![Vec::new(); 10] };
        assert_eq!(clip.frame_at(0.0), 0);
        assert_eq!(clip.frame_at(0.55), 5);
        assert_eq!(clip.frame_at(0.999), 9);
        assert_eq!(clip.frame_at(1.25), 2);
        assert_eq!(clip.frame_at(-0.05), 9);
    }

    #[test]
    fn far_members_change_pose_less_often() {
        assert_eq!(frame_step(50.0), 1);
        assert_eq!(frame_step(150.0), 2);
        assert_eq!(frame_step(399.0), 2);
        assert_eq!(frame_step(1000.0), 3);
    }

    #[test]
    fn skinning_blends_joint_matrices() {
        let m = [Mat4::IDENTITY, Mat4::from_translation(Vec3::X * 2.0)];
        let w = [(0, 0.5), (1, 0.5), (0, 0.0), (0, 0.0)];
        let (p, n) = skin_vertex(Vec3::ONE, Vec3::Y, &w, &m);
        assert!((p - Vec3::new(2.0, 1.0, 1.0)).length() < 1e-5);
        assert!((n - Vec3::Y).length() < 1e-5);
        // no weights: unchanged
        let (p, _) = skin_vertex(Vec3::ONE, Vec3::Y, &[(0, 0.0); 4], &m);
        assert_eq!(p, Vec3::ONE);
    }
}
