//! The scene camera around the player (the original's CameraSystem, Game +0xa68, updated by Camera_Update 0x0040e350):
//! the follow camera on the player's camera nodes with collision and fading, the weapon zoom, the death camera, the
//! blend back from a camera path, and the camera shake. Other camera owners (campath.rs paths, turret.rs gun cameras,
//! camdump.rs fixed views) place the camera themselves; the shake applies over all of them.
//!
//! Everything game specific is data: the nodes come from player.json `camera.nodes` (the player's Trotter_ref
//! anim: Camera_end the eye, Camera_start's target the look point, Camera_<weapon> the zoomed eyes, their horizontal
//! FOVs), the pitch limits from `camera.pitch_clamp`, the zoom time from p_globalsettings camera_zoom_secs (Roman's
//! `zoom_secs`), the fade rates from p_globalsettings (camfade.rs).
//!
//! What the original does (docs/subsystems/player.md 4, camera.md):
//! - Follow (Camera_Update's orbit branch 0x0040e56c, with the node names and the pitch speed the player gives it,
//!   0x004757b0): the eye at Camera_end, turned by the look pitch about Camera_start (the pivot at Roman's head) in
//!   the player's frame; the view keeps Camera_end's own direction (at its target, Camera_start's), turned by a
//!   stronger pitch looking up (x 1.2) and a weaker one looking down (x 0.9), so looking up tilts the view past the
//!   orbit and Roman drops out of the way. Five rays from the pivot (to the eye and to the eye +-5 along the eye's
//!   right and up axes) shorten the arm on the landscape and fade the objects they pass (camfade.rs).
//! - Zoom (0x00475880 / 0x00475910 / 0x0040fbb0): while Zoom is held the camera blends linearly, over camera_zoom_secs,
//!   toward the weapon's node (Camera_rifle / Camera_machinegun: eye at the head, the same look point) and its FOV.
//!   The original flips the zoom mode whenever the held state and the mode differ (0x00475850): entering (0x00475880)
//!   names the weapon's camera node (0x0040fae0: blend state 1, up), leaving (0x00475910 flag 0) starts the blend back
//!   (0x0040fb00: state 2), and with flag 1 (mounting a gun, respawn) drops it at once (0x0040fb50: state 0; turret.rs
//!   zeroes `Roman::zoom`). `Roman::zoom` is that blend weight (0x0040fbb0: +0x68 over camera_zoom_secs).
//! - Death (PlayerObject_Update 0x004788a0 at game_06.cpp:2201 sets mode 3 with 0x0040fa90): the camera stays where
//!   it is and turns to look at the player's body, smoothed over 1 s (0x0040fd50 with +0x160 = +0x164 = 1).
//! - After a camera path (Camera_Update's tail: +0x17c == 1 sets the 1 s smoothing again): the follow camera is
//!   reached from the path's last view over 1 s (0x0040fd50).
//! - Shake (0x0040fac0 starts it, 0x0040e300 applies it to the final camera matrix): the camera's height moves by the
//!   amplitude, which flips sign and shrinks by 0.75 every frame (0x005bcc20 = -0.75) until the time runs out.

use crate::levelreset::LevelAppExt;
use crate::physics::Physics;
use crate::player::Roman;
use bevy::prelude::*;
use bevy::transform::TransformSystems;

/// The camera that follows the player.
#[derive(Component)]
pub struct FollowCam;

/// The systems that place the camera this frame (the shake applies after them).
#[derive(SystemSet, Debug, Clone, PartialEq, Eq, Hash)]
pub struct CameraPlace;

pub struct CameraPlugin;

impl Plugin for CameraPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<CameraRig>()
            .init_level_resource::<CameraShake>()
            .init_level_resource::<CameraSmooth>()
            .add_message::<ShakeCamera>()
            .add_systems(First, unshake)
            .add_systems(PostUpdate, follow_camera.in_set(CameraPlace).before(TransformSystems::Propagate).run_if(crate::playing))
            .add_systems(PostUpdate, (start_shakes, shake).chain().after(CameraPlace).before(TransformSystems::Propagate))
            .add_systems(PostUpdate, log_camera.after(TransformSystems::Propagate).run_if(resource_exists::<crate::playtest::PlayState>));
    }
}

/// A camera node of the player's anim (player.json `camera.nodes`), in the engine's frame of the player (x right, y
/// up, -z ahead): the eye, the point it looks at and the vertical FOV.
#[derive(Clone, Copy, Debug)]
pub struct CamNode {
    pub eye: Vec3,
    pub target: Vec3,
    pub fov: f32,
}

/// The follow camera's set-up (player.json `camera`).
#[derive(Resource)]
pub struct CameraRig {
    loaded: bool,
    /// the eye at rest (Camera_end) and the look point (Camera_start's target)
    pub follow: CamNode,
    /// the point the look pitch turns the eye about (Camera_start's position, at Roman's head: 0x0040e56c)
    pub pivot: Vec3,
    /// the zoomed eye per weapon (the weapon table's camera node names, 0x00402110: Camera_rifle, Camera_machinegun)
    pub zoom: std::collections::HashMap<String, CamNode>,
    /// the look pitch limits (+0x74 clamped to [-1.5608, 1.15], 0x005bcebc / 0x005bceb8)
    pub pitch: (f32, f32),
    /// the side and up offsets of the four extra collision rays (0x0040e56c: +-5 along the look axes)
    pub ray_offset: f32,
    /// the smoothing time of the death camera and of the return from a camera path (+0x164 = 1.0)
    pub smooth_secs: f32,
}

impl Default for CameraRig {
    fn default() -> Self {
        // the lev_1_m1 values of player.json, used until it is read
        let node = |eye: Vec3, fov_h: f32| CamNode { eye, target: Vec3::new(0.0, 6.877, -7.194), fov: vertical_fov(fov_h) };
        CameraRig {
            loaded: false,
            follow: node(Vec3::new(0.0, 11.128, 22.703), 1.0471976),
            pivot: Vec3::new(0.0, 7.749, -1.058),
            zoom: [("rifle".to_string(), node(Vec3::new(0.0, 7.918, 0.13), 0.4363323)), ("machinegun".to_string(), node(Vec3::new(0.0, 7.918, 0.13), 0.5235988))].into(),
            pitch: (-1.5608, 1.15),
            ray_offset: 5.0,
            smooth_secs: 1.0,
        }
    }
}

/// A node's horizontal FOV made vertical for the original's camera aspect (0x0040a4d0 with node word +0x9a == 0:
/// 2 atan(tan(h / 2) / camera +0x154), +0x154 = 1.702128).
pub fn vertical_fov(h: f32) -> f32 {
    2.0 * ((h * 0.5).tan() / 1.702_128).atan()
}

impl CameraRig {
    fn load(&mut self, dir: &std::path::Path) {
        self.loaded = true;
        let Some(cam) = std::fs::read_to_string(dir.join("player.json")).ok().and_then(|t| serde_json::from_str::<serde_json::Value>(&t).ok()).map(|p| p["camera"].clone()) else {
            warn!("player.json camera missing: the follow camera keeps lev_1_m1's values");
            return;
        };
        // game model space (the player faces -Y, z up) to the engine's frame of the player (faces -Z, y up); x is 0
        // for every lev_1_m1 node (its sign: guess)
        let v = |a: &serde_json::Value| {
            let f = |i: usize| a[i].as_f64().unwrap_or(0.0) as f32;
            Vec3::new(-f(0), f(2), f(1))
        };
        let node = |n: &serde_json::Value| CamNode { eye: v(&n["pos"]), target: v(&n["target"]), fov: vertical_fov(n["fov"].as_f64().unwrap_or(1.047) as f32) };
        let nodes = &cam["nodes"];
        if let (Some(end), Some(start)) = (nodes.get("Camera_end"), nodes.get("Camera_start")) {
            self.follow = CamNode { target: v(&start["target"]), ..node(end) };
            self.pivot = v(&start["pos"]);
        }
        // each gun's zoom camera node by the weapon table's name (+0x100: Camera_rifle, Camera_sniper, camera_gurner for
        // the gurner and the shark gun, camera_vindaloo); the nodes' names in player.json differ in case
        for id in crate::weapons::WeaponId::ALL {
            let Some(want) = id.table().camera else { continue };
            if let Some(n) = nodes.as_object().and_then(|o| o.iter().find(|(k, _)| k.eq_ignore_ascii_case(want)).map(|(_, n)| n)) {
                self.zoom.insert(id.name().into(), node(n));
            }
        }
        if let Some(p) = cam["pitch_clamp"].as_array() {
            self.pitch = (p[0].as_f64().unwrap_or(-1.5608) as f32, p[1].as_f64().unwrap_or(1.15) as f32);
        }
    }
}

/// Start a camera shake (0x0040fac0: amplitude in game units, seconds). Fx objects send it when the camera is within
/// their radius (fxvm.rs, opcode shake).
#[derive(Message, Clone, Copy, Debug)]
pub struct ShakeCamera {
    pub amplitude: f32,
    pub secs: f32,
}

/// The running shake (camera +0x14c amplitude, +0x150 time left) and the height offset it put on the camera.
#[derive(Resource, Default, Debug)]
pub struct CameraShake {
    pub amplitude: f32,
    pub left: f32,
    applied: f32,
    /// shakes started (playtest evidence)
    pub started: u32,
}

/// The per-frame smoothing of the camera toward its target view (0x0040fd50): the time left of the blend and the
/// last view shown; `death` holds the eye where the player died.
#[derive(Resource, Default)]
pub struct CameraSmooth {
    left: f32,
    last: Option<Transform>,
    death: Option<Vec3>,
    held_by_path: bool,
    /// the last frame's FOV was a zoom blend (not the level camera's own)
    zoom_blended: bool,
}

/// The rest pitch of the engine's look pitch (Roman::pitch, rest -0.15 rad: the nodes' own tilt is in the eye and
/// target positions).
const REST_PITCH: f32 = -0.15;

/// The view's pitch for the orbit's pitch (0x0040e56c: the camera matrix is turned by +0x74 adjusted, +0x74 x 1.2 at
/// or above 0 (0x005bcd2c = 0.2) and x 0.9 below (0x005bcbfc = 0.1), while the eye orbits by +0x74 itself).
fn view_pitch(orbit: f32) -> f32 {
    if orbit < 0.0 { orbit - orbit * 0.1 } else { orbit + orbit * 0.2 }
}

/// The inverse of `view_pitch`.
fn orbit_pitch(view: f32) -> f32 {
    if view < 0.0 { view / 0.9 } else { view / 1.2 }
}

impl CameraRig {
    /// The unzoomed follow camera's eye before collision and its view rotation, in Roman's frame (yaw not applied),
    /// for the orbit pitch (+0x74 = Roman::pitch - REST_PITCH): Camera_end turned about the pivot by the pitch, the
    /// view (Camera_end looking at its target) turned by `view_pitch` (0x0040e56c).
    pub fn orbit(&self, pitch: f32) -> (Vec3, Quat) {
        let node = self.follow;
        let eye = self.pivot + Quat::from_rotation_x(pitch) * (node.eye - self.pivot);
        let rest = Transform::from_translation(node.eye).looking_at(node.target, Vec3::Y).rotation;
        (eye, Quat::from_rotation_x(view_pitch(pitch)) * rest)
    }
}

impl CameraRig {
    /// The look pitch limits in Roman::pitch's terms.
    pub fn pitch_limits(&self) -> (f32, f32) {
        (self.pitch.0 + REST_PITCH, self.pitch.1 + REST_PITCH)
    }

    /// The look yaw and pitch (Roman::yaw / pitch) that put the unzoomed follow camera's crosshair on `point` for
    /// Roman at `feet`: the crosshair's ray runs from the eye through the look point (follow_camera), so the look point
    /// must see `point` along the arm reversed. (For the playtest bot, which aims as a player does: crosshair on target.)
    pub fn look_at(&self, feet: Vec3, point: Vec3) -> (f32, f32) {
        let node = self.follow;
        // the view direction at rest (pitch REST_PITCH), in Roman's frame: from the eye to the look point
        let rest = (node.target - node.eye).normalize_or(Vec3::NEG_Z);
        let rest_yaw = (-rest.x).atan2(-rest.z);
        let rest_elev = rest.y.atan2(rest.with_y(0.0).length());
        // the eye moves with the yaw and the pitch (CameraRig::orbit): a few fixed-point steps from the eye
        let mut yaw = { let d = point - feet; (-d.x).atan2(-d.z) };
        let mut pitch = 0.0;
        for _ in 0..6 {
            let to = point - (feet + Quat::from_rotation_y(yaw) * self.orbit(pitch).0);
            yaw = (-to.x).atan2(-to.z) - rest_yaw;
            pitch = orbit_pitch(to.y.atan2(to.with_y(0.0).length()) - rest_elev).clamp(self.pitch.0, self.pitch.1);
        }
        (yaw, pitch + REST_PITCH)
    }
}

/// One step of 0x0040fd50: the time counts down by dt; the view is the target blended with the last view shown by
/// t = time left / total (the last view's weight), rotations slerped, positions lerped.
fn smooth_step(last: &Transform, target: &Transform, left: f32, total: f32) -> Transform {
    let t = (left / total.max(1e-6)).clamp(0.0, 1.0);
    Transform { translation: target.translation.lerp(last.translation, t), rotation: target.rotation.slerp(last.rotation, t), scale: target.scale }
}

/// Third-person camera behind Roman at his yaw and pitch, pulled in when the landscape is in the way; zoom, death
/// camera and the return from camera paths (module comment).
#[allow(clippy::too_many_arguments)]
pub fn follow_camera(
    time: Res<Time>,
    path: Res<crate::campath::CameraPath>,
    physics: NonSend<Physics>,
    roman: Query<(Entity, &Roman, &Transform, Option<&crate::skin::Animator>), Without<FollowCam>>,
    mut cam: Query<(&mut Transform, &mut Projection), With<FollowCam>>,
    joints: Query<&GlobalTransform>,
    placed: Query<(Entity, &crate::level::Placed)>,
    mut fade: ResMut<crate::camfade::CameraFade>,
    (topsy, cheats, mounted, lib): (Res<crate::topsy::Topsy>, Res<crate::cheats::Cheats>, Res<crate::turret::Mounted>, Res<crate::skin::AnimLibrary>),
    (mut rig, mut smooth, mut base_fov): (ResMut<CameraRig>, ResMut<CameraSmooth>, crate::levelreset::PerLevel<Option<f32>>),
) {
    // the unzoomed FOV, taken from the level's camera (a restart while zoomed must not keep the zoomed one)
    let base_fov = base_fov.get();
    if !rig.loaded {
        rig.load(&lib.dir);
    }
    // a camera path (campath.rs) or a mounted gun's camera node (turret.rs) holds the camera; when the path lets go
    // the follow camera is reached over 1 s (Camera_Update's tail with +0x17c == 1)
    if path.holds_camera() || mounted.0.is_some() {
        smooth.held_by_path = path.holds_camera();
        smooth.last = None;
        return;
    }
    let (Ok((roman_entity, r, rt, anim)), Ok((mut c, mut proj))) = (roman.single(), cam.single_mut()) else { return };
    let dt = time.delta_secs();
    if std::mem::take(&mut smooth.held_by_path) {
        smooth.left = rig.smooth_secs;
        smooth.last = Some(*c);
    }
    // the death camera (mode 3): the eye stays, the view turns to the body (the look-at node: "center" for a body
    // without its anim, else Camera_start of the actor's frame; the engine's ragdoll moves the bones, so the center
    // bone; guess), smoothed over 1 s
    if r.dead {
        if smooth.death.is_none() {
            smooth.death = Some(c.translation);
            smooth.left = rig.smooth_secs;
            smooth.last = Some(*c);
        }
        let eye = smooth.death.unwrap_or(c.translation);
        let body = anim
            .and_then(|a| a.skeleton.index.get("center").and_then(|&i| a.joints.get(i)))
            .and_then(|j| joints.get(*j).ok())
            .map_or(rt.translation + Vec3::Y * 6.877, |g| g.translation());
        let target = Transform::from_translation(eye).looking_at(body, Vec3::Y);
        *c = place_smoothed(&mut smooth, target, dt, rig.smooth_secs);
        return;
    }
    smooth.death = None;
    // Camera_Update 0x0040e350 with the player's camera nodes: the eye at Camera_end (22.703 behind Roman, 11.128
    // up, centred), looking at Camera_start's target (7.194 ahead, 6.877 up): at rest the view tilts down 0.141 rad,
    // as in the original's camera dumps. The look pitch (r.pitch, rest -0.15) turns the eye about the pivot,
    // Camera_start (1.058 ahead, 7.749 up: Roman's head), and the view by the stronger view pitch (CameraRig::orbit).
    // Five rays from the pivot (to the eye and to the eye +-5 along the eye's right and up axes) pass through
    // objects, which fade (camfade.rs), and the nearest landscape hit shortens the arm (0x0040e56c: objects hit are
    // excluded and the ray cast again; only what remains stops it).
    let yaw = Quat::from_rotation_y(r.yaw);
    let orbit = r.pitch - REST_PITCH;
    let pitch = Quat::from_rotation_x(orbit);
    let (orbit_eye, orbit_view) = rig.orbit(orbit);
    let target = rt.translation + yaw * rig.pivot;
    let arm = yaw * (orbit_eye - rig.pivot);
    let len = arm.length();
    let eye = target + arm;
    // the eye's own axes (the view node turned rigidly with the orbit: 0x58 in 0x0040e56c)
    let eye_rot = yaw * pitch * Transform::from_translation(rig.follow.eye).looking_at(rig.follow.target, Vec3::Y).rotation;
    let (right, up) = (eye_rot * Vec3::X * rig.ray_offset, eye_rot * Vec3::Y * rig.ray_offset);
    let mut dist = len;
    for end in [eye, eye + right, eye - right, eye + up, eye - up] {
        let dir = (end - target).normalize_or(Vec3::Y);
        // every ray as long as the arm (0x2c: the eye distance)
        let mut from = target;
        let mut left = len;
        // through objects: the landscape alone stops the ray; placed objects and moving bodies hit fade
        while let Some((body, at, normal)) = physics.raycast(from, dir, left) {
            // a landscape face hit from behind (the look target inside a steep hill at its foot: Jolt's rays hit back
            // faces) does not stop the arm; the original's camera stays behind Roman there (fightO 1440, guess: its
            // Havok landscape ray is one-sided)
            if physics.landscape == Some(body) && normal.dot(dir) < 0.0 {
                dist = dist.min(at.distance(target));
                break;
            }
            let object = physics.bodies.get(&body).copied().or_else(|| placed.iter().find(|(_, p)| p.collider == Some(physics.main_body(body))).map(|(e, _)| e));
            if let Some(object) = object.filter(|o| *o != roman_entity) {
                fade.hit(object);
            }
            let step = at.distance(from) + 0.05;
            from = at + dir * 0.05;
            left -= step;
            if left <= 0.0 {
                break;
            }
        }
    }
    let mut eye = target + arm / len * dist;
    let mut rot = yaw * orbit_view;
    // zoom: the weapon's camera node (Camera_rifle / Camera_machinegun: the eye at the head, the same look point),
    // position and FOV blended linearly by the zoom weight (0x0040fbb0: fov = target w + fov (1 - w)); the pitch
    // turns the look about the eye
    if let Projection::Perspective(pp) = &mut *proj {
        // re-read only while no blend is on screen: on the frame the zoom weight reaches 0 the projection still holds
        // the last blend, and taking that as the base lowered the FOV a little after every zoom (35.3 instead of
        // 37.5 when the weight's last step before 0 was large)
        if base_fov.is_none() || r.zoom <= 0.0 && !smooth.zoom_blended {
            *base_fov = Some(pp.fov);
        }
        let weapon = r.arsenal.in_hand_id().map_or("", crate::weapons::WeaponId::name);
        let w = r.zoom;
        smooth.zoom_blended = w > 0.0 && rig.zoom.contains_key(weapon);
        if let (Some(z), true) = (rig.zoom.get(weapon).copied(), w > 0.0) {
            pp.fov = base_fov.unwrap_or(pp.fov) * (1.0 - w) + z.fov * w;
            let zeye = rt.translation + yaw * z.eye;
            let zrot = yaw * pitch * Transform::from_translation(z.eye).looking_at(z.target, Vec3::Y).rotation;
            eye = eye.lerp(zeye, w);
            rot = rot.slerp(zrot, w);
        } else if let Some(b) = *base_fov {
            pp.fov = b;
        }
    }
    // the Topsy Turvy bomb turns the view over (topsy.rs); so does the upside-down cheat (WAAAAAAH, cheats.rs)
    let flip = if cheats.on(0x4000) { std::f32::consts::PI } else { 0.0 };
    let view = Transform { translation: eye, rotation: rot * Quat::from_rotation_z(topsy.roll + flip), scale: c.scale };
    *c = place_smoothed(&mut smooth, view, dt, rig.smooth_secs);
}

/// The view with the 0x0040fd50 smoothing while it runs (each frame from the last view shown).
fn place_smoothed(smooth: &mut CameraSmooth, target: Transform, dt: f32, total: f32) -> Transform {
    let out = match smooth.last {
        Some(last) if smooth.left > 0.0 => {
            smooth.left = (smooth.left - dt).max(0.0);
            smooth_step(&last, &target, smooth.left, total)
        }
        _ => target,
    };
    smooth.last = Some(out);
    out
}

/// Takes last frame's shake off the camera before anything places it this frame.
fn unshake(mut shake: ResMut<CameraShake>, mut cam: Query<&mut Transform, With<Camera3d>>) {
    let off = std::mem::take(&mut shake.applied);
    if off != 0.0 {
        if let Ok(mut t) = cam.single_mut() {
            t.translation.y -= off;
        }
    }
}

/// 0x0040fac0: a new shake replaces the running one.
fn start_shakes(mut ev: MessageReader<ShakeCamera>, mut shake: ResMut<CameraShake>) {
    for s in ev.read() {
        shake.amplitude = s.amplitude;
        shake.left = s.secs;
        shake.started += 1;
    }
}

/// 0x0040e300 on the placed camera: while time is left the height moves by the amplitude, which then flips and
/// shrinks (x -0.75 per frame of the original: per 1/60 s here, so the shake looks the same at any frame rate).
fn shake(time: Res<Time>, mut shake: ResMut<CameraShake>, mut cam: Query<&mut Transform, With<Camera3d>>) {
    if shake.left <= 0.0 {
        return;
    }
    let Ok(mut t) = cam.single_mut() else { return };
    let dt = time.delta_secs();
    t.translation.y += shake.amplitude;
    shake.applied = shake.amplitude;
    shake.amplitude *= (-0.75f32).powf((dt * 60.0).round().max(1.0));
    shake.left -= dt;
}

/// Playtest evidence: the camera's distance from Roman's look point (`cam_dist`), its height above him (`cam_up`),
/// its FOV in degrees (`cam_fov`), the shake (`cam_shake` amplitude, `cam_shake_left`) and the angle between the view
/// and the line to Roman's body (`cam_aim_err`, degrees).
#[allow(clippy::too_many_arguments)]
fn log_camera(
    cam: Query<(&GlobalTransform, &Projection), With<Camera3d>>,
    roman: Query<(&Transform, &Roman, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    rig: Res<CameraRig>,
    shake: Res<CameraShake>,
    smooth: Res<CameraSmooth>,
    (path, mounted): (Res<crate::campath::CameraPath>, Res<crate::turret::Mounted>),
    mut ps: ResMut<crate::playtest::PlayState>,
) {
    let (Ok((c, p)), Ok((rt, r, anim))) = (cam.single(), roman.single()) else { return };
    let r2 = |v: f32| (v * 100.0).round() / 100.0;
    // who holds the camera, and for the death camera how far it moved since the death
    let mode = if path.holds_camera() {
        "path"
    } else if mounted.0.is_some() {
        "turret"
    } else if smooth.death.is_some() {
        "death"
    } else {
        "follow"
    };
    ps.0.insert("cam_mode".into(), mode.into());
    ps.0.insert("cam_held".into(), r2(smooth.death.map_or(0.0, |d| d.distance(c.translation()))).into());
    ps.0.insert("look_pitch".into(), r2(r.pitch).into());
    // where the camera itself looks (radians above the horizon): a restart must start at the fresh level's view
    ps.0.insert("cam_pitch".into(), r2(c.forward().y.clamp(-1.0, 1.0).asin()).into());
    let look = rt.translation + Vec3::Y * rig.follow.target.y;
    ps.0.insert("cam_dist".into(), r2(c.translation().distance(look)).into());
    ps.0.insert("cam_up".into(), r2(c.translation().y - rt.translation.y).into());
    ps.0.insert("cam_pos".into(), format!("{:.1},{:.1},{:.1}", c.translation().x, c.translation().y, c.translation().z).into());
    if let Projection::Perspective(pp) = p {
        ps.0.insert("cam_fov".into(), r2(pp.fov.to_degrees()).into());
    }
    ps.0.insert("cam_shake".into(), r2(shake.amplitude.abs() * (shake.left > 0.0) as u8 as f32).into());
    ps.0.insert("cam_shake_left".into(), r2(shake.left.max(0.0)).into());
    ps.0.insert("cam_shakes".into(), shake.started.into());
    let body = anim
        .and_then(|a| a.skeleton.index.get("center").and_then(|&i| a.joints.get(i)))
        .and_then(|j| joints.get(*j).ok())
        .map_or(rt.translation, |g| g.translation());
    let to = (body - c.translation()).normalize_or_zero();
    // how far the camera is from Roman's body: close behind him, he fills the screen (player feedback 2026-10-18)
    ps.0.insert("cam_body_dist".into(), r2(body.distance(c.translation())).into());
    ps.0.insert("cam_aim_err".into(), r2(c.forward().as_vec3().dot(to).clamp(-1.0, 1.0).acos().to_degrees()).into());
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn node_fov_is_made_vertical_for_the_design_aspect() {
        // Camera_end's 60 degrees horizontal -> 37.5 vertical (0.654 rad in the original's camera dumps)
        assert!((vertical_fov(1.0471976) - 0.654).abs() < 2e-3);
        // Camera_rifle 25 degrees
        assert!((vertical_fov(0.4363323).to_degrees() - 14.8).abs() < 0.1);
    }

    #[test]
    fn looking_up_orbits_the_head_and_tilts_the_view_further() {
        let rig = CameraRig::default();
        // at rest: Camera_end, looking at Camera_start's target
        let (eye, view) = rig.orbit(0.0);
        assert!(eye.distance(rig.follow.eye) < 1e-4);
        let fwd = view * Vec3::NEG_Z;
        assert!((fwd - (rig.follow.target - rig.follow.eye).normalize()).length() < 1e-4);
        // looking up 1.0: the eye stays 24 from the head (not 30 from the far look point) and the view turns 1.2
        let (eye, view) = rig.orbit(1.0);
        assert!((eye.distance(rig.pivot) - rig.follow.eye.distance(rig.pivot)).abs() < 1e-3);
        let elev = |q: Quat| (q * Vec3::NEG_Z).y.asin();
        assert!((elev(view) - elev(rig.orbit(0.0).1) - 1.2).abs() < 1e-3);
        // looking down: 0.9
        assert!((elev(rig.orbit(-1.0).1) - elev(rig.orbit(0.0).1) + 0.9).abs() < 1e-3);
    }

    #[test]
    fn smoothing_reaches_the_target_when_the_time_runs_out() {
        let last = Transform::from_xyz(0.0, 10.0, 0.0);
        let target = Transform::from_xyz(10.0, 10.0, 0.0);
        // a second left of one: still the last view
        assert_eq!(smooth_step(&last, &target, 1.0, 1.0).translation, last.translation);
        assert_eq!(smooth_step(&last, &target, 0.0, 1.0).translation, target.translation);
        assert!((smooth_step(&last, &target, 0.25, 1.0).translation.x - 7.5).abs() < 1e-5);
    }
}
