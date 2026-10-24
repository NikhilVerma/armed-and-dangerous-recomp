//! Presentation settings: what the renderer adds on top of the original's look, as two presets (docs/decision.md
//! 2026-10-15) and per-setting changes in options.json.
//!
//! - **Original**: the engine's faithful look (the original's textures, its lighting and fog, MSAA). Every tool run
//!   (--play, --snap, --camera, --bench, --background, --size) uses it, so comparisons with the original and the
//!   playtest suite never see a remaster feature.
//! - **Remaster** (the default for a normal launch, what the player plays): the AI retextures, screen-space ambient
//!   occlusion with snow exempt, temporal anti-aliasing with sharpening, larger soft shadows covering the whole view,
//!   soft particles, a dissolve instead of a hard cut at the far clip, bloom on over-bright effects only, GPU
//!   snowfall in place of the camera's snow cylinders (snow.rs).
//!
//! The game rules never depend on these (no setting reaches the living: AI, Roman, timing). The bodies' settings
//! (`corpse_*`, `ragdoll_limit`: how long the dead lie and how many ragdoll, corpses.rs) touch only the dead.
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

mod bc_decode;
mod farclip;
pub mod geometry;
pub mod snow;
mod textures;
// the environment cube maps of ice, water and metal (2026-10-23)
pub mod env_map;
// the old remaster's atmosphere, AO shaping and output passes (agent-port-atmos, 2026-10-19)
pub mod ao_extras;
mod effects_log;
pub mod height_fog;
pub mod motion_blur;
pub mod present;

pub use textures::{set_level as set_level_textures, texture_path, ui_texture_path, world_sampler};

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
    /// 0 low .. 3 ultra (Bevy's quality levels: XeGTAO's slices x steps, High = 3 x 3 as the old remaster)
    pub ao_quality: u8,
    /// XeGTAO's radius in game units (1 unit = 0.2 m; the effect reaches 1.457 times it): the old remaster's 24
    /// ("below ~20 the term is nearly white at the game's scale", archive/hook/enhance/shadows.cpp)
    pub ao_radius: f32,
    /// 0..1: how much of the occlusion applies
    pub ao_strength: f32,
    /// texture name parts of the snow surfaces, which keep only `ao_snow` of the occlusion (0: none): snow has its
    /// own illumination (the hook's XeGTAO darkened the snow; docs/player-feedback.md). Matched against a material's
    /// base, second and normal maps.
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
    /// alpha-textured casters (pine branch cards, leaves) cast shadows only where their texture's alpha reaches this
    /// (gamematerial_prepass.wesl); 0 = the whole card, as the original's silhouettes (shadows.md: drawn with the
    /// override material 0x005fe498, flags 0x020a3060, blend bits 8-11 = 0: no alpha test; static reading)
    pub shadow_alpha_cutoff: f32,
    /// particles fade where they meet geometry over this depth (game units; 0 = the original's hard edge)
    pub soft_particles: f32,
    /// the far clip (the original camera's far plane) dissolves over its last part instead of cutting hard
    pub far_fade: bool,
    /// bloom intensity on what is brighter than white (stacked additive effects; 0 = off, no HDR target)
    pub bloom: f32,
    /// dead bodies lying at once: one more makes the oldest fade (0 = no cap, the original's: bodies go by their
    /// timers alone; corpses.rs)
    pub corpse_keep: u32,
    /// seconds a body lies before it is removed (0 = the original's: a ragdoll 5 s, an animated death 0.5 s after its
    /// anim; Corpse_CountDown 0x0042eba0)
    pub corpse_time: f32,
    /// the body's fade at the end, in seconds (0 = the original's 0.5)
    pub corpse_fade: f32,
    /// ragdolls down at once; a death beyond it is animated (the original's 4: Character_GoRagdoll 0x00431310)
    pub ragdoll_limit: u32,

    // ---- geometry, surface detail and shadow tone, ported from the old remaster (geometry/mod.rs) ----
    /// tessellation*, terrain_detail*, lod_max, lod_scale, alpha_coverage, aa_mip_bias, shadow_strength
    #[serde(flatten)]
    pub geometry: geometry::GeometrySettings,
    // --- engine limits and input (the old remaster's [limits] / [persistence] / [input] options; each group's
    // settings and presets live with the module that applies them). Flattened: options.json names each setting
    // directly ("physics_fixed_step": false, "decal_pool": 512, ...).
    #[serde(flatten)]
    pub physics: crate::physicsstep::PhysicsStepSettings,
    #[serde(flatten)]
    pub pools: crate::pools::PoolSettings,
    #[serde(flatten)]
    pub debris: crate::debris::DebrisSettings,
    #[serde(flatten)]
    pub cursor: crate::cursor::CursorSettings,
    // --- end of engine limits and input
    /// the display, view and UI-layout settings (display/mod.rs: fov, hor_plus, letterbox, fps_limit, render_scale,
    /// ...), flattened: one key each in the options' `graphics` changes
    #[serde(flatten)]
    pub display: crate::display::DisplaySettings,
    // ---- the old remaster's atmosphere, AO shaping and output passes (agent-port-atmos; each module's doc says
    // where it came from). Flattened: the options keep the old and.ini names (fog_modern, shutter_speed, ...).
    #[serde(flatten)]
    pub height_fog: height_fog::HeightFogSettings,
    #[serde(flatten)]
    pub ao_extras: ao_extras::AoExtras,
    #[serde(flatten)]
    pub motion_blur: motion_blur::MotionBlurSettings,
    #[serde(flatten)]
    pub present: present::PresentSettings,
    // --- GPU snowfall (snow.rs), flattened: options.json says "snow", "snow_amount", "snow_tuning"
    #[serde(flatten)]
    pub snowfall: snow::SnowSettings,
    // --- the environment cube maps (env_map.rs): "env_maps", "env_map_size"
    #[serde(flatten)]
    pub env_map: env_map::EnvMapSettings,
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
            ao_radius: 24.0,
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
            shadow_alpha_cutoff: 0.0,
            soft_particles: 0.0,
            far_fade: false,
            bloom: 0.0,
            corpse_keep: 0,
            corpse_time: 0.0,
            corpse_fade: 0.0,
            ragdoll_limit: 4,
            geometry: geometry::GeometrySettings::original(),
            physics: crate::physicsstep::PhysicsStepSettings::original(),
            pools: crate::pools::PoolSettings::original(),
            debris: crate::debris::DebrisSettings::original(),
            cursor: crate::cursor::CursorSettings::original(),
            display: crate::display::DisplaySettings::original(),
            height_fog: height_fog::HeightFogSettings::original(),
            ao_extras: ao_extras::AoExtras::original(),
            motion_blur: motion_blur::MotionBlurSettings::original(),
            present: present::PresentSettings::original(),
            snowfall: snow::SnowSettings::original(),
            env_map: env_map::EnvMapSettings::original(),
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
            shadow_alpha_cutoff: 0.5,
            soft_particles: 6.0,
            far_fade: true,
            bloom: 0.08,
            corpse_keep: 48,
            corpse_time: 60.0,
            corpse_fade: 1.5,
            ragdoll_limit: 16,
            geometry: geometry::GeometrySettings::remaster(),
            physics: crate::physicsstep::PhysicsStepSettings::remaster(),
            pools: crate::pools::PoolSettings::remaster(),
            debris: crate::debris::DebrisSettings::remaster(),
            cursor: crate::cursor::CursorSettings::remaster(),
            display: crate::display::DisplaySettings::remaster(),
            height_fog: height_fog::HeightFogSettings::remaster(),
            ao_extras: ao_extras::AoExtras::remaster(),
            motion_blur: motion_blur::MotionBlurSettings::remaster(),
            present: present::PresentSettings::remaster(),
            snowfall: snow::SnowSettings::remaster(),
            env_map: env_map::EnvMapSettings::remaster(),
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
    let (settings, rejected) = resolve_checked(preset, changes);
    for r in rejected {
        warn!("graphics: {r}");
    }
    settings
}

/// [`resolve`], also returning why each skipped change was skipped (an unknown name, a value of the wrong type), for
/// callers that run before the log is up (`--graphics`, [`GraphicsLaunch::new`]).
pub fn resolve_checked(preset: Preset, changes: &serde_json::Map<String, serde_json::Value>) -> (GraphicsSettings, Vec<String>) {
    let base = GraphicsSettings::of(preset);
    let mut rejected = Vec::new();
    let Ok(serde_json::Value::Object(mut merged)) = serde_json::to_value(&base) else { return (base, rejected) };
    for (k, v) in changes {
        if k == "preset" {
            continue;
        }
        if !merged.contains_key(k) {
            // a misspelt or retired setting would otherwise vanish without a word
            rejected.push(format!("unknown setting {k}, ignored"));
            continue;
        }
        let mut trial = merged.clone();
        trial.insert(k.clone(), v.clone());
        if serde_json::from_value::<GraphicsSettings>(serde_json::Value::Object(trial)).is_ok() {
            merged.insert(k.clone(), v.clone());
        } else {
            rejected.push(format!("{k} = {v} is of the wrong type, kept the preset's value"));
        }
    }
    (serde_json::from_value(serde_json::Value::Object(merged)).unwrap_or(base), rejected)
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

    /// A numeric setting's stepper range (first, last, positions), for the settings that have one
    /// (geometry::STEPPERS, snow::STEPPERS).
    pub fn stepper(key: &str) -> Option<(f32, f32, usize)> {
        geometry::STEPPERS.iter().chain(snow::STEPPERS.iter()).find(|s| s.0 == key).map(|s| (s.1, s.2, s.3))
    }

    /// A numeric setting's value (0 for unknown names).
    pub fn number(o: &GameOptions, key: &str) -> f32 {
        value(&resolve(o.graphics_preset, &o.graphics), key).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32
    }

    /// Sets a numeric setting, as an integer when the setting is one.
    pub fn set_number(o: &mut GameOptions, key: &str, v: f32) {
        let v = match value(&resolve(o.graphics_preset, &o.graphics), key) {
            Some(serde_json::Value::Number(n)) if n.is_u64() || n.is_i64() => serde_json::Value::from(v.round() as i64),
            Some(serde_json::Value::Number(_)) => serde_json::Value::from(v as f64),
            _ => return,
        };
        set_change(o.graphics_preset, &mut o.graphics, key, v);
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
        // the command line is read before the log starts: say on stderr what will not apply
        for r in resolve_checked(preset_arg.unwrap_or_default(), &map).1 {
            eprintln!("--graphics: {r}");
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

/// The ambient occlusion's strength on a game material: none on blended surfaces (not in the depth prepass: the
/// occlusion there would be the surface's behind them).
pub fn ao_weight(s: &GraphicsSettings, blended: bool) -> f32 {
    if blended { 0.0 } else { s.ao_strength }
}

/// Whether a material with these textures is a snow surface for the ambient occlusion (`ao_exempt`).
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
            .add_plugins((height_fog::HeightFogPlugin, ao_extras::AoExtrasPlugin, motion_blur::MotionBlurPlugin, present::PresentPlugin, effects_log::EffectsLogPlugin))
            .add_plugins((farclip::FarClipPlugin, geometry::GeometryPlugin, snow::SnowPlugin, env_map::EnvMapPlugin, bc_decode::BcDecodePlugin))
            .add_systems(
                PostUpdate,
                (
                    sync,
                    apply_cameras,
                    apply_shadows,
                    (
                        apply_game_materials::<crate::gamematerial::Solid>,
                        apply_game_materials::<crate::gamematerial::AlphaTested>,
                        apply_particle_materials,
                    ),
                )
                    .chain(),
            );
    }
}

/// A normal launch follows the options (the Graphics page applies at once); a pinned run keeps its preset.
pub(crate) fn sync(launch: Res<GraphicsLaunch>, options: Res<crate::options::GameOptions>, mut settings: ResMut<GraphicsSettings>) {
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
pub(crate) fn apply_cameras(
    mut commands: Commands,
    s: Res<GraphicsSettings>,
    cams: Query<Entity, (With<Camera3d>, Without<env_map::EnvProbeView>)>,
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
    // the old remaster's output pass needs the scene in floating point (present.rs: dithering before the 8-bit store)
    let hdr = s.bloom > 0.0 || s.present.needs_hdr();
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
            c.insert(ScreenSpaceAmbientOcclusion { quality_level, radius: s.ao_radius.max(0.01), constant_object_thickness: s.ao_extras.ao_thickness.max(0.0) });
        }
        // soft particles and the snowfall read the depth prepass
        if s.soft_particles > 0.0 || s.snowfall.snow {
            c.insert(DepthPrepass);
        }
        // the motion blur reads the depth and motion vectors (motion_blur.rs; not under MSAA)
        if s.motion_blur.enabled() && msaa == Msaa::Off {
            c.insert((DepthPrepass, MotionVectorPrepass));
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
    (options, game_lights, mission): (Res<crate::options::GameOptions>, Option<Res<crate::gamematerial::GameLights>>, Option<Res<crate::mission::Mission>>),
) {
    if !s.is_changed() && new_lights.is_empty() && !options.is_changed() && !mission.as_ref().is_some_and(|m| m.is_added()) {
        return;
    }
    let size = s.shadow_map_size.clamp(512, 8192).next_power_of_two() as usize;
    if map.size != size {
        map.size = size;
    }
    let fog_far = game_lights.as_ref().and_then(|l| l.fog.map(|f| f.1)).unwrap_or(1500.0);
    let world_far = crate::options::world_far(fog_far, options.level_draw_distance(mission.as_deref()));
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

/// The assets to update: all of them when the settings changed, else the ones added since the last frame.
fn ids_to_apply<A: Asset>(changed: bool, assets: &Assets<A>, events: &mut MessageReader<AssetEvent<A>>) -> Vec<AssetId<A>> {
    let added: Vec<_> = events.read().filter_map(|e| if let AssetEvent::Added { id } = e { Some(*id) } else { None }).collect();
    if changed { assets.ids().collect() } else { added }
}

/// Sets one parameter of the game materials (either coverage) to what `want` gives for each: all of them when
/// `changed`, else the ones added since the last frame. A material whose value is already right is not touched (a
/// mutable access marks it changed and Bevy re-prepares it).
pub(crate) fn set_game_material_param<C: crate::gamematerial::Coverage, T: PartialEq>(
    changed: bool,
    game: &mut Assets<crate::gamematerial::GameMaterial<C>>,
    events: &mut MessageReader<AssetEvent<crate::gamematerial::GameMaterial<C>>>,
    param: fn(&mut crate::gamematerial::GameMaterialParams) -> &mut T,
    want: impl Fn(&crate::gamematerial::GameMaterial<C>) -> T,
) {
    for id in ids_to_apply(changed, game, events) {
        let differs = game.get(id).is_some_and(|m| {
            let mut params = m.params.clone();
            *param(&mut params) != want(m)
        });
        if differs {
            if let Some(mut m) = game.get_mut(id) {
                let w = want(&m);
                *param(&mut m.params) = w;
            }
        }
    }
}

/// Per-material remaster values of the game materials (either coverage): the occlusion weight, the far dissolve
/// and the shadow's alpha cutoff (remaster.xyz), and the geometry settings' values (geometry/mod.rs `material_values`).
fn apply_game_materials<C: crate::gamematerial::Coverage>(
    s: Res<GraphicsSettings>,
    mut game: ResMut<Assets<crate::gamematerial::GameMaterial<C>>>,
    mut events: MessageReader<AssetEvent<crate::gamematerial::GameMaterial<C>>>,
) {
    let far = if s.far_fade { 1.0 } else { 0.0 };
    let cutoff = s.shadow_alpha_cutoff.clamp(0.0, 1.0);
    let want = |m: &crate::gamematerial::GameMaterial<C>| Vec3::new(ao_weight(&s, m.alpha), far, cutoff);
    let geometry = geometry::material_values(&s);
    for id in ids_to_apply(s.is_changed(), &game, &mut events) {
        // read first: a mutable access marks the material changed (re-prepared) even when nothing differs
        let geometry_of = |m: &crate::gamematerial::GameMaterial<C>| geometry::for_material(geometry, m.alpha, m.params.diffuse.w);
        if game.get(id).is_some_and(|m| m.params.remaster.xyz() != want(m) || m.params.geometry != geometry_of(m)) {
            if let Some(mut m) = game.get_mut(id) {
                let w = want(&m);
                m.params.remaster = w.extend(m.params.remaster.w);
                m.params.geometry = geometry_of(&m);
            }
        }
    }
}

/// The particles' softness.
fn apply_particle_materials(
    s: Res<GraphicsSettings>,
    mut particles: ResMut<Assets<crate::fxvm::ParticleMaterial>>,
    mut particle_events: MessageReader<AssetEvent<crate::fxvm::ParticleMaterial>>,
) {
    let soft = s.soft_particles.max(0.0);
    for id in ids_to_apply(s.is_changed(), &particles, &mut particle_events) {
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
