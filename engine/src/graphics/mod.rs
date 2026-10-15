//! Presentation settings: what the renderer adds on top of the original's look, as two presets (docs/decision.md
//! 2026-10-15) and per-setting changes in options.json.
//!
//! - **Original**: the engine's faithful look (the original's textures, its lighting and fog, MSAA). Every tool run
//!   (--play, --snap, --camera, --bench, --background, --size) uses it, so comparisons with the original and the
//!   playtest suite never see a remaster feature.
//! - **Remaster** (the default for a normal launch, what the player plays): the AI retextures, screen-space ambient
//!   occlusion with snow exempt, temporal anti-aliasing with sharpening, larger soft shadows covering the whole view,
//!   soft particles, a dissolve instead of a hard cut at the far clip, bloom on over-bright effects only.
//!
//! The game rules never depend on these (no setting reaches physics, AI or timing).
//!
//! Data: options.json `graphics_preset` ("remaster" / "original") and `graphics` (any field of [`GraphicsSettings`]
//! by name, applied over the preset's values: a mod or a player can change one setting without code); `--preset
//! remaster|original` overrides the preset for a run (tool runs then use the bare preset, without the options'
//! changes, so benches are reproducible).
//!
//! API: [`GraphicsPlugin`]; [`GraphicsSettings`] (resource, the settings in effect); [`resolve`] (a preset plus
//! changes); [`texture_path`] / [`world_sampler`] for asset loading (textures.rs). Libraries: Bevy's SSAO (an
//! XeGTAO implementation), TAA, SMAA, contrast-adaptive sharpening, PCSS soft shadows, bloom.
//!
//! The far clip with a depth prepass (TAA, SSAO, soft particles): farclip.rs has the sky fill the pixels the game
//! material clips, whose depth the prepass holds, so the sky shows beyond the clip as in the Original.

mod farclip;
mod textures;

pub use textures::{texture_path, world_sampler};

use bevy::anti_alias::contrast_adaptive_sharpening::ContrastAdaptiveSharpening;
use bevy::anti_alias::smaa::{Smaa, SmaaPreset};
use bevy::anti_alias::taa::TemporalAntiAliasing;
use bevy::camera::Hdr;
use bevy::render::camera::{MipBias, TemporalJitter};
use bevy::core_pipeline::prepass::{DepthPrepass, MotionVectorPrepass, NormalPrepass};
use bevy::light::{CascadeShadowConfig, CascadeShadowConfigBuilder, DirectionalLightShadowMap, ShadowFilteringMethod};
use bevy::pbr::{ScreenSpaceAmbientOcclusion, ScreenSpaceAmbientOcclusionQualityLevel};
use bevy::post_process::bloom::{Bloom, BloomCompositeMode, BloomPrefilter};
use bevy::prelude::*;
use serde::{Deserialize, Serialize};
use std::sync::RwLock;

#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Debug, Default, Hash)]
#[serde(rename_all = "lowercase")]
pub enum Preset {
    #[default]
    Remaster,
    Original,
}

impl Preset {
    pub fn parse(s: &str) -> Option<Self> {
        serde_json::from_value(serde_json::Value::from(s.to_lowercase())).ok()
    }
    pub fn name(self) -> &'static str {
        match self {
            Preset::Remaster => "Remaster",
            Preset::Original => "Original",
        }
    }
}

#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Debug, Hash)]
#[serde(rename_all = "lowercase")]
pub enum AntiAliasing {
    Off,
    /// 4x multisampling (the engine's look before the presets)
    Msaa,
    /// Bevy's SMAA (high): no history, no ghosting
    Smaa,
    /// Bevy's TAA: also smooths shading, shadow and texture shimmer; sharpened afterwards
    Taa,
}

impl AntiAliasing {
    pub const ALL: [AntiAliasing; 4] = [AntiAliasing::Off, AntiAliasing::Msaa, AntiAliasing::Smaa, AntiAliasing::Taa];
    pub fn name(self) -> &'static str {
        match self {
            AntiAliasing::Off => "Off",
            AntiAliasing::Msaa => "MSAA 4x",
            AntiAliasing::Smaa => "SMAA",
            AntiAliasing::Taa => "TAA",
        }
    }
}

/// The settings in effect (a preset with the options' changes, or the bare preset in a tool run).
#[derive(Resource, Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct GraphicsSettings {
    pub preset: Preset,
    /// the AI retextures (work/remaster/<level>/textures_remaster, tools/remaster_textures.py) where one exists;
    /// read when the level loads
    pub remaster_textures: bool,
    /// texture name parts that keep the original even with remaster textures (an A/B of one texture, or a
    /// retexture a player or mod does not want), as the hook's [overrides] exclude
    pub remaster_textures_exclude: Vec<String>,
    /// screen-space ambient occlusion on the ambient and fill light (not the sun, not emissive)
    pub ambient_occlusion: bool,
    /// 0 low .. 3 ultra (Bevy's quality levels)
    pub ao_quality: u8,
    /// sample radius in game units (1 unit = 0.2 m)
    pub ao_radius: f32,
    /// 0..1: how much of the occlusion applies
    pub ao_strength: f32,
    /// texture name parts whose surfaces get no occlusion: snow has its own illumination (the hook's XeGTAO
    /// darkened the snow; docs/player-feedback.md). Matched against a material's base, second and normal maps.
    pub ao_exempt: Vec<String>,
    pub anti_aliasing: AntiAliasing,
    /// contrast-adaptive sharpening after TAA / SMAA (0 = off)
    pub sharpening: f32,
    /// anisotropic filtering of the world's textures (1 = off), read when the level loads
    pub anisotropy: u16,
    /// the same filtering for effect and decal textures too (the Original keeps their plain trilinear sampling)
    pub anisotropic_fx: bool,
    /// the sun's shadow map, per cascade (power of two)
    pub shadow_map_size: u32,
    pub shadow_cascades: usize,
    /// the shadows' reach: 0 = the level's own cascades (level.rs: half the level's extent); above 0, this multiple of
    /// the world's far clip (the original camera's far plane, options.rs world_far), so every shadow in view is in
    /// a cascade (none appears late) and none is spent beyond what is drawn
    pub shadow_distance: f32,
    /// percentage-closer soft shadows: a penumbra growing with the distance to the caster
    pub soft_shadows: bool,
    /// the light's size for PCSS (texels; larger = softer)
    pub shadow_softness: f32,
    /// particles fade where they meet geometry over this depth (game units; 0 = the original's hard edge)
    pub soft_particles: f32,
    /// the far clip (the original camera's far plane) dissolves over its last part instead of cutting hard
    pub far_fade: bool,
    /// bloom intensity on what is brighter than white (stacked additive effects; 0 = off, no HDR target)
    pub bloom: f32,
}

impl GraphicsSettings {
    /// The engine's faithful look (as before the presets: MSAA 4x, 16x anisotropic world textures, Bevy's default
    /// 2048 shadow cascades with the hard Gaussian filter).
    pub fn original() -> Self {
        GraphicsSettings {
            preset: Preset::Original,
            remaster_textures: false,
            remaster_textures_exclude: Vec::new(),
            ambient_occlusion: false,
            ao_quality: 2,
            ao_radius: 4.0,
            ao_strength: 1.0,
            ao_exempt: vec!["ground".into(), "snow".into()],
            anti_aliasing: AntiAliasing::Msaa,
            sharpening: 0.0,
            anisotropy: 16,
            anisotropic_fx: false,
            shadow_map_size: 2048,
            shadow_cascades: 4,
            shadow_distance: 0.0,
            soft_shadows: false,
            shadow_softness: 6.0,
            soft_particles: 0.0,
            far_fade: false,
            bloom: 0.0,
        }
    }

    pub fn remaster() -> Self {
        GraphicsSettings {
            preset: Preset::Remaster,
            remaster_textures: true,
            ambient_occlusion: true,
            anti_aliasing: AntiAliasing::Taa,
            sharpening: 0.35,
            anisotropic_fx: true,
            shadow_map_size: 4096,
            shadow_distance: 1.0,
            soft_shadows: true,
            soft_particles: 6.0,
            far_fade: true,
            bloom: 0.08,
            ..Self::original()
        }
    }

    pub fn of(preset: Preset) -> Self {
        match preset {
            Preset::Remaster => Self::remaster(),
            Preset::Original => Self::original(),
        }
    }
}

impl Default for GraphicsSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// A preset with changes (options.json `graphics`), key by key: a change of the wrong type or an unknown key is
/// skipped (logged), as GameOptions::from_json treats the options file.
pub fn resolve(preset: Preset, changes: &serde_json::Map<String, serde_json::Value>) -> GraphicsSettings {
    let base = GraphicsSettings::of(preset);
    let Ok(serde_json::Value::Object(mut merged)) = serde_json::to_value(&base) else { return base };
    for (k, v) in changes {
        if k == "preset" || !merged.contains_key(k) {
            continue;
        }
        let mut trial = merged.clone();
        trial.insert(k.clone(), v.clone());
        if serde_json::from_value::<GraphicsSettings>(serde_json::Value::Object(trial)).is_ok() {
            merged.insert(k.clone(), v.clone());
        } else {
            warn!("graphics: {k} is of the wrong type, kept the preset's value");
        }
    }
    serde_json::from_value(serde_json::Value::Object(merged)).unwrap_or(base)
}

/// Sets one setting in a changes map; a value equal to the preset's own is removed (the map keeps only changes).
pub fn set_change(preset: Preset, changes: &mut serde_json::Map<String, serde_json::Value>, key: &str, value: serde_json::Value) {
    let base = serde_json::to_value(GraphicsSettings::of(preset)).unwrap_or_default();
    if base.get(key) == Some(&value) {
        changes.remove(key);
    } else {
        changes.insert(key.to_owned(), value);
    }
}

/// The Graphics page's steppers and checkboxes (gamemenu.rs) over the options: the preset (Remaster / Original;
/// "Custom" while any setting differs from it), the anti-aliasing, and any boolean setting by name.
pub mod menu {
    use super::*;
    use crate::options::GameOptions;

    pub const PRESETS: [Preset; 2] = [Preset::Remaster, Preset::Original];

    pub fn preset_index(o: &GameOptions) -> usize {
        PRESETS.iter().position(|p| *p == o.graphics_preset).unwrap_or(0)
    }

    /// Choosing a preset drops the single changes (the preset's own values again).
    pub fn set_preset(o: &mut GameOptions, i: usize) {
        o.graphics_preset = PRESETS[i.min(PRESETS.len() - 1)];
        o.graphics.clear();
    }

    pub fn preset_text(o: &GameOptions) -> String {
        if o.graphics.is_empty() { o.graphics_preset.name().to_owned() } else { format!("{} (custom)", o.graphics_preset.name()) }
    }

    pub fn aa_index(o: &GameOptions) -> usize {
        let aa = resolve(o.graphics_preset, &o.graphics).anti_aliasing;
        AntiAliasing::ALL.iter().position(|a| *a == aa).unwrap_or(0)
    }

    pub fn set_aa(o: &mut GameOptions, i: usize) {
        let aa = AntiAliasing::ALL[i.min(AntiAliasing::ALL.len() - 1)];
        set_change(o.graphics_preset, &mut o.graphics, "anti_aliasing", serde_json::to_value(aa).unwrap_or_default());
    }

    pub fn aa_text(o: &GameOptions) -> String {
        AntiAliasing::ALL[aa_index(o)].name().to_owned()
    }

    fn value(s: &GraphicsSettings, key: &str) -> Option<serde_json::Value> {
        serde_json::to_value(s).ok().and_then(|v| v.get(key).cloned())
    }

    /// A setting as a checkbox by name: a boolean, or a number that is on above 0 (false for unknown names).
    pub fn flag(o: &GameOptions, key: &str) -> bool {
        match value(&resolve(o.graphics_preset, &o.graphics), key) {
            Some(serde_json::Value::Bool(b)) => b,
            Some(serde_json::Value::Number(n)) => n.as_f64().unwrap_or(0.0) > 0.0,
            _ => false,
        }
    }

    /// Flips a checkbox setting; a number turns off to 0 and on to the Remaster preset's value.
    pub fn toggle(o: &mut GameOptions, key: &str) {
        let v = match value(&resolve(o.graphics_preset, &o.graphics), key) {
            Some(serde_json::Value::Bool(b)) => (!b).into(),
            Some(serde_json::Value::Number(n)) if n.as_f64().unwrap_or(0.0) > 0.0 => 0.0.into(),
            Some(serde_json::Value::Number(_)) => value(&GraphicsSettings::remaster(), key).unwrap_or(1.0.into()),
            _ => return,
        };
        set_change(o.graphics_preset, &mut o.graphics, key, v);
    }
}

/// How this run chooses its settings: a tool run (or --preset) is pinned to a bare preset, plus any `--graphics
/// key=value,...` changes (A/B tests, benches); a normal launch follows the options (and the Graphics page) live.
#[derive(Resource, Clone, Debug)]
pub struct GraphicsLaunch {
    pub pinned: Option<Preset>,
    pub changes: serde_json::Map<String, serde_json::Value>,
}

impl GraphicsLaunch {
    /// `--preset` wins; else a tool run is Original; else the options decide. `changes` ("key=value,...", values as
    /// JSON or bare words) apply over a pinned preset.
    pub fn new(preset_arg: Option<Preset>, tool_run: bool, changes: Option<&str>) -> Self {
        let mut map = serde_json::Map::new();
        for kv in changes.unwrap_or("").split(',').filter(|s| !s.is_empty()) {
            let (k, v) = kv.split_once('=').unwrap_or((kv, "true"));
            let v = serde_json::from_str(v).unwrap_or_else(|_| serde_json::Value::from(v));
            map.insert(k.trim().to_owned(), v);
        }
        GraphicsLaunch { pinned: preset_arg.or(if tool_run { Some(Preset::Original) } else { None }), changes: map }
    }

    /// The settings at start-up (the textures and samplers are chosen once, when the level loads).
    pub fn initial(&self, options: &crate::options::GameOptions) -> GraphicsSettings {
        match self.pinned {
            Some(p) => resolve(p, &self.changes),
            None => resolve(options.graphics_preset, &options.graphics),
        }
    }
}

/// The settings for code without world access (asset loading helpers in scene.rs / gamematerial.rs): a copy of the
/// resource, kept in step by `sync`. Deliberate: those helpers run inside level spawning with only the AssetServer.
static CURRENT: RwLock<Option<GraphicsSettings>> = RwLock::new(None);

/// The settings in effect (the Original's when none are set yet: unit tests, tools).
pub fn current() -> GraphicsSettings {
    CURRENT.read().ok().and_then(|c| c.clone()).unwrap_or_else(GraphicsSettings::original)
}

fn publish(s: &GraphicsSettings) {
    if let Ok(mut c) = CURRENT.write() {
        *c = Some(s.clone());
    }
}

/// Whether a material with these textures is exempt from ambient occlusion (`ao_exempt`).
pub fn ao_exempt(textures: &[Option<&str>]) -> bool {
    let s = current();
    textures.iter().flatten().any(|t| {
        let t = t.to_lowercase();
        s.ao_exempt.iter().any(|p| !p.is_empty() && t.contains(&p.to_lowercase()))
    })
}

pub struct GraphicsPlugin {
    pub launch: GraphicsLaunch,
    pub initial: GraphicsSettings,
}

impl GraphicsPlugin {
    /// Prepares the settings before the app is built: the texture set and the default sampler depend on them.
    pub fn new(launch: GraphicsLaunch, options: &crate::options::GameOptions, scene: &std::path::Path) -> Self {
        let initial = launch.initial(options);
        publish(&initial);
        textures::init(scene, &initial);
        GraphicsPlugin { launch, initial }
    }

    /// The default image sampler (effects, decals without their own): anisotropic in the Remaster.
    pub fn image_plugin(&self) -> ImagePlugin {
        if self.initial.anisotropic_fx && self.initial.anisotropy > 1 {
            ImagePlugin { default_sampler: textures::fx_sampler(self.initial.anisotropy), ..default() }
        } else {
            ImagePlugin::default()
        }
    }
}

impl Plugin for GraphicsPlugin {
    fn build(&self, app: &mut App) {
        info!(
            "graphics: {} preset{}{}",
            self.initial.preset.name(),
            if self.launch.pinned.is_some() { " (pinned for this run)" } else { "" },
            if self.initial.remaster_textures { format!(", {} remaster textures", textures::count()) } else { String::new() }
        );
        app.insert_resource(self.launch.clone())
            .insert_resource(self.initial.clone())
            .insert_resource(DirectionalLightShadowMap { size: self.initial.shadow_map_size as usize })
            .add_plugins(farclip::FarClipPlugin)
            .add_systems(PostUpdate, (sync, apply_cameras, apply_shadows, apply_materials).chain());
    }
}

/// A normal launch follows the options (the Graphics page applies at once); a pinned run keeps its preset.
fn sync(launch: Res<GraphicsLaunch>, options: Res<crate::options::GameOptions>, mut settings: ResMut<GraphicsSettings>) {
    if launch.pinned.is_some() || !options.is_changed() {
        return;
    }
    let want = resolve(options.graphics_preset, &options.graphics);
    if *settings != want {
        info!("graphics: settings changed ({} preset)", want.preset.name());
        *settings = want;
    }
}

/// The camera's components for the anti-aliasing, AO, HDR and bloom settings. The Original adds nothing to what the
/// camera had before the presets (Msaa 4x, no prepass); switching back removes everything a remaster setting added.
#[allow(clippy::type_complexity)]
fn apply_cameras(
    mut commands: Commands,
    s: Res<GraphicsSettings>,
    cams: Query<Entity, With<Camera3d>>,
    others: Query<Entity, (With<Camera>, Without<Camera3d>)>,
    new_cams: Query<(), Added<Camera>>,
) {
    if !s.is_changed() && new_cams.is_empty() {
        return;
    }
    publish(&s);
    let ao = s.ambient_occlusion && s.anti_aliasing != AntiAliasing::Msaa;
    if s.ambient_occlusion && !ao {
        warn!("graphics: ambient occlusion needs an anti-aliasing other than MSAA (Bevy's SSAO); off");
    }
    let hdr = s.bloom > 0.0;
    let msaa = match s.anti_aliasing {
        AntiAliasing::Msaa => Msaa::Sample4,
        _ => Msaa::Off,
    };
    for e in &cams {
        let mut c = commands.entity(e);
        // start from the Original's camera, then add
        c.remove::<(TemporalAntiAliasing, TemporalJitter, MipBias, Smaa, ContrastAdaptiveSharpening, ScreenSpaceAmbientOcclusion)>()
            .remove::<(DepthPrepass, NormalPrepass, MotionVectorPrepass, Bloom, Hdr, ShadowFilteringMethod)>();
        c.insert(msaa);
        match s.anti_aliasing {
            AntiAliasing::Taa => {
                c.insert((TemporalAntiAliasing::default(), ShadowFilteringMethod::Temporal));
            }
            AntiAliasing::Smaa => {
                c.insert(Smaa { preset: SmaaPreset::High });
            }
            _ => {}
        }
        if s.sharpening > 0.0 && matches!(s.anti_aliasing, AntiAliasing::Taa | AntiAliasing::Smaa) {
            c.insert(ContrastAdaptiveSharpening { enabled: true, sharpening_strength: s.sharpening.min(1.0), denoise: false });
        }
        if ao {
            let quality_level = match s.ao_quality {
                0 => ScreenSpaceAmbientOcclusionQualityLevel::Low,
                1 => ScreenSpaceAmbientOcclusionQualityLevel::Medium,
                2 => ScreenSpaceAmbientOcclusionQualityLevel::High,
                _ => ScreenSpaceAmbientOcclusionQualityLevel::Ultra,
            };
            c.insert(ScreenSpaceAmbientOcclusion { quality_level, radius: s.ao_radius.max(0.01), constant_object_thickness: s.ao_radius * 0.3 });
        }
        // soft particles read the depth prepass
        if s.soft_particles > 0.0 {
            c.insert(DepthPrepass);
        }
        if hdr {
            // only what is brighter than white blooms (the game's surfaces are clamped to white: gamematerial.wesl),
            // added on top as the original's additive effects are
            c.insert((
                Hdr,
                Bloom {
                    intensity: s.bloom,
                    composite_mode: BloomCompositeMode::Additive,
                    prefilter: BloomPrefilter { threshold: 1.0, threshold_softness: 0.3 },
                    ..Bloom::OLD_SCHOOL
                },
            ));
        }
    }
    // the HUD and movie cameras draw over the 3D view: the same target format and sample count, or Bevy gives them
    // a main texture of their own. The HUD camera never clears (ClearColorConfig::None), so its own texture kept the
    // title menu's art after New Game and was blended over the world (docs/player-feedback.md 22: Remaster only,
    // where the 3D camera's TAA turns MSAA off while a 2D camera's default is 4x)
    for e in &others {
        commands.entity(e).insert(msaa);
        if hdr {
            commands.entity(e).insert(Hdr);
        } else {
            commands.entity(e).remove::<Hdr>();
        }
    }
}

/// The level's own cascade set-up, kept when a remaster setting changes it.
#[derive(Component, Clone)]
struct LevelCascades(CascadeShadowConfig);

/// Shadow map size, cascades and their reach, soft shadows: on the lights level.rs spawns (ShadowLight).
#[allow(clippy::type_complexity)]
fn apply_shadows(
    mut commands: Commands,
    s: Res<GraphicsSettings>,
    mut map: ResMut<DirectionalLightShadowMap>,
    mut lights: Query<(Entity, &mut DirectionalLight, &CascadeShadowConfig, Option<&LevelCascades>), With<crate::options::ShadowLight>>,
    new_lights: Query<(), Added<crate::options::ShadowLight>>,
    (options, game_lights): (Res<crate::options::GameOptions>, Option<Res<crate::gamematerial::GameLights>>),
) {
    if !s.is_changed() && new_lights.is_empty() && !options.is_changed() {
        return;
    }
    let size = s.shadow_map_size.clamp(512, 8192).next_power_of_two() as usize;
    if map.size != size {
        map.size = size;
    }
    let fog_far = game_lights.as_ref().and_then(|l| l.fog.map(|f| f.1)).unwrap_or(1500.0);
    let world_far = crate::options::world_far(fog_far, options.draw_distance);
    for (e, mut l, cfg, level) in &mut lights {
        let level = match level {
            Some(l) => l.0.clone(),
            None => {
                commands.entity(e).insert(LevelCascades(cfg.clone()));
                cfg.clone()
            }
        };
        l.soft_shadow_size = (s.soft_shadows).then_some(s.shadow_softness);
        if s.shadow_distance <= 0.0 {
            commands.entity(e).insert(level);
            continue;
        }
        let reach = world_far * s.shadow_distance;
        let cfg = CascadeShadowConfigBuilder {
            num_cascades: s.shadow_cascades.clamp(1, 4),
            first_cascade_far_bound: 60.0,
            maximum_distance: reach,
            overlap_proportion: 0.3,
            ..default()
        }
        .build();
        commands.entity(e).insert(cfg);
    }
}

/// Per-material remaster values: the game material's occlusion weight and far dissolve, the particles' softness.
fn apply_materials(
    s: Res<GraphicsSettings>,
    mut game: ResMut<Assets<crate::gamematerial::GameMaterial>>,
    mut particles: ResMut<Assets<crate::fxvm::ParticleMaterial>>,
    mut game_events: MessageReader<AssetEvent<crate::gamematerial::GameMaterial>>,
    mut particle_events: MessageReader<AssetEvent<crate::fxvm::ParticleMaterial>>,
) {
    let changed = s.is_changed();
    let new_game: Vec<_> = game_events.read().filter_map(|e| if let AssetEvent::Added { id } = e { Some(*id) } else { None }).collect();
    let new_particles: Vec<_> = particle_events.read().filter_map(|e| if let AssetEvent::Added { id } = e { Some(*id) } else { None }).collect();
    let far = if s.far_fade { 1.0 } else { 0.0 };
    let want = |m: &crate::gamematerial::GameMaterial| Vec2::new(if m.ao_exempt { 0.0 } else { s.ao_strength }, far);
    let ids: Vec<_> = if changed { game.ids().collect() } else { new_game };
    for id in ids {
        if game.get(id).is_some_and(|m| m.params.remaster.xy() != want(m)) {
            if let Some(mut m) = game.get_mut(id) {
                let w = want(&m);
                m.params.remaster.x = w.x;
                m.params.remaster.y = w.y;
            }
        }
    }
    let soft = s.soft_particles.max(0.0);
    let ids: Vec<_> = if changed { particles.ids().collect() } else { new_particles };
    for id in ids {
        if particles.get(id).is_some_and(|m| m.params.y != soft) {
            if let Some(mut m) = particles.get_mut(id) {
                m.params.y = soft;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The Original is the engine's look before the presets; the Remaster turns the goodies on.
    #[test]
    fn presets() {
        let o = GraphicsSettings::original();
        assert_eq!((o.remaster_textures, o.ambient_occlusion, o.anti_aliasing, o.soft_particles, o.bloom, o.far_fade), (false, false, AntiAliasing::Msaa, 0.0, 0.0, false));
        assert_eq!((o.anisotropy, o.shadow_map_size), (16, 2048));
        let r = GraphicsSettings::remaster();
        assert!(r.remaster_textures && r.ambient_occlusion && r.soft_shadows && r.far_fade);
        assert_eq!(r.anti_aliasing, AntiAliasing::Taa);
        assert_eq!(Preset::parse("ORIGINAL"), Some(Preset::Original));
        assert_eq!(Preset::parse("nope"), None);
    }

    /// Changes apply key by key over the preset; wrong types and unknown keys are skipped.
    #[test]
    fn changes() {
        let mut m = serde_json::Map::new();
        m.insert("ambient_occlusion".into(), false.into());
        m.insert("anti_aliasing".into(), "smaa".into());
        m.insert("shadow_map_size".into(), "big".into());
        m.insert("no_such".into(), 1.into());
        let s = resolve(Preset::Remaster, &m);
        assert!(!s.ambient_occlusion);
        assert_eq!(s.anti_aliasing, AntiAliasing::Smaa);
        assert_eq!(s.shadow_map_size, 4096);
        assert!(s.remaster_textures);
        // a value equal to the preset's is not kept as a change
        set_change(Preset::Remaster, &mut m, "ambient_occlusion", true.into());
        assert!(!m.contains_key("ambient_occlusion"));
        set_change(Preset::Original, &mut m, "bloom", 0.2.into());
        assert_eq!(resolve(Preset::Original, &m).bloom, 0.2);
    }

    /// The Graphics page: a preset clears the changes; toggles and the AA stepper store only differences.
    #[test]
    fn menu_rows() {
        let mut o = crate::options::GameOptions::default();
        assert_eq!(menu::preset_text(&o), "Remaster");
        menu::toggle(&mut o, "ambient_occlusion");
        assert!(!menu::flag(&o, "ambient_occlusion"));
        assert_eq!(menu::preset_text(&o), "Remaster (custom)");
        menu::toggle(&mut o, "ambient_occlusion");
        assert!(o.graphics.is_empty());
        menu::set_aa(&mut o, 2);
        assert_eq!(menu::aa_text(&o), "SMAA");
        menu::set_preset(&mut o, 1);
        assert_eq!((o.graphics_preset, o.graphics.len(), menu::aa_text(&o).as_str()), (Preset::Original, 0, "MSAA 4x"));
    }

    /// Tool runs are pinned to the Original unless --preset says otherwise.
    #[test]
    fn launch() {
        let o = crate::options::GameOptions::default();
        assert_eq!(GraphicsLaunch::new(None, true, None).initial(&o).preset, Preset::Original);
        assert_eq!(GraphicsLaunch::new(Some(Preset::Remaster), true, None).initial(&o).preset, Preset::Remaster);
        assert_eq!(GraphicsLaunch::new(None, false, None).initial(&o).preset, Preset::Remaster);
        let ab = GraphicsLaunch::new(Some(Preset::Remaster), true, Some("ambient_occlusion=false,anti_aliasing=smaa,bloom=0"));
        let s = ab.initial(&o);
        assert_eq!((s.ambient_occlusion, s.anti_aliasing, s.bloom), (false, AntiAliasing::Smaa, 0.0));
    }
}
