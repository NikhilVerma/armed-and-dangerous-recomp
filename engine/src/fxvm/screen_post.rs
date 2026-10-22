//! The screen effects' draw (PostProcess_Pass 0x0047c600, render_r3.cpp:1565) on the game's 3D view, after the
//! scene's anti-aliasing and before the present pass and the HUD, as the original drew them at the end of the
//! scene pass, under the 2D pass:
//! - the blur (Post_DrawBlur 0x0047d0f0): the screen copied down a chain of 5 small targets, 512x256 halving to
//!   32x16 (PostProcess_CreateTargets 0x0047c920), the amount split into a level and a fraction (Post_SplitLevel
//!   0x0047bca0); level - 1 drawn opaque over the screen when the level is above 0, the level over that at the
//!   fraction. The remaster's first step averages the whole screen area of a target texel (the original's single
//!   bilinear tap skipped most pixels; the old remaster's post_blur_prescale fixed it the same way).
//! - the fade (Post_DrawFade 0x0047cff0): the colour over the screen at the alpha (clamped, as a byte).
//!
//! The original blended its 8-bit display-encoded back buffer: the pass works on the game's values (the scene
//! taken to them as gamematerial.wesl writes them, a 2.2 power) and turns the result back to linear light.

use super::screen::{ScreenEffects, clamp01};
use bevy::core_pipeline::tonemapping::tonemapping;
use bevy::core_pipeline::{Core3d, Core3dSystems, FullscreenShader};
use bevy::prelude::*;
use bevy::render::camera::ExtractedCamera;
use bevy::render::extract_component::{ComponentUniforms, DynamicUniformIndex, ExtractComponent, ExtractComponentPlugin, UniformComponentPlugin};
use bevy::render::render_resource::binding_types::{sampler, texture_2d, uniform_buffer};
use bevy::render::render_resource::*;
use bevy::render::renderer::{RenderContext, RenderDevice, ViewQuery};
use bevy::render::texture::{CachedTexture, TextureCache};
use bevy::render::view::{ExtractedView, ViewTarget};
use bevy::render::{Render, RenderApp, RenderStartup, RenderSystems};

/// The blur chain's targets: 512x256 (2^9 x 2^8) halving while 5 fit (PostProcess_CreateTargets 0x0047c920).
const BLUR_LEVELS: u32 = 5;
const BLUR_TOP: UVec2 = UVec2::new(512, 256);

/// The chain's format: the original's 8-bit targets, holding the game's (display-encoded) values.
const LEVEL_FORMAT: TextureFormat = TextureFormat::Rgba8Unorm;

/// This frame's draw on the game's view (screen_post.wesl `ScreenPost`); present only while something draws.
#[derive(Component, ExtractComponent, ShaderType, Clone, Copy, Default, Debug, PartialEq)]
#[extract_app(RenderApp)]
pub struct ScreenPost {
    /// the fade's game colour and alpha (0: none)
    fade: Vec4,
    /// the blur's chain level (0..BLUR_LEVELS)
    level: u32,
    /// 1: level - 1 drawn opaque first
    opaque_below: u32,
    /// the level drawn over at this alpha
    fraction: f32,
    /// 1: the blur draws
    blur: u32,
}

/// The original's colour bytes: an alpha as Ftol(a * 255) / 255.
fn byte(a: f32) -> f32 {
    (a * 255.0).trunc() / 255.0
}

impl ScreenPost {
    /// PostProcess_Pass's draws from the accumulators (the blur alone: its ring form is not ported; see screen.rs).
    fn from_effects(fx: &ScreenEffects) -> Option<Self> {
        let mut post = ScreenPost::default();
        let blur = fx.accum.blur;
        if blur != 0.0 {
            let (level, fraction) = split_level(blur, BLUR_LEVELS);
            post.level = level;
            post.opaque_below = (level > 0) as u32;
            post.fraction = byte(fraction);
            post.blur = 1;
        }
        let (colour, alpha) = fx.accum.fade;
        if alpha != 0.0 {
            post.fade = colour.extend(byte(clamp01(alpha)));
        }
        (post.blur != 0 || post.fade.w > 0.0).then_some(post)
    }
}

/// Post_SplitLevel 0x0047bca0: the amount (clamped to 0..1) times the level count, as a level (truncated, at most
/// the last) and the rest above it.
fn split_level(amount: f32, levels: u32) -> (u32, f32) {
    let scaled = clamp01(amount) * levels as f32;
    let level = (scaled.trunc() as u32).min(levels - 1);
    (level, scaled - level as f32)
}

pub struct ScreenPostPlugin;

impl Plugin for ScreenPostPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "screen_post.wesl");
        app.add_plugins((ExtractComponentPlugin::<ScreenPost>::default(), UniformComponentPlugin::<ScreenPost>::default()));
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return };
        render_app
            .init_resource::<SpecializedRenderPipelines<ScreenPostPipelines>>()
            .add_systems(RenderStartup, init_pipelines)
            .add_systems(Render, (prepare_textures.in_set(RenderSystems::PrepareResources), prepare_pipelines.in_set(RenderSystems::Prepare)))
            .add_systems(
                Core3d,
                draw.in_set(Core3dSystems::PostProcess)
                    .after(tonemapping)
                    .after(bevy::anti_alias::contrast_adaptive_sharpening::cas)
                    .after(bevy::anti_alias::smaa::smaa)
                    .before(bevy::core_pipeline::fullscreen_material::fullscreen_material_system::<crate::graphics::present::PresentPass>),
            );
    }
}

/// The draw on the game's view while an effect wants one (after the accumulation: screen.rs `update`).
pub(super) fn follow_effects(mut commands: Commands, fx: Res<ScreenEffects>, mut views: Query<(Entity, Option<&mut ScreenPost>), (With<Camera3d>, With<crate::display::GameView>)>) {
    let want = ScreenPost::from_effects(&fx);
    for (e, have) in &mut views {
        match (want, have) {
            (Some(w), Some(mut h)) => {
                if *h != w {
                    *h = w;
                }
            }
            (Some(w), None) => {
                commands.entity(e).insert(w);
            }
            (None, Some(_)) => {
                commands.entity(e).remove::<ScreenPost>();
            }
            (None, None) => {}
        }
    }
}

#[derive(Resource)]
struct ScreenPostPipelines {
    downsample_layout: BindGroupLayoutDescriptor,
    composite_layout: BindGroupLayoutDescriptor,
    sampler: Sampler,
    /// the screen into the first level (to the game's values)
    downsample_screen: CachedRenderPipelineId,
    /// a level into the next
    downsample: CachedRenderPipelineId,
    fullscreen: FullscreenShader,
    shader: Handle<Shader>,
}

fn init_pipelines(mut commands: Commands, assets: Res<AssetServer>, device: Res<RenderDevice>, pipeline_cache: Res<PipelineCache>, fullscreen: Res<FullscreenShader>) {
    let filtered = || texture_2d(TextureSampleType::Float { filterable: true });
    let downsample_layout =
        BindGroupLayoutDescriptor::new("screen_post_downsample_layout", &BindGroupLayoutEntries::sequential(ShaderStages::FRAGMENT, (filtered(), sampler(SamplerBindingType::Filtering))));
    let composite_layout = BindGroupLayoutDescriptor::new(
        "screen_post_composite_layout",
        &BindGroupLayoutEntries::sequential(ShaderStages::FRAGMENT, (filtered(), sampler(SamplerBindingType::Filtering), filtered(), filtered(), uniform_buffer::<ScreenPost>(true))),
    );
    let shader: Handle<Shader> = assets.load(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("screen_post.wesl")).with_source("embedded"));
    let downsample = |entry: &'static str| {
        pipeline_cache.queue_render_pipeline(RenderPipelineDescriptor {
            label: Some(format!("screen_post_{entry}").into()),
            layout: vec![downsample_layout.clone()],
            vertex: fullscreen.to_vertex_state(),
            fragment: Some(FragmentState {
                shader: shader.clone(),
                entry_point: Some(entry.into()),
                targets: vec![Some(ColorTargetState { format: LEVEL_FORMAT, blend: None, write_mask: ColorWrites::ALL })],
                ..default()
            }),
            ..default()
        })
    };
    let sampler = device.create_sampler(&SamplerDescriptor {
        label: Some("screen_post_sampler"),
        mag_filter: FilterMode::Linear,
        min_filter: FilterMode::Linear,
        ..default()
    });
    commands.insert_resource(ScreenPostPipelines {
        downsample_screen: downsample("downsample_screen"),
        downsample: downsample("downsample"),
        downsample_layout,
        composite_layout,
        sampler,
        fullscreen: fullscreen.clone(),
        shader,
    });
}

impl SpecializedRenderPipeline for ScreenPostPipelines {
    /// the view target's format
    type Key = TextureFormat;

    fn specialize(&self, format: TextureFormat) -> RenderPipelineDescriptor {
        RenderPipelineDescriptor {
            label: Some("screen_post_composite".into()),
            layout: vec![self.composite_layout.clone()],
            vertex: self.fullscreen.to_vertex_state(),
            fragment: Some(FragmentState {
                shader: self.shader.clone(),
                entry_point: Some("composite".into()),
                targets: vec![Some(ColorTargetState { format, blend: None, write_mask: ColorWrites::ALL })],
                ..default()
            }),
            ..default()
        }
    }
}

#[derive(Component)]
struct CompositePipeline(CachedRenderPipelineId);

fn prepare_pipelines(
    mut commands: Commands,
    pipeline_cache: Res<PipelineCache>,
    mut specialized: ResMut<SpecializedRenderPipelines<ScreenPostPipelines>>,
    pipelines: Res<ScreenPostPipelines>,
    views: Query<(Entity, &ExtractedView), With<ScreenPost>>,
) {
    for (e, view) in &views {
        let id = specialized.specialize(&pipeline_cache, &pipelines, view.target_format);
        commands.entity(e).insert(CompositePipeline(id));
    }
}

/// The blur chain's targets of a view.
#[derive(Component)]
struct BlurLevels([CachedTexture; BLUR_LEVELS as usize]);

fn prepare_textures(mut commands: Commands, mut cache: ResMut<TextureCache>, device: Res<RenderDevice>, views: Query<Entity, (With<ScreenPost>, With<ExtractedCamera>)>) {
    for e in &views {
        let levels = std::array::from_fn(|i| {
            let size = (BLUR_TOP >> i as u32).max(UVec2::ONE);
            cache.get(
                &device,
                TextureDescriptor {
                    label: Some("screen_post_blur_level"),
                    size: Extent3d { width: size.x, height: size.y, depth_or_array_layers: 1 },
                    mip_level_count: 1,
                    sample_count: 1,
                    dimension: TextureDimension::D2,
                    format: LEVEL_FORMAT,
                    usage: TextureUsages::RENDER_ATTACHMENT | TextureUsages::TEXTURE_BINDING,
                    view_formats: &[],
                },
            )
        });
        commands.entity(e).insert(BlurLevels(levels));
    }
}

fn draw(
    view: ViewQuery<(&ViewTarget, &ScreenPost, &DynamicUniformIndex<ScreenPost>, &BlurLevels, &CompositePipeline)>,
    pipelines: Res<ScreenPostPipelines>,
    pipeline_cache: Res<PipelineCache>,
    uniforms: Res<ComponentUniforms<ScreenPost>>,
    mut ctx: RenderContext,
) {
    let (target, post, index, levels, composite) = view.into_inner();
    let (Some(downsample_screen), Some(downsample), Some(composite), Some(uniform)) = (
        pipeline_cache.get_render_pipeline(pipelines.downsample_screen),
        pipeline_cache.get_render_pipeline(pipelines.downsample),
        pipeline_cache.get_render_pipeline(composite.0),
        uniforms.uniforms().binding(),
    ) else {
        return;
    };
    let device = ctx.render_device().clone();
    let screen = target.post_process_write();
    let pass = |encoder: &mut CommandEncoder, label: &'static str, dst: &TextureView, pipeline: &RenderPipeline, group: &BindGroup, offsets: &[u32]| {
        let mut pass = encoder.begin_render_pass(&RenderPassDescriptor {
            label: Some(label),
            color_attachments: &[Some(RenderPassColorAttachment { view: dst, depth_slice: None, resolve_target: None, ops: Operations::default() })],
            depth_stencil_attachment: None,
            timestamp_writes: None,
            occlusion_query_set: None,
            multiview_mask: None,
        });
        pass.set_pipeline(pipeline);
        pass.set_bind_group(0, group, offsets);
        pass.draw(0..3, 0..1);
    };
    // Post_BlitChain 0x0047cf10: the screen down the chain to the drawn level
    if post.blur != 0 {
        let layout = pipeline_cache.get_bind_group_layout(&pipelines.downsample_layout);
        for i in 0..=post.level as usize {
            let (src, pipeline) = if i == 0 { (screen.source, downsample_screen) } else { (&levels.0[i - 1].default_view, downsample) };
            let group = device.create_bind_group("screen_post_downsample", &layout, &BindGroupEntries::sequential((src, &pipelines.sampler)));
            pass(ctx.command_encoder(), "screen_post_downsample", &levels.0[i].default_view, pipeline, &group, &[]);
        }
    }
    let below = post.level.saturating_sub(1) as usize;
    let group = device.create_bind_group(
        "screen_post_composite",
        &pipeline_cache.get_bind_group_layout(&pipelines.composite_layout),
        &BindGroupEntries::sequential((screen.source, &pipelines.sampler, &levels.0[below].default_view, &levels.0[post.level as usize].default_view, uniform)),
    );
    pass(ctx.command_encoder(), "screen_post_composite", screen.destination, composite, &group, &[index.index()]);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Post_SplitLevel over the 5 targets: the pause blur 0.75 is level 3 at 0.75 over level 2.
    #[test]
    fn split() {
        assert_eq!(split_level(0.75, 5), (3, 0.75));
        assert_eq!(split_level(1.0, 5), (4, 1.0));
        assert_eq!(split_level(0.1, 5), (0, 0.5));
        assert_eq!(split_level(f32::NAN, 5), (0, 0.0));
    }
}
