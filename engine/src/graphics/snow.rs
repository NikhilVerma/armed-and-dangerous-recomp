//! GPU snowfall: the Remaster's snow, simulated and drawn on the GPU (snow.wesl). A port of the old remaster's Vulkan
//! snow (archive/hook/enhance/weather.cpp, archive/src/rhi/vulkan/vk_snow.cpp, docs/subsystems/weather.md) to Bevy.
//!
//! The original has no weather system: a level's `camera_fx` script attaches textured cylinders around the camera
//! (`fxo_snow`, `fxo_snow_2`, lev_1_m1's fx_camera_l1_m1) whose UV scrolling reads as falling snow. With the setting
//! on, in a level whose camera fx attached those cylinders, the cylinders stay (the original's soft flakes;
//! [`SnowTuning::keep_cylinders`], else hidden as the fx "invisible" opcode 0x16 does: the fx script, its lifetime and
//! the camera path run on), the camera-locked flake emitter stops emitting
//! ([`SnowTuning::mutes`]: its sprites fly at the lens as big soft blobs) and a volume of flakes around the camera
//! falls instead:
//! - each flake is a particle in a storage buffer, moved by a compute pass: born in the sky above the camera, falling
//!   at its terminal speed through gusting, swirling wind with drag, born again when it lands on a visible surface
//!   (the depth prepass) or leaves the volume; sideways the volume wraps around the camera. It fills the visible world
//!   (150 units each way), not a box at the lens ("a screensaver cloud riding with the camera": the player, 2026-10-11)
//! - the flakes advance with the game's clock ([`Time<Virtual>`]): they stop while the game is paused
//! - each is drawn as a soft sprite stretched along its own motion over the shutter time (motion-blur streaks; never
//!   the camera's motion), hidden behind the scene and fading where it meets it, lit by the level's lights and fogged
//!   by its fog as the game material is, after the anti-aliasing (TAA has no motion vectors for it) and before bloom.
//!
//! Data: [`SnowSettings`], flattened into [`GraphicsSettings`]: `snow` (on / off), `snow_amount` (flakes) and
//! `snow_tuning` ([`SnowTuning`], the old remaster's defaults). Remaster preset on, Original off (the cylinders as in
//! the original).
//!
//! API: [`SnowPlugin`] (added by the graphics plugin); [`SnowCamera`] on the game's camera (the view that snows; the
//! save thumbnail's camera does not); [`Snowfall`] (resource: whether it snows, the flake count, the cylinders).

use super::GraphicsSettings;
use crate::fxvm::{FxEntity, FxVm};
use crate::gamematerial::GameLights;
use bevy::anti_alias::taa::temporal_anti_alias;
use bevy::core_pipeline::prepass::ViewPrepassTextures;
use bevy::core_pipeline::schedule::{Core3d, Core3dSystems};
use bevy::platform::collections::HashMap;
use bevy::prelude::*;
use bevy::render::camera::ExtractedCamera;
use bevy::render::render_resource::binding_types::{storage_buffer_read_only_sized, storage_buffer_sized, texture_depth_2d, texture_depth_2d_multisampled, uniform_buffer};
use bevy::render::render_resource::{
    encase, BindGroupEntries, BindGroupLayoutDescriptor, BindGroupLayoutEntries, BlendComponent, BlendFactor, BlendOperation, BlendState, Buffer,
    BufferDescriptor, BufferUsages, CachedComputePipelineId, CachedRenderPipelineId, ColorTargetState, ColorWrites, ComputePassDescriptor,
    ComputePipelineDescriptor, FragmentState, PipelineCache, RenderPassDescriptor, RenderPipelineDescriptor, ShaderStages, ShaderType,
    TextureFormat, VertexState,
};
use bevy::render::diagnostic::RecordDiagnostics;
use bevy::render::renderer::{RenderContext, RenderDevice, RenderQueue, ViewQuery};
use bevy::render::sync_world::RenderEntity;
use bevy::render::view::{ExtractedView, Msaa, ViewTarget};
use bevy::render::{Extract, ExtractSchedule, Render, RenderApp, RenderStartup, RenderSystems};
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

/// The snowfall's settings (a flattened block of [`GraphicsSettings`]): the Remaster preset snows, the Original keeps
/// the game's cylinders.
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct SnowSettings {
    /// GPU snowfall in place of the camera's snow cylinders, in levels whose camera fx attach them
    pub snow: bool,
    /// flakes in the volume around the camera (at most 4,000,000). The default, 200,000, gives about the visible
    /// amount of the original's snow cylinders at the same cameras (tools/compare_gallery.py, lev_1_m1 close-up,
    /// 2026-10-23: bright specks over the village roof beyond the scene's own, the old remaster's 1,500,000 flakes
    /// 274, 200,000 ~90, the Original preset's cylinders 87; the player: "snow is insane"). The amount slider goes to
    /// 3,000,000 for a blizzard
    pub snow_amount: u32,
    /// the snowfall's look and motion
    pub snow_tuning: SnowTuning,
}

impl SnowSettings {
    pub fn original() -> Self {
        SnowSettings { snow: false, snow_amount: DEFAULT_FLAKES, snow_tuning: SnowTuning::default() }
    }

    pub fn remaster() -> Self {
        SnowSettings { snow: true, ..Self::original() }
    }
}

impl Default for SnowSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// The default flake count (see [`SnowSettings::snow_amount`]).
pub const DEFAULT_FLAKES: u32 = 200_000;

/// The Graphics menu's stepper for the amount (name, min, max, positions): 0 to 3,000,000 flakes in steps of 100,000
/// (the old remaster's slider), 0 shown as Off.
pub const STEPPERS: [(&str, f32, f32, usize); 1] = [("snow_amount", 0.0, 3_000_000.0, 31)];

/// The snowfall's look and motion (the old remaster's defaults: archive/src/rhi/rhi.h snow hints, vk_device.cpp Snow()).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct SnowTuning {
    /// the fx objects the snowfall goes with: a level snows when its camera fx attach one; hidden while it snows unless
    /// `keep_cylinders`
    pub replaces: Vec<String>,
    /// the original's snow cylinders stay drawn under the snowfall. They are the original's falling snow: two flake
    /// layers scrolling on different uv sets (the second 1.5 / 2x as dense) under a wind-spray mask, soft dots of a few
    /// pixels all over the view. Until 2026-10-25 every layer read the first uv set (the second flake layer as coarse
    /// as the first, the mask tiled 72 times), so they looked sparse and the snowfall replaced them; drawn right they
    /// carry the original's amount and look (work/texdensity/: the snowfall alone showed a tenth of their brightening
    /// over the near walls), and the snowfall adds depth and motion over them
    pub keep_cylinders: bool,
    /// the camera-locked particle emitters the snowfall replaces too (they stop emitting while it snows): lev_1_m1's
    /// fx_camera_l1_m1_locked carries an invisible `fxo_glow_red` that sprays flake sprites along the view at the lens
    /// all level (docs/subsystems/weather.md "snow bubbles"); they pass the near plane as big soft blobs over the
    /// scene. The player reported those "bubbles" and the old remaster muted them with its snowfall (67d7adf,
    /// archive/hook/enhance/weather.cpp); the Original preset keeps them. Empty: the remaster keeps them too
    pub mutes: Vec<String>,
    /// horizontal half size of the volume around the camera (game units)
    pub radius: f32,
    /// flakes are born this high above the camera
    pub sky: f32,
    /// and fall out this far below it
    pub below: f32,
    /// terminal fall speed (units / s)
    pub fall: f32,
    /// base wind on the game's ground plane (game x, y; units / s)
    pub wind: [f32; 2],
    /// swirl speed (units / s)
    pub turbulence: f32,
    /// how fast a flake follows the air (drag time, s)
    pub drag: f32,
    /// flake radius (units): 0.03, a little larger than the old remaster's 0.02, so the fewer flakes read as the
    /// original's soft 2-4 pixel dots at the follow camera's distance rather than pin pricks
    pub size: f32,
    /// flake opacity ("like 0.7": the player, 2026-10-12)
    pub opacity: f32,
    /// motion-streak exposure (s): the old remaster's shutter 1/120 s times its snow_shutter_scale 4 (the player:
    /// "snow particles have NO motion blur" at 1/120 s)
    pub streak: f32,
    /// longest streak (pixels)
    pub streak_max_px: f32,
    /// flakes fade over this depth in front of a surface (units)
    pub soft: f32,
    /// red flakes drawn without the scene's depth test (tell them from the game's own effects)
    pub debug: bool,
}

impl Default for SnowTuning {
    fn default() -> Self {
        SnowTuning {
            replaces: vec!["fxo_snow".into(), "fxo_snow_2".into()],
            keep_cylinders: true,
            mutes: vec!["fxo_glow_red".into()],
            radius: 150.0,
            sky: 70.0,
            below: 40.0,
            fall: 1.6,
            wind: [0.9, 0.35],
            turbulence: 0.6,
            drag: 0.5,
            size: 0.03,
            opacity: 0.7,
            streak: 4.0 / 120.0,
            streak_max_px: 96.0,
            soft: 0.3,
            debug: false,
        }
    }
}

/// The view that snows: the game's camera.
#[derive(Component, Clone, Copy, Default)]
pub struct SnowCamera;

/// A replaced fx object hidden by the snowfall, with the visibility it had.
#[derive(Component)]
struct HiddenBySnow(Visibility);

/// Whether it snows this frame and the snowfall's parameters, as the render world reads them.
#[derive(Resource, Clone, Default)]
pub struct Snowfall {
    pub active: bool,
    /// flakes simulated (0 while it does not snow)
    pub flakes: u32,
    /// the snow clock (game seconds) and this frame's step (0 while paused)
    pub time: f32,
    pub dt: f32,
    /// replaced fx objects hidden now / still drawn
    pub cylinders_hidden: usize,
    pub cylinders_shown: usize,
    /// camera-locked emitters muted now ([`SnowTuning::mutes`])
    pub emitters_muted: usize,
    gpu: SnowFrame,
}

/// The view-independent part of the shader's uniform (the rest comes from the view in the render world).
#[derive(Clone, Copy, Default)]
struct SnowFrame {
    air: Vec4,
    volume: Vec4,
    streak: f32,
    look: Vec4,
    color: Vec4,
    fog: Vec4,
    fog_range: Vec4,
}

/// Frames the snow pass drew (shared with the render world: the playtest state's proof that the GPU pass runs).
#[derive(Resource, Clone, Default)]
struct SnowStats(Arc<AtomicU64>);

pub struct SnowPlugin;

impl Plugin for SnowPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "snow.wesl");
        let stats = SnowStats::default();
        app.init_resource::<Snowfall>()
            .insert_resource(stats.clone())
            .add_systems(PostUpdate, (track, playtest_state).chain().before(bevy::camera::visibility::VisibilitySystems::VisibilityPropagate));
        let Some(render_app) = app.get_sub_app_mut(RenderApp) else { return };
        render_app
            .insert_resource(stats)
            .init_resource::<SnowGpu>()
            .add_systems(RenderStartup, init_pipelines)
            .add_systems(ExtractSchedule, extract)
            .add_systems(Render, prepare.in_set(RenderSystems::Prepare))
            .add_systems(Core3d, draw.after(temporal_anti_alias).in_set(Core3dSystems::EarlyPostProcess));
    }
}

/// Whether the level snows (its camera fx attached a replaced object), hides / restores those objects, runs the clock
/// and prepares the frame's parameters.
fn track(
    mut commands: Commands,
    settings: Res<GraphicsSettings>,
    vtime: Res<Time<Virtual>>,
    lights: Option<Res<GameLights>>,
    mut snow: ResMut<Snowfall>,
    fx: Query<(Entity, &Name, &Visibility, Option<&HiddenBySnow>), With<FxEntity>>,
    (mut vm, camera): (Option<ResMut<FxVm>>, Query<Entity, With<SnowCamera>>),
) {
    let settings = &settings.snowfall;
    let t = &settings.snow_tuning;
    let replaced = |n: &Name| t.replaces.iter().any(|r| r.eq_ignore_ascii_case(n.as_str()));
    let level_snows = fx.iter().any(|(_, n, ..)| replaced(n));
    let active = settings.snow && settings.snow_amount > 0 && level_snows;
    let hide = active && !t.keep_cylinders;
    let (mut hidden, mut shown) = (0, 0);
    for (e, n, vis, mark) in &fx {
        if !replaced(n) {
            continue;
        }
        match (hide, mark) {
            (true, None) => {
                commands.entity(e).insert((Visibility::Hidden, HiddenBySnow(*vis)));
                hidden += 1;
            }
            (true, Some(_)) => hidden += 1,
            (false, Some(m)) => {
                commands.entity(e).insert(m.0).remove::<HiddenBySnow>();
                shown += (m.0 != Visibility::Hidden) as usize;
            }
            (false, None) => shown += (*vis != Visibility::Hidden) as usize,
        }
    }
    if active != snow.active {
        info!("snow: GPU snowfall {} ({} flakes, {} cylinders hidden)", if active { "on" } else { "off" }, settings.snow_amount, hidden);
    }
    // the camera's own flake emitter goes with the cylinders (an empty list mutes nothing, and unmutes)
    let mutes: &[String] = if active { &t.mutes } else { &[] };
    let muted = vm.as_deref_mut().map_or(0, |vm| vm.mute_camera_emitters(camera.iter().next(), mutes));
    if muted != snow.emitters_muted {
        info!("snow: {muted} camera-locked particle emitter(s) muted");
    }
    snow.emitters_muted = muted;
    snow.active = active;
    snow.flakes = if active { settings.snow_amount.min(MAX_FLAKES) } else { 0 };
    snow.cylinders_hidden = hidden;
    snow.cylinders_shown = shown;
    snow.dt = if active { vtime.delta_secs().min(0.1) } else { 0.0 };
    snow.time += snow.dt;
    // the wind on the game's ground plane, in engine space (Y up: the ground plane is x, z)
    let wind = crate::level::game_to_engine() * Vec3::new(t.wind[0], t.wind[1], 0.0);
    // lit as the level's surfaces are (gamematerial.wesl: gamma space, clamped): ambient, plus the sun and fill on a
    // flake's average facing
    let base = if t.debug { Vec3::new(1.0, 0.05, 0.05) } else { Vec3::new(0.95, 0.97, 1.0) };
    let light = lights.as_deref().map_or(Vec3::ONE, |l| (l.ambient + l.sun_col * 0.6 + l.fill_col * 0.3).clamp(Vec3::ZERO, Vec3::ONE));
    let (fog, fog_range) = match lights.as_deref().and_then(|l| l.fog) {
        Some((near, far, col)) if !t.debug => (col.extend(1.0), Vec4::new(near, far, 0.0, 0.0)),
        _ => (Vec4::ZERO, Vec4::ZERO),
    };
    snow.gpu = SnowFrame {
        air: Vec4::new(wind.x, wind.z, t.fall, t.turbulence),
        volume: Vec4::new(t.radius, t.sky, t.below, t.drag.max(1e-3)),
        streak: t.streak,
        look: Vec4::new(t.size, t.soft.max(1e-3), t.streak_max_px, t.debug as u32 as f32),
        color: (base * if t.debug { Vec3::ONE } else { light }).extend(if t.debug { 1.0 } else { t.opacity }),
        fog,
        fog_range,
    };
}

/// The playtest state "snow": active, flakes, the replaced objects hidden / shown, frames the GPU pass drew.
fn playtest_state(snow: Res<Snowfall>, stats: Res<SnowStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "snow".into(),
        serde_json::json!({
            "active": snow.active,
            "flakes": snow.flakes,
            "cylinders_hidden": snow.cylinders_hidden,
            "cylinders_shown": snow.cylinders_shown,
            "emitters_muted": snow.emitters_muted,
            "frames_drawn": stats.0.load(Ordering::Relaxed),
        }),
    );
}

/// At most this many flakes (the compute dispatch's workgroups stay under the 65535 limit).
const MAX_FLAKES: u32 = 4_000_000;
const WORKGROUP: u32 = 64;
/// Bytes per flake: position + size, velocity + seed.
const FLAKE_BYTES: u64 = 32;

// ---- render world --------------------------------------------------------------------------------------------------

/// The shader's uniform (snow.wesl `Snow`).
#[derive(ShaderType, Clone, Copy, Default)]
struct SnowUniform {
    view_from_world: Mat4,
    clip_from_view: Mat4,
    cam: Vec4,
    air: Vec4,
    volume: Vec4,
    clock: Vec4,
    viewport: Vec4,
    look: Vec4,
    color: Vec4,
    fog: Vec4,
    fog_range: Vec4,
}

/// The extracted [`Snowfall`] for this frame.
#[derive(Resource, Clone, Default)]
struct SnowExtracted(Snowfall);

/// The snowfall and the view that snows: the one active [`SnowCamera`] (a render entity marked on an earlier frame
/// whose camera is gone or inactive is unmarked, so a stale view never shares the flakes and their uniform).
fn extract(
    mut commands: Commands,
    snow: Extract<Res<Snowfall>>,
    cams: Extract<Query<(RenderEntity, &Camera), With<SnowCamera>>>,
    mut marked: Local<Option<Entity>>,
) {
    commands.insert_resource(SnowExtracted(snow.clone()));
    let mut active = cams.iter().filter(|(_, c)| c.is_active).map(|(e, _)| e);
    let now = active.next();
    if active.next().is_some() {
        warn_once!("snow: more than one active SnowCamera; only the first snows");
    }
    if *marked != now {
        if let Some(old) = marked.and_then(|e| commands.get_entity(e).ok()) {
            let mut old = old;
            old.remove::<(SnowCamera, SnowView)>();
        }
        *marked = now;
    }
    if let Some(e) = now {
        commands.entity(e).insert(SnowCamera);
    }
}

/// The bind group layouts (simulate / draw, depth with or without samples) and the pipelines made from them.
#[derive(Resource)]
struct SnowPipelines {
    shader: Handle<Shader>,
    /// [simulate, draw] x [single-sampled depth, multisampled]
    layouts: [[BindGroupLayoutDescriptor; 2]; 2],
    simulate: HashMap<bool, CachedComputePipelineId>,
    draw: HashMap<(TextureFormat, bool), CachedRenderPipelineId>,
}

fn init_pipelines(mut commands: Commands, assets: Res<AssetServer>) {
    let layout = |sim: bool, ms: bool| {
        let stages = if sim { ShaderStages::COMPUTE } else { ShaderStages::VERTEX_FRAGMENT };
        let flakes = if sim { storage_buffer_sized(false, None) } else { storage_buffer_read_only_sized(false, None) };
        let depth = if ms { texture_depth_2d_multisampled() } else { texture_depth_2d() };
        BindGroupLayoutDescriptor::new(
            if sim { "snow_simulate_layout" } else { "snow_draw_layout" },
            &BindGroupLayoutEntries::sequential(stages, (uniform_buffer::<SnowUniform>(false), flakes, depth)),
        )
    };
    commands.insert_resource(SnowPipelines {
        shader: bevy::asset::load_embedded_asset!(assets.as_ref(), "snow.wesl"),
        layouts: [[layout(true, false), layout(true, true)], [layout(false, false), layout(false, true)]],
        simulate: HashMap::new(),
        draw: HashMap::new(),
    });
}

impl SnowPipelines {
    fn simulate(&mut self, cache: &PipelineCache, ms: bool) -> CachedComputePipelineId {
        let (shader, layout) = (self.shader.clone(), self.layouts[0][ms as usize].clone());
        *self.simulate.entry(ms).or_insert_with(|| {
            let mut defs = vec!["SIM".into()];
            if ms {
                defs.push("MULTISAMPLED".into());
            }
            cache.queue_compute_pipeline(ComputePipelineDescriptor {
                label: Some("snow_simulate".into()),
                layout: vec![layout],
                shader,
                shader_defs: defs,
                entry_point: Some("simulate".into()),
                ..default()
            })
        })
    }

    fn draw(&mut self, cache: &PipelineCache, format: TextureFormat, ms: bool) -> CachedRenderPipelineId {
        let (shader, layout) = (self.shader.clone(), self.layouts[1][ms as usize].clone());
        *self.draw.entry((format, ms)).or_insert_with(|| {
            let defs: Vec<_> = if ms { vec!["MULTISAMPLED".into()] } else { Vec::new() };
            // alpha-blended over the scene; the target's alpha is kept
            let blend = BlendState {
                color: BlendComponent { src_factor: BlendFactor::SrcAlpha, dst_factor: BlendFactor::OneMinusSrcAlpha, operation: BlendOperation::Add },
                alpha: BlendComponent { src_factor: BlendFactor::Zero, dst_factor: BlendFactor::One, operation: BlendOperation::Add },
            };
            cache.queue_render_pipeline(RenderPipelineDescriptor {
                label: Some("snow_draw".into()),
                layout: vec![layout],
                vertex: VertexState { shader: shader.clone(), shader_defs: defs.clone(), entry_point: Some("vertex".into()), ..default() },
                fragment: Some(FragmentState {
                    shader,
                    shader_defs: defs,
                    entry_point: Some("fragment".into()),
                    targets: vec![Some(ColorTargetState { format, blend: Some(blend), write_mask: ColorWrites::ALL })],
                    ..default()
                }),
                ..default()
            })
        })
    }
}

/// The flakes' buffer and the uniform, kept across frames.
#[derive(Resource, Default)]
struct SnowGpu {
    uniform: Option<Buffer>,
    flakes: Option<Buffer>,
    capacity: u32,
    /// the volume holds flakes around `last_cam`
    filled: bool,
    last_cam: Vec3,
}

/// What the snow pass of a view needs this frame.
#[derive(Component)]
struct SnowView {
    simulate: CachedComputePipelineId,
    draw: CachedRenderPipelineId,
    multisampled: bool,
    flakes: u32,
    /// the simulation runs (time moved, or the volume is filled)
    step: bool,
}

#[allow(clippy::too_many_arguments)]
fn prepare(
    mut commands: Commands,
    snow: Option<Res<SnowExtracted>>,
    pipelines: Option<ResMut<SnowPipelines>>,
    cache: Res<PipelineCache>,
    device: Res<RenderDevice>,
    queue: Res<RenderQueue>,
    mut gpu: ResMut<SnowGpu>,
    views: Query<(Entity, &ExtractedView, &Msaa), With<SnowCamera>>,
) {
    let (Some(snow), Some(mut pipelines)) = (snow, pipelines) else { return };
    let snow = &snow.0;
    for (e, view, msaa) in &views {
        if !snow.active || snow.flakes == 0 {
            commands.entity(e).remove::<SnowView>();
            // the next snowy frame fills the volume afresh (a pause keeps the flakes: it stays active)
            gpu.filled = false;
            continue;
        }
        let ms = *msaa != Msaa::Off;
        if snow.flakes > gpu.capacity || gpu.flakes.is_none() {
            gpu.flakes = Some(device.create_buffer(&BufferDescriptor {
                label: Some("snow_flakes"),
                size: snow.flakes as u64 * FLAKE_BYTES,
                usage: BufferUsages::STORAGE,
                mapped_at_creation: false,
            }));
            gpu.capacity = snow.flakes;
            gpu.filled = false;
        }
        let uniform = gpu
            .uniform
            .get_or_insert_with(|| {
                device.create_buffer(&BufferDescriptor {
                    label: Some("snow_uniform"),
                    size: SnowUniform::min_size().get(),
                    usage: BufferUsages::UNIFORM | BufferUsages::COPY_DST,
                    mapped_at_creation: false,
                })
            })
            .clone();
        // the pipelines compile in the background: until both are ready nothing runs, and the volume stays to be filled
        let (simulate, draw) = (pipelines.simulate(&cache, ms), pipelines.draw(&cache, view.target_format, ms));
        if cache.get_compute_pipeline(simulate).is_none() || cache.get_render_pipeline(draw).is_none() {
            commands.entity(e).remove::<SnowView>();
            continue;
        }
        let cam = view.world_from_view.translation();
        // a camera jump (level start, teleport, cut) fills the volume around the new position at once
        let jump = (cam - gpu.last_cam).abs().element_sum();
        let fill = !gpu.filled || jump > snow.gpu.volume.x;
        gpu.filled = true;
        gpu.last_cam = cam;
        let f = &snow.gpu;
        let u = SnowUniform {
            view_from_world: view.world_from_view.to_matrix().inverse(),
            clip_from_view: view.clip_from_view,
            cam: cam.extend(if fill { 1.0 } else { 0.0 }),
            air: f.air,
            volume: f.volume,
            clock: Vec4::new(snow.time, snow.dt, snow.flakes as f32, f.streak),
            viewport: view.viewport.as_vec4(),
            look: f.look,
            color: f.color,
            fog: f.fog,
            fog_range: f.fog_range,
        };
        let mut bytes = encase::UniformBuffer::new(Vec::<u8>::new());
        if bytes.write(&u).is_err() {
            continue;
        }
        queue.write_buffer(&uniform, 0, bytes.as_ref());
        commands.entity(e).insert(SnowView {
            simulate,
            draw,
            multisampled: ms,
            flakes: snow.flakes,
            step: fill || snow.dt > 0.0,
        });
    }
}

/// The snow pass: simulate, then draw over the anti-aliased scene.
fn draw(
    view: ViewQuery<(&ViewTarget, &ViewPrepassTextures, &SnowView, &ExtractedCamera)>,
    pipelines: Option<Res<SnowPipelines>>,
    cache: Res<PipelineCache>,
    gpu: Res<SnowGpu>,
    stats: Res<SnowStats>,
    mut ctx: RenderContext,
) {
    let (target, prepass, sv, camera) = view.into_inner();
    let (Some(pipelines), Some(uniform), Some(flakes), Some(depth)) = (pipelines, gpu.uniform.as_ref(), gpu.flakes.as_ref(), prepass.depth.as_ref()) else {
        return;
    };
    let (Some(simulate), Some(draw)) = (cache.get_compute_pipeline(sv.simulate), cache.get_render_pipeline(sv.draw)) else { return };
    let ms = sv.multisampled as usize;
    let depth_view = &depth.texture.default_view;
    // GPU timings for --profile (render/snow_simulate, render/snow_draw)
    let diagnostics = ctx.diagnostic_recorder();
    let diagnostics = diagnostics.as_deref();
    if sv.step {
        let layout = cache.get_bind_group_layout(&pipelines.layouts[0][ms]);
        let group = ctx.render_device().create_bind_group("snow_simulate", &layout, &BindGroupEntries::sequential((uniform.as_entire_binding(), flakes.as_entire_binding(), depth_view)));
        let span = diagnostics.time_span(ctx.command_encoder(), "snow_simulate");
        {
            let mut pass = ctx.command_encoder().begin_compute_pass(&ComputePassDescriptor { label: Some("snow_simulate"), timestamp_writes: None });
            pass.set_pipeline(simulate);
            pass.set_bind_group(0, &group, &[]);
            pass.dispatch_workgroups(sv.flakes.div_ceil(WORKGROUP), 1, 1);
        }
        span.end(ctx.command_encoder());
    }
    let layout = cache.get_bind_group_layout(&pipelines.layouts[1][ms]);
    let group = ctx.render_device().create_bind_group("snow_draw", &layout, &BindGroupEntries::sequential((uniform.as_entire_binding(), flakes.as_entire_binding(), depth_view)));
    let mut pass = ctx.begin_tracked_render_pass(RenderPassDescriptor {
        label: Some("snow_draw"),
        color_attachments: &[Some(target.get_unsampled_color_attachment())],
        depth_stencil_attachment: None,
        timestamp_writes: None,
        occlusion_query_set: None,
        multiview_mask: None,
    });
    let span = diagnostics.pass_span(&mut pass, "snow_draw");
    pass.set_render_pipeline(draw);
    pass.set_bind_group(0, &group, &[]);
    if let Some(viewport) = camera.viewport.as_ref() {
        pass.set_camera_viewport(viewport);
    }
    pass.draw(0..6, 0..sv.flakes);
    span.end(&mut pass);
    stats.0.fetch_add(1, Ordering::Relaxed);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The old remaster's defaults; the wind blows along the game's ground plane (engine x, z: no vertical part).
    #[test]
    fn defaults_and_wind_axes() {
        let t = SnowTuning::default();
        assert_eq!((t.radius, t.sky, t.fall, t.opacity), (150.0, 70.0, 1.6, 0.7));
        let w = crate::level::game_to_engine() * Vec3::new(t.wind[0], t.wind[1], 0.0);
        assert!(w.y.abs() < 1e-6 && (w.x - 0.9).abs() < 1e-6 && (w.z + 0.35).abs() < 1e-6);
    }
}
