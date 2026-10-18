//! Objects between the follow camera and Roman fade out (Camera_Update 0x0040e350, orbit mode 0x0040e56c): each
//! object a camera ray hits drops its alpha by camera_fade_out_rate per second down to camera_falloff, and once no
//! ray hits it, rises by camera_fade_in_rate per second back to 1, where it leaves the list (p_globalsettings: 3, 3,
//! 0.3 on lev_1_m1; read at 0x00424456..0x00424494). A faded object draws with its own copies of its materials
//! (the fade in the game material's emissive.w); at 1 the shared ones come back. Both game material types fade
//! (gamematerial.rs: solid and alpha-tested), each by its own instance of the generic systems.

use crate::levelreset::LevelAppExt;
use crate::gamematerial::{AlphaTested, Coverage, GameMaterial, Solid};
use bevy::platform::collections::{HashMap, HashSet};
use bevy::prelude::*;

#[derive(Resource, Default)]
pub struct CameraFade {
    /// objects hit this frame
    hits: HashSet<Entity>,
    /// faded objects and their alpha (below 1)
    faded: HashMap<Entity, f32>,
    /// objects whose fade ended this frame: their meshes get the shared materials back
    ended: Vec<Entity>,
}

impl CameraFade {
    pub fn hit(&mut self, object: Entity) {
        self.hits.insert(object);
    }
}

/// On a mesh of a faded object: the shared material its own copy replaced (restored when the fade ends).
#[derive(Component)]
struct FadedFrom<C: Coverage>(Handle<GameMaterial<C>>);

pub struct CameraFadePlugin;

impl Plugin for CameraFadePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<CameraFade>().add_systems(
            Update,
            (
                (fade, (fade_materials::<Solid>, fade_materials::<AlphaTested>)).chain(),
                object_alpha::<Solid>,
                object_alpha::<AlphaTested>,
            ),
        )
        .add_systems(PostUpdate, log_object_alpha);
    }
}

/// The objects' fade alphas from this frame's hits.
fn fade(time: Res<Time>, data: Res<crate::gamedata::GameData>, mut f: ResMut<CameraFade>) {
    let g = data.props("p_globalsettings");
    let (floor, rate_in, rate_out) = (g.f("camera_falloff", 0.3), g.f("camera_fade_in_rate", 3.0), g.f("camera_fade_out_rate", 3.0));
    let dt = time.delta_secs();
    let f = &mut *f;
    f.ended.clear();
    for e in &f.hits {
        f.faded.entry(*e).or_insert(1.0);
    }
    for (e, alpha) in f.faded.iter_mut() {
        *alpha = if f.hits.contains(e) { (*alpha - rate_out * dt).max(floor) } else { *alpha + rate_in * dt };
        if *alpha >= 1.0 {
            f.ended.push(*e);
        }
    }
    for e in &f.ended {
        f.faded.remove(e);
    }
    f.hits.clear();
}

/// The faded objects' meshes of one material type: their own material copies at the fade's alpha, and the shared
/// materials back once the fade ended.
fn fade_materials<C: Coverage>(
    mut commands: Commands,
    f: Res<CameraFade>,
    children: Query<&Children>,
    mut meshes: Query<(&mut MeshMaterial3d<GameMaterial<C>>, Option<&FadedFrom<C>>)>,
    mut materials: ResMut<Assets<GameMaterial<C>>>,
) {
    for (e, alpha) in &f.faded {
        for m in std::iter::once(*e).chain(children.iter_descendants(*e)) {
            let Ok((mut mm, from)) = meshes.get_mut(m) else { continue };
            if from.is_none() {
                let shared = mm.0.clone();
                let Some(copy) = materials.get(&shared).cloned() else { continue };
                mm.0 = materials.add(copy);
                commands.entity(m).insert(FadedFrom(shared));
            }
            if let Some(mut mat) = materials.get_mut(&mm.0) {
                mat.params.emissive.w = 1.0 - *alpha;
            }
        }
    }
    for e in &f.ended {
        for m in std::iter::once(*e).chain(children.iter_descendants(*e)) {
            let Ok((mut mm, Some(FadedFrom(shared)))) = meshes.get_mut(m) else { continue };
            let own = std::mem::replace(&mut mm.0, shared.clone());
            materials.remove(&own);
            commands.entity(m).remove::<FadedFrom<C>>();
        }
    }
}

/// An object's own alpha (the objects' vt+0x100 setter: a ticker bomb site at unarmed_falloff until armed,
/// PlayerTickerBomb_Load 0x0044de90 / 0x0044e4a0; PickupObject_Update 0x00472200's fade). Its meshes draw with their
/// own copies of their materials, the alpha in emissive.w as the camera fade's (1 - alpha).
#[derive(Component, Clone, Copy, PartialEq, Debug)]
pub struct ObjectAlpha(pub f32);

/// The state log's "character_alpha": the characters fading (alpha below 1) and their meshes, the whole hierarchy (a
/// character's body, the weapon in his hand, a pack on his back: Character_SetFade 0x0042eb60 sets the character's,
/// its attachments' (+0x61c) and its held object's (+0xb08) alpha), and how many of those draw with the alpha.
fn log_object_alpha(q: Query<(Entity, &ObjectAlpha), With<crate::skin::Animator>>, children: Query<&Children>, meshes: Query<Has<OwnAlphaMaterial>, With<Mesh3d>>, play: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut p) = play else { return };
    let (mut objects, mut total, mut faded) = (0, 0, 0);
    for (e, a) in &q {
        if a.0 >= 1.0 {
            continue;
        }
        objects += 1;
        for m in std::iter::once(e).chain(children.iter_descendants(e)) {
            if let Ok(own) = meshes.get(m) {
                total += 1;
                faded += own as u32;
            }
        }
    }
    p.0.insert("character_alpha".into(), serde_json::json!({"objects": objects, "meshes": total, "faded": faded, "unfaded": total - faded}));
}

/// On a mesh drawing with its own copy of its material for its object's alpha (freed with the mesh).
#[derive(Component)]
struct OwnAlphaMaterial;

fn object_alpha<C: Coverage>(
    mut commands: Commands,
    q: Query<(Entity, &ObjectAlpha), Changed<ObjectAlpha>>,
    children: Query<&Children>,
    mut meshes: Query<(&mut MeshMaterial3d<GameMaterial<C>>, Has<OwnAlphaMaterial>)>,
    mut materials: ResMut<Assets<GameMaterial<C>>>,
) {
    for (e, a) in &q {
        for m in std::iter::once(e).chain(children.iter_descendants(e)) {
            let Ok((mut mm, own)) = meshes.get_mut(m) else { continue };
            if !own {
                let Some(copy) = materials.get(&mm.0).cloned() else { continue };
                mm.0 = materials.add(copy);
                commands.entity(m).try_insert(OwnAlphaMaterial);
            }
            if let Some(mut mat) = materials.get_mut(&mm.0) {
                mat.params.emissive.w = 1.0 - a.0.clamp(0.0, 1.0);
            }
        }
    }
}
