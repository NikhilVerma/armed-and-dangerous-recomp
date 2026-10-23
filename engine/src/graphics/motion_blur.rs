//! Per-object motion blur by shutter speed: the old remaster's ([render] shutter_speed, registered in
//! archive/hook/enhance/weather.cpp; the passes in archive/src/rhi/vulkan/vk_motionblur.cpp, the uniforms in vk_device.cpp
//! `MotionBlur`). The player (docs/player-feedback.md, "Standing preferences"): per object, never from camera pans;
//! the setting is a shutter speed (an exposure of 1/N s), not a shutter angle. Bevy's own `MotionBlur` blurs camera
//! motion too and takes a shutter angle, hence this port.
//!
//! Each pixel's motion (Bevy's motion vector prepass: object and camera together) minus the camera's share (the
//! pixel's prepass depth taken to last frame's camera), the shorter of that world motion and the screen motion, so a
//! character the camera follows or turns around stays sharp (items 5 / the 2026-10-12 note in the old shader); then
//! McGuire et al. 2012: per-pixel velocity scaled by the shutter, the longest per 48-pixel tile, its 3x3
//! neighbourhood's longest, and a depth-aware gather along it (motion_blur_tiles.wesl, motion_blur_reconstruct.wesl).
//! Runs after TAA, before bloom. Particles and blended surfaces write no motion vectors, so they do not blur (the old
//! remaster streaked only its own GPU snow, by this shutter: the snowfall's port).

use bevy::core_pipeline::prepass::ViewPrepassTextures;
use bevy::core_pipeline::{Core3d, Core3dSystems, FullscreenShader};
use bevy::image::ToExtents;
use bevy::prelude::*;
use bevy::render::camera::ExtractedCamera;
use bevy::render::diagnostic::RecordDiagnostics;
use bevy::render::extract_component::{ComponentUniforms, DynamicUniformIndex, ExtractComponent, ExtractComponentPlugin, UniformComponentPlugin};
use bevy::render::render_resource::binding_types::{texture_2d, texture_depth_2d, texture_storage_2d, uniform_buffer};
use bevy::render::render_resource::*;
use bevy::render::renderer::{RenderContext, RenderDevice, ViewQuery};
use bevy::render::sync_component::SyncComponent;
use bevy::render::texture::{CachedTexture, TextureCache};
use bevy::render::view::{ExtractedView, ViewTarget};
use bevy::render::{Render, RenderApp, RenderStartup, RenderSystems};
use serde::{Deserialize, Serialize};

/// The motion blur's setting (options.json key as the old and.ini [render] key).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct MotionBlurSettings {
    /// the exposure as a shutter speed, 1/N s (0 = off; 120 = film at 60 fps; old slider 0..1000)
    pub shutter_speed: f32,
}

impl MotionBlurSettings {
    pub fn original() -> Self {
        MotionBlurSettings { shutter_speed: 0.0 }
    }

    /// The old remaster's default: 1/120 s.
    pub fn remaster() -> Self {
        MotionBlurSettings { shutter_speed: 120.0 }
    }

    pub fn enabled(&self) -> bool {
        self.shutter_speed > 0.0
    }

    /// The share of a frame's motion the shutter is open for, (1/N s) / frame time, at most 4 (vk_device.cpp).
    pub fn exposure(&self, frame_seconds: f32) -> f32 {
        if !self.enabled() || frame_seconds <= 0.0 {
            return 0.0;
        }
        ((1.0 / self.shutter_speed) / frame_seconds.max(1.0 / 1000.0)).min(4.0)
    }
}

impl Default for MotionBlurSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The longest blur in pixels and the tile size (vk_motionblur.h kMotionBlurTile; motion_blur_shared.wesl TILE).
pub const MAX_RADIUS: u32 = 48;

/// The passes' uniform (motion_blur_shared.wesl `MotionBlurParams`).
#[derive(Component, ShaderType, Clone, Copy, Debug, Default)]
pub struct MotionBlurUniform {
    pub reproject: Mat4,
    pub view_from_clip: Mat4,
    pub exposure: f32,
    pub max_radius: f32,
}

/// The blur on a 3D camera: last frame's camera and this frame's uniform (extracted while it blurs).
#[derive(Component, Clone, Debug, Default)]
pub struct ObjectMotionBlur {
    pub uniform: MotionBlurUniform,
    previous_clip_from_world: Option<Mat4>,
}

impl SyncComponent<RenderApp> for ObjectMotionBlur {
    type Target = MotionBlurUniform;
}

impl ExtractComponent<RenderApp> for ObjectMotionBlur {
    type QueryData = &'static Self;
    type QueryFilter = With<Camera>;
    type Out = MotionBlurUniform;

    fn extract_component(item: &ObjectMotionBlur) -> Option<MotionBlurUniform> {
        (item.uniform.exposure > 0.0).then_some(item.uniform)
    }
}

pub struct MotionBlurPlugin;

impl Plugin for MotionBlurPlugin {
    fn build(&self, app: &mut App) {
        bevy::shader::load_shader_library!(app, "motion_blur_shared.wesl");
        bevy::asset::embedded_asset!(app, "motion_blur_tiles.wesl");
        bevy::asset::embedded_asset!(app, "motion_blur_reconstruct.wesl");
        app.add_plugins((ExtractComponentPlugin::<ObjectMotionBlur>::default(), UniformComponentPlugin::<MotionBlurUniform>::default()))
            .add_systems(PostUpdate, follow_camera.after(bevy::camera::CameraUpdateSystems).after(bevy::transform::TransformSystems::Propagate));
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return };
        render_app
            .init_resource::<SpecializedRenderPipelines<MotionBlurPipelines>>()
            .add_systems(RenderStartup, init_pipelines)
            .add_systems(Render, (prepare_textures.in_set(RenderSystems::PrepareResources), prepare_pipelines.in_set(RenderSystems::Prepare)))
            .add_systems(Core3d, motion_blur.in_set(Core3dSystems::PostProcess).before(bevy::post_process::bloom::bloom));
    }
}

/// The blur on every 3D camera while the setting wants it, with this frame's camera motion. The prepasses it reads
/// come with the settings (graphics/mod.rs `apply_cameras`).
fn follow_camera(
    mut commands: Commands,
    s: Res<super::GraphicsSettings>,
    time: Res<Time>,
    mut cams: Query<(Entity, &Camera, &GlobalTransform, Option<&mut ObjectMotionBlur>), With<Camera3d>>,
) {
    for (e, cam, t, blur) in &mut cams {
        if !s.motion_blur.enabled() || s.anti_aliasing == super::AntiAliasing::Msaa {
            if blur.is_some() {
                commands.entity(e).remove::<ObjectMotionBlur>();
            }
            continue;
        }
        let clip_from_view = cam.clip_from_view();
        let clip_from_world = clip_from_view * Mat4::from(t.affine().inverse());
        let mut next = blur.as_deref().cloned().unwrap_or_default();
        next.uniform = MotionBlurUniform {
            reproject: next.previous_clip_from_world.unwrap_or(clip_from_world) * clip_from_world.inverse(),
            view_from_clip: clip_from_view.inverse(),
            exposure: if next.previous_clip_from_world.is_some() { s.motion_blur.exposure(time.delta_secs()) } else { 0.0 },
            max_radius: MAX_RADIUS as f32,
        };
        next.previous_clip_from_world = Some(clip_from_world);
        match blur {
            Some(mut b) => *b = next,
            None => {
                commands.entity(e).insert(next);
            }
        }
    }
}

#[derive(Resource)]
struct MotionBlurPipelines {
    tiles_layout: BindGroupLayoutDescriptor,
    reconstruct_layout: BindGroupLayoutDescriptor,
    velocity: CachedComputePipelineId,
    tile_max: CachedComputePipelineId,
    neighbour_max: CachedComputePipelineId,
    fullscreen: FullscreenShader,
    reconstruct_shader: Handle<Shader>,
}

fn init_pipelines(mut commands: Commands, assets: Res<AssetServer>, pipeline_cache: Res<PipelineCache>, fullscreen: Res<FullscreenShader>) {
    let unfiltered = || texture_2d(TextureSampleType::Float { filterable: false });
    let tiles_layout = BindGroupLayoutDescriptor::new(
        "motion_blur_tiles_layout",
        &BindGroupLayoutEntries::sequential(
            ShaderStages::COMPUTE,
            (
                uniform_buffer::<MotionBlurUniform>(true),
                texture_depth_2d(),
                unfiltered(),
                unfiltered(),
                texture_storage_2d(TextureFormat::Rgba16Float, StorageTextureAccess::WriteOnly),
            ),
        ),
    );
    let reconstruct_layout = BindGroupLayoutDescriptor::new(
        "motion_blur_reconstruct_layout",
        &BindGroupLayoutEntries::sequential(
            ShaderStages::FRAGMENT,
            (uniform_buffer::<MotionBlurUniform>(true), texture_depth_2d(), unfiltered(), unfiltered(), unfiltered()),
        ),
    );
    let tiles_shader: Handle<Shader> = assets.load(embedded(bevy::asset::embedded_path!("motion_blur_tiles.wesl")));
    let compute = |entry: &'static str| {
        pipeline_cache.queue_compute_pipeline(ComputePipelineDescriptor {
            label: Some(format!("motion_blur_{entry}").into()),
            layout: vec![tiles_layout.clone()],
            shader: tiles_shader.clone(),
            entry_point: Some(entry.into()),
            ..default()
        })
    };
    commands.insert_resource(MotionBlurPipelines {
        velocity: compute("velocity"),
        tile_max: compute("tile_max"),
        neighbour_max: compute("neighbour_max"),
        tiles_layout,
        reconstruct_layout,
        fullscreen: fullscreen.clone(),
        reconstruct_shader: assets.load(embedded(bevy::asset::embedded_path!("motion_blur_reconstruct.wesl"))),
    });
}

fn embedded(path: std::path::PathBuf) -> bevy::asset::AssetPath<'static> {
    bevy::asset::AssetPath::from_path_buf(path).with_source("embedded")
}

impl SpecializedRenderPipeline for MotionBlurPipelines {
    /// the view target's format
    type Key = TextureFormat;

    fn specialize(&self, format: TextureFormat) -> RenderPipelineDescriptor {
        RenderPipelineDescriptor {
            label: Some("motion_blur_reconstruct".into()),
            layout: vec![self.reconstruct_layout.clone()],
            vertex: self.fullscreen.to_vertex_state(),
            fragment: Some(FragmentState {
                shader: self.reconstruct_shader.clone(),
                targets: vec![Some(ColorTargetState { format, blend: None, write_mask: ColorWrites::ALL })],
                ..default()
            }),
            ..default()
        }
    }
}

#[derive(Component)]
struct ReconstructPipeline(CachedRenderPipelineId);

fn prepare_pipelines(
    mut commands: Commands,
    pipeline_cache: Res<PipelineCache>,
    mut specialized: ResMut<SpecializedRenderPipelines<MotionBlurPipelines>>,
    pipelines: Res<MotionBlurPipelines>,
    views: Query<(Entity, &ExtractedView), With<MotionBlurUniform>>,
) {
    for (e, view) in &views {
        let id = specialized.specialize(&pipeline_cache, &pipelines, view.target_format);
        commands.entity(e).insert(ReconstructPipeline(id));
    }
}

/// Per view: object velocity (full size), the tiles' longest and their neighbourhoods' longest.
#[derive(Component)]
struct MotionBlurTextures {
    velocity: CachedTexture,
    tiles: CachedTexture,
    neighbours: CachedTexture,
}

fn prepare_textures(
    mut commands: Commands,
    mut cache: ResMut<TextureCache>,
    device: Res<RenderDevice>,
    views: Query<(Entity, &ExtractedCamera), With<MotionBlurUniform>>,
) {
    for (e, cam) in &views {
        let Some(size) = cam.physical_viewport_size else { continue };
        let mut texture = |label: &'static str, size: UVec2| {
            cache.get(
                &device,
                TextureDescriptor {
                    label: Some(label),
                    size: size.to_extents(),
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: TextureDimension::D2,
                    format: TextureFormat::Rgba16Float,
                    usage: TextureUsages::STORAGE_BINDING | TextureUsages::TEXTURE_BINDING,
                    view_formats: &[],
                },
            )
        };
        let tiles = UVec2::new(size.x.div_ceil(MAX_RADIUS), size.y.div_ceil(MAX_RADIUS));
        let textures = MotionBlurTextures {
            velocity: texture("motion_blur_velocity", size),
            tiles: texture("motion_blur_tiles", tiles),
            neighbours: texture("motion_blur_neighbours", tiles),
        };
        commands.entity(e).insert(textures);
    }
}

#[allow(clippy::type_complexity)]
fn motion_blur(
    view: ViewQuery<(&ViewTarget, &ViewPrepassTextures, &Msaa, &DynamicUniformIndex<MotionBlurUniform>, &MotionBlurTextures, &ReconstructPipeline)>,
    pipelines: Res<MotionBlurPipelines>,
    pipeline_cache: Res<PipelineCache>,
    uniforms: Res<ComponentUniforms<MotionBlurUniform>>,
    mut ctx: RenderContext,
) {
    let (target, prepass, msaa, index, textures, reconstruct) = view.into_inner();
    // the prepass textures are multisampled under MSAA: the setting needs another anti-aliasing (follow_camera)
    if msaa.samples() != 1 {
        return;
    }
    let (Some(depth), Some(motion), Some(uniform)) = (prepass.depth_only_view(), prepass.motion_vectors_view(), uniforms.uniforms().binding()) else {
        return;
    };
    let (Some(velocity), Some(tile_max), Some(neighbour_max), Some(reconstruct)) = (
        pipeline_cache.get_compute_pipeline(pipelines.velocity),
        pipeline_cache.get_compute_pipeline(pipelines.tile_max),
        pipeline_cache.get_compute_pipeline(pipelines.neighbour_max),
        pipeline_cache.get_render_pipeline(reconstruct.0),
    ) else {
        return;
    };
    let device = ctx.render_device().clone();
    let tiles_layout = pipeline_cache.get_bind_group_layout(&pipelines.tiles_layout);
    let pass_group = |label: &'static str, src: &CachedTexture, dst: &CachedTexture| {
        device.create_bind_group(label, &tiles_layout, &BindGroupEntries::sequential((uniform.clone(), depth, motion, &src.default_view, &dst.default_view)))
    };
    // the velocity pass reads only the prepass: `src` is any texture other than its output
    let passes = [
        (velocity, pass_group("motion_blur_velocity", &textures.neighbours, &textures.velocity), textures.velocity.texture.size()),
        (tile_max, pass_group("motion_blur_tile_max", &textures.velocity, &textures.tiles), textures.tiles.texture.size()),
        (neighbour_max, pass_group("motion_blur_neighbour_max", &textures.tiles, &textures.neighbours), textures.neighbours.texture.size()),
    ];
    let post = target.post_process_write();
    let reconstruct_group = device.create_bind_group(
        "motion_blur_reconstruct",
        &pipeline_cache.get_bind_group_layout(&pipelines.reconstruct_layout),
        &BindGroupEntries::sequential((uniform.clone(), depth, post.source, &textures.velocity.default_view, &textures.neighbours.default_view)),
    );

    let diagnostics = ctx.diagnostic_recorder();
    let diagnostics = diagnostics.as_deref();
    let span = diagnostics.time_span(ctx.command_encoder(), "motion_blur");
    {
        let encoder = ctx.command_encoder();
        for (pipeline, group, size) in &passes {
            let mut pass = encoder.begin_compute_pass(&ComputePassDescriptor { label: Some("motion_blur_tiles"), timestamp_writes: None });
            pass.set_pipeline(pipeline);
            pass.set_bind_group(0, group, &[index.index()]);
            pass.dispatch_workgroups(size.width.div_ceil(8), size.height.div_ceil(8), 1);
        }
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some("motion_blur_reconstruct"),
            color_attachments: &[Some(RenderPassColorAttachment {
                view: post.destination,
                depth_slice: None,
                resolve_target: None,
                ops: Operations::default(),
            })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
            multiview_mask: None,
        });
        pass.set_pipeline(reconstruct);
        pass.set_bind_group(0, &reconstruct_group, &[index.index()]);
        pass.draw(0..3, 0..1);
    }
    span.end(ctx.command_encoder());
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 1/120 s at 60 fps is half a frame's motion; capped at 4 frames; off at 0 or paused.
    #[test]
    fn exposure() {
        let s = MotionBlurSettings::remaster();
        assert!((s.exposure(1.0 / 60.0) - 0.5).abs() < 1e-6);
        assert_eq!(MotionBlurSettings { shutter_speed: 1.0 }.exposure(1.0 / 60.0), 4.0);
        assert_eq!(MotionBlurSettings::original().exposure(1.0 / 60.0), 0.0);
        assert_eq!(s.exposure(0.0), 0.0);
    }
}
