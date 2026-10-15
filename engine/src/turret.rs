//! Mounted guns (StigTurret, vtable 0x005a19d8; behavior "stig_mgun_turret": lev_1_m1's stig_mgun 32 at the archway).
//! The spec, with every address, is docs/subsystems/turret.md. In short:
//! - Entering: within message_radius (30, 3D) of the gun and able to act, the prompt ui_vehicle_entry ("Press %s to
//!   enter mounted gun", 0x004662a0) shows; Use (action 0x14) mounts (0x004788a0 -> vt+0x178 0x00498f60 ->
//!   0x00498a80): Roman is hidden with his weapons (vt+0xf4(0), 0x004a0e00(0)) and attached at ref_player, the camera
//!   takes the gun's camera node camera_bone_name (Camera01, a child of the pivot bone), "wpn_stig_mount" plays and
//!   Game+0xac8 = 4. Use again leaves (vt+0x17c 0x00498fb0 -> 0x00498bb0): Roman shown, put at player_exit_bone_name
//!   (ref_exit), grounded (Object_ForceGround 10, 20), the angles reset to 0, the move loop stopped. ui_vehicle_exit
//!   shows while mounted (0x00466210). Death while mounted leaves the gun (0x0047ae40).
//! - Aiming (update 0x004990a0): +0x620 (about z) += (TurnLeft - TurnRight) * turn_max_speed * dt, +0x61c (about x) +=
//!   (LookDown - LookUp) * pitch_max_speed * dt, clamped to yaw_range / pitch_range (degrees; positive x = muzzle
//!   down); the pivot bone callback 0x00498ff0 turns pivot_bone_name: rest * Rz(+0x620) * Rx(+0x61c). Moving plays
//!   the loop "wpn_stig_move_lp", still stops it.
//! - Firing: Fire held (button 8) -> ready 0x004988e0 (fx flag 0x40; recycle_time since the last shot) -> 0x00498960:
//!   a projectile_name shot from emitter_bone_name to the crosshair's point (0x0047dbf0, crosshair ray 2000 long in
//!   mode 4), a shot counted (Game+0x2718), the muzzle fx (rapid_fire: one held fx_muzzle_flash_stig_mgun on
//!   muzzle_bone_name, 0x00497fd0, released two updates after the trigger is let go, 0x00497ed0). No ammo, no heat.
//! - The player takes half damage while mounted (0x0047afa0, player.rs hook).
//!
//! Hooks outside this file: player.rs (control / weapons skip Roman while mounted, follow_camera yields, damage
//! halved), hud.rs (crosshair range), mission.rs (`sites` and `hint` visible for ordering and the hint texts).

use crate::levelreset::LevelAppExt;
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::transform::TransformSystems;

use crate::input::Act;

/// The gun the player is on (Game+0xacc while Game+0xac8 = 4).
#[derive(Resource, Default)]
pub struct Mounted(pub Option<Entity>);

/// One mounted gun's state and its property values (0x004980c0 / 0x00498d60).
#[derive(Component)]
pub struct Turret {
    /// +0x620 (about the pivot's z) and +0x61c (about its x), radians
    yaw: f32,
    pitch: f32,
    turn_speed: f32,
    pitch_speed: f32,
    yaw_range: (f32, f32),
    pitch_range: (f32, f32),
    radius: f32,
    recycle: f32,
    projectile: String,
    muzzle_fx: Option<String>,
    /// bones (skeleton indices): pivot, emitter, muzzle, player, exit
    pivot: usize,
    emitter: usize,
    muzzle: usize,
    seat: usize,
    exit: Option<usize>,
    /// the camera node relative to the pivot bone (game model space)
    camera: Mat4,
    /// time of the last shot (+0x524)
    last_fire: Option<f32>,
    /// muzzle fx state bits 0x40 / 0x80 of +0x530 and the held fx's anchor (+0x534)
    fx_flags: u8,
    fx_anchor: Option<Entity>,
    /// the move loop playing (+0x65c)
    moving: bool,
    /// the AI side (shared with Roman's mounting, as the original's one object): the AI occupant +0x628 (flag 4 of
    /// +0x530: hides Roman's prompt, 0x00498760), the grunt walking to it (flag 0x10, vt+0x15c(1): the AI search
    /// skips it, Roman's prompt does not), ai_can_use (flag 0x100), ai_never_leave (bit 9 clear when set), the AI
    /// projectile (ai_projectile_name +0x480) and ref_grunt_exit
    pub ai: Option<Entity>,
    pub reserved: Option<Entity>,
    pub ai_can_use: bool,
    pub never_leave: bool,
    pub ai_projectile: String,
    pub grunt_exit: Option<usize>,
}

impl Turret {
    /// An AI gunner's aim (0x00408c70: the gun's four corner poses stig_mgun_*_tl/tr/bl/br blended by the gunner's
    /// weights +0x580 horizontal (0 left .. 1 right) and +0x584 vertical (0 bottom .. 1 top)). The corners turn the
    /// pivot by about +-65 degrees about z and +-25 about x (stig_mgun_idle_tl center = qz(65) * qx(-25)); the blend is
    /// taken as the angles' blend (guess at the quaternion blend's difference, under 1 degree inside the corners).
    pub fn ai_aim(&mut self, h: f32, v: f32) {
        self.yaw = 65f32.to_radians() * (1.0 - 2.0 * h);
        self.pitch = 25f32.to_radians() * (1.0 - 2.0 * v);
    }

    /// The muzzle bone's world matrix and others for the AI side.
    pub fn bone_world(&self, gt: &GlobalTransform, a: &crate::skin::Animator, i: usize) -> Mat4 {
        gt.to_matrix() * bone_model(&a.skeleton, i, self.pivot, turn(self))
    }

    pub fn emitter(&self) -> usize {
        self.emitter
    }

    /// The gun's forward at rest (model -y through the pivot), world.
    pub fn forward(&self, gt: &GlobalTransform, a: &crate::skin::Animator) -> Vec3 {
        (gt.to_matrix() * bone_model(&a.skeleton, self.pivot, usize::MAX, Quat::IDENTITY)).transform_vector3(Vec3::NEG_Y)
    }

    /// A shot's muzzle flash (vt+0x144 0x00497fd0 with rapid_fire): one held fx on the muzzle bone, kept while shots
    /// come (flag 0x40), released two updates after the last (0x00497ed0).
    pub fn flash(&mut self, commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, a: &crate::skin::Animator, joints: &Query<&GlobalTransform>) {
        self.fx_flags = (self.fx_flags | 0x40) & !0x80;
        if self.fx_anchor.is_none() {
            if let (Some(name), Some(&joint)) = (self.muzzle_fx.clone(), a.joints.get(self.muzzle)) {
                let anchor = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
                commands.entity(joint).add_child(anchor);
                let jm = joints.get(joint).map(|j| j.compute_transform()).unwrap_or_default();
                fx.write(crate::fx::StartFx { name, at: jm.translation, rotation: jm.rotation, follow: Some(anchor), position_only: false });
                self.fx_anchor = Some(anchor);
            }
        }
    }

    /// The muzzle fx step of the update (0x00498710 -> 0x00497ed0): 0x40 set -> clear; else 0x80 clear -> set; else
    /// release.
    fn fx_step(&mut self, commands: &mut Commands) {
        if self.fx_flags & 0x40 != 0 {
            self.fx_flags &= !0x40;
        } else if self.fx_flags & 0x80 == 0 {
            self.fx_flags |= 0x80;
        } else if let Some(anchor) = self.fx_anchor.take() {
            commands.entity(anchor).despawn();
        }
    }
}

pub struct TurretPlugin;

impl Plugin for TurretPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<Mounted>()
            .add_systems(Update, setup)
            .add_systems(Update, ride.after(crate::mission::sites).after(crate::player::grab_cursor).run_if(crate::playing))
            .add_systems(PostUpdate, pose.after(crate::skin::AnimateSet).before(TransformSystems::Propagate));
    }
}

/// A bone's model-space matrix (game frame) from the skeleton's rest pose, with the pivot turned.
fn bone_model(sk: &crate::skin::Skeleton, i: usize, pivot: usize, turn: Quat) -> Mat4 {
    let mut m = Mat4::IDENTITY;
    let mut j = i as i32;
    while j >= 0 {
        let k = j as usize;
        let mut t = sk.rest[k];
        if k == pivot {
            t.rotation *= turn;
        }
        m = t.to_matrix() * m;
        j = sk.parents[k];
    }
    m
}

/// The pivot callback's rotation (0x00498ff0: dest = source * qz(+0x620) * qx(+0x61c), Quat_Mul(a, b) = b * a).
fn turn(t: &Turret) -> Quat {
    Quat::from_rotation_z(t.yaw) * Quat::from_rotation_x(t.pitch)
}

/// Mounted-gun components for the placed guns (their model must be skinned: tools/remaster_export_anim.py
/// RIGID_SKINNED).
fn setup(
    mut commands: Commands,
    data: Res<crate::gamedata::GameData>,
    lib: Res<crate::skin::AnimLibrary>,
    q: Query<(Entity, &Name, &crate::skin::Animator), (With<crate::level::InstanceId>, Without<Turret>)>,
    mut done: crate::levelreset::PerLevel<std::collections::HashSet<Entity>>,
) {
    let done = done.get();
    for (e, name, a) in &q {
        if !done.insert(e) {
            continue;
        }
        let p = data.template_props(name.as_str());
        if p.s("behavior") != Some("stig_mgun_turret") {
            continue;
        }
        let bone = |k: &str| p.s(k).and_then(|n| a.skeleton.index.get(&n.to_lowercase()).copied());
        let (Some(pivot), Some(emitter), Some(seat)) = (bone("pivot_bone_name"), bone("emitter_bone_name"), bone("player_bone_name")) else {
            warn!("turret {name}: bones missing");
            continue;
        };
        // the camera node: the reference anim's camera (world in the model) taken into the pivot's frame (the corner
        // anims stig_mgun_idle_* parent Camera01 to center)
        let model = data.templates.get(name.as_str()).and_then(|t| t.model.clone()).unwrap_or_default();
        let read = |path: std::path::PathBuf| std::fs::read_to_string(path).ok().and_then(|s| serde_json::from_str::<serde_json::Value>(&s).ok());
        let reference = read(lib.dir.join("skeletons").join(format!("{model}.json"))).and_then(|v| v["reference_anim"].as_str().map(str::to_owned)).unwrap_or_default();
        let cam_name = p.s("camera_bone_name").unwrap_or("camera01").to_lowercase();
        let cam = read(lib.dir.join("anims").join(format!("{reference}.json"))).and_then(|v| {
            let cams = v["cameras"].as_object()?.clone();
            let (_, c) = cams.into_iter().find(|(k, _)| k.to_lowercase() == cam_name)?;
            let t = c["t"][0].as_array()?.iter().skip(1).filter_map(|x| x.as_f64()).map(|x| x as f32).collect::<Vec<_>>();
            let r = c["r"][0].as_array()?.iter().skip(1).filter_map(|x| x.as_f64()).map(|x| x as f32).collect::<Vec<_>>();
            Some(Mat4::from_rotation_translation(Quat::from_xyzw(r[0], r[1], r[2], r[3]).normalize(), Vec3::new(t[0], t[1], t[2])))
        });
        let Some(cam) = cam else {
            warn!("turret {name}: no camera node {cam_name} in {reference}");
            continue;
        };
        let camera = bone_model(&a.skeleton, pivot, usize::MAX, Quat::IDENTITY).inverse() * cam;
        let deg = |k: &str, d: f32| p.f(k, d).to_radians();
        commands.entity(e).insert(Turret {
            yaw: 0.0,
            pitch: 0.0,
            turn_speed: p.f("turn_max_speed", 2.0),
            pitch_speed: p.f("pitch_max_speed", 2.0),
            yaw_range: (deg("yaw_range_min", -45.0), deg("yaw_range_max", 45.0)),
            pitch_range: (deg("pitch_range_min", -45.0), deg("pitch_range_max", 45.0)),
            radius: p.f("message_radius", 20.0),
            recycle: p.f("recycle_time", 1.0),
            projectile: format!("p_{}", p.s("projectile_name").unwrap_or("")),
            muzzle_fx: p.s("muzzle_fx_name").map(str::to_owned),
            pivot,
            emitter,
            muzzle: bone("muzzle_bone_name").unwrap_or(emitter),
            seat,
            exit: bone("player_exit_bone_name"),
            camera,
            last_fire: None,
            fx_flags: 0,
            fx_anchor: None,
            moving: false,
            ai: None,
            reserved: None,
            ai_can_use: p.i("ai_can_use", 1) != 0,
            never_leave: p.i("ai_never_leave", 0) != 0,
            ai_projectile: format!("p_{}", p.s("ai_projectile_name").or(p.s("projectile_name")).unwrap_or("")),
            grunt_exit: a.skeleton.index.get("ref_grunt_exit").copied(),
        });
        info!("turret {name}: pivot {pivot} camera {:?}", camera.w_axis.truncate());
    }
}

/// The clamp of a turret angle, skipped when either limit is 0 (0x004990a0).
fn clamp(v: f32, (lo, hi): (f32, f32)) -> f32 {
    if lo == 0.0 || hi == 0.0 { v } else { v.clamp(lo, hi) }
}

#[allow(clippy::too_many_arguments)]
pub(crate) fn ride(
    mut commands: Commands,
    time: Res<Time>,
    (ctl, motion, look, playtest, opts): (Res<crate::input::Controls>, Res<AccumulatedMouseMotion>, Res<crate::player::LookDelta>, Option<Res<crate::playtest::Playtest>>, Res<crate::options::GameOptions>),
    mut mounted: ResMut<Mounted>,
    mut turrets: Query<(Entity, &mut Turret, &GlobalTransform, &crate::skin::Animator)>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &mut Transform, &crate::fx::Health, &mut Visibility)>,
    (mut physics, path, topsy): (NonSendMut<crate::physics::Physics>, Res<crate::campath::CameraPath>, Res<crate::topsy::Topsy>),
    (mut hud, mut mission, xhair): (ResMut<crate::hud::HudState>, Option<ResMut<crate::mission::Mission>>, Res<crate::hud::Crosshair>),
    (mut sounds, mut stop, mut fx): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSound>, MessageWriter<crate::fx::StartFx>),
    (data, mut stats, mut state): (Res<crate::gamedata::GameData>, ResMut<crate::player::PlayerStats>, ResMut<crate::playtest::PlayState>),
    joints: Query<&GlobalTransform>,
) {
    let Ok((re, mut r, mut rt, health, mut vis)) = roman.single_mut() else { return };
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let dead = r.dead || health.0 <= 0.0;
    // PlayerCanAct (0x0047a950): alive, no camera path, not in the topsy states
    let can_act = !dead && !path.holds_camera() && !topsy.active();
    let use_pressed = ctl.pressed(Act::Use);
    let hint = |m: &Option<ResMut<crate::mission::Mission>>, k: &str| m.as_ref().map(|m| m.hint(k)).unwrap_or_default();
    let mut log = Vec::new();
    let world_of = |gt: &GlobalTransform, a: &crate::skin::Animator, t: &Turret, i: usize| gt.to_matrix() * bone_model(&a.skeleton, i, t.pivot, turn(t));

    if let Some(me) = mounted.0 {
        // a mount of another kind is not this module's (defence.rs: the base turret)
        let Ok((_, mut t, gt, a)) = turrets.get_mut(me) else {
            return;
        };
        // the muzzle fx step (0x00498710 -> 0x00497ed0)
        t.fx_step(&mut commands);
        // leaving: Use again (0x004788a0 -> vt+0x17c), or death (0x0047ae40)
        if dead || use_pressed {
            if let Some(anchor) = t.fx_anchor.take() {
                commands.entity(anchor).despawn();
            }
            if t.moving {
                t.moving = false;
                stop.write(crate::audio::StopSound("wpn_stig_move_lp".into()));
            }
            // the exit bone's position (before the angles reset: ref_exit is a root bone), grounded (Object_ForceGround
            // 10, 20: a ray from 10 above, 20 down)
            let at = t.exit.map(|i| world_of(gt, a, &t, i).w_axis.truncate()).unwrap_or(rt.translation);
            let feet = physics.raycast(at + Vec3::Y * 10.0, -Vec3::Y, 20.0).map(|h| at.with_y(h.1.y)).unwrap_or(at);
            physics.place_walker(&r.walker, feet);
            rt.translation = feet;
            r.vel = Vec3::ZERO;
            r.ground_ref = feet.y;
            r.grace = 0.5;
            r.falling = false;
            r.fall_fast_t = 0.0;
            r.peak_vz = 0.0;
            *vis = Visibility::Inherited;
            t.yaw = 0.0;
            t.pitch = 0.0;
            t.fx_flags = 0;
            mounted.0 = None;
            log.push("mounted gun left".to_string());
        } else {
            // aiming: the mouse as the player's turn (0.0100 rad / count at turn speed 0.3 x 10) and pitch (0.0057 rad /
            // count at 0.95) scaled to the gun's speeds; keys and the stick at full deflection = the speed in rad/s
            let d = if playtest.is_some() { motion.delta } else { look.0 };
            let k = opts.mouse_sensitivity + 0.5;
            let inv = if opts.invert_mouse { -1.0 } else { 1.0 };
            let stick = ctl.axis(Act::Look);
            let (y0, p0) = (t.yaw, t.pitch);
            let yaw = t.yaw - d.x * 0.0100 / 3.0 * t.turn_speed * k - stick.x * t.turn_speed * dt;
            let pitch = t.pitch + d.y * 0.0057 / 0.95 * t.pitch_speed * k * inv - stick.y * t.pitch_speed * dt * inv;
            t.yaw = clamp(yaw, t.yaw_range);
            t.pitch = clamp(pitch, t.pitch_range);
            let moved = (t.yaw - y0).abs() > 0.0 || (t.pitch - p0).abs() > 0.0;
            if moved && !t.moving {
                t.moving = true;
                sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::at("wpn_stig_move_lp", gt.translation()) });
            } else if !moved && t.moving {
                t.moving = false;
                stop.write(crate::audio::StopSound("wpn_stig_move_lp".into()));
            }
            // Roman stays at the seat (Object_Attach to player_bone_name)
            let seat = world_of(gt, a, &t, t.seat).w_axis.truncate();
            physics.place_walker(&r.walker, seat);
            rt.translation = seat;
            // firing (button 8 held: 0x004988e0 ready, 0x00498960 fire)
            if ctl.down(Act::Fire) || ctl.pressed(Act::Fire) {
                t.fx_flags = (t.fx_flags | 0x40) & !0x80;
                if t.last_fire.is_none_or(|l| now - l > t.recycle) {
                    let em = world_of(gt, a, &t, t.emitter);
                    let from = em.w_axis.truncate();
                    // the gun's own aim (bone -y) when the crosshair has no point yet
                    let fwd = em.transform_vector3(Vec3::NEG_Y).normalize_or(Vec3::NEG_Z);
                    let dir = xhair.point.map(|q| (q - from).normalize_or(fwd)).filter(|d| d.dot(fwd) > 0.0).unwrap_or(fwd);
                    let bullet = data.props(&t.projectile);
                    crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, from, dir, true, Some(re));
                    stats.shots += 1;
                    t.last_fire = Some(now);
                    // the muzzle flash (0x00497fd0, rapid fire: one held fx on the muzzle bone)
                    t.flash(&mut commands, &mut fx, a, &joints);
                }
            }
            if hud.prompt.is_none() {
                hud.prompt = Some(hint(&mission, "ui_vehicle_exit"));
            }
        }
        state.0.insert("turret".into(), serde_json::json!({"mounted": mounted.0.is_some(), "yaw": t.yaw.to_degrees(), "pitch": t.pitch.to_degrees(), "ai": false}));
    } else {
        // the muzzle fx step of the guns an AI uses
        for (_, mut t, _, _) in &mut turrets {
            t.fx_step(&mut commands);
        }
        // the prompt (0x004662a0): the first gun not held by an AI (0x00498760: flag 4 clear; a grunt only walking to it
        // does not hide it) whose message_radius reaches the player (3D distance)
        let near = turrets.iter().find(|(_, t, gt, _)| t.ai.is_none() && gt.translation().distance(rt.translation) <= t.radius).map(|(e, ..)| e);
        let ai = turrets.iter().find_map(|(_, t, gt, _)| t.ai.map(|_| (t.yaw.to_degrees(), t.pitch.to_degrees(), gt.translation())));
        if let (Some(e), true) = (near, can_act) {
            if hud.prompt.is_none() {
                hud.prompt = Some(hint(&mission, "ui_vehicle_entry"));
            }
            if use_pressed {
                // 0x00498f60 / 0x00498a80
                if let Ok((_, t, gt, a)) = turrets.get(e) {
                    let seat = world_of(gt, a, t, t.seat).w_axis.truncate();
                    physics.place_walker(&r.walker, seat);
                    rt.translation = seat;
                    // facing the gun's forward (model -y)
                    let fwd = (gt.to_matrix() * bone_model(&a.skeleton, t.pivot, usize::MAX, Quat::IDENTITY)).transform_vector3(Vec3::NEG_Y);
                    r.yaw = (-fwd.x).atan2(-fwd.z);
                    r.zoom = 0.0;
                    r.vel = Vec3::ZERO;
                    *vis = Visibility::Hidden;
                    sounds.write(crate::audio::PlaySound::ui("wpn_stig_mount"));
                    mounted.0 = Some(e);
                    hud.prompt = Some(hint(&mission, "ui_vehicle_exit"));
                    log.push("mounted gun entered".to_string());
                }
            }
        }
        state.0.insert("turret".into(), serde_json::json!({"mounted": false, "near": near.is_some(), "ai": ai.is_some(), "yaw": ai.map(|a| a.0).unwrap_or(0.0), "pitch": ai.map(|a| a.1).unwrap_or(0.0)}));
    }
    if let Some(m) = mission.as_mut() {
        m.log.extend(log);
    }
}

/// The pivot bone turned by the angles (0x00498ff0), and while mounted the camera at the gun's camera node (Camera_Update
/// 0x0040e350 with only a camera name: the node's matrix; its FOV 0.654 vertical is the follow camera's).
fn pose(
    mounted: Res<Mounted>,
    turrets: Query<(Entity, &Turret, &GlobalTransform, &crate::skin::Animator)>,
    mut joints: Query<&mut Transform, Without<crate::player::FollowCam>>,
    mut cam: Query<&mut Transform, With<crate::player::FollowCam>>,
) {
    for (e, t, gt, a) in &turrets {
        if let (Some(&j), Some(rest)) = (a.joints.get(t.pivot), a.skeleton.rest.get(t.pivot)) {
            if let Ok(mut jt) = joints.get_mut(j) {
                jt.rotation = rest.rotation * turn(t);
            }
        }
        if mounted.0 == Some(e) {
            if let Ok(mut c) = cam.single_mut() {
                let m = gt.to_matrix() * bone_model(&a.skeleton, t.pivot, t.pivot, turn(t)) * t.camera;
                let (_, rot, pos) = m.to_scale_rotation_translation();
                c.translation = pos;
                c.rotation = rot;
            }
        }
    }
}
