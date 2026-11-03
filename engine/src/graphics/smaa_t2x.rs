//! SMAA T2x is SMAA with a light temporal step, for a picture that stays sharp in motion. Bevy has plain SMAA only.
//!
//! TAA blends many past frames, and that softened the textures: in a moving shot, a sharp test pattern kept only half
//! the contrast it had with no anti-aliasing. T2x (Jimenez et al. 2012) blends just one past frame, which is enough to
//! calm most shimmer.
//!
//! - Each frame, the camera shifts by a quarter pixel, in turn one way and then the other.
//! - Bevy's SMAA smooths the edges of that frame.
//! - This pass blends the result with the previous frame's SMAA result, found through the motion vectors.
//!
//! The history holds single frames, not a running blend, so old frames can't build up into blur. The blend also fades
//! out as a pixel moves, because resampling a moving history softens it. Fast motion is therefore plain SMAA.

use bevy::anti_alias::contrast_adaptive_sharpening::cas;
use bevy::anti_alias::smaa::smaa;
use bevy::core_pipeline::prepass::ViewPrepassTextures;
use bevy::core_pipeline::{Core3d, Core3dSystems, FullscreenShader};
use bevy::diagnostic::FrameCount;
use bevy::image::ToExtents;
use bevy::prelude::*;
use bevy::render::camera::{ExtractedCamera, TemporalJitter};
use bevy::render::extract_component::{ExtractComponent, ExtractComponentPlugin};
use bevy::render::render_resource::binding_types::{sampler, texture_2d};
use bevy::render::render_resource::*;
use bevy::render::renderer::{RenderContext, RenderDevice, ViewQuery};
use bevy::render::sync_component::SyncComponent;
use bevy::render::texture::{CachedTexture, TextureCache};
use bevy::render::view::{ExtractedView, ViewTarget};
use bevy::render::{Render, RenderApp, RenderStartup, RenderSystems};

/// The camera's shift each frame, in pixels: the two sample positions of the T2x paper.
const JITTER: [Vec2; 2] = [Vec2::new(-0.25, 0.25), Vec2::new(0.25, -0.25)];

/// Turns SMAA T2x on for a camera. graphics/mod.rs adds it with Bevy's SMAA, the jitter and the prepasses.
#[derive(Component, Clone, Copy, Debug, Default)]
pub struct SmaaT2x;

impl SyncComponent<RenderApp> for SmaaT2x {
    type Target = SmaaT2x;
}

impl ExtractComponent<RenderApp> for SmaaT2x {
    type QueryData = &'static Self;
    type QueryFilter = With<Camera>;
    type Out = SmaaT2x;

    fn extract_component(item: &SmaaT2x) -> Option<SmaaT2x> {
        Some(*item)
    }
}

pub struct SmaaT2xPlugin;

impl Plugin for SmaaT2xPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "smaa_t2x.wesl");
        app.add_plugins(ExtractComponentPlugin::<SmaaT2x>::default());
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return };
        render_app
            .init_resource::<SpecializedRenderPipelines<ResolvePipeline>>()
            .add_systems(RenderStartup, init_pipeline)
            .add_systems(
                Render,
                (
                    set_jitter.in_set(RenderSystems::PrepareViews),
                    prepare_pipelines.in_set(RenderSystems::Prepare),
                    prepare_history.in_set(RenderSystems::PrepareResources),
                ),
            )
            // after SMAA, which it blends, and before the sharpening, whose result must stay out of the history
            .add_systems(Core3d, resolve.after(smaa).before(cas).in_set(Core3dSystems::PostProcess));
    }
}

fn set_jitter(frame: Res<FrameCount>, mut views: Query<&mut TemporalJitter, With<SmaaT2x>>) {
    for mut jitter in &mut views {
        jitter.offset = JITTER[frame.0 as usize % JITTER.len()];
    }
}

#[derive(Resource)]
struct ResolvePipeline {
    layout: BindGroupLayoutDescriptor,
    sampler: Sampler,
    fullscreen: FullscreenShader,
    shader: Handle<Shader>,
}

fn init_pipeline(mut commands: Commands, assets: Res<AssetServer>, device: Res<RenderDevice>, fullscreen: Res<FullscreenShader>) {
    let colour = || texture_2d(TextureSampleType::Float { filterable: true });
    let layout = BindGroupLayoutDescriptor::new(
        "smaa_t2x_layout",
        &BindGroupLayoutEntries::sequential(
            ShaderStages::FRAGMENT,
            (colour(), colour(), texture_2d(TextureSampleType::Float { filterable: false }), sampler(SamplerBindingType::Filtering)),
        ),
    );
    let sampler = device.create_sampler(&SamplerDescriptor {
        label: Some("smaa_t2x_sampler"),
        mag_filter: FilterMode::Linear,
        min_filter: FilterMode::Linear,
        ..default()
    });
    let path = bevy::asset::embedded_path!("smaa_t2x.wesl");
    commands.insert_resource(ResolvePipeline {
        layout,
        sampler,
        fullscreen: fullscreen.clone(),
        shader: assets.load(bevy::asset::AssetPath::from_path_buf(path).with_source("embedded")),
    });
}

impl SpecializedRenderPipeline for ResolvePipeline {
    /// the view target's format
    type Key = TextureFormat;

    fn specialize(&self, format: TextureFormat) -> RenderPipelineDescriptor {
        let target = Some(ColorTargetState { format, blend: None, write_mask: ColorWrites::ALL });
        RenderPipelineDescriptor {
            label: Some("smaa_t2x_resolve".into()),
            layout: vec![self.layout.clone()],
            vertex: self.fullscreen.to_vertex_state(),
            fragment: Some(FragmentState {
                shader: self.shader.clone(),
                // the blended picture, and this frame alone for the next frame's blend
                targets: vec![target.clone(), target],
                ..default()
            }),
            ..default()
        }
    }
}

#[derive(Component)]
struct ResolvePipelineId(CachedRenderPipelineId);

fn prepare_pipelines(
    mut commands: Commands,
    pipeline_cache: Res<PipelineCache>,
    mut specialized: ResMut<SpecializedRenderPipelines<ResolvePipeline>>,
    pipeline: Res<ResolvePipeline>,
    views: Query<(Entity, &ExtractedView), With<SmaaT2x>>,
) {
    for (e, view) in &views {
        let id = specialized.specialize(&pipeline_cache, &pipeline, view.target_format);
        commands.entity(e).insert(ResolvePipelineId(id));
    }
}

/// Last frame's picture, which this frame reads, and the texture this frame writes. They swap every frame.
#[derive(Component)]
struct History {
    read: CachedTexture,
    write: CachedTexture,
}

fn prepare_history(
    mut commands: Commands,
    mut cache: ResMut<TextureCache>,
    device: Res<RenderDevice>,
    frame: Res<FrameCount>,
    views: Query<(Entity, &ExtractedCamera, &ExtractedView), With<SmaaT2x>>,
) {
    for (e, cam, view) in &views {
        let Some(size) = cam.physical_viewport_size else { continue };
        let mut texture = |label: &'static str| {
            cache.get(
                &device,
                TextureDescriptor {
                    label: Some(label),
                    size: size.to_extents(),
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: TextureDimension::D2,
                    format: view.target_format,
                    usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    view_formats: &[],
                },
            )
        };
        let (a, b) = (texture("smaa_t2x_history_a"), texture("smaa_t2x_history_b"));
        let (read, write) = if frame.0 % 2 == 0 { (a, b) } else { (b, a) };
        commands.entity(e).insert(History { read, write });
    }
}

fn resolve(
    view: ViewQuery<(&ViewTarget, &ViewPrepassTextures, &History, &ResolvePipelineId), With<SmaaT2x>>,
    pipeline: Res<ResolvePipeline>,
    pipeline_cache: Res<PipelineCache>,
    mut ctx: RenderContext,
) {
    let (target, prepass, history, id) = view.into_inner();
    let (Some(motion), Some(render_pipeline)) = (prepass.motion_vectors_view(), pipeline_cache.get_render_pipeline(id.0)) else {
        return;
    };
    let post = target.post_process_write();
    let group = ctx.render_device().create_bind_group(
        "smaa_t2x",
        &pipeline_cache.get_bind_group_layout(&pipeline.layout),
        &BindGroupEntries::sequential((post.source, &history.read.default_view, motion, &pipeline.sampler)),
    );
    let attachment = |view| {
        Some(RenderPassColorAttachment { view, depth_slice: None, resolve_target: None, ops: Operations::default() })
    };
    let mut pass = ctx.command_encoder().begin_render_pass(&RenderPassDescriptor {
        label: Some("smaa_t2x_resolve"),
        color_attachments: &[attachment(post.destination), attachment(&history.write.default_view)],
        depth_stencil_attachment: None,
        timestamp_writes: None,
        occlusion_query_set: None,
        multiview_mask: None,
    });
    pass.set_pipeline(render_pipeline);
    pass.set_bind_group(0, &group, &[]);
    pass.draw(0..3, 0..1);
}
