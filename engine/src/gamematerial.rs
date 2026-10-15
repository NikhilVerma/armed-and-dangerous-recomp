//! The original's materials (gamematerial.wesl): kinds, extra maps and colours from the exported material records
//! (tools/remaster_export.py, docs/formats/big-skin.md 3.5), the level's lights as the original lights them
//! (the sun per pixel through the normal map, the ambient-only light and the fill per vertex), the level's linear
//! fog. Rendering piece 1 (docs/remaster-engine.md): the landscape first, compared with tools/render_compare.py.

use bevy::image::{ImageAddressMode, ImageLoaderSettings, ImageSampler};
use bevy::prelude::*;
use bevy::render::render_resource::{AsBindGroup, ShaderType};
use serde::Deserialize;

#[derive(ShaderType, Debug, Clone, Default)]
pub struct GameMaterialParams {
    pub kind: u32,
    pub has_normal: u32,
    pub has_texture2: u32,
    pub has_mask: u32,
    pub emissive: Vec4,
    pub ambient: Vec4,
    pub diffuse: Vec4,
    pub specular: Vec4,
    pub env: Vec4,
    pub sun_dir: Vec4,
    pub sun_col: Vec4,
    pub fill_dir: Vec4,
    pub fill_col: Vec4,
    pub ambient_light: Vec4,
    pub fog_col: Vec4,
    pub fog_range: Vec4,
    /// presentation settings (graphics.rs; zero in the Original): x the ambient occlusion's weight (0: exempt, as
    /// snow), y 1 = dissolve at the far clip instead of a hard cut
    pub remaster: Vec4,
}

#[derive(AsBindGroup, Debug, Clone, Asset, TypePath)]
pub struct GameMaterial {
    #[texture(0)]
    #[sampler(1)]
    pub base: Handle<Image>,
    #[texture(2)]
    #[sampler(3)]
    pub normal: Handle<Image>,
    #[texture(4)]
    #[sampler(5)]
    pub texture2: Handle<Image>,
    #[texture(6)]
    #[sampler(7)]
    pub mask: Handle<Image>,
    #[uniform(8)]
    pub params: GameMaterialParams,
    pub alpha: bool,
    /// no ambient occlusion on this surface (graphics.rs `ao_exempt`: snow; blended surfaces)
    pub ao_exempt: bool,
}

impl Material for GameMaterial {
    fn vertex_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("gamematerial.wesl")).with_source("embedded"))
    }
    fn fragment_shader() -> bevy::shader::ShaderRef {
        bevy::shader::ShaderRef::Path(bevy::asset::AssetPath::from_path_buf(bevy::asset::embedded_path!("gamematerial.wesl")).with_source("embedded"))
    }
    // No prepass fragment shader of its own: one makes Bevy bind the material in every depth-only pass (the sun's
    // four shadow cascades lost their batching: 4K 113 -> 64 fps, 2026-10-15). The far clip in the remaster's depth
    // prepass is handled by culling at the far plane instead (options.rs).
    fn alpha_mode(&self) -> AlphaMode {
        // alpha textures blend (the material log: flags bits 8-11 = 3 -> blend table 0x0057d9e0 entry 3 = SRCALPHA,
        // INVSRCALPHA, ADD, with alpha test on), not a hard 0.5 cut: thin soft twigs as in the original
        // objects the camera fades (camfade.rs: emissive.w > 0) blend too
        if self.alpha || self.params.emissive.w > 0.0 { AlphaMode::Blend } else { AlphaMode::Opaque }
    }
}

pub struct GameMaterialPlugin;

impl Plugin for GameMaterialPlugin {
    fn build(&self, app: &mut App) {
        bevy::asset::embedded_asset!(app, "gamematerial.wesl");
        app.add_plugins(MaterialPlugin::<GameMaterial>::default()).add_systems(PostUpdate, swap.run_if(resource_exists::<GameLights>));
    }
}

/// Every part spawned with a standard material that came from an original material record gets the game material
/// instead (one game material per standard one).
fn swap(
    mut commands: Commands,
    q: Query<(Entity, &MeshMaterial3d<StandardMaterial>), Added<MeshMaterial3d<StandardMaterial>>>,
    lib: Option<Res<crate::scene::Library>>,
    lights: Res<GameLights>,
    assets: Res<AssetServer>,
    mut materials: ResMut<Assets<GameMaterial>>,
    mut made: Local<std::collections::HashMap<AssetId<StandardMaterial>, Handle<GameMaterial>>>,
) {
    let Some(lib) = lib else { return };
    for (e, m) in &q {
        let Some((def, texture, alpha)) = lib.game_defs.get(&m.0.id()) else { continue };
        let h = made.entry(m.0.id()).or_insert_with(|| materials.add(make(def, texture.as_deref(), *alpha, &lights, &assets, &lights.white))).clone();
        commands.entity(e).remove::<MeshMaterial3d<StandardMaterial>>().insert(MeshMaterial3d(h));
    }
}

/// A material record as exported.
#[derive(Deserialize, Clone, Default, Debug)]
pub struct MaterialDef {
    #[serde(default)]
    pub kind: u32,
    #[serde(default)]
    pub normal_map: Option<String>,
    #[serde(default)]
    pub texture2: Option<String>,
    #[serde(default)]
    pub mask: Option<String>,
    #[serde(default)]
    pub emissive: [f32; 3],
    #[serde(default)]
    pub ambient: [f32; 3],
    #[serde(default)]
    pub diffuse: [f32; 3],
    #[serde(default)]
    pub specular: [f32; 3],
    #[serde(default)]
    pub power: f32,
}

/// The level's lighting as the original applies it (scene lights): the first directional light that is not
/// ambient-only is the sun, the next the fill; the ambient-only light gives the ambient term. Directions point
/// toward the light, engine space. Fog from global_settings.
#[derive(Resource, Clone, Default)]
pub struct GameLights {
    pub sun_dir: Vec3,
    pub sun_col: Vec3,
    pub fill_dir: Vec3,
    pub fill_col: Vec3,
    pub ambient: Vec3,
    pub fog: Option<(f32, f32, Vec3)>,
    /// the environment cube's colour (approximation: the fog colour, the sky at the horizon)
    pub env: Vec3,
    /// a white texture for the unused maps
    pub white: Handle<Image>,
}

impl GameLights {
    pub fn from_scene(scene: &crate::scene::SceneFile) -> Self {
        let mut l = GameLights::default();
        let mut dirs = Vec::new();
        for light in &scene.lights {
            let p = |k: &str| light.props.get(k).and_then(|v| v.as_f64()).unwrap_or(0.0) as f32;
            if light.props.get("lighttype").and_then(|v| v.as_str()) != Some("dir") || p("lighton") == 0.0 {
                continue;
            }
            let col = Vec3::new(p("redvalue"), p("greenvalue"), p("bluevalue")) / 255.0 * p("intensity").max(0.0);
            if p("ambientonly") != 0.0 {
                l.ambient += col;
                continue;
            }
            let m = crate::scene::game_matrix(&light.transform);
            // the light shines down its local -Z: toward it is +Z
            let to = crate::level::game_to_engine() * m.z_axis.truncate().normalize();
            dirs.push((to, col));
        }
        if let Some((d, c)) = dirs.first() {
            (l.sun_dir, l.sun_col) = (*d, *c);
        }
        if let Some((d, c)) = dirs.get(1) {
            (l.fill_dir, l.fill_col) = (*d, *c);
        }
        let g = |k: &str| scene.settings.get(k).and_then(|v| v.as_f64()).map(|v| v as f32);
        if let (Some(near), Some(far)) = (g("fog_near"), g("fog_far")) {
            let c = Vec3::new(g("fog_red").unwrap_or(200.0), g("fog_green").unwrap_or(200.0), g("fog_blue").unwrap_or(200.0)) / 255.0;
            l.fog = Some((near, far, c));
            l.env = c;
        }
        l
    }
}

/// A texture by its game name (the remaster's version when the preset uses one: graphics.rs), with the world
/// sampler (repeat, trilinear, anisotropic).
pub fn load_tex(assets: &AssetServer, name: &str, srgb: bool) -> Handle<Image> {
    let sampler = crate::graphics::world_sampler(ImageAddressMode::Repeat, false);
    assets
        .load_builder()
        .with_settings(move |s: &mut ImageLoaderSettings| {
            s.is_srgb = srgb;
            s.sampler = ImageSampler::Descriptor(sampler.clone());
        })
        .load(crate::graphics::texture_path(name))
}

/// A game material for one submesh.
pub fn make(def: &MaterialDef, texture: Option<&str>, alpha: bool, lights: &GameLights, assets: &AssetServer, white: &Handle<Image>) -> GameMaterial {
    let tex = |n: Option<&str>, srgb: bool| n.map(|n| load_tex(assets, n, srgb)).unwrap_or_else(|| white.clone());
    let v4 = |c: [f32; 3], w: f32| Vec4::new(c[0], c[1], c[2], w);
    let l3 = |v: Vec3, w: f32| v.extend(w);
    let (fog_col, fog_range) = match lights.fog {
        // far clip at the original camera's far plane (1712 = fog_far * 1712.132 / 1500, its camera dumps): beyond
        // it the sky dome shows, as in its captures (without the clip a hard pale band of fully fogged land showed)
        Some((near, far, c)) => (c.extend(1.0), Vec4::new(near, far, far * 1712.132 / 1500.0, 0.0)),
        None => (Vec4::ZERO, Vec4::ZERO),
    };
    // debug (render investigations): AND_DEBUG_NOFOG=1 draws without fog and far clip
    let (fog_col, fog_range) = if std::env::var("AND_DEBUG_NOFOG").is_ok() { (Vec4::ZERO, Vec4::ZERO) } else { (fog_col, fog_range) };
    // blended surfaces are not in the depth prepass: the occlusion there would be the surface's behind them
    let ao_exempt = alpha || crate::graphics::ao_exempt(&[texture, def.texture2.as_deref(), def.normal_map.as_deref()]);
    let g = crate::graphics::current();
    GameMaterial {
        base: tex(texture, true),
        normal: tex(def.normal_map.as_deref(), false),
        texture2: tex(def.texture2.as_deref(), true),
        mask: tex(def.mask.as_deref(), true),
        params: GameMaterialParams {
            kind: def.kind,
            has_normal: def.normal_map.is_some() as u32,
            has_texture2: def.texture2.is_some() as u32,
            has_mask: def.mask.is_some() as u32,
            emissive: v4(def.emissive, 0.0),
            ambient: v4(def.ambient, 0.0),
            diffuse: v4(def.diffuse, 0.0),
            specular: v4(def.specular, if def.specular.iter().any(|c| *c > 0.0) { def.power } else { 0.0 }),
            // guess: TFACTOR 0.3 (the environment materials' floats +0x38.. hold 0.3; the render state was not traced)
            env: l3(lights.env, 0.3),
            sun_dir: l3(lights.sun_dir, 0.0),
            sun_col: l3(lights.sun_col, 0.0),
            fill_dir: l3(lights.fill_dir, 0.0),
            fill_col: l3(lights.fill_col, 0.0),
            ambient_light: l3(lights.ambient, 0.0),
            fog_col,
            fog_range,
            remaster: Vec4::new(if ao_exempt { 0.0 } else { g.ao_strength }, if g.far_fade { 1.0 } else { 0.0 }, 0.0, 0.0),
        },
        alpha,
        ao_exempt,
    }
}
