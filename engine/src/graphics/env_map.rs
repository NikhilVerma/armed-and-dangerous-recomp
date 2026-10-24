//! Environment maps: the reflections of the original's environment materials (the ice and water of lev_1_m1's
//! landscape, bronze pipes, metal), a cube map of the level rendered from its env map marker.
//!
//! What the original does (archive/src/renderer/render_r1.cpp, render_r6.cpp, render_r4.cpp; the material set-up in
//! archive/src/loader/loader_01.cpp and auto_00502cb0.cpp):
//! - A level places at most one `envmapmarker` (class EnvMapMarker, property set p_envmapmarker: EnvMapSize 128,
//!   EnvMapBpp 32, EnvMapNearClip 2.5, EnvMapFreq 0). Its load (EnvMap_Load 0x00418a70) names its texture
//!   "cubemap_texture"; EnvMap_Render 0x00418c80 renders the scene into the cube's faces from the marker
//!   (CubeMap_RenderScene 0x00489320: a pi/2 view per face, the camera's near plane replaced by the marker's), one
//!   face per frame; with EnvMapFreq 0 (every shipped marker) that happens once, when the level starts, and the cube
//!   never changes after.
//! - An environment material (kind bit 4 without a blend mask, gamematerial.rs) binds "cubemap_texture" in stage 0:
//!   colour op MODULATE(cube, TFACTOR) (op word 0x03020184: op 4, args TEXTURE, TFACTOR), then stage 1 MULTIPLYADD
//!   (0x00020199: CURRENT + TEXTURE * DIFFUSE): `cube * factor + base * lighting`. The texture factor is the record's
//!   float +0x40 as a grey (0.3 for ice, water and bronze; MaterialDef::env_factor).
//!
//! Here the cube is baked once per level and preset, as the original renders it once: `and_remaster --scene LEVEL
//! --preset P --bake-env-map` (tools/bake_env_maps.py runs every level) lets the level load and settle, renders the
//! six faces from the marker with six cameras ([`EnvProbeView`]) into one 3 x 2 atlas and saves it as
//! `<level>/env_map_<preset>.png`. A level loads its atlas ([`atlas_path`]; main.rs puts it in
//! [`GameLights::env_atlas`](crate::gamematerial::GameLights)) and gamematerial.wesl samples it along the per-pixel
//! reflection vector (`env_lookup`; the original's fixed function made the vector per vertex). Baked rather than
//! captured in play: extra 3D cameras during play would break every "the 3D camera" lookup of the game logic for
//! those frames (the scripted runs diverged), and a static cube costs nothing per frame. Better than the original
//! where it is free: 512 pixels per face in the Remaster (`env_map_size`; the marker's 128 in the Original).

use crate::gamematerial::GameLights;
use bevy::camera::{RenderTarget, Viewport};
use bevy::prelude::*;
use bevy::render::render_resource::TextureFormat;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

/// The environment maps' settings (flattened into [`super::GraphicsSettings`]; read when a level loads).
#[derive(Clone, Serialize, Deserialize, PartialEq, Debug)]
#[serde(default)]
pub struct EnvMapSettings {
    /// the level's environment cube map (off, or not baked: the environment materials reflect the fog colour, the
    /// engine's look before 2026-10-23)
    pub env_maps: bool,
    /// pixels per cube face when baking (0: the marker's own EnvMapSize, 128 in every shipped level)
    pub env_map_size: u32,
}

impl EnvMapSettings {
    pub fn original() -> Self {
        EnvMapSettings { env_maps: true, env_map_size: 0 }
    }

    pub fn remaster() -> Self {
        EnvMapSettings { env_maps: true, env_map_size: 512 }
    }

    /// The face size for a marker of `marker_size` (EnvMapSize), a power of two from 16 to 2048.
    pub fn face_size(&self, marker_size: u32) -> u32 {
        let s = if self.env_map_size > 0 { self.env_map_size } else { marker_size };
        s.clamp(16, 2048).next_power_of_two()
    }
}

impl Default for EnvMapSettings {
    fn default() -> Self {
        Self::remaster()
    }
}

/// A level's env map marker: where its cube is rendered from (engine space), its near plane and face size.
#[derive(Clone, Debug, PartialEq)]
pub struct EnvProbe {
    pub position: Vec3,
    pub near: f32,
    pub face: u32,
}

impl EnvProbe {
    /// The level's env map marker (a logic object whose property set has EnvMapSize; the first if several), with the
    /// face size the settings ask for.
    pub fn from_scene(scene: &crate::scene::SceneFile, data: &crate::gamedata::GameData, settings: &EnvMapSettings) -> Option<Self> {
        scene.logic.iter().find_map(|l| {
            let p = data.instance_props(&l.template, &l.props);
            if !p.0.contains_key("envmapsize") {
                return None;
            }
            let at = Vec3::new(l.transform[0][3], l.transform[1][3], l.transform[2][3]);
            Some(EnvProbe {
                position: crate::level::game_to_engine() * at,
                near: p.f("envmapnearclip", 2.5).max(0.01),
                face: settings.face_size(p.f("envmapsize", 128.0).abs() as u32),
            })
        })
    }
}

/// The baked atlas of a level for a preset (relative to the level's folder, the asset root).
pub fn atlas_file(preset: super::Preset) -> String {
    format!("env_map_{}.png", preset.name().to_lowercase())
}

/// The atlas a level should load: the preset's, else the other preset's, else none (relative to `scene`).
pub fn atlas_path(scene: &Path, preset: super::Preset) -> Option<String> {
    [preset, super::Preset::Remaster, super::Preset::Original].into_iter().map(atlas_file).find(|f| scene.join(f).is_file())
}

/// An atlas to render into: 3 x 2 faces of `face` pixels, filled with `fill`.
pub fn atlas_target(face: u32, fill: Color) -> Image {
    let mut image = Image::new_target_texture(face * 3, face * 2, TextureFormat::Rgba8UnormSrgb, None);
    let c = fill.to_srgba().to_u8_array();
    if let Some(data) = image.data.as_mut() {
        for px in data.chunks_exact_mut(4) {
            px.copy_from_slice(&[c[0], c[1], c[2], 255]);
        }
    }
    image
}

/// The cube's faces, engine space (Y up): the direction each face looks along and its up. gamematerial.wesl
/// `env_lookup` picks the face by the reflection's largest axis and rebuilds the same basis (Bevy's `looking_to`:
/// right = forward x up).
pub const FACES: [(Vec3, Vec3); 6] = [
    (Vec3::X, Vec3::Y),
    (Vec3::NEG_X, Vec3::Y),
    (Vec3::Y, Vec3::Z),
    (Vec3::NEG_Y, Vec3::NEG_Z),
    (Vec3::Z, Vec3::Y),
    (Vec3::NEG_Z, Vec3::Y),
];

/// A face's place in the atlas (pixels): column face % 3, row face / 3.
pub fn face_origin(face: usize, size: u32) -> UVec2 {
    UVec2::new(face as u32 % 3, face as u32 / 3) * size
}

/// A camera rendering one cube face into the atlas (bake runs only).
#[derive(Component)]
pub struct EnvProbeView;

/// `--bake-env-map`: bake the level's atlas to `out`, then quit.
#[derive(Resource)]
pub struct EnvMapBake {
    pub out: PathBuf,
    frames: u32,
    target: Option<Handle<Image>>,
}

impl EnvMapBake {
    pub fn new(out: PathBuf) -> Self {
        EnvMapBake { out, frames: 0, target: None }
    }
}

/// Bake timeline (frames of the level): the faces' cameras come once the level has settled (its textures loaded,
/// the objects placed), render for a few frames (their pipelines compile meanwhile), the atlas is saved, the run ends.
const BAKE_START: u32 = 90;
const BAKE_SAVE: u32 = BAKE_START + 30;
const BAKE_QUIT: u32 = BAKE_SAVE + 30;

pub struct EnvMapPlugin;

impl Plugin for EnvMapPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, bake.run_if(resource_exists::<EnvMapBake>));
    }
}

/// The bake: the six face cameras at the marker, the atlas saved, then exit.
fn bake(
    mut commands: Commands,
    mut job: ResMut<EnvMapBake>,
    lights: Option<Res<GameLights>>,
    mut images: ResMut<Assets<Image>>,
    mut exit: MessageWriter<AppExit>,
) {
    let Some(lights) = lights else { return };
    let Some(probe) = lights.env_probe.clone() else {
        info!("EVENT env map: the level has no env map marker, nothing baked");
        exit.write(AppExit::Success);
        return;
    };
    job.frames += 1;
    match job.frames {
        BAKE_START => {
            // the clear colour behind everything (the sky dome covers it) is the fog colour, as before the bake
            let target = images.add(atlas_target(probe.face, Color::srgb(lights.env.x, lights.env.y, lights.env.z)));
            let far = lights.fog.map_or(1500.0, |f| crate::options::world_far(f.1, 1.0));
            for (i, (forward, up)) in FACES.iter().enumerate() {
                commands.spawn((
                    Camera3d::default(),
                    Camera {
                        order: -20 - i as isize,
                        viewport: Some(Viewport { physical_position: face_origin(i, probe.face), physical_size: UVec2::splat(probe.face), ..default() }),
                        ..default()
                    },
                    RenderTarget::from(target.clone()),
                    Projection::Perspective(PerspectiveProjection { fov: std::f32::consts::FRAC_PI_2, aspect_ratio: 1.0, near: probe.near, far, ..default() }),
                    Transform::from_translation(probe.position).looking_to(*forward, *up),
                    Msaa::Off,
                    bevy::core_pipeline::tonemapping::Tonemapping::None,
                    EnvProbeView,
                    Name::new(format!("env_probe_{i}")),
                ));
            }
            job.target = Some(target);
            info!("env map: baking {} px faces from {:.0?}", probe.face, probe.position);
        }
        BAKE_SAVE => {
            if let Some(t) = job.target.clone() {
                let out = job.out.clone();
                commands.spawn(bevy::render::view::screenshot::Screenshot::image(t)).observe(bevy::render::view::screenshot::save_to_disk(out));
            }
        }
        f if f >= BAKE_QUIT => {
            if job.out.is_file() {
                info!("EVENT env map: baked {}", job.out.display());
                exit.write(AppExit::Success);
            } else if f > BAKE_QUIT + 120 {
                error!("env map: {} was not written", job.out.display());
                exit.write(AppExit::error());
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The faces are a cube: each axis direction once, every up perpendicular to its direction.
    #[test]
    fn faces_cover_the_axes() {
        let mut seen = Vec::new();
        for (f, u) in FACES {
            assert!(f.dot(u).abs() < 1e-6);
            seen.push((f.x as i32, f.y as i32, f.z as i32));
        }
        seen.sort();
        seen.dedup();
        assert_eq!(seen.len(), 6);
        assert_eq!(face_origin(4, 128), UVec2::new(128, 128));
    }

    #[test]
    fn face_size_follows_the_settings() {
        assert_eq!(EnvMapSettings::original().face_size(128), 128);
        assert_eq!(EnvMapSettings::remaster().face_size(128), 512);
        assert_eq!(EnvMapSettings { env_maps: true, env_map_size: 300 }.face_size(128), 512);
    }
}
