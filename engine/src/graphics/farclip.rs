//! The sky behind the original's far clip where a depth prepass runs (TAA, SSAO, soft particles: the Remaster).
//!
//! The game material clips each pixel at the original camera's far plane (gamematerial_shared.wesl `far_clipped`: a
//! hard cut, or the Remaster's dissolve), but Bevy's prepass draws whole objects: where an object reached the clip,
//! its prepass depth failed the sky's depth test and the clear (fog) colour showed instead of the sky. A prepass shader
//! of the game material's own would fix the depth but bind the material in every depth-only pass, the sun's shadow
//! cascades included, which costs them their batching (4K bench 113 -> 64 fps, docs/remaster-engine.md); a twin
//! material for just the meshes reaching the clip still cost ~3 ms of CPU a frame (2026-10-15). So the sky does it:
//! its parts (the level's background objects) get [`SkyMaterial`], Bevy's standard material with a fragment shader
//! (farclip.wesl) that, with a prepass, draws without a depth test and keeps only the pixels where nothing is in
//! front of it or where what is in front was clipped (the game material's test, on the prepass depth). Without a
//! prepass (the Original preset) it is the standard material, depth-tested as before.

use crate::gamematerial::GameLights;
use crate::level::Background;
use bevy::mesh::MeshVertexBufferLayoutRef;
use bevy::pbr::{ExtendedMaterial, MaterialExtension, MaterialExtensionKey, MaterialExtensionPipeline, MaterialPlugin, MeshPipelineKey};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, CompareFunction, RenderPipelineDescriptor, ShaderType, SpecializedMeshPipelineError};
use bevy::shader::{ShaderDefVal, ShaderRef};

/// The sky's material: Bevy's standard material (unlit) that fills the pixels the game material clips.
pub type SkyMaterial = ExtendedMaterial<StandardMaterial, SkyBehindClip>;

#[derive(ShaderType, Debug, Clone, Copy, Default, PartialEq)]
pub struct SkyClip {
    /// the game material's far clip (its fog_range.z: options.rs world_far at draw distance 1); 0 = none
    pub far: f32,
    /// 1 = the Remaster's dissolve before the clip (GraphicsSettings::far_fade)
    pub fade: f32,
}

#[derive(Asset, AsBindGroup, TypePath, Debug, Clone, Default)]
pub struct SkyBehindClip {
    #[uniform(100)]
    pub clip: SkyClip,
}

impl MaterialExtension for SkyBehindClip {
    fn fragment_shader() -> ShaderRef {
        ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("farclip.wesl")).with_source("embedded"))
    }

    /// With a depth prepass, the colour pass draws the sky without a depth test or depth writes (the shader decides).
    /// The prepass itself stays depth-tested: drawn there without the test, the sky wrote its normals and motion
    /// vectors over everything drawn before it (most of the solid scenery), and the ambient occlusion read them as
    /// surfaces facing the wrong way: hard-edged dark bands and polygons across walls and ground (2026-10-24).
    fn specialize(
        _pipeline: &MaterialExtensionPipeline,
        descriptor: &mut RenderPipelineDescriptor,
        _layout: &MeshVertexBufferLayoutRef,
        key: MaterialExtensionKey<Self>,
    ) -> Result<(), SpecializedMeshPipelineError> {
        let prepass_pipeline = is_prepass_pipeline(descriptor);
        if key.mesh_key.contains(MeshPipelineKey::DEPTH_PREPASS) && !prepass_pipeline {
            if let Some(ds) = descriptor.depth_stencil.as_mut() {
                ds.depth_compare = Some(CompareFunction::Always);
                ds.depth_write_enabled = Some(false);
            }
        }
        Ok(())
    }
}

/// Whether Bevy's prepass (depth, normals, motion vectors) builds this pipeline: it defines PREPASS_PIPELINE (by
/// name; the value of the boolean definition is just "true").
fn is_prepass_pipeline(descriptor: &RenderPipelineDescriptor) -> bool {
    descriptor.vertex.shader_defs.iter().any(|d| matches!(d, ShaderDefVal::Bool(name, true) if name == "PREPASS_PIPELINE"))
}

pub struct FarClipPlugin;

impl Plugin for FarClipPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "farclip.wesl");
        app.add_plugins(MaterialPlugin::<SkyMaterial>::default()).add_systems(PostUpdate, (swap_sky, follow_settings).chain());
    }
}

/// The clip the sky's shader repeats: the game material's (gamematerial.rs `make`).
fn sky_clip(lights: Option<&GameLights>, settings: &super::GraphicsSettings) -> SkyClip {
    let far = lights.filter(|l| l.fog.is_some()).map_or(0.0, |l| crate::options::world_far(l.clip_far, 1.0));
    SkyClip { far, fade: if settings.far_fade { 1.0 } else { 0.0 } }
}

/// The background objects' parts take the sky material (one per standard material).
#[allow(clippy::type_complexity)]
fn swap_sky(
    mut commands: Commands,
    parts: Query<(Entity, &MeshMaterial3d<StandardMaterial>, &ChildOf), Added<MeshMaterial3d<StandardMaterial>>>,
    backgrounds: Query<(), With<Background>>,
    standard: Res<Assets<StandardMaterial>>,
    mut sky: ResMut<Assets<SkyMaterial>>,
    (settings, lights): (Res<super::GraphicsSettings>, Option<Res<GameLights>>),
    mut made: Local<HashMap<AssetId<StandardMaterial>, Handle<SkyMaterial>>>,
) {
    let clip = sky_clip(lights.as_deref(), &settings);
    for (e, m, parent) in &parts {
        if !backgrounds.contains(parent.parent()) {
            continue;
        }
        let Some(base) = standard.get(&m.0) else { continue };
        let h = made.entry(m.0.id()).or_insert_with(|| sky.add(SkyMaterial { base: base.clone(), extension: SkyBehindClip { clip } })).clone();
        commands.entity(e).remove::<MeshMaterial3d<StandardMaterial>>().insert(MeshMaterial3d(h));
    }
}

/// The far fade setting (Graphics page) and the level's fog reach the sky's copy of the clip.
fn follow_settings(settings: Res<super::GraphicsSettings>, lights: Option<Res<GameLights>>, mut sky: ResMut<Assets<SkyMaterial>>) {
    if !settings.is_changed() && !lights.as_ref().is_some_and(|l| l.is_changed()) {
        return;
    }
    let clip = sky_clip(lights.as_deref(), &settings);
    let ids: Vec<_> = sky.ids().collect();
    for id in ids {
        if sky.get(id).is_some_and(|m| m.extension.clip != clip) {
            if let Some(mut m) = sky.get_mut(id) {
                m.extension.clip = clip;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The sky's clip is the game material's: the far plane at draw distance 1, the dissolve as the setting says.
    #[test]
    fn clip_follows_the_game_material() {
        let lights = GameLights { fog: Some((300.0, 1500.0, Vec3::ONE)), clip_far: 1712.132, ..Default::default() };
        let r = sky_clip(Some(&lights), &super::super::GraphicsSettings::remaster());
        assert!((r.far - 1712.132).abs() < 0.01);
        assert_eq!(r.fade, 1.0);
        assert_eq!(sky_clip(None, &super::super::GraphicsSettings::original()), SkyClip { far: 0.0, fade: 0.0 });
    }

    /// The prepass is told apart by its definition's name (a boolean definition's value is "true"): only the colour
    /// pass loses the depth test.
    #[test]
    fn prepass_pipeline_by_name() {
        let mut d = RenderPipelineDescriptor::default();
        d.vertex.shader_defs = vec!["MORPH_TARGETS".into()];
        assert!(!is_prepass_pipeline(&d));
        d.vertex.shader_defs.push("PREPASS_PIPELINE".into());
        assert!(is_prepass_pipeline(&d));
    }
}
