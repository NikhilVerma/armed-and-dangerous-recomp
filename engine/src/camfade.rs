//! Objects between the follow camera and Roman fade out (Camera_Update 0x0040e350, orbit mode 0x0040e56c): each
//! object a camera ray hits drops its own alpha (vt+0x100, [`ObjectAlpha`]) by camera_fade_out_rate per second down
//! to camera_falloff, and joins the camera's list (+0x158); each frame, before the rays, the listed objects not hit
//! the frame before (flag 0x800 of +0x424) rise by camera_fade_in_rate per second, and at 1 they are set to 1 and
//! leave the list (p_globalsettings: 3, 3, 0.3 on lev_1_m1; read at 0x00424456..0x00424494). The fade works on the
//! object's own alpha, so it starts from that alpha (an object already at or below the falloff is not lowered) and
//! leaves the object at 1 (an unarmed ticker bomb site, at unarmed_falloff, comes back at 1: docs/original-bugs.md).
//! Only the follow camera runs it: under a camera path or a gun's camera the list waits. An object with an alpha below 1
//! draws with its own copies of its materials (the alpha in the game material's emissive.w as 1 - alpha), at 1 the
//! shared ones come back. Both game material types fade (gamematerial.rs: solid and alpha-tested), each by its own
//! instance of the generic systems.

use crate::levelreset::LevelAppExt;
use crate::gamematerial::{AlphaTested, Coverage, GameMaterial, Solid};
use bevy::platform::collections::HashSet;
use bevy::prelude::*;

#[derive(Resource, Default)]
pub struct CameraFade {
    /// objects hit this frame
    hits: HashSet<Entity>,
    /// objects hit the frame before (flag 0x800): they do not fade in this frame
    hit_before: HashSet<Entity>,
    /// the camera's list +0x158: objects it faded that have not come back to 1
    list: HashSet<Entity>,
    /// the follow camera cast its rays this frame (the list is only walked in the orbit branch)
    ran: bool,
}

impl CameraFade {
    pub fn hit(&mut self, object: Entity) {
        self.hits.insert(object);
    }

    /// The follow camera's rays ran this frame.
    pub fn rays_cast(&mut self) {
        self.ran = true;
    }
}

pub struct CameraFadePlugin;

impl Plugin for CameraFadePlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<CameraFade>()
            .add_systems(
                PostUpdate,
                (fade, follow_alpha, (object_alpha::<Solid>, object_alpha::<AlphaTested>))
                    .chain()
                    .after(crate::camera::CameraPlace)
                    .before(bevy::transform::TransformSystems::Propagate),
            )
            .add_systems(PostUpdate, log_object_alpha);
    }
}

/// The camera's fade of this frame (0x0040e56c, game_01.cpp:2952-2970 and 3040-3100): the listed objects not hit the
/// frame before fade in (at 1 they leave the list), then this frame's hits fade out.
fn fade(mut commands: Commands, time: Res<Time>, data: Res<crate::gamedata::GameData>, mut f: ResMut<CameraFade>, mut alphas: Query<Option<&mut ObjectAlpha>>) {
    let f = &mut *f;
    if !std::mem::take(&mut f.ran) {
        f.hits.clear();
        return;
    }
    let g = data.props("p_globalsettings");
    let (floor, rate_in, rate_out) = (g.f("camera_falloff", 0.3), g.f("camera_fade_in_rate", 3.0), g.f("camera_fade_out_rate", 3.0));
    let dt = time.delta_secs();
    // fade in: the listed objects not hit the frame before
    let mut back = Vec::new();
    for &e in &f.list {
        if f.hit_before.contains(&e) {
            continue;
        }
        let Some(a) = alphas.get(e).ok().map(|a| a.map_or(1.0, |a| a.0)) else {
            back.push(e);
            continue;
        };
        let mut a = a + rate_in * dt;
        if a >= 1.0 {
            a = 1.0;
            back.push(e);
        }
        set_alpha(&mut commands, &mut alphas, e, a);
    }
    for e in back {
        f.list.remove(&e);
    }
    f.hit_before.clear();
    // fade out: this frame's hits, from their own alpha while above the falloff
    for &e in &f.hits {
        let Some(a) = alphas.get(e).ok().map(|a| a.map_or(1.0, |a| a.0)) else { continue };
        if a > floor {
            set_alpha(&mut commands, &mut alphas, e, (a - rate_out * dt).max(floor));
        }
        f.list.insert(e);
        f.hit_before.insert(e);
    }
    f.hits.clear();
}

/// Sets an object's own alpha (vt+0x100), adding it to an object that had none (alpha 1).
fn set_alpha(commands: &mut Commands, alphas: &mut Query<Option<&mut ObjectAlpha>>, e: Entity, a: f32) {
    match alphas.get_mut(e) {
        Ok(Some(mut cur)) => {
            if cur.0 != a {
                cur.0 = a;
            }
        }
        Ok(None) => {
            commands.entity(e).try_insert(ObjectAlpha(a));
        }
        Err(_) => {}
    }
}

/// An object's own alpha (the objects' vt+0x100 setter: a ticker bomb site at unarmed_falloff until armed,
/// PlayerTickerBomb_Load 0x0044de90 / 0x0044e4a0; PickupObject_Update 0x00472200's fade). Its meshes draw with their
/// own copies of their materials, the alpha in emissive.w as the camera fade's (1 - alpha).
#[derive(Component, Clone, Copy, PartialEq, Debug)]
pub struct ObjectAlpha(pub f32);

/// On an object attached to a character outside its hierarchy (Q's cape, cloth.rs): it takes that character's alpha.
/// Character_SetFade 0x0042eb60 sets the alpha of the character's attachments (+0x61c) with its own, so Q's cape
/// fades with him. (The camera never fades a character: camera.rs follow_camera, 0x00413a90.)
#[derive(Component, Clone, Copy, Debug)]
pub struct AlphaFollows(pub Entity);

/// The attachments' alpha from their characters' (0x0042eb60), after this frame's fade.
fn follow_alpha(mut commands: Commands, followers: Query<(Entity, &AlphaFollows)>, mut alphas: Query<Option<&mut ObjectAlpha>>) {
    for (e, f) in &followers {
        let Ok(owner) = alphas.get(f.0).map(|a| a.map_or(1.0, |a| a.0)) else { continue };
        let own = alphas.get(e).ok().flatten().map_or(1.0, |a| a.0);
        if own != owner {
            set_alpha(&mut commands, &mut alphas, e, owner);
        }
    }
}

/// The state log's "character_alpha": the characters fading (alpha below 1) and their meshes, the whole hierarchy (a
/// character's body, the weapon in his hand, a pack on his back: Character_SetFade 0x0042eb60 sets the character's,
/// its attachments' (+0x61c) and its held object's (+0xb08) alpha), and how many of those draw with the alpha.
fn log_object_alpha(q: Query<(Entity, &ObjectAlpha), With<crate::skin::Animator>>, children: Query<&Children>, meshes: Query<Has<OwnAlphaMaterial>, (With<Mesh3d>, Without<IsDepthCopy>)>, play: Option<ResMut<crate::playtest::PlayState>>) {
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

/// On a mesh drawing with its own copy of its material for its object's alpha (freed with the mesh, or when the
/// alpha is back at 1).
#[derive(Component)]
struct OwnAlphaMaterial;

/// On such a mesh: the shared material its own copy replaced.
#[derive(Component)]
struct AlphaFrom<C: Coverage>(Handle<GameMaterial<C>>);

/// On a fading mesh with the remaster's `solid_fade`: its depth copy, a child drawing the same mesh into the depth
/// buffer only, just before the mesh blends (gamematerial.rs `depth_only`). The engine's plain fade (the Original
/// preset) blends every front face of the object over the scene in index order, without depth writes (as the
/// original, guess: 0x004897d0 sets state bit 0x20 for alpha < 1, and Renderer_ApplyMaterial 0x00505320 sets
/// ZWRITEENABLE to !(flags bit 0x20)): a thick-walled open pipe beside the camera filled the screen with its bore
/// and rims at alpha 0.9 (lev_2_m2 walk, docs/journal/2026-10-25-cam.md). With the depth first only the nearest
/// surface blends.
#[derive(Component)]
struct DepthCopy(Entity);

/// On the depth copy itself: not a part of the object to fade or count.
#[derive(Component)]
struct IsDepthCopy;

/// The objects' alphas onto their meshes (the whole hierarchy: Character_SetFade 0x0042eb60): below 1 each mesh
/// draws with its own material copy at the alpha, at 1 the shared material comes back. Solid, non-alpha-textured
/// meshes get a depth copy while they fade when `solid_fade` is on ([`DepthCopy`]).
#[allow(clippy::type_complexity)]
fn object_alpha<C: Coverage>(
    mut commands: Commands,
    q: Query<(Entity, &ObjectAlpha), Changed<ObjectAlpha>>,
    children: Query<&Children>,
    mut meshes: Query<(&mut MeshMaterial3d<GameMaterial<C>>, Option<&AlphaFrom<C>>, Option<&DepthCopy>, &Mesh3d, Option<&bevy::mesh::skinning::SkinnedMesh>), Without<IsDepthCopy>>,
    mut materials: ResMut<Assets<GameMaterial<C>>>,
    settings: Option<Res<crate::graphics::GraphicsSettings>>,
) {
    let solid_fade = !C::ALPHA_TESTED && settings.is_some_and(|s| s.geometry.solid_fade);
    for (e, a) in &q {
        let opaque = a.0 >= 1.0;
        for m in std::iter::once(e).chain(children.iter_descendants(e)) {
            let Ok((mut mm, from, depth, mesh, skin)) = meshes.get_mut(m) else { continue };
            match (from, opaque) {
                (Some(AlphaFrom(shared)), true) => {
                    let own = std::mem::replace(&mut mm.0, shared.clone());
                    materials.remove(&own);
                    commands.entity(m).try_remove::<(AlphaFrom<C>, OwnAlphaMaterial, DepthCopy)>();
                    if let Some(&DepthCopy(d)) = depth {
                        commands.entity(d).try_despawn();
                    }
                    continue;
                }
                (None, true) => continue,
                (None, false) => {
                    let shared = mm.0.clone();
                    let Some(copy) = materials.get(&shared).cloned() else { continue };
                    if solid_fade && !copy.alpha && !copy.additive {
                        let depth_material = materials.add(copy.depth_copy());
                        let mut d = commands.spawn((
                            Mesh3d(mesh.0.clone()),
                            MeshMaterial3d(depth_material),
                            Transform::IDENTITY,
                            Visibility::Inherited,
                            bevy::light::NotShadowCaster,
                            IsDepthCopy,
                            ChildOf(m),
                        ));
                        if let Some(s) = skin {
                            d.insert(s.clone());
                        }
                        let d = d.id();
                        commands.entity(m).try_insert(DepthCopy(d));
                    }
                    mm.0 = materials.add(copy);
                    commands.entity(m).try_insert((OwnAlphaMaterial, AlphaFrom(shared)));
                }
                (Some(_), false) => {}
            }
            if let Some(mut mat) = materials.get_mut(&mm.0) {
                mat.params.emissive.w = 1.0 - a.0.clamp(0.0, 1.0);
            }
        }
    }
}
