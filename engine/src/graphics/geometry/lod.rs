//! Level of detail (settings `lod_max`, `lod_scale`): an object draws one of its template's LOD models, chosen by its
//! distance as the original chooses it, or always its finest.
//!
//! The rule is Object_SelectLod 0x00452f50 (archive/src/renderer/render_r5.cpp): d = the object's depth along the camera's view
//! direction (clamped at 0), m = tan(fov / 2) * d * A + B, with (A, B) the model LOD quality's pair from the table
//! 0x005b0c78 (read from the exe: (16, 100), (8, 50), (4, 25), (2, 0), (1, 0); the game runs at quality 4, (1, 0):
//! docs/structs/Game.md +0x4a4). The LODs are walked in order, skipping the collision and shadow LODs (template record
//! +0 and +4: collision_lod_index, shadow_lod_index); each one whose m is not below the last chosen LOD's cutoff
//! becomes the choice. Cutoffs (the template's +0x40, ObjectSystem_LoadTemplates 0x0045bf70, docs/formats/big-obj.md):
//! -1 for the last LOD and the collision / shadow ones, else 0, then the property set's `lodcutoff_<i>`; a negative
//! cutoff counts as itself + 2^32 (0x005bcb28), i.e. never.
//!
//! The old remaster's two switches (archive/hook/enhance/render.cpp, used in Object_SelectLod): `lod_max` draws the finest at
//! every distance (d = 0), `lod_scale` divides d (the finer meshes kept to that multiple of the distance; Remaster 2).
//! Since 2026-10-13 the engine drew the finest everywhere (the human's decision, docs/decision.md), which is the
//! Remaster's `lod_max`; the Original switches as the game does.
//!
//! Cost: none while `lod_max` is on: the other LODs' models are only loaded and spawned (hidden) once switching is
//! wanted. Guesses (labelled): the camera's +0xf4 field of view that 0x0040f840 returns is the horizontal one (camera
//! nodes store horizontal fields of view, camera.rs `vertical_fov`); a LOD whose model the export lacks falls back to
//! the nearest finer one that has a model (the original would draw nothing).

use bevy::camera::visibility::{NoFrustumCulling, VisibilitySystems};
use bevy::light::NotShadowCaster;
use bevy::prelude::*;

use crate::graphics::GraphicsSettings;
use crate::scene::PartBind;

/// The model LOD quality pair (A, B) the game runs with (table 0x005b0c78 at quality 4).
const QUALITY_SCALE: f32 = 1.0;
const QUALITY_OFFSET: f32 = 0.0;
/// A negative cutoff's offset (0x005bcb28 = 2^32): such a LOD is never left for a coarser one.
const NEGATIVE_CUTOFF_OFFSET: f64 = 4_294_967_296.0;
/// The original camera's aspect (camera +0x154, camera.rs `vertical_fov`): its horizontal field of view from Bevy's
/// vertical one.
const ORIGINAL_ASPECT: f32 = 1.702_128;

/// An object whose template has several LOD models: what switching needs.
#[derive(Component, Clone, Debug)]
pub struct LodSource {
    /// per LOD index: the model (None: not exported)
    pub models: Vec<Option<String>>,
    /// per LOD index: the cutoff, m at and beyond which the next LOD takes over
    pub cutoffs: Vec<f64>,
    /// the collision and shadow LODs, never drawn by the selection
    pub skipped: [Option<usize>; 2],
    /// whether the parts cast and receive shadows (as the finest LOD's do)
    pub shadowing: crate::level::Shadowing,
}

impl LodSource {
    /// From an object's template and property set; None when there is nothing to switch between.
    pub fn new(template: &crate::gamedata::Template, props: &crate::gamedata::Props, shadowing: crate::level::Shadowing) -> Option<Self> {
        let n = template.lods.len();
        let index = |k: &str| props.has(k).then(|| props.i(k, -1)).filter(|i| *i >= 0 && (*i as usize) < n).map(|i| i as usize);
        let skipped = [index("collision_lod_index"), index("shadow_lod_index")];
        let cutoffs = (0..n)
            .map(|i| {
                let default = if i + 1 == n || skipped.contains(&Some(i)) { -1 } else { 0 };
                let raw = props.i(&format!("lodcutoff_{i}"), default) as i32;
                if raw < 0 { raw as f64 + NEGATIVE_CUTOFF_OFFSET } else { raw as f64 }
            })
            .collect();
        let models: Vec<Option<String>> = template.lods.iter().map(|m| Some(m.clone()).filter(|m| !m.is_empty())).collect();
        let s = LodSource { models, cutoffs, skipped, shadowing };
        let drawn: std::collections::HashSet<&str> = (0..n).filter(|i| !s.is_skipped(*i)).filter_map(|i| s.models[i].as_deref()).collect();
        (drawn.len() > 1).then_some(s)
    }

    fn is_skipped(&self, i: usize) -> bool {
        self.skipped.contains(&Some(i))
    }

    /// The LOD index for the metric m (Object_SelectLod's walk); 0 when none qualifies.
    pub fn select(&self, m: f64) -> usize {
        let mut chosen = None;
        let mut threshold = 0.0f64;
        for i in 0..self.cutoffs.len() {
            if !self.is_skipped(i) && m >= threshold {
                chosen = Some(i);
                threshold = self.cutoffs[i];
            }
        }
        chosen.unwrap_or(0)
    }
}

/// The spawned parts of each LOD (index into the template's LODs) and the one drawn.
#[derive(Component)]
pub struct LodParts {
    by_lod: Vec<Vec<Entity>>,
    shown: usize,
    finest: usize,
}

impl LodParts {
    /// Whether a coarser LOD than the finest is drawn now.
    pub fn coarser(&self) -> bool {
        self.shown != self.finest
    }

    /// The LOD to draw for a selected index: itself when it has parts, else the nearest finer one that has.
    fn drawable(&self, selected: usize) -> usize {
        (0..=selected.min(self.by_lod.len().saturating_sub(1))).rev().find(|i| !self.by_lod[*i].is_empty()).unwrap_or(0)
    }
}

pub struct LodPlugin;

impl Plugin for LodPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PostUpdate, (attach_lods, select_lods).chain().after(crate::graphics::sync).before(VisibilitySystems::VisibilityPropagate));
    }
}

/// Spawns the other LODs' parts (hidden) under objects with a [`LodSource`] once switching is wanted (`lod_max` off).
/// The finest LOD's parts are the ones the level spawned (the object's direct children with a mesh); a skinned LOD
/// shares the object's rig. Objects with parts riding single bones keep their finest model (no switching).
#[allow(clippy::too_many_arguments)]
fn attach_lods(
    mut commands: Commands,
    settings: Res<GraphicsSettings>,
    objects: Query<(Entity, &LodSource, Option<&Children>, Option<&crate::skin::Animator>), Without<LodParts>>,
    parts: Query<(), With<Mesh3d>>,
    lib: Option<ResMut<crate::scene::Library>>,
    assets: Res<AssetServer>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
) {
    if settings.geometry.lod_max || objects.is_empty() {
        return;
    }
    let Some(mut lib) = lib else { return };
    for (e, src, children, animator) in &objects {
        let finest: Vec<Entity> = children.map(|c| c.iter().filter(|c| parts.contains(*c)).collect()).unwrap_or_default();
        let first = (0..src.models.len()).find(|i| !src.is_skipped(*i)).unwrap_or(0);
        let finest_model = src.models.get(first).cloned().flatten();
        let skeleton = finest_model.as_deref().and_then(|m| lib.load_model(m, false, &assets, &mut meshes, &mut materials)).map(|m| (m.skeleton.clone(), m.binds.iter().any(|b| matches!(b, PartBind::Bone(_)))));
        let mut by_lod: Vec<Vec<Entity>> = vec![Vec::new(); src.models.len()];
        match skeleton {
            // a skinned model switches only on its rig
            Some((skeleton, false)) if !finest.is_empty() && (skeleton.is_none() || animator.is_some()) => {
                by_lod[first] = finest;
                for i in (first + 1)..src.models.len() {
                    if src.is_skipped(i) {
                        continue;
                    }
                    let Some(name) = src.models[i].as_deref() else { continue };
                    if src.models[..i].iter().zip(0..).any(|(m, j)| !src.is_skipped(j) && m.as_deref() == Some(name)) {
                        // the same model as a finer LOD (pickup_rifle_ammo_0 three times): that LOD's parts
                        by_lod[i] = by_lod[(0..i).rev().find(|j| src.models[*j].as_deref() == Some(name)).unwrap_or(first)].clone();
                        continue;
                    }
                    let Some(model) = lib.load_model(name, false, &assets, &mut meshes, &mut materials) else { continue };
                    if model.skeleton != skeleton || model.binds.iter().any(|b| matches!(b, PartBind::Bone(_))) {
                        continue;
                    }
                    for ((mesh, mat), bind) in model.parts.iter().zip(&model.binds) {
                        let mut c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()), Visibility::Hidden));
                        if !src.shadowing.casts {
                            c.insert(NotShadowCaster);
                        }
                        if !src.shadowing.receives {
                            c.insert(bevy::light::NotShadowReceiver);
                        }
                        if let (PartBind::Skinned, Some(a)) = (bind, animator) {
                            c.insert((crate::skin::skinned_mesh(a), NoFrustumCulling));
                        }
                        let c = c.id();
                        commands.entity(e).add_child(c);
                        by_lod[i].push(c);
                    }
                }
            }
            // nothing to switch (no parts, parts on single bones, or a skinned model without its rig)
            _ => {}
        }
        commands.entity(e).insert(LodParts { by_lod, shown: first, finest: first });
    }
}

/// Each frame: every switching object's LOD from its depth along the camera's view (Object_SelectLod 0x00452f50).
fn select_lods(
    settings: Res<GraphicsSettings>,
    cams: Query<(&Camera, &GlobalTransform, &Projection), (With<Camera3d>, Without<crate::graphics::env_map::EnvProbeView>)>,
    mut objects: Query<(&GlobalTransform, &LodSource, &mut LodParts)>,
    mut vis: Query<&mut Visibility>,
) {
    let Some((_, cam, proj)) = cams.iter().find(|(c, _, _)| c.is_active) else { return };
    let Projection::Perspective(pp) = proj else { return };
    let g = &settings.geometry;
    // tan of half the original's (horizontal) field of view
    let tan_half = (pp.fov * 0.5).tan() * ORIGINAL_ASPECT;
    let (eye, ahead) = (cam.translation(), cam.forward().as_vec3());
    for (at, src, mut lods) in &mut objects {
        let mut d = (at.translation() - eye).dot(ahead).max(0.0);
        if g.lod_max {
            d = 0.0;
        } else if g.lod_scale > 1.0 {
            d /= g.lod_scale;
        }
        let m = (tan_half * d) as f64 * QUALITY_SCALE as f64 + QUALITY_OFFSET as f64;
        let want = lods.drawable(src.select(m));
        if want == lods.shown {
            continue;
        }
        let (old, new) = (lods.by_lod[lods.shown].clone(), lods.by_lod[want].clone());
        for p in old.iter().filter(|p| !new.contains(p)) {
            if let Ok(mut v) = vis.get_mut(*p) {
                *v = Visibility::Hidden;
            }
        }
        for p in &new {
            if let Ok(mut v) = vis.get_mut(*p) {
                *v = Visibility::Inherited;
            }
        }
        lods.shown = want;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(kv: &[(&str, i64)]) -> crate::gamedata::Props {
        crate::gamedata::Props(kv.iter().map(|(k, v)| ((*k).to_owned(), serde_json::Value::from(*v))).collect())
    }

    fn template(lods: &[&str]) -> crate::gamedata::Template {
        serde_json::from_value(serde_json::json!({ "model": lods[0], "lods": lods, "property": "p" })).expect("template")
    }

    /// The pine tree (lev_1_m1 p_pine_tree): eight drawn LODs and a collision LOD; the grunt: three and a collision.
    #[test]
    fn selects_as_the_original() {
        let pine = template(&["pine_tree_0", "pine_tree_1", "pine_tree_2", "pine_tree_3", "pine_tree_4", "pine_tree_5", "pine_tree_6", "pine_tree_7", "pine_tree_coll"]);
        let p = props(&[("lodcutoff_0", 20), ("lodcutoff_1", 60), ("lodcutoff_2", 120), ("lodcutoff_3", 170), ("lodcutoff_4", 220), ("lodcutoff_5", 270), ("lodcutoff_6", 320), ("lodcutoff_7", 10000), ("lodcutoff_8", 20000), ("collision_lod_index", 8)]);
        let s = LodSource::new(&pine, &p, crate::level::Shadowing::casting(true)).expect("switching");
        assert_eq!([0.0, 19.9, 20.0, 59.0, 60.0, 319.0, 320.0, 1e6].map(|m| s.select(m)), [0, 0, 1, 1, 2, 6, 7, 7]);
        let grunt = template(&["grunt_1", "grunt_2", "grunt_6", "grunt_coll_9"]);
        let g = LodSource::new(&grunt, &props(&[("lodcutoff_0", 30), ("lodcutoff_1", 70), ("lodcutoff_2", 100000), ("lodcutoff_3", 800000), ("collision_lod_index", 3)]), crate::level::Shadowing::casting(true)).expect("switching");
        assert_eq!([10.0, 30.0, 70.0, 1e7].map(|m| g.select(m)), [0, 1, 2, 2]);
        // no cutoffs in the set: the finest LOD's default cutoff 0 hands over at once, the last (-1) never
        let house = template(&["h_0", "h_1", "h_coll"]);
        let h = LodSource::new(&house, &props(&[("collision_lod_index", 2)]), crate::level::Shadowing::casting(true)).expect("switching");
        assert_eq!([0.0, 5.0].map(|m| h.select(m)), [1, 1]);
        // one model only: nothing to switch
        assert!(LodSource::new(&template(&["a_0", "a_0", "a_coll"]), &props(&[("collision_lod_index", 2)]), crate::level::Shadowing::casting(true)).is_none());
    }
}
