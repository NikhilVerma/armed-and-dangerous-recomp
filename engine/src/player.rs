//! The player, Roman (template `trotter`, property set `p_trotter`), and his weapons, from the game's data:
//! movement speeds, jump, health (p_trotter), the rifle and its projectile (p_player_rifle / proj_rifle_bullet
//! values via docs/subsystems/player.md), sticky bombs (p_player_sticky*), the level's starting loadout (the
//! mission instance: player_add_rifle). Controls and the camera follow are STAND-INS until the original's
//! input map and camera nodes are ported (docs/subsystems/player.md, camera.md).

use crate::fx::{Destructible, Fuse, Health};
use crate::gamedata::{GameData, Props};
use crate::level::game_to_engine;
use crate::physics::{Body, Physics, Walker};
use bevy::input::mouse::AccumulatedMouseMotion;
use bevy::prelude::*;
use bevy::window::{CursorGrabMode, CursorOptions, PrimaryWindow};
use oxijolt::prelude::*;

#[derive(Resource)]
pub struct BombAssets {
    pub parts: Vec<(Handle<Mesh>, Handle<StandardMaterial>)>,
    pub shape: Shape,
    /// The model's rest rotation (game axes to engine axes).
    pub base: Quat,
}

/// Roman. Values from p_trotter (instance values over the template's).
#[derive(Component)]
pub struct Roman {
    pub walker: Walker,
    pub yaw: f32,
    pub pitch: f32,
    pub vel: Vec3,
    pub on_ground: bool,
    /// falling and landing (0x00478200, player.md "Landing and falling"): the grounded grace (+0x570, 0.5 s after the
    /// last contact), the ground reference height (+0x520), the most negative vertical speed in the air (+0x56c),
    /// the time falling faster than fall_speed (+0xa60), the falling state (+0xa34 0x200), and the landing anim
    /// playing (0x100: its name, time in it, length)
    pub grace: f32,
    pub ground_ref: f32,
    pub peak_vz: f32,
    pub fall_fast_t: f32,
    pub falling: bool,
    pub landing: Option<(&'static str, f32, f32)>,
    /// remaster safety net (not in the original): the last place where Roman moved freely, and how long he has
    /// been pushing without moving (wedged in a collider)
    pub free_trail: std::collections::VecDeque<Vec3>,
    pub free_t: f32,
    pub wedged: f32,
    /// zoom (mode +0xacc, held Zoom: Z / E): the blend weight toward the weapon's camera node, 0..1 over
    /// camera_zoom_secs (0x0040fbb0)
    pub zoom: f32,
    pub zoom_secs: f32,
    /// in the jump state (flag 0x80) from the take-off to the landing; a plain fall keeps the ground drive
    pub jumping: bool,
    pub fwd_speed: f32,
    pub side_speed: f32,
    /// movement_power_max (full stick: keys), jump_fwd / side_max_speed and jump_movement_power (PlayerObject_Load)
    pub power: f32,
    pub jump_fwd_speed: f32,
    pub jump_side_speed: f32,
    pub jump_power: f32,
    pub jump_impulse: f32,
    pub fall_speed: f32,
    pub anti_gravity: f32,
    pub max_health: f32,
    pub rifle: Weapon,
    /// the Flemming machine gun once taken in the pub (Roman's second weapon slot)
    pub mg: Option<Weapon>,
    /// the machine gun is in hand (Next Weapon: Q)
    pub use_mg: bool,
    /// Topsy Turvy bombs (a special from the pub; its throw is not ported)
    pub topsy: u32,
    /// the machine gun's fire loop is playing
    pub mg_firing: bool,
    /// the special in hand (Next Special: Tab): 0 sticky bombs, 1 Topsy Turvy bomb
    pub special: u8,
    pub stickies: u32,
    pub dead: bool,
    /// the death presentation started (fx_trotter_death, the ragdoll), and whether the ragdoll took
    pub death_shown: bool,
    pub ragdolled: bool,
    pub base: Quat,
    /// seconds since the last shot, and of reload left (for the anims)
    pub since_fire: f32,
    /// the hit reaction (PlayerObject_OnHit 0x0047afa0, anim update 0x00476a10): trotter_hit_small (< 65 damage) or
    /// trotter_hit_med, full body, seconds left (0: none); it plays only standing still and the latch (+0xa34 0x4000)
    /// holds further knockback while it plays
    pub hit_anim: f32,
    /// the last hit's direction of travel (for the hit-side marker), the pain-line gate, the low-health breath timer
    /// and whether the death line played (PlayerObject_OnHit 0x0047afa0, game_06.cpp:2155, vt+0xd4)
    pub hit_from: Option<Vec3>,
    pub pain_t: f32,
    pub breath_t: f32,
    pub death_said: bool,
    pub hit_med: bool,
    /// the drive slow-down after a knockback (+0x9b4 from +0x9b8 = 1 s)
    pub slow: f32,
    /// movement_power_min (the drive's power at no stick)
    pub power_min: f32,
    /// seconds since Fire Special threw a sticky bomb (the throw anim)
    pub since_throw: f32,
    pub reloading: f32,
    /// a reload in progress: seconds into the reload anim, and whether its load sound played (0x00475ff0 state 3:
    /// the sound at frame 0x0059eef4[w] (rifle 10, machine gun 15), the clip refilled at frame 0x0059eedc[w] = 43)
    pub reload_clock: Option<(f32, bool)>,
    /// a weapon switch in progress: seconds into the weapon-change anim (trotter_wchange); it completes at frame 13
    /// (0x00475ff0 -> 0x004a16b0)
    pub wchange: Option<f32>,
    pub move_local: Vec2,
}

#[derive(Clone)]
pub struct Weapon {
    pub name: &'static str,
    /// its property set (projectile, muzzle fx)
    pub props: &'static str,
    /// aim spread in % (machinegun_spread_pct)
    pub spread: f32,
    /// the reserve is unlimited (weapon type 2: the HUD shows "%d/" and the infinity sign)
    pub unlimited: bool,
    pub recycle: f32,
    pub reload_time: f32,
    pub clip_size: u32,
    pub clip: u32,
    pub ammo: u32,
    pub speed: f32,
    pub damage: f32,
    pub range: f32,
    pub impact_force: f32,
    pub timer: f32,
}

/// A projectile in flight (the game's bullets are projectiles, not hitscan): moves `vel` per second, hits the
/// first thing on its path each frame.
#[derive(Component)]
pub struct Projectile {
    pub vel: Vec3,
    /// impact_force (Projectile_Load default 2010)
    pub impact: f32,
    pub life: f32,
    pub damage: f32,
    pub from_player: bool,
    pub owner: Option<Entity>,
    /// impact fx base name (fx_end_material + "_" + the hit material)
    pub fx_end: Option<String>,
    /// head_shot_multiplier / torso_shot_multiplier (Projectile_ApplyHitDamage 0x0047eec0; code defaults 1)
    pub head_mult: f32,
    pub torso_mult: f32,
    /// damages_buildings off (the default): its damage record carries 0x400 (0x0047eba0), which buildings and
    /// objects without takes_bullet_damage ignore
    pub no_buildings: bool,
}

/// A projectile of a property set (speed x3 as Projectile_Load 0x0047e0c0; damage, lifetime, fx_start trail
/// following it, fx_end_material on impact).
/// Bones that stand for collision parts in the hit test (the main body bones, as the ragdoll parts).
/// Whether the segment from `from` along `seg` passes within a body part's radius of the skeleton (bone to parent).
fn ray_hits_parts(a: &crate::skin::Animator, joints: &Query<&GlobalTransform>, from: Vec3, seg: Vec3) -> bool {
    let pos = |i: usize| joints.get(a.joints[i]).ok().map(|j| j.translation());
    a.skeleton.names.iter().enumerate().any(|(i, n)| {
        let n = n.trim().to_lowercase();
        if !BODY_PARTS.contains(&n.as_str()) {
            return false;
        }
        let r = match n.as_str() {
            "head" | "neck" => 0.8,
            "chest" | "belly" | "center" => 1.0,
            _ => 0.5,
        };
        let Some(b) = pos(i) else { return false };
        let pa = a.skeleton.parents.get(i).copied().filter(|&q| q >= 0).and_then(|q| pos(q as usize)).unwrap_or(b);
        segment_distance(from, from + seg, pa, b) < r
    })
}

/// Shortest distance between segments p0-p1 and q0-q1.
fn segment_distance(p0: Vec3, p1: Vec3, q0: Vec3, q1: Vec3) -> f32 {
    let (d1, d2, r) = (p1 - p0, q1 - q0, p0 - q0);
    let (a, e, f) = (d1.length_squared(), d2.length_squared(), d2.dot(r));
    let (s, t) = if a <= 1e-9 && e <= 1e-9 {
        (0.0, 0.0)
    } else if a <= 1e-9 {
        (0.0, (f / e).clamp(0.0, 1.0))
    } else {
        let c = d1.dot(r);
        if e <= 1e-9 {
            ((-c / a).clamp(0.0, 1.0), 0.0)
        } else {
            let b = d1.dot(d2);
            let den = a * e - b * b;
            let mut s = if den > 1e-9 { ((b * f - c * e) / den).clamp(0.0, 1.0) } else { 0.0 };
            let mut t = (b * s + f) / e;
            if t < 0.0 {
                t = 0.0;
                s = (-c / a).clamp(0.0, 1.0);
            } else if t > 1.0 {
                t = 1.0;
                s = ((b - c) / a).clamp(0.0, 1.0);
            }
            (s, t)
        }
    };
    (p0 + d1 * s).distance(q0 + d2 * t)
}

/// Roman's mass for the knockback (his Havok body's: density x collision volume, not in the data; fitted to the
/// original's push in the second encounter, fight2O: ~13 units back over the fight)
const ROMAN_MASS: f32 = 80.0;

/// Damage feedback (PlayerObject_OnHit 0x0047afa0 and the update, game_06.cpp:2155): the hit side (the bullet's
/// travel against Roman's heading: dot >= 0.7 from behind, <= -0.7 from the front, else left / right) lights its
/// ui_hit_* marker for damage_flash_duration (p_globalsettings, 0.1 s; 0x004670f0); a pain line rom50069..73_Hurt at
/// most once a second (the fallback of the voice pool 0x0064a300, whose entries are filled at run time: guess that
/// the fallback is what plays); at or below 20% health one of fol_breath_heavy_1..4 (table 0x005fb9bc) every second;
/// on death one of rom50063..68_Dead (table 0x005ce334).
fn damage_feedback(
    time: Res<Time>,
    data: Res<GameData>,
    mut q: Query<(&mut Roman, &Transform, &Health)>,
    mut hud: ResMut<crate::hud::HudState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let dt = time.delta_secs();
    for f in hud.hit_flash.iter_mut() {
        *f = (*f - dt).max(0.0);
    }
    let Ok((mut r, t, h)) = q.single_mut() else { return };
    let mut pick = |names: &[&str]| names[((rng.rand() * names.len() as f32) as usize).min(names.len() - 1)].to_owned();
    if r.dead || h.0 <= 0.0 {
        if !r.death_said {
            r.death_said = true;
            let n = pick(&["rom50063_dead", "rom50064_dead", "rom50065_dead", "rom50066_dead", "rom50067_dead", "rom50068_dead"]);
            sounds.write(crate::audio::PlaySound::at(&n, t.translation));
        }
        return;
    }
    if let Some(d) = r.hit_from.take() {
        let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
        let right = Quat::from_rotation_y(r.yaw) * Vec3::X;
        let d = d.with_y(0.0).normalize_or_zero();
        let dot = d.dot(fwd);
        // 0 top (from the front), 1 bottom (from behind), 2 left, 3 right
        let side = if dot >= 0.7 { 1 } else if dot <= -0.7 { 0 } else if d.dot(right) > 0.0 { 2 } else { 3 };
        hud.hit_flash[side] = data.props("p_globalsettings").f("damage_flash_duration", 0.1);
        if r.pain_t <= 0.0 {
            r.pain_t = 1.0;
            let n = pick(&["rom50069_hurt", "rom50070_hurt", "rom50071_hurt", "rom50072_hurt", "rom50073_hurt"]);
            sounds.write(crate::audio::PlaySound::at(&n, t.translation));
        }
    }
    r.pain_t -= dt;
    if h.0 <= r.max_health * 0.2 {
        r.breath_t -= dt;
        if r.breath_t <= 0.0 {
            r.breath_t = 1.0;
            let n = pick(&["fol_breath_heavy_1", "fol_breath_heavy_2", "fol_breath_heavy_3", "fol_breath_heavy_4"]);
            sounds.write(crate::audio::PlaySound::at(&n, t.translation));
        }
    } else {
        r.breath_t = 0.0;
    }
}

const BODY_PARTS: [&str; 16] = ["head", "neck", "chest", "belly", "center", "l_shoulder", "r_shoulder", "l_arm", "r_arm", "l_wrist", "r_wrist", "l_leg", "r_leg", "l_shin", "r_shin", "l_ankle"];

pub fn spawn_projectile(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, p: &Props, from: Vec3, dir: Vec3, from_player: bool, owner: Option<Entity>) {
    let speed = p.f("speed", 300.0) * 3.0;
    let e = commands
        .spawn((
            // its game frame (as placed objects carry): game +Y along the flight, game +Z up, so the fx scripts
            // attached to it (fx_start trails) run in the frame they were authored in
            Transform::from_translation(from).with_rotation(Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation * crate::level::game_to_engine()),
            Projectile {
                vel: dir * speed,
                life: p.f("lifetime", 4.0).min(3.0),
                damage: p.f("damage", 10.0),
                from_player,
                owner,
                fx_end: p.s("fx_end_material").map(str::to_owned),
                head_mult: p.f("head_shot_multiplier", 1.0),
                torso_mult: p.f("torso_shot_multiplier", 1.0),
                impact: p.f("impact_force", 2010.0),
                no_buildings: p.f("damages_buildings", 0.0) == 0.0,
            },
        ))
        .id();
    if let Some(name) = p.s("fx_start") {
        fx.write(crate::fx::StartFx { name: name.to_owned(), at: from, rotation: Quat::IDENTITY, follow: Some(e), position_only: false });
    }
}

/// The third-person camera rig around Roman.
#[derive(Component)]
pub struct FollowCam;

#[derive(Resource, Default)]
pub struct PlayerStats {
    /// head shots by the player (Game+0x2714)
    pub head_shots: u32,
    pub health: f32,
    pub max_health: f32,
    pub clip: u32,
    pub ammo: u32,
    pub stickies: u32,
    pub position: Vec3,
    pub kills: u32,
    pub shots: u32,
    /// player shots that hit a character (Game+0x271c)
    pub hits: u32,
    pub weapon: &'static str,
    pub unlimited: bool,
    /// the special in hand: 0 sticky bombs, 1 the Topsy Turvy bomb; and its count
    pub special: u8,
    pub topsy: u32,
}

pub struct PlayerPlugin;

impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, hand_weapon);
        app.init_resource::<PlayerStats>()
            .init_resource::<LookDelta>()
            .add_systems(Update, grab_cursor.before(control))
            .add_systems(Update, (control, weapons, projectiles, damage_feedback, death).chain().run_if(crate::playing))
            .add_systems(PostUpdate, follow_camera.before(TransformSystems::Propagate).run_if(crate::playing));
    }
}

/// Builds Roman from his property set at a placed trotter instance (feet at `feet`, engine space).
pub fn make_roman(physics: &mut Physics, p: &Props, data: &GameData, feet: Vec3, yaw: f32) -> Option<Roman> {
    let radius = p.f("collision_radius", 4.0).clamp(2.0, 5.0);
    // steep ground: the player's contact callback 0x00477c90 zeroes the friction of contacts with normal z <= 0.7
    // (0x005bcd48, about 45.6 degrees) and 0x00478200 stops counting them as ground, so Roman slides down them
    let walker = physics.add_walker(feet, radius, 11.0, 0.7f32.acos())?;
    let r = data.props("p_player_rifle");
    let b = data.props("proj_rifle_bullet");
    Some(Roman {
        walker,
        yaw,
        pitch: -0.15,
        vel: Vec3::ZERO,
        on_ground: false,
        grace: 0.0,
        ground_ref: 0.0,
        peak_vz: 0.0,
        fall_fast_t: 0.0,
        falling: false,
        landing: None,
        free_trail: std::collections::VecDeque::from([feet]),
        free_t: 0.0,
        wedged: 0.0,
        zoom: 0.0,
        zoom_secs: data.props("p_globalsettings").f("camera_zoom_secs", 0.5),
        jumping: false,
        fwd_speed: p.f("fwd_max_speed", 40.0),
        side_speed: p.f("side_max_speed", 40.0),
        power: p.f("movement_power_max", 10.0),
        jump_fwd_speed: p.f("jump_fwd_max_speed", 2.0),
        jump_side_speed: p.f("jump_side_max_speed", 2.0),
        jump_power: p.f("jump_movement_power", 10.0),
        jump_impulse: p.f("jump_impulse", 70.0),
        fall_speed: p.f("fall_speed", -50.0),
        anti_gravity: p.f("jump_anti_gravity", 5.0),
        max_health: p.f("max_health", 400.0),
        rifle: Weapon {
            name: "rifle",
            props: "p_player_rifle",
            spread: 0.0,
            unlimited: true,
            recycle: r.f("recycle_time", 0.5),
            reload_time: r.f("clip_reload_time", 1.0),
            clip_size: r.i("clip_size", 8) as u32,
            clip: r.i("clip_size", 8) as u32,
            // weapon type 2: unlimited reserve (the HUD shows "8/" and the infinity sign; hud.md)
            ammo: u32::MAX,
            speed: b.f("speed", 300.0),
            damage: b.f("damage", 50.0),
            range: r.f("range", 500.0),
            impact_force: b.f("impact_force", 7370.0),
            timer: 0.0,
        },
        mg: None,
        use_mg: false,
        topsy: 0,
        mg_firing: false,
        special: 0,
        stickies: 5,
        dead: false,
        death_shown: false,
        ragdolled: false,
        base: game_to_engine(),
        since_fire: 10.0,
        hit_anim: 0.0,
        hit_from: None,
        pain_t: 0.0,
        breath_t: 0.0,
        death_said: false,
        hit_med: false,
        slow: 0.0,
        power_min: p.f("movement_power_min", 7.0),
        since_throw: 10.0,
        reloading: 0.0,
        reload_clock: None,
        wchange: None,
        move_local: Vec2::ZERO,
    })
}

/// Shows the weapon model in use in Roman's hand.
fn hand_weapon(roman: Query<&Roman>, mut held: Query<(&crate::level::HandWeapon, &mut Visibility)>) {
    let Ok(r) = roman.single() else { return };
    let mg = r.use_mg && r.mg.is_some();
    for (w, mut v) in &mut held {
        let show = (w.0 == "machinegun") == mg;
        let want = if show { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
    }
}

/// The Flemming machine gun with the game's values (p_player_machinegun, its bullet, p_globalsettings: clip 100,
/// max ammo 1000, spread 2 %, range 400), filled as the pub fills it (0x004a13a0 tops ammo up).
pub fn machinegun(data: &GameData) -> Weapon {
    let m = data.props("p_player_machinegun");
    let b = data.props("p_player_machinegun_bullet");
    let g = data.props("p_globalsettings");
    let clip = g.i("machinegun_max_clip", 100) as u32;
    Weapon {
        name: "machinegun",
        props: "p_player_machinegun",
        spread: g.f("machinegun_spread_pct", 2.0),
        unlimited: false,
        recycle: m.f("recycle_time", 0.1),
        reload_time: m.f("clip_reload_time", 1.0),
        clip_size: clip,
        clip,
        ammo: g.i("machinegun_max_ammo", 1000) as u32,
        speed: b.f("speed", 300.0),
        damage: b.f("damage", 20.0),
        range: g.f("machinegun_range", 400.0),
        impact_force: b.f("impact_force", 5695.0),
        timer: 0.0,
    }
}

/// Mouse look input for this frame (window pixels): the cursor's movement from the window centre, where it is put
/// back every frame. Raw device motion is not used: some trackpads report absolute positions there (the view then
/// runs away in one direction).
#[derive(Resource, Default)]
pub struct LookDelta(pub Vec2);

/// The cursor: confined, hidden and re-centred in play (its movement is the look input, LookDelta), free and shown in
/// the menus (title, pause, pub screen).
pub fn grab_cursor(
    mut windows: Query<(&mut Window, &mut CursorOptions), With<PrimaryWindow>>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    state: Res<State<crate::AppState>>,
    menu: Res<crate::gamemenu::GameMenu>,
    mut look: ResMut<LookDelta>,
    mut settle: Local<u32>,
    motion: Res<AccumulatedMouseMotion>,
    mut last_abs: Local<Option<Vec2>>,
) {
    look.0 = Vec2::ZERO;
    // scripted runs leave the mouse to the person at the desk
    if playtest.is_some() {
        return;
    }
    let play = *state.get() == crate::AppState::Playing && menu.page != crate::gamemenu::Page::Pub;
    let Ok((mut w, mut c)) = windows.single_mut() else { return };
    let (mode, visible) = if play { (CursorGrabMode::Confined, false) } else { (CursorGrabMode::None, true) };
    if c.grab_mode != mode || c.visible != visible {
        c.grab_mode = mode;
        c.visible = visible;
        // the first frames after entering play only centre the cursor
        *settle = 2;
    }
    if !play || !w.focused {
        *settle = 2;
        return;
    }
    let centre = Vec2::new(w.width() / 2.0, w.height() / 2.0);
    if let Some(p) = w.cursor_position() {
        if *settle == 0 {
            look.0 = p - centre;
        }
    }
    // when the cursor did not move, the raw device motion: streamed input (Moonlight from a Mac trackpad) can leave
    // the cursor still while raw input arrives, and some devices report absolute positions there (0..65535 over the
    // desktop, which winit passes on as motion: the view ran away "only down"): those are turned into the change
    // since the last report, scaled to window pixels
    let raw = motion.delta;
    if look.0 == Vec2::ZERO && raw != Vec2::ZERO && *settle == 0 {
        if raw.x.abs() > 2000.0 || raw.y.abs() > 2000.0 {
            if let Some(prev) = *last_abs {
                let d = (raw - prev) * Vec2::new(w.width() / 65535.0, w.height() / 65535.0) * 4.0;
                if d.length() < 400.0 {
                    look.0 = d;
                }
            }
            *last_abs = Some(raw);
        } else {
            look.0 = raw;
        }
    }
    *settle = settle.saturating_sub(1);
    w.set_cursor_position(Some(centre));
}

/// WASD relative to the camera yaw, Space jump, mouse look (stand-in bindings).
#[allow(clippy::too_many_arguments)]
fn control(
    path: Res<crate::campath::CameraPath>,
    time: Res<Time>,
    mut bot: ResMut<crate::playtest::Bot>,
    grunts: Query<(&crate::ai::Grunt, &GlobalTransform)>,
    pickups: Query<&GlobalTransform, With<crate::pickup::Pickup>>,
    explosives: Query<(&crate::fx::Destructible, &GlobalTransform)>,
    motion: Res<AccumulatedMouseMotion>,
    look: Res<LookDelta>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    opts: Res<crate::options::GameOptions>,
    topsy: Res<crate::topsy::Topsy>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(&mut Roman, &mut Transform, &mut Health)>,
    mut stats: ResMut<PlayerStats>,
    ctl: Res<crate::input::Controls>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    let dt = time.delta_secs();
    for (mut r, mut t, mut h) in &mut q {
        stats.health = h.0.max(0.0);
        stats.max_health = r.max_health;
        if h.0 <= 0.0 {
            r.dead = true;
        }
        // mouse look as the original's: measured with scripted mouse counts (hook MOUSEX / MOUSEY, camera dumps):
        // 0.0100 rad of turn and 0.0057 rad of pitch per count at the default sensitivity, linear; the multiplier is
        // m_mouseSensitivity + 0.5 (player.md 2: default 0.5 -> 1.0)
        let k = 0.0100 * (opts.mouse_sensitivity + 0.5);
        let kpitch = 0.0057 * (opts.mouse_sensitivity + 0.5);
        let inv = if opts.invert_mouse { -1.0 } else { 1.0 };
        // the topsy states (flags 0x7800000) take the turn input away
        // zoom: held Zoom (Z, E; 0x00478xxx game_06.cpp:2819-2836) enters the weapon camera; turning and pitching
        // then use the weapon's zoomed speeds (rifle / machine gun 0.05 / 0.25 against 0.3 / 0.95: 0x00475990 /
        // 0x004759e0)
        let zoom_held = ctl.down(crate::input::Act::Aim) && !r.dead;
        let step = dt / r.zoom_secs.max(1e-3);
        r.zoom = if zoom_held { (r.zoom + step).min(1.0) } else { (r.zoom - step).max(0.0) };
        let (kt, kp) = if zoom_held { (0.05 / 0.3, 0.25 / 0.95) } else { (1.0, 1.0) };
        if !topsy.active() {
            // the player's look input is the cursor's movement (LookDelta); scripted runs inject device motion
            let d = if playtest.is_some() { motion.delta } else { look.0 };
            r.yaw -= d.x * k * kt;
            r.pitch = (r.pitch - d.y * kpitch * inv * kp).clamp(-1.1, 0.7);
            // the right stick (input.rs): shaped |a|^2 (zoomed |a|^3), 3 rad/s of turn and 1.7 of pitch at full
            // deflection, zoomed 0.05 / 0.25 of 0.3 / 0.95 as the mouse
            let e = if zoom_held { 3 } else { 2 };
            let shape = |a: f32| a.signum() * a.abs().powi(e);
            let stick = ctl.axis(crate::input::Act::Look);
            r.yaw -= shape(stick.x) * 3.0 * dt * kt;
            r.pitch = (r.pitch + shape(stick.y) * 1.7 * dt * inv * kp).clamp(-1.1, 0.7);
        }
        let mut wish = Vec3::ZERO;
        // playtest bot: turn toward the goal and walk; when fighting, face the nearest live grunt
        let mut bot_walk: Option<Vec3> = None;
        // plan a route when the goal changes (A* over the ground, nav.rs), then follow its waypoints
        // (re)plan: a new goal, or the partial route ran out short of the goal
        let route_done = bot.route.is_empty() && bot.goto.is_some_and(|g| (g - t.translation).with_y(0.0).length() > 30.0);
        bot.replan_timer -= dt;
        if bot.goto.is_some() && (bot.planned_for != bot.goto || (route_done && bot.replan_timer <= 0.0)) {
            bot.planned_for = bot.goto;
            bot.replan_timer = 1.5;
            let blocked = bot.blocked.clone();
            bot.route = crate::nav::path(&physics, t.translation, bot.goto.unwrap(), &blocked).unwrap_or_default();
            // no route from here (a dead end the planner cannot see out of): back along the way it came, then plan
            // again from there
            if bot.route.is_empty() && bot.trail.len() > 3 {
                bot.route = bot.trail.iter().rev().skip(1).take(8).copied().collect();
            }
            info!("bot: route to {:?}: {} waypoints {:?}", bot.goto, bot.route.len(), bot.route.iter().map(|p| [p.x as i32, p.y as i32, p.z as i32]).collect::<Vec<_>>());
        }
        // no progress toward the goal for 15 s (routes that lead nowhere in a walled yard): retrace the trail out,
        // then plan afresh from there
        if let Some(g) = bot.goto {
            let d = (g - t.translation).with_y(0.0).length();
            if d < bot.best_goal_dist - 10.0 || bot.best_goal_dist == 0.0 {
                bot.best_goal_dist = d;
                bot.no_gain = 0.0;
            } else {
                bot.no_gain += dt;
            }
            if bot.no_gain > 15.0 && bot.trail.len() > 3 {
                info!("bot: no progress for 15 s, retracing its trail");
                bot.route = bot.trail.iter().rev().skip(1).copied().collect();
                bot.trail.clear();
                bot.no_gain = 0.0;
                bot.best_goal_dist = 0.0;
                bot.replan_timer = 12.0;
            }
        } else {
            bot.best_goal_dist = 0.0;
            bot.no_gain = 0.0;
        }
        bot.trail_t += dt;
        if bot.trail_t >= 1.0 {
            bot.trail_t = 0.0;
            if bot.trail.last().is_none_or(|l| l.distance(t.translation) > 6.0) {
                bot.trail.push(t.translation);
                if bot.trail.len() > 20 {
                    bot.trail.remove(0);
                }
            }
        }
        while bot.route.first().is_some_and(|w| (*w - t.translation).with_y(0.0).length() < 8.0) {
            bot.route.remove(0);
        }
        // the bot's tactics, as a player would play (debug tooling): engage the nearest grunt in rifle range with line
        // of sight; hold position while it is far, back off when badly hurt, a sticky bomb for a group
        let me = t.translation;
        let live: Vec<Vec3> = grunts.iter().filter(|(g, _)| g.mind != crate::ai::Mind::Dead).map(|(_, gt)| gt.translation()).collect();
        let target = if bot.fight {
            live.iter()
                .copied()
                .filter(|p| p.distance(me) < 450.0 && crate::ai::line_of_sight(&physics, me, *p))
                .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
        } else {
            None
        };
        // explosives (kegs): never shoot with one next to Roman; shoot one from a distance when grunts stand by it
        let kegs: Vec<(Vec3, f32)> = explosives
            .iter()
            .filter(|(d, _)| d.area_damage > 0.0 && d.health > 0.0 && d.dying.is_none())
            .map(|(d, gt)| (gt.translation() + Vec3::Y * 2.0, d.radius_outer))
            .collect();
        let danger = kegs.iter().any(|(k, r)| k.distance(me) < r + 25.0);
        let keg_target = if bot.fight {
            kegs.iter()
                .filter(|(k, r)| {
                    let d = k.distance(me);
                    d > r + 50.0 && d < 400.0 && live.iter().any(|g| g.distance(*k) < *r) && {
                        let eye = me + Vec3::Y * 8.0;
                        let v = *k - eye;
                        physics.raycast(eye, v.normalize_or(Vec3::Y), v.length() + 2.0).is_none_or(|h| h.1.distance(*k) < 6.0)
                    }
                })
                .map(|(k, _)| *k)
                .min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)))
        } else {
            None
        };
        let aim_at = keg_target.or(target.map(|p| p + Vec3::Y * 5.0));
        bot.fire_ok = aim_at.is_some() && !danger;
        let low = h.0 < r.max_health * 0.35;
        bot.throw_timer -= dt;
        // an engagement: hold up to 20 s, then push on for 15 s; it ends after 5 s without a target
        bot.no_target = if target.is_some() { 0.0 } else { bot.no_target + dt };
        bot.hold = if bot.no_target > 5.0 || bot.hold > 35.0 { 0.0 } else { bot.hold + dt };
        if let Some(p) = target {
            let d = p.distance(me);
            let group = live.iter().filter(|q| q.distance(p) < 35.0).count();
            if d < 150.0 && group >= 2 && r.stickies > 0 && bot.throw_timer <= 0.0 {
                bot.throw = true;
                bot.throw_timer = 3.0;
            }
        }
        // an explosive beside Roman while under fire: step away from it (the bot holds its fire there)
        let near_keg = kegs.iter().filter(|(k, r)| k.distance(me) < r + 25.0).map(|(k, _)| *k).min_by(|a, b| a.distance(me).total_cmp(&b.distance(me)));
        let tactic: Option<Vec3> = match target {
            Some(_) if near_keg.is_some() => near_keg.map(|k| (me - k).with_y(0.0).normalize_or_zero()),
            Some(p) if low => Some((me - p).with_y(0.0).normalize_or_zero()),
            Some(p) if p.distance(me) > 120.0 && bot.hold < 20.0 => Some(Vec3::ZERO),
            _ => None,
        };
        // a hurt bot detours to a pickup nearby (farther when badly hurt), as a player would
        let reach = if low { 200.0 } else { 100.0 };
        let detour = if bot.goto.is_some() && h.0 < r.max_health * 0.6 {
            pickups
                .iter()
                .map(|p| p.translation())
                .filter(|p| p.distance(t.translation) < reach && !bot.skip_pickups.iter().any(|s| s.distance(*p) < 6.0))
                .min_by(|a, b| a.distance(t.translation).total_cmp(&b.distance(t.translation)))
        } else {
            None
        };
        // a pickup not reached in 8 s is given up on
        bot.detour_t = if detour.is_some() { bot.detour_t + dt } else { 0.0 };
        let detour = match detour {
            Some(p) if bot.detour_t > 8.0 => {
                info!("bot: pickup at {:?} not reached (Roman at {:?}), skipping", [p.x as i32, p.y as i32, p.z as i32], [me.x as i32, me.y as i32, me.z as i32]);
                bot.skip_pickups.push(p);
                bot.detour_t = 0.0;
                None
            }
            d => d,
        };
        if let (Some(dir), None) = (tactic, detour) {
            // hold (zero) or back off; the route is kept for later
            bot_walk = Some(dir);
        } else if let Some(p) = detour {
            let dir = (p - t.translation).with_y(0.0).normalize_or_zero();
            if !bot.fight {
                r.yaw = (-dir.x).atan2(-dir.z);
            }
            bot_walk = Some(dir);
        } else if let Some(g) = bot.goto {
            let to = (bot.route.first().copied().unwrap_or(g) - t.translation).with_y(0.0);
            let near = if bot.near > 0.0 { bot.near } else { 12.0 };
            let to = if (g - t.translation).with_y(0.0).length() < 12.0 { (g - t.translation).with_y(0.0) } else { to };
            if (g - t.translation).with_y(0.0).length() < near {
                bot.goto = None;
                bot.arrived = true;
            } else {
                // stuck (under 4 units of progress in a second): jump and veer for a moment
                bot.stuck_timer += dt;
                if bot.stuck_timer > 1.0 {
                    let moved = t.translation.with_y(0.0).distance(bot.last_pos.with_y(0.0));
                    bot.veer = if moved < 4.0 { 1.2 } else { (bot.veer - 1.0).max(0.0) };
                    // progress = the distance to the next waypoint (or the goal) shrinking
                    let target = bot.route.first().copied().unwrap_or(g);
                    let d_now = (target - t.translation).with_y(0.0).length();
                    bot.no_progress = if d_now > bot.last_dist - 5.0 { bot.no_progress + 1.0 } else { 0.0 };
                    bot.last_dist = d_now;
                    bot.last_pos = t.translation;
                    bot.stuck_timer = 0.0;
                    // stuck 3 s: block the waypoint ahead and plan again around it
                    if bot.no_progress >= 3.0 {
                        let spot = bot.route.first().copied().unwrap_or(t.translation);
                        bot.blocked.push(spot);
                        bot.route.clear();
                        bot.replan_timer = 0.0;
                        bot.planned_for = None;
                        bot.no_progress = 0.0;
                        bot.last_dist = f32::MAX;
                        info!("bot: stuck near {:?}, replanning", [spot.x as i32, spot.z as i32]);
                    }
                }
                let dir = to.normalize();
                let dir = if bot.veer > 0.0 { Quat::from_rotation_y(0.9 * if (bot.veer * 10.0) as i32 % 2 == 0 { 1.0 } else { -1.0 }) * dir } else { dir };
                if !bot.fight {
                    r.yaw = (-dir.x).atan2(-dir.z);
                }
                bot_walk = Some(dir);
            }
        }
        if bot.fight {
            if let Some(p) = aim_at {
                let to = p - (me + Vec3::Y * 8.5);
                r.yaw = (-to.x).atan2(-to.z);
                r.pitch = (to.y / to.with_y(0.0).length().max(1.0)).atan() - 0.08;
            }
        }
        if !r.dead && path.name.as_deref() == Some("camera_path_l1m1") {
            // PlayerObject_Update 0x004788a0: on lev_1_m1 the stick is held forward while the intro path plays
            wish += Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z * r.fwd_speed;
        }
        if !r.dead {
            if let Some(dir) = bot_walk {
                wish += dir * r.fwd_speed;
            }
            let f = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
            let s = Quat::from_rotation_y(r.yaw) * Vec3::X;
            // the Move action (WASD: full deflection; the left stick: analog): target speed max x a + (1 - a) x min for
            // any deflection a (fwd / side_max_speed_min 4, game_06.cpp:2586); backward at 0.7 (stand-in)
            let m = ctl.axis(crate::input::Act::Move).clamp(Vec2::splat(-1.0), Vec2::ONE);
            if m != Vec2::ZERO {
                let sp = |max: f32, a: f32| max * a + (1.0 - a) * 4.0f32.min(max);
                let fy = if m.y > 0.0 { sp(r.fwd_speed, m.y) } else if m.y < 0.0 { -sp(r.fwd_speed, -m.y) * 0.7 } else { 0.0 };
                let sx = if m.x != 0.0 { m.x.signum() * sp(r.side_speed, m.x.abs()) } else { 0.0 };
                wish += f * fy + s * sx;
            }
        }
        // the Topsy Turvy states hold Roman in place
        if topsy.active() {
            wish = Vec3::ZERO;
        }
        // the drive (PlayerObject_Update 0x004799d5 through 0x00471810): force = d * mass * power * s^5, d the wanted
        // minus the current velocity, s = sqrt(1 - c^2) with c = (d . up) / |d| clamped to [0, 1], the power scaled by
        // max_speed / |d| when |d| exceeds the max speed. On the ground the max speeds are fwd / side_max_speed (40)
        // with movement_power (lerp min..max by the stick: keys are full stick, max 10); jumping (state flag 0x80)
        // jump_fwd / side_max_speed (6.5) with jump_movement_power (4). Integrated exactly per frame.
        // the power: movement_power_min..max by the stick (keys: full stick); for 1 s after a knockback (w = 1 -
        // slow) min becomes w x 7 + (1 - w) and max x (0.5 (1 - w) + w) (game_06.cpp:2572)
        let stick = (Vec2::new(wish.x, wish.z).length() / r.fwd_speed.max(1e-3)).min(1.0);
        let w = 1.0 - r.slow;
        let (pmin, pmax) = if r.slow > 0.0 { (w * r.power_min + (1.0 - w), r.power * (0.5 * (1.0 - w) + w)) } else { (r.power_min, r.power) };
        r.slow = (r.slow - dt).max(0.0);
        // the hit anim plays only standing still: movement, firing or the air end it, and the latch with it
        if r.hit_anim > 0.0 {
            // (the knockback's hop does not end it: the original counts him grounded for 0.5 s after leaving the ground,
            // 0x00478200 +0x570; only the jump state does)
            r.hit_anim = if stick > 0.0 || r.jumping || r.since_fire < 0.1 { 0.0 } else { (r.hit_anim - dt).max(0.0) };
        }
        let (max_speed, power) = (r.fwd_speed.max(r.side_speed), pmin + (pmax - pmin) * stick);
        if r.jumping {
            // air control: the stick adds jump_fwd / side_max_speed x jump_movement_power (6.5 x 4 = 26 units/s^2) to
            // the take-off velocity. Fitted to the original's measured jump (hook camera dump "player", jumpA: +2.17
            // units/s every 5 frames from 39.5 to 62.6 in the air, peak 14.6 at 0.42 s); the drive's wanted velocity
            // in the jump state (+0x4c0 / +0x9xx terms of 0x004799d5) is not fully decoded.
            if r.fwd_speed > 0.0 {
                let a = Vec3::new(wish.x, 0.0, wish.z) * (r.jump_fwd_speed / r.fwd_speed) * r.jump_power;
                r.vel += a * dt;
            }
        }
        let d = Vec3::new(wish.x - r.vel.x, if r.on_ground { 0.0 } else { -r.vel.y }, wish.z - r.vel.z);
        let dist = d.length();
        if dist > 1e-4 && !r.jumping && !(r.falling && !r.on_ground) {
            let strength = if max_speed > 0.0 && dist > max_speed { power * max_speed / dist } else { power };
            let c = (d.y / dist).clamp(0.0, 1.0);
            let s5 = (1.0 - c * c).max(0.0).sqrt().clamp(0.0, 1.0).powi(5);
            let k = 1.0 - (-strength * s5 * dt).exp();
            r.vel.x += d.x * k;
            r.vel.z += d.z * k;
            if !r.on_ground {
                r.vel.y += d.y * k;
            }
        }
        if r.on_ground {
            r.vel.y = r.vel.y.max(-5.0);
            if (ctl.pressed(crate::input::Act::Jump) || (bot.veer > 0.0 && bot.goto.is_some())) && !r.dead {
                // the impulse, then this step's gravity as in the airborne steps (velocity before position): the
                // original's arc (jumpA, peak 14.6) fits 70 and gravity 160.9 that way
                r.vel.y = r.jump_impulse - 64.348 * r.anti_gravity * 0.5 * dt;
                r.jumping = true;
            }
        } else {
            // world gravity -64.348 x anti_gravity x 0.5 (0x005bcb4c): -160.9 with jump_anti_gravity 5 in lev_1_m1
            // (docs/subsystems/player.md); jump impulse 70 -> about 15 units high
            r.vel.y = (r.vel.y - 64.348 * r.anti_gravity * 0.5 * dt).max(r.fall_speed.min(-50.0) * 4.0);
        }
        let vel = r.vel;
        let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
        let right = Quat::from_rotation_y(r.yaw) * Vec3::X;
        r.move_local = Vec2::new(vel.dot(right), vel.dot(fwd));
        let before = t.translation;
        let vy = r.vel.y;
        let was_jump = r.jumping;
        if let Some((feet, on)) = physics.move_walker(&r.walker, vel, dt) {
            if on && r.vel.y <= 0.0 {
                r.vel.y = 0.0;
            }
            r.on_ground = on;
            if on && r.vel.y <= 0.0 {
                r.jumping = false;
            }
            t.translation = feet;
        }
        fall_and_land(&mut r, &t, &mut h, vy, was_jump, dt, &physics, &mut fx, t.translation.distance(before));
        // safety net (remaster): pushing for 1.5 s without moving at all and without ground under him (wedged in a
        // wall's collider: seen in a bot run next to v_wall_tall_a) puts him back where he last moved freely. Standing
        // against a wall or the foot of a steep slope is the original's behaviour and is left alone
        let moved = t.translation.distance(before);
        let pushing = Vec2::new(wish.x, wish.z).length() > 1.0 || r.jumping;
        if moved > 0.2 * dt * r.fwd_speed {
            r.wedged = 0.0;
            // a trail of places he moved through, every 0.5 s, the last 3 s
            r.free_t += dt;
            if r.free_t >= 0.5 && on_free_ground(&physics, t.translation) {
                r.free_t = 0.0;
                r.free_trail.push_back(t.translation);
                if r.free_trail.len() > 6 {
                    r.free_trail.pop_front();
                }
            }
        } else if pushing && !r.dead && physics.walker_in_air(&r.walker) {
            r.wedged += dt;
            if r.wedged > 1.5 {
                r.wedged = 0.0;
                let back = r.free_trail.front().copied().unwrap_or(t.translation) + Vec3::Y * 2.0;
                physics.place_walker(&r.walker, back);
                t.translation = back;
                r.vel = Vec3::ZERO;
            }
        }
        // Roman faces where the camera looks (his model faces -Y in game space = +Z engine: turn by pi)
        t.rotation = Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * r.base;
        stats.position = t.translation;
        let w = if r.use_mg { r.mg.as_ref().unwrap_or(&r.rifle) } else { &r.rifle };
        stats.clip = w.clip;
        stats.ammo = w.ammo;
        stats.weapon = w.name;
        stats.unlimited = w.unlimited;
        stats.special = r.special;
        stats.topsy = r.topsy;
        stats.stickies = r.stickies;
    }
}

fn aim(r: &Roman, t: &Transform) -> (Vec3, Vec3) {
    let dir = Quat::from_euler(EulerRot::YXZ, r.yaw, r.pitch + 0.08, 0.0) * Vec3::NEG_Z;
    (t.translation + Vec3::Y * 8.5 + dir * 3.0, dir)
}

#[allow(clippy::too_many_arguments)]
fn weapons(
    mut commands: Commands,
    time: Res<Time>,
    buttons: Res<ButtonInput<MouseButton>>,
    ctl: Res<crate::input::Controls>,
    mut physics: NonSendMut<Physics>,
    (texts, mut subs): (Res<crate::gamemenu::Texts>, MessageWriter<crate::audio::Subtitle>),
    data: Res<GameData>,
    mut bot: ResMut<crate::playtest::Bot>,
    mut q: Query<(Entity, &mut Roman, &Transform, Option<&crate::skin::Animator>)>,
    mut stats: ResMut<PlayerStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut topsy: ResMut<crate::topsy::Topsy>,
    (mut sticky, cam, joints, xhair): (ResMut<crate::sticky::StickyThrow>, Query<&GlobalTransform, With<FollowCam>>, Query<&GlobalTransform, Without<FollowCam>>, Res<crate::hud::Crosshair>),
) {
    let dt = time.delta_secs();
    // the aim point (Game+0xa6c): where the crosshair's ray from the camera meets the world, else far along it; the
    // player's shots fly from the weapon toward it (they flew parallel to the view from above Roman's head and missed
    // what the crosshair was on: the player's gun "did nothing")
    // (the crosshair's own ray, hud.rs crosshair_aim: Hud_UpdateCrosshair 0x00406f50's hud +0x5c, weapon range,
    // characters by their collision parts; this one is the fallback before it has run)
    let aim_point = xhair.point.or_else(|| cam.single().ok().map(|c| {
        let (o, d) = (c.translation(), c.forward().as_vec3());
        let mut from = o + d * 20.0;
        let mut left = 1000.0;
        loop {
            match physics.raycast(from, d, left) {
                // past Roman's own body, if any
                Some((b, at, _)) if physics.bodies.get(&b).is_some_and(|&x| q.get(x).is_ok()) => {
                    left -= at.distance(from) + 0.05;
                    from = at + d * 0.05;
                    if left <= 0.0 {
                        break o + d * 1020.0;
                    }
                }
                Some((_, at, _)) => break at,
                None => break o + d * 1020.0,
            }
        }
    }));
    for (e, mut r, t, ra) in &mut q {
        if r.dead {
            continue;
        }
        r.since_fire += dt;
        r.since_throw += dt;
        r.reloading -= dt;
        // Next Weapon (Q): the rifle and, once taken in the pub, the machine gun
        // the switch plays the weapon-change anim and completes at its frame 13 (0x004a16b0): the weapon, the chat line
        // "ui_change_weapon" with its name and the sound ui_weapon_put_away (2D)
        if ctl.pressed(crate::input::Act::SwitchWeapon) && r.mg.is_some() && r.wchange.is_none() && r.reload_clock.is_none() {
            r.wchange = Some(0.0);
        }
        if let Some(c) = r.wchange {
            let c = c + dt;
            if c >= 12.0 / 30.0 && c - dt < 12.0 / 30.0 {
                r.use_mg = !r.use_mg;
                let name = texts.get(if r.use_mg { "weap_machinegun" } else { "weap_rifle" });
                subs.write(crate::audio::Subtitle { speaker: None, text: texts.get("ui_change_weapon").replace("%s", &name), duration: 3.0 });
                sounds.write(crate::audio::PlaySound::ui("ui_weapon_put_away"));
            }
            // the anim's length (trotter_wchange_t 1.13 s; frame 13 is 0.4 s in: its first frame is 1)
            r.wchange = (c < 1.133).then_some(c);
        }
        let mg = r.use_mg && r.mg.is_some();
        let mut reloaded = false;
        // a tap counts as a shot: a trackpad tap over Moonlight arrives as press and release in one frame, never
        // "pressed" at the frame's end (the player's gun did nothing)
        let fire = ctl.down(crate::input::Act::Fire) || ctl.pressed(crate::input::Act::Fire) || (buttons.just_pressed(MouseButton::Left) && ctl.active_play) || (bot.fight && bot.fire_ok);
        let idle = r.reload_clock.is_none();
        let (shoot, reload_time) = {
            let w = if mg { r.mg.as_mut().unwrap() } else { &mut r.rifle };
            w.timer -= dt;
            let starting = idle && ((ctl.pressed(crate::input::Act::Reload) && w.clip < w.clip_size && w.ammo > 0) || (w.clip == 0 && w.ammo > 0 && w.timer <= 0.0));
            if starting {
                w.timer = w.reload_time.max(42.0 / 30.0);
                reloaded = true;
            }
            let lock = w.reload_time;
            (fire && w.timer <= 0.0 && w.clip > 0, lock)
        };
        if reloaded {
            r.reload_clock = Some((0.0, false));
        }
        // the reload runs on the reload anim's frames (30 per second, counted from its first frame, 1: AnimEventTime
        // game_05.cpp:1328): the load sound, then the refill
        if let Some((clock, said)) = r.reload_clock {
            let clock = clock + dt;
            let sound_at = if mg { 14.0 } else { 9.0 } / 30.0;
            let mut said = said;
            if !said && clock >= sound_at {
                said = true;
                sounds.write(crate::audio::PlaySound::at(if mg { "wpn_rome_machgun_load" } else { "wpn_rome_rifle_load" }, t.translation));
            }
            if clock >= 42.0 / 30.0 {
                let w = if mg { r.mg.as_mut().unwrap() } else { &mut r.rifle };
                let n = (w.clip_size - w.clip).min(w.ammo);
                w.clip += n;
                if !w.unlimited {
                    w.ammo -= n;
                }
                r.reload_clock = None;
            } else {
                r.reload_clock = Some((clock, said));
            }
        }
        if reloaded {
            // the upper-body reload anim (trotter_reload, 1.97 s)
            r.reloading = reload_time.max(1.97);
        }
        // the machine gun's fire sound: start, loop while the trigger is held, end (wpn_rome_machgun_fire_*)
        if mg && shoot && !r.mg_firing {
            r.mg_firing = true;
            sounds.write(crate::audio::PlaySound::at("wpn_rome_machgun_fire_start", t.translation));
            sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::at("wpn_rome_machgun_fire_lp", t.translation) });
        }
        if r.mg_firing && (!fire || !mg || r.mg.as_ref().is_some_and(|w| w.clip == 0)) {
            r.mg_firing = false;
            stop.write(crate::audio::StopSound("wpn_rome_machgun_fire_lp".into()));
            sounds.write(crate::audio::PlaySound::at("wpn_rome_machgun_fire_end", t.translation));
        }
        if shoot {
            r.since_fire = 0.0;
            let (props, spread) = {
                let w = if mg { r.mg.as_mut().unwrap() } else { &mut r.rifle };
                w.clip -= 1;
                w.timer = w.recycle;
                (w.props, w.spread)
            };
            stats.shots += 1;
            let (from, dir) = aim(&r, t);
            // the shot leaves the gun: the weapon's ref_shoot_00 (0x0047dbf0: P = that bone's matrix; measured in the
            // original, hook and-spawnshots.txt: 2.84 ahead of the weapon's origin in the hand, 0.43 up), and flies
            // to the crosshair's point (Q = hud +0x88)
            let hand = ra.and_then(|a| a.skeleton.index.get("ref_rhand").and_then(|&i| joints.get(a.joints[i]).ok())).map(|j| j.translation());
            let from = match (hand, aim_point) {
                (Some(h), Some(p)) => {
                    let d = (p - h).normalize_or(dir);
                    h + d * 2.84 + Vec3::Y * 0.43
                }
                _ => from,
            };
            let dir = match aim_point {
                Some(p) if !(bot.fight && bot.fire_ok) && (p - from).length() > 1.0 => (p - from).normalize_or(dir),
                _ => dir,
            };
            // spread: a random offset of up to spread % across the aim
            let dir = if spread > 0.0 {
                let k = spread / 100.0;
                (dir + Vec3::new(rng.range(-k, k), rng.range(-k, k), rng.range(-k, k))).normalize_or(dir)
            } else {
                dir
            };
            let wp = data.props(props);
            let bullet = data.props(&format!("p_{}", wp.s("projectile_name").unwrap_or("player_rifle_bullet")));
            spawn_projectile(&mut commands, &mut fx, &bullet, from, dir, true, Some(e));
            if let Some(m) = wp.s("muzzle_fx_name") {
                fx.write(crate::fx::StartFx { name: m.to_owned(), at: from, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None, position_only: false });
            }
            // (the rifle's fire sound comes from its muzzle fx, fx_muzzle_flash_hawkings op 0x39: the original's sound
            // log has one wpn_rome_rifle_fire per shot)
            let _ = mg;
        }
        // sticky bomb: thrown along the aim with the game's sticky bomb values (stand-in throw strength)
        let bot_throw = std::mem::take(&mut bot.throw);
        // Next Special (Tab): sticky bombs / the Topsy Turvy bomb
        if ctl.pressed(crate::input::Act::NextSpecial) && r.topsy > 0 {
            r.special = 1 - r.special;
            // 0x004a17d0: the chat line "ui_change_special_weapon" and ui_weapon_put_away
            let name = texts.get(if r.special == 1 { "weap_topsyturvy" } else { "weap_stickybomb" });
            subs.write(crate::audio::Subtitle { speaker: None, text: texts.get("ui_change_special_weapon").replace("%s", &name), duration: 3.0 });
            sounds.write(crate::audio::PlaySound::ui("ui_weapon_put_away"));
        }
        if r.special == 1 && r.topsy == 0 {
            r.special = 0;
        }
        // Fire Special with the Topsy Turvy bomb (topsy.rs)
        if r.special == 1 && ctl.pressed(crate::input::Act::Special) && r.topsy > 0 && !topsy.active() {
            r.topsy -= 1;
            topsy.start = true;
        }
        // Fire Special: Mouse 2 (the original's binding; F is Activate)
        // (0x004a1aa0 -> 0x004a1930: weapon 10 queued; the bomb leaves the hand on the throw anim's "fire" note, 0.267 s
        // into trotter_throw / trotter_run_throw (0x004a1970, game_05.cpp:1579; the 0.4 s queue is only a fallback),
        // and the count drops then (game_09.cpp:1813; sticky.rs). No throw sound: the bomb's flight loop starts at launch
        if r.special == 0 && (ctl.pressed(crate::input::Act::Special) || bot_throw) && r.stickies > 0 && sticky.pending.is_none() {
            r.since_throw = 0.0;
            sticky.pending = Some(0.267);
        }
    }
}

/// Roman's death (player.md 6, game_06.cpp:2154-2168): fx_trotter_death and a ragdoll from his pose, pushed by his
/// velocity (the 5 s to the mission failed screen: mission.rs).
fn death(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Roman, &Transform, Option<&mut crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    for (e, mut r, t, a) in &mut q {
        if !r.dead || r.death_shown {
            continue;
        }
        r.death_shown = true;
        fx.write(crate::fx::StartFx { name: "fx_trotter_death".into(), at: t.translation, rotation: crate::level::game_to_engine(), follow: Some(e), position_only: true });
        if let Some(mut a) = a {
            if let Some(rd) = crate::ragdoll::start(&mut physics, &a, &joints, r.vel) {
                a.layers.clear();
                commands.entity(e).insert(rd);
                r.ragdolled = true;
            }
        }
        info!("EVENT player: died (ragdoll {})", r.ragdolled);
    }
}

/// Falling and landing (0x00478200 / 0x00478100; player.md "Landing and falling").
#[allow(clippy::too_many_arguments)]
fn fall_and_land(r: &mut Roman, t: &Transform, h: &mut Health, vy: f32, jumped: bool, dt: f32, physics: &Physics, fx: &mut MessageWriter<crate::fx::StartFx>, moved: f32) {
    let y = t.translation.y;
    r.grace = if r.on_ground { 0.5 } else { (r.grace - dt).max(0.0) };
    if r.grace > 0.0 {
        r.ground_ref = y;
    }
    if !r.on_ground {
        r.peak_vz = r.peak_vz.min(vy);
        // falling: slower than fall_speed (-50; after a jump fall_speed_jump -70)
        let threshold = if jumped { -70.0 } else { -50.0 };
        if vy <= 0.0 && vy < threshold {
            r.falling = true;
            r.fall_fast_t += dt;
        }
    } else if r.grace > 0.0 && r.landing.is_none() {
        r.fall_fast_t = 0.0;
    }
    // death by falling: below z -1000, more than death_falling_distance (110) under the ground reference, or falling
    // fast for longer than death_falling_time (1 s): the whole health as damage (0x00478100 flag 1)
    let game_z = (crate::level::game_to_engine().inverse() * t.translation).z;
    if !r.dead && h.0 > 0.0 && (game_z < -1000.0 || y < r.ground_ref - 110.0 || r.fall_fast_t > 1.0) {
        info!("EVENT player: fell to his death (dropped {:.0}, {:.2} s falling)", r.ground_ref - y, r.fall_fast_t);
        h.0 = 0.0;
    }
    // landing: grounded after falling (or a jump), once: the land fx, the landing damage from the peak downward speed,
    // the landing anim (land_hard after more than fall_land_hard_time 0.6 s falling, else jump_land)
    if r.on_ground && vy <= 0.0 && (r.falling || jumped) && r.landing.is_none() && !r.dead {
        let k = (-r.peak_vz - 150.0) / 200.0;
        if k > 0.0 {
            let k = k.min(1.0);
            let d = (1.0 - k) * 150.0 + k * 300.0;
            info!("EVENT player: landing damage {d:.0} (vz {:.0})", r.peak_vz);
            h.0 -= d;
        }
        let mat = physics.raycast(t.translation + Vec3::Y * 2.0, Vec3::NEG_Y, 6.0).and_then(|hit| physics.materials.get(&hit.0).cloned()).unwrap_or_else(|| "snow".into());
        fx.write(crate::fx::StartFx { name: format!("fx_fs_roman_land_{mat}"), at: t.translation, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
        r.landing = Some(if r.fall_fast_t > 0.6 { ("trotter_land_hard", 0.0, 2.633) } else { ("trotter_jump_land", 0.0, 0.633) });
        r.falling = false;
        r.peak_vz = 0.0;
    }
    if let Some((n, el, len)) = r.landing {
        // moving ends it (game_05.cpp:1721-1726)
        let el = el + dt;
        r.landing = if el >= len || (moved > 0.01 && Vec2::new(r.vel.x, r.vel.z).length() > 3.0) { None } else { Some((n, el, len)) };
    }
}

/// Projectiles: one ray per frame along the path; damage on the first body hit (characters' Health, objects'
/// Destructible), a tracer line for the path.
#[allow(clippy::too_many_arguments)]
fn projectiles(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut Projectile, &mut Transform)>,
    mut chars: Query<(Entity, &crate::physics::Body, &mut Health, Option<&crate::skin::Animator>, Has<crate::ai::Grunt>), Without<Projectile>>,
    joints: Query<&GlobalTransform>,
    mut objects: Query<(&crate::level::Placed, &mut Destructible)>,
    mut roman: Query<(Entity, &mut Roman, &Transform, Option<&crate::skin::Animator>), Without<Projectile>>,
    mut roman_health: Query<&mut Health, (With<Roman>, Without<crate::physics::Body>, Without<Projectile>)>,
    mut companions: Query<(Entity, &mut crate::ai::Companion, &Transform, Option<&crate::skin::Animator>, &mut Health), (Without<Roman>, Without<crate::physics::Body>, Without<Projectile>)>,
    mut stats: ResMut<PlayerStats>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    hulls: Option<Res<crate::hitparts::HitHulls>>,
    rtime: Res<Time<Real>>,
    binds: Res<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
    mut victims: Query<&mut crate::ai::Grunt>,
) {
    let dt = time.delta_secs();
    for (e, mut p, mut t) in &mut q {
        let step = p.vel * dt;
        let from = t.translation;
        p.life -= dt;
        let mut hit = physics.raycast_bullet(from, step.normalize_or(Vec3::Y), step.length().max(0.01));
        // the original's bullets hit a character's collision parts (Projectile_Update 0x0047e580: a detail-level-2 ray
        // against the collision model's parts), not a capsule round the whole body: a ray into a grunt's capsule hits
        // only where it meets one of grunt_coll_9's boxes posed by the skeleton (hitparts.rs); other characters: within
        // a part's radius of the skeleton (guessed radii: torso 1.0, head 0.8, limbs 0.5)
        let mut hull_part: Option<String> = None;
        if let Some((body, _, _)) = hit {
            if let Some((_, _, _, Some(a), grunt)) = chars.iter().find(|(ce, b, _, _, _)| b.id == body && Some(*ce) != p.owner) {
                let seg = step.normalize_or(Vec3::Y) * (step.length() + 10.0);
                match hulls.as_ref().filter(|h| grunt && !h.grunt.is_empty()) {
                    Some(h) => match crate::hitparts::hit(&h.grunt, a, &joints, &binds, from, from + seg) {
                        Some((_, part)) => hull_part = Some(part),
                        None => hit = None,
                    },
                    None => {
                        if !ray_hits_parts(a, &joints, from, seg) {
                            hit = None;
                        }
                    }
                }
            }
        }
        // enemy bullets hit Roman on his collision model's parts (trotter_coll_0 posed by his skeleton, hitparts.rs;
        // the walker is not a body); without it, by distance to his capsule
        let mut hit_roman = false;
        if !p.from_player {
            if let Ok((_, r, rt, ra)) = roman.single() {
                let c = rt.translation + Vec3::Y * r.walker.lift;
                let to = from + step;
                let seg = to - from;
                let u = ((c - from).dot(seg) / seg.length_squared().max(1e-6)).clamp(0.0, 1.0);
                let near = (from + seg * u).distance(c);
                hit_roman = match (ra, hulls.as_ref().filter(|h| !h.roman.is_empty())) {
                    (Some(a), Some(h)) if near < 12.0 => {
                        let rh = crate::hitparts::hit(&h.roman, a, &joints, &binds, from, to);
                        if let (Some((f, part)), true) = (&rh, std::env::var("AIDBG").is_ok()) {
                            // as the hook's [test] log_hits: the part and the hit point minus his position (game axes)
                            let at = crate::level::game_to_engine().inverse() * (from + (to - from) * *f - rt.translation);
                            let jp = a.skeleton.index.get(part.as_str()).and_then(|&i| joints.get(a.joints[i]).ok()).map(|j| crate::level::game_to_engine().inverse() * (j.translation() - rt.translation)).unwrap_or(Vec3::ZERO);
                            info!("AIDBG romanhit {} {:.2} {:.2} {:.2} anim {:.2} bone {:.2} {:.2} {:.2} t {:.3}", part, at.x, at.y, at.z, r.hit_anim, jp.x, jp.y, jp.z, rtime.elapsed_secs());
                        }
                        rh.is_some()
                    }
                    (Some(_), Some(_)) => false,
                    _ => near < 4.5,
                };
            }
        }
        // impact fx: fx_end_material + the surface's material (landscape: snow, guess)
        let impact = |body: Option<BodyId>| -> Option<String> {
            let base = p.fx_end.clone()?;
            let mat = body.and_then(|b| physics.materials.get(&b).cloned()).unwrap_or_else(|| "snow".into());
            Some(format!("{base}_{mat}"))
        };
        // and the companions, on theirs (molmer_coll_0, q111_coll_0): the nearest hit along the step
        let mut hit_companion = None;
        // (every shot: characters are in the projectile list +0xe90; the shooter's own chain is skipped)
        if !hit_roman {
            if let Some(h) = hulls.as_ref() {
                let to = from + step;
                for (ce, c, ct, ca, _) in companions.iter() {
                    if p.owner == Some(ce) {
                        continue;
                    }
                    let Some(a) = ca else { continue };
                    let parts = if c.q { &h.q111 } else { &h.molmer };
                    let seg = to - from;
                    let cc = ct.translation + Vec3::Y * 5.0;
                    let u = ((cc - from).dot(seg) / seg.length_squared().max(1e-6)).clamp(0.0, 1.0);
                    if parts.is_empty() || (from + seg * u).distance(cc) > 12.0 {
                        continue;
                    }
                    if let Some((f, _)) = crate::hitparts::hit(parts, a, &joints, &binds, from, to) {
                        if hit_companion.is_none_or(|(bf, _)| f < bf) {
                            hit_companion = Some((f, c.q));
                        }
                    }
                }
            }
        }
        if let Some((_, q)) = hit_companion {
            if let Some(n) = impact(None).map(|n| n.replace("_snow", "_grunt")) {
                fx.write(crate::fx::StartFx { name: n, at: from + step, rotation: Quat::IDENTITY, follow: None, position_only: false });
            }
            // the companions are teammates of the player (vt+0xe4 true): a grunt's shot hurts them, the player's and the
            // other companion's stop on them and do nothing (0x00430340: source and victim on the same team)
            if let Some((_, mut c, _, _, mut ch)) = companions.iter_mut().find(|(_, c, _, _, _)| c.q == q) {
                if p.from_player {
                    c.friendly = true;
                } else {
                    ch.0 -= p.damage;
                    c.attacker = p.owner;
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        if hit_roman {
            if let Some(n) = impact(None).map(|n| n.replace("_snow", "_grunt")) {
                fx.write(crate::fx::StartFx { name: n, at: from + step, rotation: Quat::IDENTITY, follow: None, position_only: false });
            }
            if let Ok(mut h) = roman_health.single_mut() {
                h.0 -= p.damage;
                if let Ok((_, mut r, _, _)) = roman.single_mut() {
                    r.hit_from = Some(p.vel.normalize_or(Vec3::Z));
                }
                // each live companion takes the attacker unless engaged (0x00486df0)
                if let Some(o) = p.owner {
                    for (_, mut c, _, _, _) in companions.iter_mut() {
                        c.roman_attacked(o);
                    }
                }
                // PlayerObject_OnHit 0x0047afa0: alive, with an impact force and the latch clear: the hit anim (by the
                // damage: < 65 small) and a knockback, one impulse 0.1 x min(10 x impact_force, 4000) x (d.x, d.y, 0.4)
                // with d = (forward.xy, 0.7) normalised (0x0047e580), not scaled by mass; then 1 s of a weakened drive
                if let (true, Ok((_, mut r, _, _))) = (h.0 > 0.0 && p.impact != 0.0, roman.single_mut()) {
                    if r.hit_anim <= 0.0 {
                        r.hit_med = p.damage >= 65.0;
                        r.hit_anim = if r.hit_med { 1.967 } else { 0.633 };
                        let f = p.vel.normalize_or(Vec3::Z);
                        let n = (f.x * f.x + f.z * f.z + 0.49).sqrt();
                        let j = 0.1 * (10.0 * p.impact).min(4000.0) * Vec3::new(f.x / n, 0.4, f.z / n);
                        r.vel += j / ROMAN_MASS;
                        r.slow = 1.0;
                    }
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        if let Some((body, at, normal)) = hit {
            if !p.from_player && std::env::var("AIDBG").is_ok() {
                let kind = if physics.landscape == Some(body) { "ground" } else if chars.iter().any(|(_, b, _, _, g)| b.id == body && g) { "grunt" } else { "other" };
                let g = crate::level::game_to_engine().inverse() * at;
                info!("AIDBG bulletend {} {:.1} {:.1} {:.1} t {:.3}", kind, g.x, g.y, g.z, rtime.elapsed_secs());
            }
            if let Some(n) = impact(Some(body)) {
                // the fx's game +Z (its up) along the surface normal
                let rot = Quat::from_rotation_arc(Vec3::Y, normal.normalize_or(Vec3::Y)) * crate::level::game_to_engine();
                fx.write(crate::fx::StartFx { name: n, at, rotation: rot, follow: None, position_only: false });
            }
            if let Some((ce, _, mut h, anim, grunt)) = chars.iter_mut().find(|(ce, b, _, _, _)| b.id == body && Some(*ce) != p.owner) {
                // same team (0x00430340: the source's and the victim's vt+0xe4 agree): a grunt's shot does no damage
                // to a grunt (the engine's earlier grunts killed each other with stray arrows)
                if grunt && !p.from_player {
                    commands.entity(e).despawn();
                    continue;
                }
                let was_alive = h.0 > 0.0;
                // the part hit (0x0047eec0: the hit part's bone "head" / "neck" -> head_shot_multiplier, "chest" /
                // "belly" -> torso_shot_multiplier). The engine's characters collide as one capsule: the part is
                // taken as the skeleton bone nearest the hit point (approximation of the original's per-part hulls)
                let part = hull_part.clone().or_else(|| anim.and_then(|a| {
                    a.skeleton
                        .names
                        .iter()
                        .enumerate()
                        // the body's main bones (helpers such as ref_*, camera targets and fingers are not parts)
                        .filter(|(_, n)| BODY_PARTS.contains(&n.trim().to_lowercase().as_str()))
                        .filter_map(|(i, n)| joints.get(a.joints[i]).ok().map(|j| (n.trim().to_lowercase(), j.translation().distance(at))))
                        .min_by(|x, y| x.1.total_cmp(&y.1))
                        .map(|(n, _)| n)
                }));
                let mult = match part.as_deref() {
                    Some("head" | "neck") => p.head_mult,
                    Some("chest" | "belly") => p.torso_mult,
                    _ => 1.0,
                };
                if p.from_player && mult > 1.0 && matches!(part.as_deref(), Some("head" | "neck")) {
                    stats.head_shots += 1;
                }
                h.0 -= p.damage * mult;
                if p.from_player && grunt && p.owner.is_some_and(|o| roman.contains(o)) {
                    stats.hits += 1;
                }
                // the damage message's source (+0x2c): the shooter, offered to the grunt as a target (ai.rs think)
                if let (Some(o), Ok(mut g)) = (p.owner, victims.get_mut(ce)) {
                    g.attacker = Some(o);
                }
                if std::env::var("AIDBG").is_ok() {
                    info!("AIDBG hit {:?} for {} -> {} part {:?}", body, p.damage * mult, h.0, part);
                }
                if p.from_player && was_alive && h.0 <= 0.0 {
                    stats.kills += 1;
                }
            } else if let Some((_, mut d)) = objects.iter_mut().find(|(pl, _)| pl.collider == Some(body)) {
                // a direct hit (not a blast): kills exploding props at once (FxExplosion_Die, non-chain record); the
                // object's damage filter first (the record's source: the shooter, Projectile_ApplyHitDamage 0x0047eec0)
                let by = if !p.from_player {
                    crate::fx::Src::Badguy
                } else if p.owner.is_some_and(|o| roman.contains(o)) {
                    crate::fx::Src::Player
                } else {
                    crate::fx::Src::Teammate
                };
                if !d.accepts(by, p.no_buildings) {
                    commands.entity(e).despawn();
                    continue;
                }
                d.health -= p.damage;
                if d.health <= 0.0 && d.dying.is_none() {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        t.translation = from + step;
        if p.life <= 0.0 {
            commands.entity(e).despawn();
        }
    }
}

/// Third-person camera behind Roman's shoulder at his yaw and pitch, pulled in when geometry is in the way.
fn follow_camera(
    path: Res<crate::campath::CameraPath>,
    physics: NonSend<Physics>,
    roman: Query<(Entity, &Roman, &Transform), Without<FollowCam>>,
    mut cam: Query<&mut Transform, With<FollowCam>>,
    topsy: Res<crate::topsy::Topsy>,
    cheats: Res<crate::cheats::Cheats>,
    placed: Query<(Entity, &crate::level::Placed)>,
    mut fade: ResMut<crate::camfade::CameraFade>,
    mut proj: Query<&mut Projection, With<FollowCam>>,
    mut base_fov: Local<Option<f32>>,
) {
    if path.holds_camera() {
        return;
    }
    let (Ok((roman_entity, r, rt)), Ok(mut c)) = (roman.single(), cam.single_mut()) else { return };
    let roman_entity = Some(roman_entity);
    // Camera_Update 0x0040e350 with the player's camera nodes (player.json, Trotter_ref): the eye at Camera_end
    // (22.703 behind Roman, 11.128 up, centred), looking at Camera_start's target (7.194 ahead, 6.877 up): at rest
    // the view tilts down 0.141 rad, as in the original's camera dumps. The look pitch (r.pitch, rest -0.15)
    // turns the eye about that target. Five rays from the target (to the eye and to the eye +-5 along the view's
    // right and up axes) pass through objects, which fade (camfade.rs), and the nearest landscape hit shortens the
    // arm (0x0040e56c: objects hit are excluded and the ray cast again; only what remains stops it).
    let rest_pitch = -0.15;
    let yaw = Quat::from_rotation_y(r.yaw);
    let target = rt.translation + yaw * Vec3::new(0.0, 6.877, -7.194);
    let arm = Vec3::new(0.0, 11.128 - 6.877, 22.703 + 7.194);
    let arm = yaw * (Quat::from_rotation_x(r.pitch - rest_pitch) * arm);
    let len = arm.length();
    let eye = target + arm;
    let look = Transform::from_translation(eye).looking_at(target, Vec3::Y);
    let (right, up) = (look.right().as_vec3() * 5.0, look.up().as_vec3() * 5.0);
    let mut dist = len;
    for end in [eye, eye + right, eye - right, eye + up, eye - up] {
        let dir = (end - target).normalize_or(Vec3::Y);
        // every ray as long as the arm (0x2c: the eye distance)
        let full = len;
        let mut from = target;
        let mut left = full;
        // through objects: the landscape alone stops the ray; placed objects and moving bodies hit fade
        while let Some((body, at, normal)) = physics.raycast(from, dir, left) {
            // a landscape face hit from behind (the look target inside a steep hill at its foot: Jolt's rays hit back
            // faces) does not stop the arm; the original's camera stays behind Roman there (fightO 1440, guess: its
            // Havok landscape ray is one-sided)
            if physics.landscape == Some(body) && normal.dot(dir) < 0.0 {
                dist = dist.min(at.distance(target));
                break;
            }
            let object = physics.bodies.get(&body).copied().or_else(|| placed.iter().find(|(_, p)| p.collider == Some(body)).map(|(e, _)| e));
            if let Some(object) = object.filter(|o| Some(*o) != roman_entity) {
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
    c.translation = target + arm / len * dist;
    // zoom: the weapon's camera node (player.json: Camera_rifle eye (0, 0.13, 7.918) at the head, looking at the same
    // target; Camera_machinegun the same eye), its FOV horizontal 0.4363 (rifle) / 0.5236 (machine gun) made vertical
    // for the 1.702 view; position and FOV blended by the weight (0x0040fbb0: fov = target w + fov (1 - w)); the pitch
    // turns the look about the eye
    let mut target = target;
    if let Ok(mut pj) = proj.single_mut() {
        if let Projection::Perspective(pp) = &mut *pj {
            if r.zoom <= 0.0 || base_fov.is_none() {
                *base_fov = Some(pp.fov);
            }
            let zoom_h: f32 = if r.use_mg { 0.5235988 } else { 0.4363323 };
            let zoom_v = 2.0 * ((zoom_h * 0.5).tan() / 1.702_128).atan();
            let w = r.zoom;
            pp.fov = base_fov.unwrap_or(pp.fov) * (1.0 - w) + zoom_v * w;
            if w > 0.0 {
                let eye_l = Vec3::new(0.0, 7.918, 0.13);
                let tgt_l = Vec3::new(0.0, 6.877, -7.194);
                let eye = rt.translation + yaw * eye_l;
                let look = eye + yaw * (Quat::from_rotation_x(r.pitch - rest_pitch) * (tgt_l - eye_l));
                c.translation = c.translation.lerp(eye, w);
                target = target.lerp(look, w);
            }
        }
    }
    // the Topsy Turvy bomb turns the view over (topsy.rs)
    let look = Transform::from_translation(c.translation).looking_at(target, Vec3::Y).rotation;
    // the Topsy Turvy screen cheat (WAAAAAAH, cheats.rs) turns the view over too
    let flip = if cheats.on(0x4000) { std::f32::consts::PI } else { 0.0 };
    c.rotation = look * Quat::from_rotation_z(topsy.roll + flip);
}

pub fn spawn_bomb(commands: &mut Commands, physics: &mut Physics, bombs: &BombAssets, data: &GameData, at: Vec3, velocity: Vec3, fuse: f32) {
    let p = data.props("p_player_tickerbomb");
    let d = Destructible {
        health: 1.0,
        fx_death: Some(TICKERBOMB_FX.into()),
        husk: None,
        area_damage: p.f("area_damage", 100.0),
        radius_inner: p.f("damage_radius_inner", 40.0),
        radius_outer: p.f("damage_radius_outer", 60.0),
        impact_force: p.f("impact_force", 25000.0),
        chain: false,
        start_delay: 0.0,
        min_delay: 0.0,
        acceleration: 1.0,
        dying: None,
        chain_delay: 0.0,
        // the fuse sets it off, not damage
        class: crate::fx::DClass::Default,
        score: 0.0,
        takes_damage: false,
        takes_bullet: false,
        player_damage: false,
        teammate_damage: false,
        badguy_damage: false,
    };
    let e = commands.spawn((Transform::from_translation(at).with_rotation(bombs.base), Visibility::default(), d, Fuse(fuse))).id();
    for (m, mat) in &bombs.parts {
        let c = commands.spawn((Mesh3d(m.clone()), MeshMaterial3d(mat.clone()))).id();
        commands.entity(e).add_child(c);
    }
    let settings = BodySettings::new_dynamic().mass(6.0).friction(0.6).restitution(0.3).angular_damping(0.5);
    if let Some(id) = physics.add_dynamic(e, &bombs.shape, at, bombs.base, velocity, settings) {
        commands.entity(e).insert(Body::rigid(id));
    }
}

/// The tickerbomb's explosion fx (guess: the sticky bomb's snow variant; the behaviour picks it in the game).
const TICKERBOMB_FX: &str = "fx_exp_sticky_snow";

/// Ground under a point with room for Roman above it (the safety net's "freely").
fn on_free_ground(physics: &Physics, at: Vec3) -> bool {
    physics.raycast(at + Vec3::Y * 12.0, -Vec3::Y, 14.0).is_some()
}
