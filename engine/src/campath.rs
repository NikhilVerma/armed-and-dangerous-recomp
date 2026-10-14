//! Camera paths (fx opcode 0x73 -> Camera_StartPath 0x0040ff50, mode 1 = cinematic): the scene camera follows
//! the path anim's camera node (world transforms, game space; cameras look down local -Z with Y up), with its
//! vertical FOV. On lev_1_m1 the player walks forward on his own while `camera_path_l1m1` plays
//! (PlayerObject_Update 0x004788a0, game_06.cpp:2451). The mission's start text waits for the path to end.

use crate::level::game_to_engine;
use crate::skin::AnimLibrary;
use bevy::prelude::*;
use serde::Deserialize;

#[derive(Message, Clone)]
pub struct StartCameraPath(pub String);

#[derive(Deserialize, Clone)]
pub(crate) struct CamTrack {
    pub t: Vec<[f32; 4]>,
    pub r: Vec<[f32; 5]>,
    #[serde(default)]
    pub fov: Vec<[f32; 2]>,
}

#[derive(Deserialize)]
struct PathFile {
    duration: f32,
    cameras: std::collections::HashMap<String, CamTrack>,
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
}

impl CameraPath {
    /// A loaded game: start paths are ignored.
    pub fn skipping() -> Self {
        CameraPath { skip: true, ..default() }
    }
    pub fn active(&self) -> bool {
        self.name.is_some()
    }
    /// The camera is the path's (playing, or held at its end).
    pub fn holds_camera(&self) -> bool {
        self.name.is_some() || self.hold
    }
}

pub struct CameraPathPlugin;

impl Plugin for CameraPathPlugin {
    fn build(&self, app: &mut App) {
        app.add_message::<StartCameraPath>().init_resource::<CameraPath>().add_systems(PostUpdate, play.before(TransformSystems::Propagate));
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

fn play(
    time: Res<Time>,
    mut ev: MessageReader<StartCameraPath>,
    mut path: ResMut<CameraPath>,
    lib: Res<AnimLibrary>,
    mut cam: Query<(&mut Transform, &mut Projection), With<Camera3d>>,
    mission: Option<Res<crate::mission::Mission>>,
) {
    if path.hold {
        // released once a mission text has shown and gone, or after 4 s without one
        let showing = mission.as_ref().is_some_and(|m| m.showing.is_some());
        path.hold_t += time.delta_secs();
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
    for StartCameraPath(name) in ev.read() {
        if path.skip {
            continue;
        }
        let file = std::fs::read_to_string(lib.dir.join("anims").join(format!("{}.json", name.to_lowercase()))).ok();
        if let Some(f) = file.and_then(|t| serde_json::from_str::<PathFile>(&t).ok()) {
            path.track = f.cameras.into_values().next();
            path.duration = f.duration;
            path.time = 0.0;
            path.name = Some(name.clone());
            info!("camera path {name}: {:.1} s", f.duration);
        }
    }
    let Some(track) = path.track.as_ref() else { return };
    let t = path.time;
    let Ok((mut ct, mut proj)) = cam.single_mut() else { return };
    if let (Some((a, b, u)), Some((ra, rb, ru))) = (lerp_key(&track.t, t), lerp_key(&track.r, t)) {
        let p = Vec3::new(a[1], a[2], a[3]).lerp(Vec3::new(b[1], b[2], b[3]), u);
        let q = Quat::from_xyzw(ra[1], ra[2], ra[3], ra[4]).slerp(Quat::from_xyzw(rb[1], rb[2], rb[3], rb[4]), ru);
        // the path plays on the level's camera_path object (Camera_StartPath 0x0040ff50: the node Camera01 in that
        // object's frame); measured against the original's camera dumps (intro, every 10 frames) it is rotated about
        // the origin as the player's placement (row 0 = (0.999962, 0.008726): 0.5 degrees), which takes the camera
        // 10 units to the side of the raw track
        let place = Mat4::from_cols(Vec4::new(0.999962, -0.008726, 0.0, 0.0), Vec4::new(0.008726, 0.999962, 0.0, 0.0), Vec4::Z, Vec4::W);
        let m = Mat4::from_quat(game_to_engine()) * place * Mat4::from_rotation_translation(q.normalize(), p);
        let (_, rot, pos) = m.to_scale_rotation_translation();
        ct.translation = pos;
        ct.rotation = rot;
    }
    if let (Some((a, _, _)), Projection::Perspective(pp)) = (lerp_key(&track.fov, t), proj.as_mut()) {
        pp.fov = a[1];
    }
    path.time += time.delta_secs();
    if path.time >= path.duration {
        path.name = None;
        path.track = None;
        path.hold = true;
        path.hold_seen = false;
        path.hold_t = 0.0;
    }
}
