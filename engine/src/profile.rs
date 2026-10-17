//! Frame cost profile (`--profile`, with `--bench`): per-pass GPU time (Bevy's RenderDiagnosticsPlugin, wgpu
//! timestamp queries) and the CPU time of the main world and the render world, without the time the render world
//! spends waiting for the swapchain. The fps says what the frames took; the costs say which stage set it: the
//! uncapped frame time is about the largest of the GPU time, the main world's CPU time and the render world's CPU
//! time (pipelined rendering overlaps the two worlds and the GPU), see [`FrameCost::estimate_ms`].
//!
//! Game-neutral: the samples go to the [`Bench`](crate::bench::Bench) JSON (`gpu/<pass>_avg` per pass, `gpu_total`,
//! `cpu_main`, `cpu_render`, `render_acquire_wait`, `render_present`, `frame_cost_estimate`: the
//! slowest stage per frame).

use crate::bench::Bench;
use bevy::diagnostic::DiagnosticsStore;
use bevy::prelude::*;
use bevy::render::diagnostic::RenderDiagnosticsPlugin;
use bevy::render::renderer::{RenderGraph, RenderGraphSystems, render_system};
use bevy::render::view::prepare_windows;
use bevy::render::{Render, RenderApp, RenderSystems};
use std::sync::{Arc, Mutex};
use std::time::Instant;

/// The render world's timings of its last frame (ms), shared with the main world.
#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct RenderTimes {
    /// the whole Render schedule
    pub total: f32,
    /// waiting for a swapchain image (prepare_windows)
    pub acquire: f32,
    /// render_system minus the render graph: presenting (and screenshot copies)
    pub present: f32,
}

impl RenderTimes {
    /// The render world's own work: its schedule without the swapchain waits.
    pub fn work(&self) -> f32 {
        (self.total - self.acquire - self.present).max(0.0)
    }
}

/// One frame's costs (ms).
#[derive(Clone, Copy, Default, Debug, PartialEq)]
pub struct FrameCost {
    pub gpu: f32,
    pub cpu_main: f32,
    pub cpu_render: f32,
}

impl FrameCost {
    /// The uncapped frame time: the main world, the render world and the GPU run in a pipeline (Bevy's pipelined
    /// rendering), so the slowest of the three sets the rate.
    pub fn estimate_ms(&self) -> f32 {
        self.gpu.max(self.cpu_main).max(self.cpu_render)
    }
}

#[derive(Resource, Clone, Default)]
struct SharedRenderTimes(Arc<Mutex<Option<RenderTimes>>>);

/// The render world's clock for the frame in progress.
#[derive(Resource, Default)]
struct RenderClock {
    start: Option<Instant>,
    acquire_start: Option<Instant>,
    render_start: Option<Instant>,
    graph_start: Option<Instant>,
    times: RenderTimes,
    graph: f32,
}

#[derive(Resource, Default)]
struct MainClock(Option<Instant>);

pub struct ProfilePlugin;

impl Plugin for ProfilePlugin {
    fn build(&self, app: &mut App) {
        let shared = SharedRenderTimes::default();
        app.add_plugins(RenderDiagnosticsPlugin)
            .insert_resource(shared.clone())
            .init_resource::<MainClock>()
            .add_systems(First, |mut c: ResMut<MainClock>| c.0 = Some(Instant::now()))
            .add_systems(Last, record.run_if(resource_exists::<Bench>));
        let Some(render) = app.get_sub_app_mut(RenderApp) else { return };
        render
            .insert_resource(shared)
            .init_resource::<RenderClock>()
            .add_systems(Render, (|mut c: ResMut<RenderClock>| c.start = Some(Instant::now())).in_set(RenderSystems::ExtractCommands))
            .add_systems(Render, (|mut c: ResMut<RenderClock>| c.acquire_start = Some(Instant::now())).in_set(RenderSystems::PrepareViews).before(prepare_windows))
            .add_systems(
                Render,
                (|mut c: ResMut<RenderClock>| c.times.acquire = since(c.acquire_start)).in_set(RenderSystems::PrepareViews).after(prepare_windows),
            )
            .add_systems(Render, (|mut c: ResMut<RenderClock>| c.render_start = Some(Instant::now())).in_set(RenderSystems::Render).before(render_system))
            .add_systems(
                Render,
                (|mut c: ResMut<RenderClock>| c.times.present = (since(c.render_start) - c.graph).max(0.0)).in_set(RenderSystems::Render).after(render_system),
            )
            .add_systems(RenderGraph, (|mut c: ResMut<RenderClock>| c.graph_start = Some(Instant::now())).before(RenderGraphSystems::Begin))
            .add_systems(RenderGraph, (|mut c: ResMut<RenderClock>| c.graph = since(c.graph_start)).after(RenderGraphSystems::Finish))
            .add_systems(Render, publish.in_set(RenderSystems::PostCleanup));
    }
}

fn since(t: Option<Instant>) -> f32 {
    t.map(|t| t.elapsed().as_secs_f32() * 1000.0).unwrap_or(0.0)
}

fn publish(mut c: ResMut<RenderClock>, shared: Res<SharedRenderTimes>) {
    c.times.total = since(c.start);
    if let Ok(mut s) = shared.0.lock() {
        *s = Some(c.times);
    }
}

/// Per-pass GPU times of the latest frame the diagnostics resolved: top-level spans only (a nested span's time is
/// in its parent's), keyed by the pass name.
fn gpu_passes(store: &DiagnosticsStore) -> Vec<(String, f32)> {
    store
        .iter()
        .filter_map(|d| {
            let parts: Vec<&str> = d.path().components().collect();
            match parts.as_slice() {
                ["render", name, "elapsed_gpu"] => d.value().map(|v| ((*name).to_owned(), v as f32)),
                _ => None,
            }
        })
        .collect()
}

fn record(
    mut bench: ResMut<Bench>,
    store: Res<DiagnosticsStore>,
    shared: Res<SharedRenderTimes>,
    main: Res<MainClock>,
    mut last_seen: Local<Option<bevy::platform::time::Instant>>,
    mut cost: Local<FrameCost>,
    (mut mesh_events, meshes): (MessageReader<AssetEvent<Mesh>>, Res<Assets<Mesh>>),
) {
    // mesh churn: every added or modified mesh is re-uploaded (the render world's mesh allocator frees and allocates
    // it again), so these counts explain allocate_and_free_meshes
    let (mut changed, mut vertices) = (0usize, 0usize);
    for e in mesh_events.read() {
        if let AssetEvent::Added { id } | AssetEvent::Modified { id } = e {
            changed += 1;
            vertices += meshes.get(*id).map_or(0, |m| m.count_vertices());
        }
    }
    bench.record("meshes_changed", changed as f32);
    bench.record("mesh_vertices_changed", vertices as f32);
    cost.cpu_main = since(main.0);
    bench.record("cpu_main", cost.cpu_main);
    if let Some(r) = shared.0.lock().ok().and_then(|mut s| s.take()) {
        cost.cpu_render = r.work();
        bench.record("cpu_render", r.work());
        bench.record("render_acquire_wait", r.acquire);
        bench.record("render_present", r.present);
    }
    bench.record("frame_cost_estimate", cost.estimate_ms());
    // the diagnostics update once per resolved frame: count each frame once
    let stamp = store.iter().find(|d| d.path().as_str().ends_with("elapsed_gpu")).and_then(|d| d.measurement()).map(|m| m.time);
    if stamp.is_none() || stamp == *last_seen {
        return;
    }
    *last_seen = stamp;
    let passes = gpu_passes(&store);
    let total: f32 = passes.iter().map(|p| p.1).sum();
    for (name, ms) in passes {
        bench.record(&format!("gpu/{name}"), ms);
    }
    bench.record("gpu_total", total);
    cost.gpu = total;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn estimate_is_the_slowest_stage() {
        let c = FrameCost { gpu: 7.5, cpu_main: 4.0, cpu_render: 6.0 };
        assert_eq!(c.estimate_ms(), 7.5);
        let r = RenderTimes { total: 20.0, acquire: 9.0, present: 5.0 };
        assert_eq!(r.work(), 6.0);
    }
}
