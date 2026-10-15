//! Objects between the follow camera and Roman fade out (Camera_Update 0x0040e350, orbit mode 0x0040e56c): each
//! object a camera ray hits drops its alpha by camera_fade_out_rate per second down to camera_falloff, and once no
//! ray hits it, rises by camera_fade_in_rate per second back to 1, where it leaves the list (p_globalsettings: 3, 3,
//! 0.3 on lev_1_m1; read at 0x00424456..0x00424494). A faded object draws with its own copies of its materials
//! (the fade in the game material's emissive.w); at 1 the shared ones come back.

use crate::levelreset::LevelAppExt;
use crate::gamematerial::GameMaterial;
use bevy::platform::collections::{HashMap, HashSet};
use bevy::prelude::*;

#[derive(Resource, Default)]
pub struct CameraFade {
    /// objects hit this frame
    hits: HashSet<Entity>,
    /// faded objects: alpha and, per mesh, (mesh entity, shared material, own copy)
    faded: HashMap<Entity, (f32, Vec<(Entity, Handle<GameMaterial>, Handle<GameMaterial>)>)>,
}

impl CameraFade {
    pub fn hit(&mut self, object: Entity) {
        self.hits.insert(object);
    }
}

pub struct CameraFadePlugin;

impl Plugin for CameraFadePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<CameraFade>().add_systems(Update, (fade, object_alpha));
    }
}

fn fade(
    time: Res<Time>,
    data: Res<crate::gamedata::GameData>,
    mut f: ResMut<CameraFade>,
    children: Query<&Children>,
    mut meshes: Query<&mut MeshMaterial3d<GameMaterial>>,
    mut materials: ResMut<Assets<GameMaterial>>,
) {
    let g = data.props("p_globalsettings");
    let (floor, rate_in, rate_out) = (g.f("camera_falloff", 0.3), g.f("camera_fade_in_rate", 3.0), g.f("camera_fade_out_rate", 3.0));
    let dt = time.delta_secs();
    let hits: Vec<Entity> = f.hits.drain().collect();
    // newly hit objects: own copies of their materials
    for e in &hits {
        if f.faded.contains_key(e) {
            continue;
        }
        let mut parts = Vec::new();
        for m in std::iter::once(*e).chain(children.iter_descendants(*e)) {
            if let Ok(mut mm) = meshes.get_mut(m) {
                let shared = mm.0.clone();
                if let Some(copy) = materials.get(&shared).cloned() {
                    let own = materials.add(copy);
                    mm.0 = own.clone();
                    parts.push((m, shared, own));
                }
            }
        }
        f.faded.insert(*e, (1.0, parts));
    }
    let mut done = Vec::new();
    for (e, (alpha, parts)) in f.faded.iter_mut() {
        *alpha = if hits.contains(e) { (*alpha - rate_out * dt).max(floor) } else { *alpha + rate_in * dt };
        if *alpha >= 1.0 {
            done.push(*e);
            continue;
        }
        for (_, _, own) in parts.iter() {
            if let Some(mut m) = materials.get_mut(own) {
                m.params.emissive.w = 1.0 - *alpha;
            }
        }
    }
    for e in done {
        if let Some((_, parts)) = f.faded.remove(&e) {
            for (m, shared, own) in parts {
                if let Ok(mut mm) = meshes.get_mut(m) {
                    mm.0 = shared;
                }
                materials.remove(&own);
            }
        }
    }
}

/// An object's own alpha (the objects' vt+0x100 setter: a ticker bomb site at unarmed_falloff until armed,
/// PlayerTickerBomb_Load 0x0044de90 / 0x0044e4a0; PickupObject_Update 0x00472200's fade). Its meshes draw with their
/// own copies of their materials, the alpha in emissive.w as the camera fade's (1 - alpha).
#[derive(Component, Clone, Copy, PartialEq, Debug)]
pub struct ObjectAlpha(pub f32);

fn object_alpha(
    q: Query<(Entity, &ObjectAlpha), Changed<ObjectAlpha>>,
    children: Query<&Children>,
    mut meshes: Query<&mut MeshMaterial3d<GameMaterial>>,
    mut materials: ResMut<Assets<GameMaterial>>,
    mut own: Local<HashSet<AssetId<GameMaterial>>>,
) {
    for (e, a) in &q {
        for m in std::iter::once(e).chain(children.iter_descendants(e)) {
            let Ok(mut mm) = meshes.get_mut(m) else { continue };
            if !own.contains(&mm.0.id()) {
                let Some(copy) = materials.get(&mm.0).cloned() else { continue };
                let h = materials.add(copy);
                own.insert(h.id());
                mm.0 = h;
            }
            if let Some(mut mat) = materials.get_mut(&mm.0) {
                mat.params.emissive.w = 1.0 - a.0.clamp(0.0, 1.0);
            }
        }
    }
}
