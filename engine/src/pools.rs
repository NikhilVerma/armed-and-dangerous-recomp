//! The fixed-size pools the original sized for a 2003 machine, as typed settings ([`PoolSettings`], part of the
//! graphics presets) applied by [`PoolsPlugin`]: the old remaster's `[limits] particle_pool_scale` / `path_nodes`
//! and `[persistence] decal_pool_choice` (archive/hook/enhance/limits.cpp `LimitsInit`, archive/hook/enhance/persistence.cpp
//! `PersistenceInit`).
//!
//! - `decal_pool`: decals at once (Decals_Spawn 0x004165c0 / Decal_Recycle 0x00417110 recycle the oldest past 160:
//!   0x00416150 `mov ebx,0xa0`). The old remaster's list: Original (160), 512, 1024, 2048 (and.ini up to 4096).
//! - `particle_pool_scale`: the three fx particle pools' capacities (2000 / 4000 / 2000, Fx_LoadChunk 0x0041c7e0)
//!   times this; a full pool refuses particles (Particle_Alloc 0x0046d910). The old remaster: x1..x16.
//! - `path_nodes`: how many nodes a path search may expand. The original's A* holds 2000 (AStar_Add 0x00470600,
//!   Grid_FindPath 0x0046fd20; the old remaster enlarged the arrays to 16000). The engine's search is not the
//!   original's: nav.rs samples its own 6-unit grid from the physics world, so the original's count does not carry
//!   over (2000 of these cells cut routes the original's grid found). Both presets keep the engine's budget, which
//!   already lifts the limit; the setting is the knob (docs/engine-limits.md S4).
//!
//! The pools are resized live: a smaller pool drops its oldest decals at the next spawn; particles over a smaller
//! capacity live out their lives.

use crate::graphics::GraphicsSettings;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct PoolSettings {
    /// decals at once (the oldest is recycled past it), 160..4096
    pub decal_pool: u32,
    /// the particle pools' capacities times this, 1..16
    pub particle_pool_scale: u32,
    /// nodes a path search may expand
    pub path_nodes: u32,
}

impl PoolSettings {
    /// The original's decal pool.
    pub const ORIGINAL_DECALS: u32 = 160;

    pub fn original() -> Self {
        PoolSettings { decal_pool: Self::ORIGINAL_DECALS, particle_pool_scale: 1, path_nodes: crate::nav::DEFAULT_NODE_BUDGET as u32 }
    }

    /// The old remaster's defaults: 1024 decals (and.ini `decal_pool`), x16 particles.
    pub fn remaster() -> Self {
        PoolSettings { decal_pool: 1024, particle_pool_scale: 16, ..Self::original() }
    }

    pub fn decals(&self) -> usize {
        self.decal_pool.clamp(Self::ORIGINAL_DECALS, 4096) as usize
    }

    pub fn particle_scale(&self) -> usize {
        self.particle_pool_scale.clamp(1, 16) as usize
    }
}

impl Default for PoolSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

pub struct PoolsPlugin;

impl Plugin for PoolsPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(PreUpdate, apply);
    }
}

/// The pools' sizes from the settings (each pool's owner keeps its own count; per-level ones are set again on the
/// new level's first frame).
fn apply(s: Option<Res<GraphicsSettings>>, mut decals: Option<ResMut<crate::decals::Decals>>, mut vm: Option<ResMut<crate::fxvm::FxVm>>) {
    let p = s.map_or_else(PoolSettings::original, |s| s.pools);
    if let Some(d) = decals.as_mut().filter(|d| d.capacity != p.decals()) {
        d.capacity = p.decals();
    }
    if let Some(vm) = vm.as_mut().filter(|vm| vm.particle_pool_scale() != p.particle_scale()) {
        vm.set_particle_pool_scale(p.particle_scale());
    }
    crate::nav::set_node_budget(p.path_nodes as usize);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn presets() {
        let o = PoolSettings::original();
        assert_eq!((o.decals(), o.particle_scale()), (160, 1));
        let r = PoolSettings::remaster();
        assert_eq!((r.decals(), r.particle_scale(), r.path_nodes), (1024, 16, o.path_nodes));
        // out of range values are held to the pools' limits
        let wild = PoolSettings { decal_pool: 9, particle_pool_scale: 99, path_nodes: 0 };
        assert_eq!((wild.decals(), wild.particle_scale()), (160, 16));
    }
}
