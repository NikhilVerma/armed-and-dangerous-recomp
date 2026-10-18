//! The saved games' thumbnails: a 128 x 76 picture of the scene kept with each save and shown on its slot.
//!
//! The original (SaveGame_CreateThumbnail 0x0044f3a0, called by the save writer 0x0048c620 with the header's +0x11c /
//! +0x120 = 128 x 76): the scene rendered into a small texture (SceneThumbnail_Render 0x0044f2d0), its rows copied
//! after the save header; a load puts them in the slot's texture (Texture_LoadRgbPixels 0x0044f1f0) and the slot
//! draws it (0x004a33d0): 128 wide, its height kept to the picture's shape (q = h * 128 / 2w = 38 either side of 70
//! below the slot's top), white, over the slot's panel.
//!
//! The remaster renders the game camera's view (the world, without the HUD or the menu on top) into a 128 x 76 image
//! the frame a save is written, and keeps it as a PNG next to the save (`game3.json` -> `game3.png`); the slot screens
//! show it (saveslots.rs). API: [`ThumbnailPlugin`], [`SaveThumbnail`], [`thumb_path`].

use bevy::camera::RenderTarget;
use bevy::prelude::*;
use bevy::render::render_resource::TextureFormat;
use bevy::render::view::screenshot::{Screenshot, save_to_disk};
use std::path::{Path, PathBuf};

/// The thumbnail's size (the header's +0x11c / +0x120).
pub const THUMB_W: u32 = 128;
pub const THUMB_H: u32 = 76;

/// Picture the scene for the save written at `save` (savegame.rs sends it).
#[derive(Message, Clone)]
pub struct SaveThumbnail {
    pub save: PathBuf,
}

/// A save's thumbnail file.
pub fn thumb_path(save: &Path) -> PathBuf {
    save.with_extension("png")
}

pub struct ThumbnailPlugin;

impl Plugin for ThumbnailPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<SaveThumbnail>().add_systems(Last, (start, finish).chain());
    }
}

/// The camera rendering a thumbnail, its picture and where it goes; frames since it was made.
#[derive(Component)]
struct ThumbCamera {
    image: Handle<Image>,
    path: PathBuf,
    frames: u32,
}

/// A camera like the game's (place, lens, fog, colours) rendering into a 128 x 76 image (SceneThumbnail_Render).
fn start(
    mut commands: Commands,
    mut ev: MessageReader<SaveThumbnail>,
    mut images: ResMut<Assets<Image>>,
    game: Query<(&GlobalTransform, &Projection, Option<&DistanceFog>), (With<crate::camera::FollowCam>, Without<ThumbCamera>)>,
) {
    for e in ev.read() {
        let Ok((t, p, fog)) = game.single() else {
            warn!("thumbnail: no game camera");
            continue;
        };
        let image = images.add(Image::new_target_texture(THUMB_W, THUMB_H, TextureFormat::Rgba8UnormSrgb, None));
        let mut c = commands.spawn((
            Camera3d::default(),
            Camera { order: -10, ..default() },
            RenderTarget::from(image.clone()),
            t.compute_transform(),
            p.clone(),
            bevy::core_pipeline::tonemapping::Tonemapping::None,
            ThumbCamera { image, path: thumb_path(&e.save), frames: 0 },
        ));
        if let Some(f) = fog {
            c.insert(f.clone());
        }
    }
}

/// The frame after: the picture is read back and written; the camera goes.
fn finish(mut commands: Commands, mut cams: Query<(Entity, &mut ThumbCamera)>) {
    for (e, mut c) in &mut cams {
        c.frames += 1;
        if c.frames == 2 {
            commands.spawn(Screenshot::image(c.image.clone())).observe(save_to_disk(c.path.clone()));
            info!("EVENT thumbnail: {}", c.path.display());
        }
        if c.frames >= 4 {
            commands.entity(e).despawn();
        }
    }
}
