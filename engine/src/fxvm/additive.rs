//! The additive fx (SRCALPHA / ONE: the flashes, glows, flames, trails, the snow domes, the additive particle pool),
//! added as the original adds them: to its display-encoded back buffer, in gamma space, saturating at 1.
//!
//! The remaster's target is linear light, where the same addition is (g + c)^2.2 - g^2.2 for a scene of gamma value
//! g: it depends on what is behind. Until 2026-10-25 every additive shader assumed g = 0.6 (the snow levels), which
//! over a dark scene added 2.5 to 4 times the original's light (lev_bonus_m1's impact flashes as a white haze with a
//! blocky rim). Now:
//! - the additive materials (`FxAddMaterial`, `FxLayerMaterial`, the additive `ParticleMaterial`) read the view's
//!   transmission texture, which puts them in Bevy's transmissive pass: after the opaque scene, which Bevy copies
//!   first (`ViewTransmissionTexture`, one step), and before the alpha-blended transparent pass. They draw their
//!   gamma-space colour times alpha, summed by the blend;
//! - `composite`, right after that pass, takes the sum S (the scene after minus the copy) and writes
//!   (g + S, at most 1)^2.2 per channel (additive.wesl). Exact for any number of overlapping adds, as the original's
//!   saturating adds of positive colours sum the same way.
//!
//! The order matches the original's draw: the additive particle pool (pool 0) before the alpha-blended pools
//! (Fx_DrawParticles 0x00423820), the fx meshes with the scene. An alpha-blended sprite in front of an additive one
//! covers it, as before; one behind it now covers it too (the transparent pass is after).

use bevy::core_pipeline::core_3d::main_transparent_pass_3d;
use bevy::core_pipeline::{Core3d, Core3dSystems, FullscreenShader};
use bevy::pbr::{ScreenSpaceTransmission, ScreenSpaceTransmissionQuality, Transmissive3d, ViewTransmissionTexture, main_transmissive_pass_3d};
use bevy::prelude::*;
use bevy::render::camera::ExtractedCamera;
use bevy::render::extract_component::{ExtractComponent, ExtractComponentPlugin};
use bevy::render::render_phase::ViewSortedRenderPhases;
use bevy::render::render_resource::binding_types::texture_2d;
use bevy::render::render_resource::*;
use bevy::render::renderer::{RenderContext, RenderDevice, ViewQuery};
use bevy::render::texture::{CachedTexture, TextureCache};
use bevy::render::view::{ExtractedView, Msaa, ViewTarget};
use bevy::render::{Render, RenderApp, RenderStartup, RenderSystems};

/// A view whose additive fx are composited in gamma space (the game's view: added by `tag_views`).
#[derive(Component, ExtractComponent, Clone, Copy, Default)]
#[extract_app(RenderApp)]
pub struct AdditiveComposite;

pub struct AdditiveCompositePlugin;

impl Plugin for AdditiveCompositePlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "additive.wesl");
        app.add_plugins(ExtractComponentPlugin::<AdditiveComposite>::default()).add_systems(PostUpdate, tag_views);
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return };
        render_app
            .init_resource::<SpecializedRenderPipelines<CompositePipelines>>()
            .add_systems(RenderStartup, init_pipelines)
            .add_systems(Render, (prepare_textures.in_set(RenderSystems::PrepareResources), prepare_pipelines.in_set(RenderSystems::Prepare)))
            .add_systems(Core3d, composite.after(main_transmissive_pass_3d).before(main_transparent_pass_3d).in_set(Core3dSystems::MainPass));
    }
}

/// The game's view (display.rs `GameView`) gets the transmissive pass (one step: one copy of the scene) and the
/// composite. Other 3D cameras (the reflection bake, thumbnails) draw the additive fx as linear sums, uncorrected.
fn tag_views(mut commands: Commands, views: Query<Entity, (With<Camera3d>, With<crate::display::GameView>, Without<AdditiveComposite>)>) {
    for e in &views {
        commands.entity(e).insert((AdditiveComposite, ScreenSpaceTransmission { steps: 1, quality: ScreenSpaceTransmissionQuality::Low }));
    }
}

#[derive(Resource)]
struct CompositePipelines {
    layout: BindGroupLayoutDescriptor,
    fullscreen: FullscreenShader,
    shader: Handle<Shader>,
}

fn init_pipelines(mut commands: Commands, assets: Res<AssetServer>, fullscreen: Res<FullscreenShader>) {
    let unfiltered = || texture_2d(TextureSampleType::Float { filterable: false });
    let layout = BindGroupLayoutDescriptor::new("fx_additive_layout", &BindGroupLayoutEntries::sequential(ShaderStages::FRAGMENT, (unfiltered(), unfiltered())));
    let shader = assets.load(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("additive.wesl")).with_source("embedded"));
    commands.insert_resource(CompositePipelines { layout, fullscreen: fullscreen.clone(), shader });
}

impl SpecializedRenderPipeline for CompositePipelines {
    /// the view target's format and its sample count (the composite writes every sample under MSAA)
    type Key = (TextureFormat, u32);

    fn specialize(&self, (format, samples): Self::Key) -> RenderPipelineDescriptor {
        RenderPipelineDescriptor {
            label: Some("fx_additive_composite".into()),
            layout: vec![self.layout.clone()],
            vertex: self.fullscreen.to_vertex_state(),
            fragment: Some(FragmentState {
                shader: self.shader.clone(),
                entry_point: Some("composite".into()),
                targets: vec![Some(ColorTargetState { format, blend: None, write_mask: ColorWrites::ALL })],
                ..default()
            }),
            multisample: MultisampleState { count: samples, ..default() },
            ..default()
        }
    }
}

#[derive(Component)]
struct CompositePipeline(CachedRenderPipelineId);

/// The scene with the additive sums on it, copied out of the main texture for the composite to read.
#[derive(Component)]
struct SummedScene(CachedTexture);

fn prepare_pipelines(
    mut commands: Commands,
    cache: Res<PipelineCache>,
    mut specialized: ResMut<SpecializedRenderPipelines<CompositePipelines>>,
    pipelines: Res<CompositePipelines>,
    views: Query<(Entity, &ExtractedView, &Msaa), With<AdditiveComposite>>,
) {
    for (e, view, msaa) in &views {
        let id = specialized.specialize(&cache, &pipelines, (view.target_format, msaa.samples()));
        commands.entity(e).insert(CompositePipeline(id));
    }
}

fn prepare_textures(mut commands: Commands, mut cache: ResMut<TextureCache>, device: Res<RenderDevice>, views: Query<(Entity, &ExtractedCamera, &ExtractedView), With<AdditiveComposite>>) {
    for (e, camera, view) in &views {
        let Some(size) = camera.physical_target_size else { continue };
        let texture = cache.get(
            &device,
            TextureDescriptor {
                label: Some("fx_additive_summed_scene"),
                size: Extent3d { width: size.x, height: size.y, depth_or_array_layers: 1 },
                mip_level_count: 1,
                sample_count: 1,
                dimension: TextureDimension::D2,
                format: view.target_format,
                usage: TextureUsages::COPY_DST | TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            },
        );
        commands.entity(e).insert(SummedScene(texture));
    }
}

/// After the transmissive pass: the scene with the sums copied out, then (scene before, scene after) composited back
/// into the view's colour attachment (every sample under MSAA, resolved into the main texture as the pass ends).
/// Nothing when no additive fx drew this frame (no transmissive items: Bevy made no copy either).
fn composite(
    view: ViewQuery<(&ExtractedCamera, &ExtractedView, &ViewTarget, Option<&ViewTransmissionTexture>, &SummedScene, &CompositePipeline)>,
    phases: Res<ViewSortedRenderPhases<Transmissive3d>>,
    pipelines: Res<CompositePipelines>,
    cache: Res<PipelineCache>,
    mut ctx: RenderContext,
) {
    let (camera, extracted, target, before, summed, pipeline) = view.into_inner();
    if phases.get(&extracted.retained_view_entity).is_none_or(|p| p.items.is_empty()) {
        return;
    }
    let (Some(before), Some(pipeline), Some(size)) = (before, cache.get_render_pipeline(pipeline.0), camera.physical_target_size) else {
        return;
    };
    let device = ctx.render_device().clone();
    ctx.command_encoder().copy_texture_to_texture(
        target.main_texture().as_image_copy(),
        summed.0.texture.as_image_copy(),
        Extent3d { width: size.x, height: size.y, depth_or_array_layers: 1 },
    );
    let group = device.create_bind_group("fx_additive", &cache.get_bind_group_layout(&pipelines.layout), &BindGroupEntries::sequential((&before.view, &summed.0.default_view)));
    let mut pass = ctx.command_encoder().begin_render_pass(&RenderPassDescriptor {
        label: Some("fx_additive_composite"),
        color_attachments: &[Some(target.get_color_attachment())],
        depth_stencil_attachment: None,
        timestamp_writes: None,
        occlusion_query_set: None,
        multiview_mask: None,
    });
    if let Some(viewport) = camera.viewport.as_ref() {
        pass.set_viewport(viewport.physical_position.x as f32, viewport.physical_position.y as f32, viewport.physical_size.x as f32, viewport.physical_size.y as f32, 0.0, 1.0);
    }
    pass.set_pipeline(pipeline);
    pass.set_bind_group(0, &group, &[]);
    pass.draw(0..3, 0..1);
}
