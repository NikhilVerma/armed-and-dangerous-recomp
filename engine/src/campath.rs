//! Camera paths (Camera_StartPath 0x0040ff50): an anim's camera node "Camera01" played on the level's `camera_path`
//! object. Two modes (camera +0x17c):
//! - cinematic (mode 1, fx opcode 0x73): the scene camera is the path's camera node (world transforms, game space;
//!   cameras look down local -Z with Y up) with its vertical FOV; Game+0xdce = 1 hides the HUD meters and holds the
//!   player. On lev_1_m1 the player walks forward on his own while `camera_path_l1m1` plays (PlayerObject_Update
//!   0x004788a0, game_06.cpp:2451). The mission's start text waits for the path to end.
//! - relative (mode 2, fx opcode 0x74 and the level's start path): the follow camera stays the player's and moves as
//!   the path's camera moves from its first frame (Camera_Update 0x0040e350 -> 0x004100e0: the camera matrix times
//!   Camera01 now times the inverse of Camera01 at the start): the black hole's jolts. No cinematic flag, play goes on.
//!   (Unreached in the shipped data: the levels whose black hole runs those fx have no camera_path object.)
//!
//! A level without a `camera_path` object plays no path at all (Camera_StartPath searches the object list for it and
//! returns at once when it is missing); only lev_1_m1 has one, and only its anims hold the path anims.

use crate::levelreset::LevelAppExt;
use crate::level::game_to_engine;
use crate::skin::AnimLibrary;
use bevy::prelude::*;
use serde::Deserialize;

/// How a path drives the camera (Camera_StartPath's mode, camera +0x17c).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PathMode {
    /// mode 1 (fx 0x73): the path's camera is the scene camera, a cinematic (Game+0xdce)
    Cinematic,
    /// mode 2 (fx 0x74, the level's start path): the follow camera moves by the path camera's motion (0x004100e0)
    Relative,
}

#[derive(Message, Clone)]
pub struct StartCameraPath {
    /// the anim (anims/<name>.json, its first camera)
    pub name: String,
    pub mode: PathMode,
}

#[derive(Deserialize, Clone)]
pub(crate) struct CamTrack {
    pub t: Vec<[f32; 4]>,
    pub r: Vec<[f32; 5]>,
    #[serde(default)]
    pub fov: Vec<[f32; 2]>,
}

impl CamTrack {
    /// The camera node's matrix at `t` (game space, in the path object's frame).
    fn matrix(&self, t: f32) -> Option<Mat4> {
        let ((a, b, u), (ra, rb, ru)) = (lerp_key(&self.t, t)?, lerp_key(&self.r, t)?);
        let p = Vec3::new(a[1], a[2], a[3]).lerp(Vec3::new(b[1], b[2], b[3]), u);
        let q = Quat::from_xyzw(ra[1], ra[2], ra[3], ra[4]).slerp(Quat::from_xyzw(rb[1], rb[2], rb[3], rb[4]), ru);
        Some(Mat4::from_rotation_translation(q.normalize(), p))
    }
}

#[derive(Deserialize)]
struct PathFile {
    duration: f32,
    cameras: std::collections::HashMap<String, CamTrack>,
}

impl PathFile {
    fn load(lib: &AnimLibrary, name: &str) -> Option<(CamTrack, f32)> {
        let text = std::fs::read_to_string(lib.dir.join("anims").join(format!("{}.json", name.to_lowercase()))).ok()?;
        let f: PathFile = serde_json::from_str(&text).ok()?;
        // the first camera by name (a hash map's first entry differs from run to run)
        let track = f.cameras.into_iter().min_by(|a, b| a.0.cmp(&b.0)).map(|(_, t)| t)?;
        Some((track, f.duration))
    }
}

/// A relative path playing (mode 2): its track, the time and the inverse of its camera's first matrix in the world
/// (camera_path +0x460, Matrix_Inverse 0x004e2b40 at the start).
struct RelativePath {
    track: CamTrack,
    duration: f32,
    time: f32,
    start_inv: Mat4,
}

/// The playing camera path, if any.
#[derive(Resource, Default)]
pub struct CameraPath {
    pub name: Option<String>,
    /// a loaded game: the intro path is not replayed
    pub skip: bool,
    pub time: f32,
    duration: f32,
    track: Option<CamTrack>,
    /// after the path: the camera stays at its last frame until the mission text that follows it is dismissed
    /// (the original's camera dumps: still at the path's end through the briefing, frames 300..500)
    pub hold: bool,
    hold_seen: bool,
    hold_t: f32,
    /// the path that ended and holds the camera
    held: Option<String>,
    /// the level's `camera_path` object's matrix (game space), looked up at the first start (camera +0x178); None
    /// inside when the level has none
    object: Option<Option<Mat4>>,
    relative: Option<RelativePath>,
}

impl CameraPath {
    /// A loaded game: start paths are ignored.
    pub fn skipping() -> Self {
        CameraPath { skip: true, ..default() }
    }
    /// A cinematic path is playing.
    pub fn active(&self) -> bool {
        self.name.is_some()
    }
    /// The camera is the path's (playing, or held at its end).
    pub fn holds_camera(&self) -> bool {
        self.name.is_some() || self.hold
    }
    /// The path playing, or the one that just ended while the mission text it leads to has not come up yet. In the
    /// original that text pauses the game on the path's last frame; here it comes a frame later (the mission's
    /// update), so what the path drives (Roman's walk on lev_1_m1) goes on until then.
    pub fn until_text(&self) -> Option<&str> {
        self.name.as_deref().or(self.held.as_deref().filter(|_| self.hold && !self.hold_seen))
    }
    /// A relative path's move of the follow camera this frame, to multiply onto the camera's matrix on the right
    /// (0x004100e0: camera x Camera01 now x Camera01 at the start, inverted; game space, whose camera frame is the
    /// engine camera's).
    pub fn relative_offset(&self) -> Option<Mat4> {
        let rel = self.relative.as_ref()?;
        let place = self.object.flatten().unwrap_or(Mat4::IDENTITY);
        Some(place * rel.track.matrix(rel.time)? * rel.start_inv)
    }
}

/// The level's `camera_path` object (Camera_StartPath's search of the object list Game+0xa84 +0xe70 by name).
fn camera_path_object(lib: &AnimLibrary) -> Option<Mat4> {
    let text = std::fs::read_to_string(lib.dir.join("scene.json")).ok()?;
    let scene: crate::scene::SceneFile = serde_json::from_str(&text).ok()?;
    scene.logic.iter().find(|l| l.template.eq_ignore_ascii_case("camera_path")).map(|l| crate::scene::game_matrix(&l.transform))
}

pub struct CameraPathPlugin;

impl Plugin for CameraPathPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<StartCameraPath>()
            .init_level_resource::<CameraPath>()
            .add_systems(Update, startup)
            .add_systems(PostUpdate, play.in_set(crate::camera::CameraPlace).before(TransformSystems::Propagate));
    }
}

pub(crate) fn lerp_key<const N: usize>(keys: &[[f32; N]], t: f32) -> Option<([f32; N], [f32; N], f32)> {
    if keys.is_empty() {
        return None;
    }
    let i = keys.partition_point(|k| k[0] <= t);
    if i == 0 {
        return Some((keys[0], keys[0], 0.0));
    }
    if i >= keys.len() {
        let k = keys[keys.len() - 1];
        return Some((k, k, 0.0));
    }
    let (a, b) = (keys[i - 1], keys[i]);
    Some((a, b, ((t - a[0]) / (b[0] - a[0]).max(1e-6)).clamp(0.0, 1.0)))
}

/// The level's start path (MissionObject slot 8 0x0044d150: with camera_startup_path_anim set, Camera_StartPath(name,
/// mode 2) once play begins; the start in a turret of the same slot is defence.rs's). No shipped level sets one.
fn startup(mission: Option<Res<crate::mission::Mission>>, state: Res<State<crate::AppState>>, mut done: crate::levelreset::PerLevel<bool>, mut start: MessageWriter<StartCameraPath>) {
    let done = done.get();
    if *done || *state.get() != crate::AppState::Playing {
        return;
    }
    let Some(m) = mission else { return };
    *done = true;
    if let Some(p) = m.startup_path.clone() {
        info!("EVENT camera path: the level starts on {p}");
        start.write(StartCameraPath { name: p, mode: PathMode::Relative });
    }
}

fn play(
    time: Res<Time>,
    mut ev: MessageReader<StartCameraPath>,
    mut path: ResMut<CameraPath>,
    lib: Res<AnimLibrary>,
    mut cam: Query<(&mut Transform, &mut Projection), With<Camera3d>>,
    mission: Option<Res<crate::mission::Mission>>,
) {
    let dt = time.delta_secs();
    if path.hold {
        // released once a mission text has shown and gone, or after 4 s without one
        let showing = mission.as_ref().is_some_and(|m| m.showing.is_some());
        path.hold_t += dt;
        if showing {
            path.hold_seen = true;
        }
        if (path.hold_seen && !showing) || (!path.hold_seen && path.hold_t > 4.0) {
            path.hold = false;
            if let Ok((_, mut proj)) = cam.single_mut() {
                if let Projection::Perspective(pp) = proj.as_mut() {
                    // gameplay: the remaster's Hor+ from the original's 60 degrees horizontal at its 1.70 design aspect
                    pp.fov = 2.0 * ((30f32).to_radians().tan() / 1.702).atan();
                }
            }
        }
    }
    for StartCameraPath { name, mode } in ev.read() {
        if path.skip && *mode == PathMode::Cinematic {
            continue;
        }
        let object = *path.object.get_or_insert_with(|| camera_path_object(&lib));
        let Some(place) = object else {
            info!("EVENT camera path: {name} not played (the level has no camera_path object)");
            continue;
        };
        let Some((track, duration)) = PathFile::load(&lib, name) else { continue };
        match mode {
            PathMode::Cinematic => {
                path.track = Some(track);
                path.duration = duration;
                path.time = 0.0;
                path.name = Some(name.clone());
                info!("camera path {name}: {duration:.1} s");
            }
            PathMode::Relative => {
                let Some(start) = track.matrix(0.0) else { continue };
                path.relative = Some(RelativePath { track, duration, time: 0.0, start_inv: (place * start).inverse() });
                info!("EVENT camera path: {name} moves the follow camera ({duration:.1} s)");
            }
        }
    }
    if let Some(rel) = path.relative.as_mut() {
        rel.time += dt;
        if rel.time >= rel.duration {
            path.relative = None;
        }
    }
    let Some(track) = path.track.as_ref() else { return };
    let t = path.time;
    let Ok((mut ct, mut proj)) = cam.single_mut() else { return };
    // the node Camera01 in the camera_path object's frame (lev_1_m1's turns it 0.5 degrees about the origin, which
    // takes the camera 10 units to the side of the raw track, as the original's camera dumps show)
    if let Some(node) = track.matrix(t) {
        let m = Mat4::from_quat(game_to_engine()) * path.object.flatten().unwrap_or(Mat4::IDENTITY) * node;
        let (_, rot, pos) = m.to_scale_rotation_translation();
        ct.translation = pos;
        ct.rotation = rot;
    }
    if let (Some((a, _, _)), Projection::Perspective(pp)) = (lerp_key(&track.fov, t), proj.as_mut()) {
        pp.fov = a[1];
    }
    path.time += dt;
    if path.time >= path.duration {
        path.held = path.name.take();
        path.track = None;
        path.hold = true;
        path.hold_seen = false;
        path.hold_t = 0.0;
    }
}
