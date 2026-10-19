//! The output pass: the old remaster's present pass ([render] tonemap_aces and dither, registered in
//! hook/enhance/render.cpp; the shader in src/rhi/vulkan/vk_present.cpp), last in the 3D camera's post-processing
//! (after TAA, bloom and sharpening): the filmic ACES curve (Narkowicz's fit, on the game's values as the old
//! pass ran it) and triangular dither noise of +-1 step of the 8-bit output against banding (present.wesl).
//!
//! Bevy's own tonemapping stays off (main.rs: its curves changed the game's colours) and its deband dither is a
//! different noise: this pass is the old one, run with Bevy's [`FullscreenMaterial`]. Either setting puts the 3D view
//! in a floating-point target (graphics/mod.rs `apply_cameras`: `Hdr`), so the dither has something to keep.

use bevy::core_pipeline::fullscreen_material::{FullscreenMaterial, FullscreenMaterialPlugin};
use bevy::core_pipeline::tonemapping::tonemapping;
use bevy::core_pipeline::Core3dSystems;
use bevy::ecs::schedule::ScheduleConfigs;
use bevy::ecs::system::BoxedSystem;
use bevy::prelude::*;
use bevy::render::extract_component::ExtractComponent;
use bevy::render::render_resource::ShaderType;
use bevy::render::RenderApp;
use bevy::shader::ShaderRef;
use serde::{Deserialize, Serialize};

/// The output pass's settings (options.json keys as the old and.ini [render] keys).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct PresentSettings {
    /// the filmic ACES curve (Narkowicz 2015) on the final colour; off in both of the old remaster's presets
    pub tonemap_aces: bool,
    /// triangular noise of +-1 step before the 8-bit output: no banding in sky, fog and snow gradients
    pub dither: bool,
}

impl PresentSettings {
    /// The colours as they are.
    pub fn original() -> Self {
        PresentSettings { tonemap_aces: false, dither: false }
    }

    /// The old remaster's defaults: dithered, no tone curve.
    pub fn remaster() -> Self {
        PresentSettings { dither: true, ..Self::original() }
    }

    pub fn enabled(&self) -> bool {
        self.tonemap_aces || self.dither
    }

    /// The pass needs the scene in a floating-point target.
    pub fn needs_hdr(&self) -> bool {
        self.enabled()
    }
}

impl Default for PresentSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The pass on a 3D camera (its uniform: present.wesl `PresentPass`).
#[derive(Component, ExtractComponent, ShaderType, Clone, Copy, Default, Debug, PartialEq)]
#[extract_app(RenderApp)]
pub struct PresentPass {
    pub aces: u32,
    pub dither: u32,
    /// varies the noise per frame
    pub frame: u32,
    _pad: u32,
}

impl FullscreenMaterial for PresentPass {
    fn fragment_shader() -> ShaderRef {
        ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("present.wesl")).with_source("embedded"))
    }

    /// Last: after the tone mapping (off) and the sharpening, which would sharpen the noise.
    fn schedule_configs(system: ScheduleConfigs<BoxedSystem>) -> ScheduleConfigs<BoxedSystem> {
        system
            .in_set(Core3dSystems::PostProcess)
            .after(tonemapping)
            .after(bevy::anti_alias::contrast_adaptive_sharpening::cas)
            .after(bevy::anti_alias::smaa::smaa)
    }
}

pub struct PresentPlugin;

impl Plugin for PresentPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "present.wesl");
        app.add_plugins(FullscreenMaterialPlugin::<PresentPass>::default()).add_systems(PostUpdate, follow_settings);
    }
}

/// The pass on every 3D camera while a setting wants it; the frame counter moves the noise.
fn follow_settings(
    mut commands: Commands,
    s: Res<super::GraphicsSettings>,
    mut cams: Query<(Entity, Option<&mut PresentPass>), With<Camera3d>>,
    mut frame: Local<u32>,
) {
    *frame = (*frame + 1) % 1024;
    for (e, pass) in &mut cams {
        if !s.present.enabled() {
            if pass.is_some() {
                commands.entity(e).remove::<PresentPass>();
            }
            continue;
        }
        let want = PresentPass { aces: s.present.tonemap_aces as u32, dither: s.present.dither as u32, frame: *frame, _pad: 0 };
        match pass {
            Some(mut p) => *p = want,
            None => {
                commands.entity(e).insert(want);
            }
        }
    }
}
