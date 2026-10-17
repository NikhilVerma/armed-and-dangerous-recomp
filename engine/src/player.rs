//! The player, Roman (template `trotter`, property set `p_trotter`), and his weapons, from the game's data:
//! movement speeds, jump, health (p_trotter), the rifle and its projectile (p_player_rifle / proj_rifle_bullet
//! values via docs/subsystems/player.md), sticky bombs (p_player_sticky*), the level's starting loadout (the
//! mission instance: player_add_rifle). Controls and the camera follow are STAND-INS until the original's
//! input map and camera nodes are ported (docs/subsystems/player.md, camera.md).

use crate::levelreset::LevelAppExt;
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
    /// landing_damage_* (+0xbd0 .. +0xbe8) and death_falling_distance / time (+0xbf4 / +0xbf0) without a jumper
    /// pack (with one, jetpack.rs Tuning's turbo numbers)
    pub landing_damage: LandingDamage,
    pub death_fall: (f32, f32),
    pub max_health: f32,
    /// Roman's weapon state (+0xc24, game_09.cpp 723-1990), replaced by these fields: the slot lists and their
    /// plumbing (list init 0x004a0b10, find / has / add 0x004a0b60 / 0x004a0b50 / 0x004a0b80, state init 0x004a0d00,
    /// add / remove a weapon 0x004a0ef0 / 0x004a0f30, the current / special ids and objects 0x004a1660 / 0x004a1670 /
    /// 0x004a15f0 / 0x004a10b0, pending switches 0x004a1680 / 0x004a17a0 / 0x004a1690 / 0x004a17b0, the per-frame
    /// timers 0x004a1890, the special select 0x004a12c0), the ammo records (getters 0x004a1300 / 0x004a1320 /
    /// 0x004a1360 / 0x004a1380, setter 0x004a14e0: `clip` / `ammo` / `stickies` / `topsy`), the weapon objects
    /// (equip 0x004a10e0 and destroy 0x004a0ff0 / 0x004a1060: the hand model's visibility, hand_weapon; attach bones
    /// 0x004a0ea0 / 0x004a1a30), the PlayerWeapon load (0x004a2150 over WeaponObject 0x004a1bf0: `Weapon` from the
    /// property set) and its collision description (0x004a1d80: no body), its owner / hand bone (0x004a1e30 /
    /// 0x004a1e10) and remove (0x004a1da0), the slot arrays 0x004a1600 / 0x004a1630, removing an id 0x004a0cd0, the
    /// reset 0x004a0fb0, the special's cooldown queue (0x004a1950 / 0x004a1930 / 0x004a1970: sticky.rs StickyThrow;
    /// can fire 0x004a1900: no throw pending and bombs left)
    pub arsenal: crate::weapons::Arsenal,
    /// a weapon switch's target slot (the pending switch +0x28, 0x004a0bb0): equipped at the change anim's frame 13
    pub switch_to: Option<usize>,
    /// the gun-specific fire loop is playing (the machine gun's wpn_rome_machgun_fire_lp)
    pub mg_firing: bool,
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
    /// "normal_twist_limit" (+0x560, degrees x 0x005bcba0 at load: radians): how far the torso twists from the legs
    /// standing before the turn anim (charanim.rs twist)
    pub twist_limit: f32,
    /// a teleport (a loaded game, a playtest warp) set the yaw: the legs take it too (the original builds a new
    /// player and look-at record)
    pub twist_reset: bool,
    pub reloading: f32,
    /// a reload in progress: seconds into the reload anim, and whether its load sound played (0x00475ff0 state 3:
    /// the sound at frame 0x0059eef4[w] (rifle 10, machine gun 15), the clip refilled at frame 0x0059eedc[w] = 43)
    pub reload_clock: Option<(f32, bool)>,
    /// a weapon switch in progress: seconds into the weapon-change anim (trotter_wchange); it completes at frame 13
    /// (0x00475ff0 -> 0x004a16b0)
    pub wchange: Option<f32>,
    pub move_local: Vec2,
    /// a standing jump waiting for trotter_jump_into to end (0x0047aaf0: flag 0x40 set, 0x80 not yet: the impulse
    /// comes when the anim sets 0x80); seconds left
    pub jump_wind: Option<f32>,
    /// this frame's horizontal speed as the update measures it before the drive (+0x4bc: the body's velocity from
    /// the last step), last frame's (+0x47c, for the stop fx) and the slide fx's anchor (+0x568: fx_roman_slide
    /// runs while it lives)
    pub hspeed: f32,
    pub prev_hspeed: f32,
    /// friction_min (+0x5a8), friction_max (+0x5a4), friction_exponent (+0x5ac): the ground contacts' friction blend
    pub friction: (f32, f32, f32),
    pub slide_fx: Option<Entity>,
    /// +0xc00: 0.5 s after each shot (0x004788a0 at 0x0047a555), counted down by dt; the empty-clip reload and the
    /// weapon fallback wait for it (0x0047a6af)
    pub fire_delay: f32,
    /// Game+0xd68: seconds the Fire key has been held with the machine gun in hand (0 when released); the machine
    /// gun's scatter grows with it (0x0047f4e0)
    pub mg_held: f32,
    /// this frame's wanted horizontal velocity (the drive's input; the slide fx needs it below 2 units/s)
    pub wish_h: f32,
    /// a rapid-fire weapon's held muzzle fx (fx_muzzle_flash_flemming loops its flash and shell casings): the anchor
    /// it follows, and the frames since the last shot (vt+0x154 0x004a1f20 starts it once; the weapon update 0x004a1dd0
    /// releases it the frame after the last shot)
    pub muzzle_fx: Option<Entity>,
    pub muzzle_idle: u8,
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
    /// an arrow (ArrowProjectile, vtable 0x005a9a40): a ballistic flight under the world's gravity (64.348), and its
    /// fly-by sound once within 70 of Roman (0x00482620)
    pub arrow: bool,
    pub flyby: bool,
    /// the damage record's class flag (projectile vt+0x13c): a mounted gun's bullet 0x80000, the forge cannon's mortar
    /// 0x100000, which an AI victim scales by its stig_ / forge_damage_multiplier (GenericAIObject_Slot52 0x00430340)
    pub kind: DamageKind,
}

/// A projectile's damage class as its record flags carry it (projectile vt+0x13c, the base 0x0047eba0 plus the class's
/// bit): the AI victims scale the damage by their own multiplier for it (GenericAIObject_Slot52 0x00430340).
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub enum DamageKind {
    #[default]
    Plain,
    /// proj_stig_bullet (class 3, vtable 0x005aec10: 0x00481010 adds 0x80000): the mounted guns' bullets
    Stig,
    /// proj_forge_mortar (class 0xb, ForgeCannonProjectile 0x005a33b8: 0x00481000 adds 0x100000)
    Forge,
}

impl DamageKind {
    /// By the projectile's behaviour (the class table of 0x0045df60 / ObjectSystem_CreateObject 0x0045f020).
    pub fn of_behavior(b: &str) -> Self {
        match b {
            "proj_stig_bullet" => DamageKind::Stig,
            "proj_forge_mortar" => DamageKind::Forge,
            _ => DamageKind::Plain,
        }
    }
}

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

/// A projectile of a property set (Projectile, vtable 0x005ac790). Spawning one stands for the original's create +
/// launch: the spawn helpers 0x0047dbf0 (from the weapon's emitter bone toward the aim point) / 0x0047de40 (from a
/// point toward a target point), SetTargetPoint 0x0047e340, SetOwner 0x0047e370 (`owner`), Launch 0x0047e400 (the
/// velocity "speed" x 3 along the direction: Projectile_Load 0x0047e0c0, GetSpeed 0x0047e360), the start effect
/// 0x0047ee30 (fx_start following it). No physics body: the collision description 0x0047e320 is replaced by the
/// per-frame ray (projectiles).
pub fn spawn_projectile(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, p: &Props, from: Vec3, dir: Vec3, from_player: bool, owner: Option<Entity>) -> Entity {
    let speed = p.f("speed", 300.0) * 3.0;
    launch_projectile(commands, fx, p, from, dir * speed, from_player, owner)
}

/// A projectile toward a target point (0x0047de40 with its target). The arrows' launch differs (ArrowProjectile
/// 0x00482510): the flight time T = 3 sqrt(|d|) / speed (speed x 3 from Projectile_Load), the velocity d / T with the
/// vertical part raised by T x 64.348 / 2, so it lands on the point under gravity; the others fly straight.
pub fn spawn_projectile_to(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, p: &Props, from: Vec3, target: Vec3, from_player: bool, owner: Option<Entity>) -> Entity {
    let speed = p.f("speed", 300.0) * 3.0;
    let vel = if p.s("behavior") == Some("proj_arrow") { arrow_velocity(from, target, speed) } else { (target - from).normalize_or(Vec3::Z) * speed };
    launch_projectile(commands, fx, p, from, vel, from_player, owner)
}

/// The arrow's launch velocity (0x00482510), engine axes (Y up).
pub fn arrow_velocity(from: Vec3, target: Vec3, speed: f32) -> Vec3 {
    let d = target - from;
    let t = 3.0 * d.length().sqrt() / speed.max(1e-3);
    if t <= 1e-4 {
        return Vec3::ZERO;
    }
    d / t + Vec3::Y * (t * 64.348 * 0.5)
}

fn launch_projectile(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, p: &Props, from: Vec3, vel: Vec3, from_player: bool, owner: Option<Entity>) -> Entity {
    let dir = vel.normalize_or(Vec3::Z);
    let e = commands
        .spawn((
            // its game frame (as placed objects carry): game +Y along the flight, game +Z up, so the fx scripts
            // attached to it (fx_start trails) run in the frame they were authored in
            Transform::from_translation(from).with_rotation(Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation * crate::level::game_to_engine()),
            Projectile {
                vel,
                arrow: p.s("behavior") == Some("proj_arrow"),
                flyby: p.s("behavior") == Some("proj_arrow"),
                kind: DamageKind::of_behavior(p.s("behavior").unwrap_or("")),
                // "lifetime" (Projectile_Load 0x0047e0c0, default 4): removed when it runs out (0x0047e4a0)
                life: p.f("lifetime", 4.0),
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
    e
}

/// The third-person camera (camera.rs).
pub use crate::camera::FollowCam;

/// p_trotter turn_max_speed / pitch_max_speed (player.json movement.turn; zoomed: the weapon's, 0x004759e0 /
/// 0x00475990)
const TURN_MAX_SPEED: f32 = 0.3;
const PITCH_MAX_SPEED: f32 = 0.95;

/// How hard the drive's force action pulls the body's velocity to the wanted one, per unit of movement power. Measured
/// on the original (oracle trace, tools/oracle_scenarios/stop.json, every frame on the lev_1_m1 start slope): from
/// rest, full stick reaches 36.6 units/s in the first moving frame (k = 0.92 at 60 fps, power 10) and his full 39.2
/// in the next; releasing the stick stops him within one frame (no slide). How the force action pool
/// (0x00455a30 -> ForceActionPool_Add 0x00403fb0) applies the force through the Havok steps is not decoded; this gain
/// on the drive's power, with the ground friction modelled after it (control), reproduces the measured profile.
const DRIVE_GAIN: f32 = 15.0;

/// The movement input's response (PlayerObject_Update, game_06.cpp:2479-2501): the two axes' vector, longer than 1
/// cut to 1 (two keys at once: a diagonal at full speed, not faster); its length through the control curve 0x00414c00
/// (fmod by control_exponent 2 with control_bias 0: the length itself) and above 0.225 (0x005bcfc4) raised by 0.4
/// (0x005bcd94), at most 1.
pub fn move_curve(m: Vec2) -> Vec2 {
    let len = m.length();
    if len <= 1e-6 {
        return Vec2::ZERO;
    }
    let c = len.min(1.0);
    let v = if c > 0.225 { (c + 0.4).min(1.0) } else { c };
    m / len * v
}

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
    /// the gun in hand by its mission name ("rifle", "machinegun", ...; "" with none), and its id
    pub weapon: &'static str,
    pub weapon_id: Option<crate::weapons::WeaponId>,
    pub unlimited: bool,
    /// the special in hand and its count
    pub special: crate::weapons::SpecialId,
    pub special_count: u32,
    /// how many guns and specials Roman carries (the HUD's weapon-cycle arrows: 0x004a1610 / 0x004a1640)
    pub guns: u32,
    pub specials: u32,
}

pub struct PlayerPlugin;

/// The order of the player's systems that others hook into: the weapon handling (fire, reload, switch, specials).
#[derive(SystemSet, Clone, Debug, PartialEq, Eq, Hash)]
pub enum PlayerSet {
    Weapons,
}

impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (apply_loadout, hand_weapon).chain());
        app.init_level_resource::<PlayerStats>()
            .init_level_resource::<Banter>()
            .init_level_resource::<LookDelta>()
            .add_systems(Update, grab_cursor.before(control))
            .add_systems(Update, (control, ground_effects, weapons.in_set(PlayerSet::Weapons), projectiles, damage_feedback, death).chain().run_if(crate::playing))
            .add_systems(Update, publish_stats.after(control).before(weapons))
            .add_systems(Update, (player_state.after(death), idle_banter).run_if(crate::playing));
    }
}

/// Gives the new Roman the level's loadout (mission.rs Loadout; the player's reset 0x00475650): the weapons the
/// mission adds, in id order into the three slots, filled as player_set_ammo_* says (0x004a14e0), the first in hand
/// (0x004a10e0 with slot 0); the specials' counts (player_set_num_*), the first of them in id order in hand; and the
/// vindaloo upgrade flag (vindaloo_is_upgraded, Game+0xdd0).
fn apply_loadout(mut q: Query<&mut Roman, Added<Roman>>, mission: Option<ResMut<crate::mission::Mission>>, data: Option<Res<GameData>>, mut upgrade: ResMut<crate::weapons::VindalooUpgrade>) {
    use crate::weapons::{Gun, GunSpec, SpecialId, WeaponId};
    let (Some(mut m), Some(data)) = (mission, data) else { return };
    for mut r in &mut q {
        let l = m.loadout.clone();
        r.arsenal.clear();
        for w in l.weapons.iter().filter_map(|w| WeaponId::from_name(w)) {
            let mut g = Gun { clip: 0, ammo: 0, timer: 0.0, id: w, spec: GunSpec::load(&data, w) };
            g.set_ammo(l.ammo.get(w.name()).copied().unwrap_or(-2));
            r.arsenal.add(g);
        }
        r.arsenal.equip_slot(0);
        for s in SpecialId::ALL {
            r.arsenal.set_count(s, l.special(s.name()).min(crate::weapons::spec::special_max(&data, s)));
        }
        let first = r.arsenal.specials().next().map_or(SpecialId::StickyBomb, |(s, _)| s);
        r.arsenal.special = first;
        upgrade.0 = l.vindaloo_upgraded;
        let guns: Vec<&str> = r.arsenal.guns().map(|g| g.id.name()).collect();
        let specials: Vec<String> = r.arsenal.specials().map(|(s, n)| format!("{} {n}", s.name())).collect();
        m.log.push(format!("loadout: {} (in hand: {}), specials: {}", guns.join(", "), r.arsenal.in_hand_id().map_or("none", WeaponId::name), specials.join(", ")));
    }
}

/// Builds Roman from his property set at a placed trotter instance (feet at `feet`, engine space).
pub fn make_roman(physics: &mut Physics, p: &Props, data: &GameData, feet: Vec3, yaw: f32) -> Option<Roman> {
    let radius = p.f("collision_radius", 4.0).clamp(2.0, 5.0);
    // steep ground: the player's contact callback 0x00477c90 zeroes the friction of contacts with normal z <= 0.7
    // (0x005bcd48, about 45.6 degrees) and 0x00478200 stops counting them as ground, so Roman slides down them
    let walker = physics.add_walker(feet, radius, 11.0, 0.7f32.acos())?;
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
        // (PlayerObject_Load's defaults, game_05.cpp:713-722)
        landing_damage: LandingDamage {
            min_hurt: p.f("landing_damage_min_hurt", 70.0),
            max_hurt: p.f("landing_damage_max_hurt", 200.0),
            min_speed: p.f("landing_damage_min_speed", 40.0),
            max_speed: p.f("landing_damage_max_speed", 100.0),
        },
        death_fall: (p.f("death_falling_distance", 200.0), p.f("death_falling_time", 3.0)),
        max_health: p.f("max_health", 400.0),
        // a player outside a mission (no loadout) holds the rifle and 5 sticky bombs; apply_loadout replaces them
        arsenal: {
            use crate::weapons::{Arsenal, Gun, GunSpec, SpecialId, WeaponId};
            let mut a = Arsenal::default();
            a.add(Gun::full(WeaponId::Rifle, GunSpec::load(data, WeaponId::Rifle)));
            a.set_count(SpecialId::StickyBomb, 5);
            a
        },
        switch_to: None,
        mg_firing: false,
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
        twist_limit: p.f("normal_twist_limit", 45.0).to_radians(),
        twist_reset: true,
        reloading: 0.0,
        reload_clock: None,
        wchange: None,
        move_local: Vec2::ZERO,
        jump_wind: None,
        hspeed: 0.0,
        prev_hspeed: 0.0,
        // (not in p_trotter: PlayerObject_Load's defaults, game_05.cpp:573)
        friction: (p.f("friction_min", 0.3), p.f("friction_max", 2.0), p.f("friction_exponent", 4.0)),
        slide_fx: None,
        fire_delay: 0.0,
        mg_held: 0.0,
        wish_h: 0.0,
        muzzle_fx: None,
        muzzle_idle: 0,
    })
}

/// Shows the weapon model in use in Roman's hand (the weapon object equip 0x004a10e0 / destroy 0x004a0ff0: one model
/// per carried gun hangs in the hand, the one in use shown). The Topsy Turvy bomb: trotter_topsy_into's note
/// "topsy_create" (0.467 s) creates player_topsyturvy in the hand and hides the weapons (PlayerObject_OnTopsyCreate
/// 0x0047bbe0, 0x004a0e00(0)); it is removed when the effect's timer runs out (Game+0xc80) and the world turns back.
/// When the weapons come back is not traced (guess: when the effect ends).
fn hand_weapon(roman: Query<&Roman>, topsy: Res<crate::topsy::Topsy>, mut held: Query<(&crate::level::HandWeapon, &mut Visibility)>) {
    let Ok(r) = roman.single() else { return };
    let gun = r.arsenal.in_hand_id();
    use crate::level::HandWeapon;
    use crate::topsy::Phase;
    let bomb = matches!(topsy.phase, Phase::Turn | Phase::Hang) || (topsy.phase == Phase::Into && topsy.t >= 14.0 / 30.0);
    let away = bomb || topsy.phase == Phase::Back;
    for (w, mut v) in &mut held {
        let show = match *w {
            HandWeapon::Topsy => bomb,
            HandWeapon::Gun(id) => !away && Some(id) == gun,
        };
        let want = if show { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
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
    mut settle: Local<CursorSettle>,
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
        settle.start();
        *last_abs = None;
    }
    if !play || !w.focused {
        settle.start();
        *last_abs = None;
        return;
    }
    let centre = Vec2::new(w.width() / 2.0, w.height() / 2.0);
    let offset = w.cursor_position().map(|p| p - centre);
    let take = settle.look(offset);
    if let Some(o) = offset.filter(|_| take) {
        look.0 = o;
    }
    // when the cursor did not move, the raw device motion: streamed input (Moonlight from a Mac trackpad) can leave
    // the cursor still while raw input arrives, and some devices report absolute positions there (0..65535 over the
    // desktop, which winit passes on as motion: the view ran away "only down"): those are turned into the change
    // since the last report, scaled to window pixels
    let raw = motion.delta;
    if look.0 == Vec2::ZERO && raw != Vec2::ZERO && take {
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
    w.set_cursor_position(Some(centre));
}

/// Entering play (or the window regaining focus) puts the cursor at the window centre; its offset from the centre is
/// look input only once the cursor has been seen there. The warp lands a frame or more later, later still at a low
/// frame rate or through a streamed desktop: a fixed two frames read the way from the clicked menu row (the yes / no
/// box's Yes, below the centre) to the centre as one big look down (feedback 28, "Roman looking down" after Restart).
#[derive(Default)]
pub struct CursorSettle {
    settling: bool,
    frames: u32,
}

impl CursorSettle {
    /// frames always skipped, as before
    const MIN_FRAMES: u32 = 2;
    /// a cursor that never shows at the centre is taken as it is after this many frames
    const MAX_FRAMES: u32 = 30;
    /// how near the centre (window pixels) counts as there
    const CENTRED: f32 = 1.5;

    pub fn start(&mut self) {
        *self = CursorSettle { settling: true, frames: 0 };
    }

    /// Whether this frame's cursor offset from the centre (None: no cursor in the window) is look input.
    pub fn look(&mut self, offset: Option<Vec2>) -> bool {
        if !self.settling {
            return true;
        }
        self.frames += 1;
        let centred = offset.is_some_and(|o| o.length() <= Self::CENTRED);
        if (self.frames > Self::MIN_FRAMES && centred) || self.frames >= Self::MAX_FRAMES {
            self.settling = false;
        }
        false
    }
}

/// WASD relative to the camera yaw, Space jump, mouse look (stand-in bindings).
#[allow(clippy::too_many_arguments)]
pub(crate) fn control(
    path: Res<crate::campath::CameraPath>,
    time: Res<Time>,
    bot: Res<crate::bot::Bot>,
    motion: Res<AccumulatedMouseMotion>,
    look: Res<LookDelta>,
    playtest: Option<Res<crate::playtest::Playtest>>,
    opts: Res<crate::options::GameOptions>,
    topsy: Res<crate::topsy::Topsy>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(&mut Roman, &mut Transform, &mut Health, Option<&mut crate::jetpack::JetPack>)>,
    (ctl, mounted, rig): (Res<crate::input::Controls>, Res<crate::turret::Mounted>, Res<crate::camera::CameraRig>),
    mut fx: MessageWriter<crate::surface::StartSurfaceFx>,
    // the breaths' decks with their own random stream (drawing on a shared one shifted other systems' rolls)
    (mut sounds, mut breaths): (MessageWriter<crate::audio::PlaySound>, crate::levelreset::PerLevel<([crate::audio::RandomDeck; 2], crate::fxvm::CrtRand)>),
) {
    let dt = time.delta_secs();
    let (breaths, rng) = breaths.get();
    for (mut r, mut t, mut h, mut jet) in &mut q {
        if h.0 <= 0.0 {
            r.dead = true;
        }
        let yaw_before = r.yaw;
        // on a mounted gun the player update takes no input (PlayerCanAct: Game+0xac8 set); turret.rs holds him
        if mounted.0.is_some() {
            continue;
        }
        // mouse look as the original's: measured with scripted mouse counts (hook MOUSEX / MOUSEY, camera dumps):
        // 0.0100 rad of turn and 0.0057 rad of pitch per count at the default sensitivity, linear; the multiplier is
        // m_mouseSensitivity + 0.5 (player.md 2: default 0.5 -> 1.0; InputPC_SetMouseSensitivity 0x004f7cf0 stores it,
        // InputPC_AxisValue 0x004f9560 multiplies the mouse axes by it)
        let k = 0.0100 * (opts.mouse_sensitivity + 0.5);
        let kpitch = 0.0057 * (opts.mouse_sensitivity + 0.5);
        let inv = if opts.invert_mouse { -1.0 } else { 1.0 };
        // the topsy states (flags 0x7800000) take the turn input away
        // zoom: held Zoom (Z, E; 0x00478xxx game_06.cpp:2819-2836) enters the weapon camera; turning and pitching
        // then use the weapon's zoomed speeds (rifle / machine gun 0.05 / 0.25 against 0.3 / 0.95: 0x00475990 /
        // 0x004759e0)
        // (the toggle form when ToggleZoom is bound, 0x0047bc80(10): unbound by default, so zoom follows the held key)
        let zoom_held = ctl.down(crate::input::Act::Aim) && !r.dead;
        let step = dt / r.zoom_secs.max(1e-3);
        r.zoom = if zoom_held { (r.zoom + step).min(1.0) } else { (r.zoom - step).max(0.0) };
        let (kt, kp) = if zoom_held { (0.05 / 0.3, 0.25 / 0.95) } else { (1.0, 1.0) };
        if !topsy.active() {
            // the look pitch limits: the camera's (+0x74 in [-1.5608, 1.15], Camera_Update 0x0040e350; camera.rs)
            let (lo, hi) = rig.pitch_limits();
            // the player's look input is the cursor's movement (LookDelta); scripted runs inject device motion
            let d = if playtest.is_some() { motion.delta } else { look.0 };
            r.yaw -= d.x * k * kt;
            r.pitch = (r.pitch - d.y * kpitch * inv * kp).clamp(lo, hi);
            // the right stick and the arrow keys (input.rs; the TurnLeft / Right, LookUp / Down actions, axes past the
            // dead zone 0.21 linear: Controls_AxisMax 0x00414a30 with InputPC_AxisValue 0x004f9560): the turn
            // turn_max_speed x axis x dt x 10 (0.3: 3 rad/s at full deflection; zoomed 0.05), the pitch
            // pitch_max_speed x axis x dt twice, by the player update (game_06.cpp:2379) and by Camera_Update
            // (game_01.cpp:2895) (0.95: 1.9 rad/s; zoomed 0.25)
            let stick = ctl.axis(crate::input::Act::Look);
            r.yaw -= stick.x * TURN_MAX_SPEED * 10.0 * dt * kt;
            r.pitch = (r.pitch + stick.y * 2.0 * PITCH_MAX_SPEED * dt * inv * kp).clamp(lo, hi);
        }
        let mut wish = Vec3::ZERO;
        // the stick as the update takes it (x right, y forward, after the move curve): the jumper pack's air control
        let mut move_input = Vec2::ZERO;
        // the playtest bot's decisions (bot.rs `think`, before this system): its walk, and its facing / aim over the look
        let bot_walk = bot.walk;
        if let Some(yaw) = bot.face {
            r.yaw = yaw;
        }
        if let Some((yaw, pitch)) = bot.aim {
            r.yaw = yaw;
            r.pitch = pitch;
        }
        if !r.dead && path.until_text() == Some("camera_path_l1m1") {
            // PlayerObject_Update 0x004788a0: on lev_1_m1 the stick is held forward while the intro path plays (and
            // until the briefing it ends on pauses the game: the original's pause comes on the path's last frame, Roman
            // still at full speed; he stops only after it)
            wish += Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z * r.fwd_speed;
        }
        if !r.dead {
            let f = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
            let s = Quat::from_rotation_y(r.yaw) * Vec3::X;
            if let Some(dir) = bot_walk {
                wish += dir * r.fwd_speed;
                move_input += Vec2::new(dir.dot(s), dir.dot(f));
            }
            // the Move action (WASD: full deflection; the left stick: analog): target speed max x a + (1 - a) x min for
            // any deflection a (fwd / side_max_speed_min 4, game_06.cpp:2586); backward at the same speed (measured: backO, the original walks back at 39 units/s)
            let m = move_curve(ctl.axis(crate::input::Act::Move));
            move_input += m;
            if m != Vec2::ZERO {
                let sp = |max: f32, a: f32| max * a + (1.0 - a) * 4.0f32.min(max);
                let fy = if m.y > 0.0 { sp(r.fwd_speed, m.y) } else if m.y < 0.0 { -sp(r.fwd_speed, -m.y) } else { 0.0 };
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
        // jump_fwd / side_max_speed (6.5) with jump_movement_power (4). Integrated exactly per frame, the power times
        // DRIVE_GAIN (the original's measured stiffness: full speed in a frame, a dead stop on release).
        // the power: movement_power_min..max by the stick (keys: full stick); for 1 s after a knockback (w = 1 -
        // slow) min becomes w x 7 + (1 - w) and max x (0.5 (1 - w) + w) (game_06.cpp:2572)
        r.wish_h = Vec2::new(wish.x, wish.z).length();
        r.hspeed = Vec2::new(r.vel.x, r.vel.z).length();
        let stick = (r.wish_h / r.fwd_speed.max(1e-3)).min(1.0);
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
        // the jumper pack (jetpack.rs): this frame's stick and turn (its wing anims, the turn sound)
        if let Some(j) = jet.as_deref_mut() {
            j.stick = if topsy.active() || r.dead { Vec2::ZERO } else { move_input.clamp_length_max(1.0) };
            j.turn = yaw_before - r.yaw;
            j.turn_sound(dt);
        }
        let flying = jet.as_deref().and_then(|j| j.air_velocity(r.vel, Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z, Quat::from_rotation_y(r.yaw) * Vec3::X, dt));
        if let (Some(v), true) = (flying, r.jumping) {
            // the pack's turbo / glide air control (0x004799d5 in the states 2 / 4)
            r.vel = v;
        } else if r.jumping {
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
            let k = 1.0 - (-strength * DRIVE_GAIN * s5 * dt).exp();
            r.vel.x += d.x * k;
            r.vel.z += d.z * k;
            if !r.on_ground {
                r.vel.y += d.y * k;
            }
        }
        // the ground contacts' friction (+0x5a0, 0x00478200; consumed by the step's contact solver): friction_max at
        // rest blending to friction_min at the max speed, w = (1 - min(|v.xy| / max speed, 1))^friction_exponent,
        // against the horizontal velocity at mu x gravity. It holds full stick at 39.2 units/s (the original's
        // measured walk: 0.653 units a frame, stop.json / the intro). The blend is taken on the speed the drive leaves
        // (the step solves drive and friction together; on the speed before it the first frame from rest would be 31
        // units/s, the original's is 36.6)
        if r.on_ground && !r.jumping && !r.dead {
            let h = Vec2::new(r.vel.x, r.vel.z);
            let speed = h.length();
            if speed > 1e-4 {
                let w = if max_speed > 0.0 { (1.0 - (speed / max_speed).min(1.0)).powf(r.friction.2) } else { 0.0 };
                let mu = r.friction.0 * (1.0 - w) + r.friction.1 * w;
                let gravity = 64.348 * r.anti_gravity * 0.5;
                let s = (speed - mu * gravity * dt).max(0.0) / speed;
                r.vel.x *= s;
                r.vel.z *= s;
            }
        }
        // the jump (0x0047aaf0): TurboJump with the grounded grace +0x570 > 0 sets flag 0x40, and 0x80 at once when the
        // horizontal speed +0x4bc is above 20 (0x005bcbe8); standing, 0x80 comes when trotter_jump_into (0.133 s) ends.
        // With both: the impulse mass x jump_impulse and "Fx_fs_roman_jump" at the feet (0x00478800)
        let mut take_off = false;
        // with a jumper pack Jump launches instead (0x0047aaf0 with +0xabc), and folds / spreads the wings in the air
        let has_pack = jet.as_ref().is_some_and(|j| j.pack);
        // (a launch's kick lands the frame after the press: the force action pool's next step)
        let launch = jet.as_deref_mut().and_then(|j| j.take_kick()).filter(|_| !r.dead);
        if let (Some(j), false) = (jet.as_deref_mut(), r.dead) {
            j.jump_input(ctl.pressed(crate::input::Act::Jump) || bot.jump, ctl.down(crate::input::Act::Jump) || bot.jump, r.grace > 0.0, r.falling, r.landing.is_some(), r.vel.y);
        }
        if let Some(w) = r.jump_wind {
            let w = w - dt;
            r.jump_wind = if w <= 0.0 || r.dead { None } else { Some(w) };
            take_off = w <= 0.0 && !r.dead;
        }
        if !has_pack && r.on_ground && r.jump_wind.is_none() && !take_off && !r.jumping && (ctl.pressed(crate::input::Act::Jump) || bot.jump) && !r.dead {
            if Vec2::new(r.vel.x, r.vel.z).length() > 20.0 {
                take_off = true;
            } else {
                r.jump_wind = Some(JUMP_INTO);
            }
        }
        if let Some(kick) = launch {
            // the launch: the kick, then this step's turbo gravity (as a jump's: velocity before position); the landing
            // anim and the fall state end (flags & 0xfffff83c)
            let g = jet.as_deref().and_then(|j| j.gravity_scale()).unwrap_or(r.anti_gravity * 0.5);
            r.vel.y = r.vel.y.max(0.0) + kick - 64.348 * g * dt;
            r.jumping = true;
            r.landing = None;
            r.falling = false;
            r.jump_wind = None;
            ground_fx(&physics, &mut fx, t.translation, "fx_fs_roman_jump");
        } else if take_off {
            // the impulse, then this step's gravity as in the airborne steps (velocity before position): the
            // original's arc (jumpA, peak 14.6) fits 70 and gravity 160.9 that way
            r.vel.y = r.jump_impulse - 64.348 * r.anti_gravity * 0.5 * dt;
            r.jumping = true;
            ground_fx(&physics, &mut fx, t.translation, "fx_fs_roman_jump");
            // the jump's breath (0x0047aaf0: 0x00493b70 with table 0x005fb99c, 2 sounds)
            const JUMP_BREATH: [&str; 2] = ["fol_breath_light_short_1", "fol_breath_light_short_2"];
            sounds.write(crate::audio::PlaySound::at(JUMP_BREATH[breaths[0].deal(2, || rng.rand() as f32 / 32768.0)], t.translation));
        } else if r.on_ground && !r.jumping {
            r.vel.y = r.vel.y.max(-5.0);
        } else {
            // (in the jump state gravity acts even when the walker reports ground: wedged under an overhang or on a
            // ledge's edge it did, the rising speed never decayed, the jump never ended and the air control piled up
            // horizontal speed: a bot run at (-102, -5) reached 394 units/s and was flung 640 units when it came free)
            // world gravity -64.348 x anti_gravity x 0.5 (0x005bcb4c): -160.9 with jump_anti_gravity 5 in lev_1_m1
            // (docs/subsystems/player.md); jump impulse 70 -> about 15 units high
            // (the jumper pack's flight has its own: turbo_anti_gravity / turbo_glide_anti_gravity x 0.5). No terminal
            // speed: the original has none (an early engine cap at 4 x fall_speed, -200, kept a pack's fall from ever
            // passing fall_speed_turbo -200, so it never armed the wings; without a pack the fall rules kill him
            // first: 110 units of drop are ~188 units/s)
            let g = jet.as_deref().and_then(|j| j.gravity_scale()).unwrap_or(r.anti_gravity * 0.5);
            r.vel.y -= 64.348 * g * dt;
        }
        let vel = r.vel;
        let fwd = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
        let right = Quat::from_rotation_y(r.yaw) * Vec3::X;
        r.move_local = Vec2::new(vel.dot(right), vel.dot(fwd));
        let before = t.translation;
        let vy = r.vel.y;
        let was_jump = r.jumping;
        // dead, the player object stays where it died (the original's capture fall2O: the position holds at the death
        // height while the ragdoll falls; the camera looks down from there)
        let vel = if r.dead { Vec3::ZERO } else { vel };
        if r.dead {
            r.vel = Vec3::ZERO;
        }
        if let Some((feet, on)) = physics.move_walker(&r.walker, vel, dt) {
            if on && r.vel.y <= 0.0 {
                r.vel.y = 0.0;
            }
            r.on_ground = on;
            if on && r.vel.y <= 0.0 {
                r.jumping = false;
            }
            // in the air, the horizontal speed is what the walker achieved: blocked by a wall or wedged, the speed
            // into it is lost, as the original's Havok body loses it at the contact (without this the air control
            // piled up speed while wedged: 2000 units/s after 80 s in a bot run, then a fling across the map)
            if !on && dt > 0.0 && !r.dead {
                let done = (feet - before) / dt;
                if Vec2::new(done.x, done.z).length() < Vec2::new(r.vel.x, r.vel.z).length() {
                    r.vel.x = done.x;
                    r.vel.z = done.z;
                }
            }
            t.translation = feet;
        }
        if fall_and_land(&mut r, &t, &mut h, vy, was_jump, dt, &physics, &mut fx, t.translation.distance(before), topsy.active(), jet.as_deref_mut()) {
            // the landing's breath (0x00478200: 0x00493b70 with table 0x005fb9cc, 2 sounds)
            const LAND_BREATH: [&str; 2] = ["fol_breath_heavy_short_1", "fol_breath_heavy_short_2"];
            sounds.write(crate::audio::PlaySound::at(LAND_BREATH[breaths[1].deal(2, || rng.rand() as f32 / 32768.0)], t.translation));
        }
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
        // Roman faces where the camera looks (his model faces -Y in game space = +Z engine: turn by pi). The Transform
        // replaces the original's matrix getters RotZ(+0x974) x base (vt+0x70..+0x7c: 0x00473b60, 0x00473bf0,
        // 0x00473c80, 0x00473d10)
        t.rotation = Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * r.base;
    }
}

/// Roman's state as the HUD, the pub and the traces read it (PlayerStats), every frame: also while the game is paused,
/// so a weapon taken in the pub is the one in hand at once (the original's trace: the machine gun from the frame it is
/// taken, the pub still open).
fn publish_stats(q: Query<(&Roman, &Transform, &Health)>, mut stats: ResMut<PlayerStats>) {
    let Ok((r, t, h)) = q.single() else { return };
    stats.health = h.0.max(0.0);
    stats.max_health = r.max_health;
    stats.position = t.translation;
    let w = r.arsenal.in_hand();
    stats.clip = w.map_or(0, |w| w.clip);
    stats.ammo = w.map_or(0, |w| w.ammo);
    stats.weapon = w.map_or("", |w| w.id.name());
    stats.weapon_id = w.map(|w| w.id);
    stats.unlimited = w.is_some_and(|w| w.spec.unlimited);
    (stats.special, stats.special_count) = r.arsenal.special_in_hand();
    stats.stickies = r.arsenal.count(crate::weapons::SpecialId::StickyBomb);
    stats.guns = r.arsenal.gun_count() as u32;
    stats.specials = r.arsenal.specials().count() as u32;
}

fn aim(r: &Roman, t: &Transform) -> (Vec3, Vec3) {
    let dir = Quat::from_euler(EulerRot::YXZ, r.yaw, r.pitch + 0.08, 0.0) * Vec3::NEG_Z;
    (t.translation + Vec3::Y * 8.5 + dir * 3.0, dir)
}

/// The reload anim's frame (counted from 1, 30 a second) of each gun's load sound (0x0059eef4[w]) and the sound
/// (0x005fb9dc[w]); the clip refills at frame 43 (0x0059eedc[w]) at the latest.
fn reload_sound(id: crate::weapons::WeaponId) -> (f32, &'static str) {
    use crate::weapons::WeaponId as W;
    match id {
        W::Machinegun => (15.0, "wpn_rome_machgun_load"),
        W::Snipergun => (20.0, "wpn_rome_sniper_load"),
        W::Rifle => (10.0, "wpn_rome_rifle_load"),
        W::Vindaloo => (43.0, "wpn_rome_vindaloo_load"),
        W::Gurner => (43.0, "wpn_rome_gurner_load"),
        W::Sharkgun => (43.0, "wpn_rome_shark_load"),
    }
}

/// The dry-fire sound of the gun in hand with nothing usable (0x004788a0's jump table 0x0047a938; none for the
/// shark gun).
fn dry_fire_sound(id: crate::weapons::WeaponId) -> Option<&'static str> {
    use crate::weapons::WeaponId as W;
    match id {
        W::Machinegun => Some("wpn_machgun_dry_fire_1"),
        W::Snipergun => Some("wpn_rifle_dry_fire_2"),
        W::Rifle => Some("wpn_rifle_dry_fire_1"),
        W::Vindaloo => Some("wpn_rocket_dry_fire_2"),
        W::Gurner => Some("wpn_mortar_dry_fire"),
        W::Sharkgun => None,
    }
}

/// The launchers' kick (0x004788a0, jump table 0x0047a920): a push of 300 x mass for 0.1 s (0x00455aa0) back along
/// his facing and 0.2 of it up, then the 1 s weakened drive (+0x9b4 / +0x9b8): a velocity change of 30 back, 6 up.
fn recoil(facing: Vec3) -> Vec3 {
    let f = facing.with_y(0.0).normalize_or_zero();
    (-f + Vec3::Y * 0.2) * 300.0 * 0.1
}

#[allow(clippy::too_many_arguments)]
fn weapons(
    mut commands: Commands,
    time: Res<Time>,
    buttons: Res<ButtonInput<MouseButton>>,
    ctl: Res<crate::input::Controls>,
    physics: NonSend<Physics>,
    (texts, mut subs): (Res<crate::gamemenu::Texts>, MessageWriter<crate::audio::Subtitle>),
    data: Res<GameData>,
    mut bot: ResMut<crate::bot::Bot>,
    mut q: Query<(Entity, &mut Roman, &Transform, Option<&crate::skin::Animator>)>,
    mut stats: ResMut<PlayerStats>,
    (mut sounds, mut stop): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopSound>),
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut topsy: ResMut<crate::topsy::Topsy>,
    (mut queue, targets, mut fired, cheats, holes): (ResMut<crate::weapons::ThrowQueue>, Res<crate::weapons::targeting::Targets>, MessageWriter<crate::weapons::GunFired>, Res<crate::cheats::Cheats>, Query<(), With<crate::weapons::blackhole::LiveHole>>),
    (cam, joints, xhair, mounted): (Query<&GlobalTransform, With<FollowCam>>, Query<&GlobalTransform, Without<FollowCam>>, Res<crate::hud::Crosshair>, Res<crate::turret::Mounted>),
) {
    use crate::weapons::{SpecialId, THROW_NOTE, WeaponId};
    let dt = time.delta_secs();
    let infinite = cheats.on(crate::cheats::INFINITE_AMMO);
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
    let change_line = |subs: &mut MessageWriter<crate::audio::Subtitle>, sounds: &mut MessageWriter<crate::audio::PlaySound>, line: &str, name_text: Option<&str>| {
        let name = texts.get(name_text.unwrap_or_default());
        subs.write(crate::audio::Subtitle { speaker: None, text: texts.get(line).replace("%s", &name), duration: 3.0 });
        sounds.write(crate::audio::PlaySound::ui("ui_weapon_put_away"));
    };
    for (e, mut r, t, ra) in &mut q {
        if r.dead {
            continue;
        }
        // on a mounted gun Roman's weapons are put away (0x00498a80: 0x004a0e00(0)); a machine gun loop stops
        if mounted.0.is_some() {
            if r.mg_firing {
                r.mg_firing = false;
                stop.write(crate::audio::StopSound("wpn_rome_machgun_fire_lp".into()));
            }
            continue;
        }
        r.since_fire += dt;
        r.since_throw += dt;
        r.reloading -= dt;
        // +0xc00: set to 0.5 by each shot, counted down by dt (0x004788a0, game_06.cpp:2800)
        r.fire_delay = (r.fire_delay - dt).max(0.0);
        // a pending switch (0x004a1680) completes at frame 13 of the weapon-change anim (0x00475ff0 state 4 ->
        // 0x004a16b0) or when its timer (+0x2c, 0.5 s from 0x004a0bb0) runs out (0x004a1690), whichever first: the
        // anim, 0.4 s. Completing destroys the old weapon object (0x004a0ff0) with its reload timer and equips the new
        // one (0x004a10e0); the chat line "ui_change_weapon" with its name and ui_weapon_put_away (2D)
        if let Some(c) = r.wchange {
            let c = c + dt;
            if c >= SWITCH_AT && c - dt < SWITCH_AT {
                if let Some(slot) = r.switch_to.take() {
                    r.arsenal.equip_slot(slot);
                    r.reload_clock = None;
                    r.reloading = 0.0;
                    let name = r.arsenal.in_hand_id().and_then(|w| w.table().name_text);
                    change_line(&mut subs, &mut sounds, "ui_change_weapon", name);
                }
            }
            // the anim's length (trotter_wchange_t 1.13 s; frame 13 is 0.4 s in: its first frame is 1)
            r.wchange = (c < 1.133).then_some(c);
        }
        // while a switch is pending the player update stops after it (0x004788a0: 0x004a1690 / 0x004a16b0, then done):
        // no reload, no fire, no special
        let switching = r.wchange.is_some_and(|c| c < SWITCH_AT);
        // Next Weapon (Q, input 0xe): the next usable slot as pending (0x004a0bb0, 0.5 s), unless it is the current one;
        // selecting leaves the zoom mode (0x00475910) and requests the weapon-change anim (+0xa34 |= 0x440000)
        if !switching && ctl.pressed(crate::input::Act::SwitchWeapon) {
            if let Some(next) = r.arsenal.step(1) {
                r.wchange = Some(0.0);
                r.switch_to = Some(next);
            }
        }
        let switching = r.wchange.is_some_and(|c| c < SWITCH_AT);
        let id = r.arsenal.in_hand_id();
        let mg = id == Some(WeaponId::Machinegun);
        // a tap counts as a shot: a trackpad tap over Moonlight arrives as press and release in one frame, never
        // "pressed" at the frame's end (the player's gun did nothing)
        let fire = ctl.down(crate::input::Act::Fire) || ctl.pressed(crate::input::Act::Fire) || (buttons.just_pressed(MouseButton::Left) && ctl.active_play) || ((bot.fight || bot.shoot.is_some()) && bot.fire_ok);
        let reloading = r.reload_clock.is_some();
        let fire_delay = r.fire_delay;
        // the shark gun is ready only with a live target picked (vt+0x138 0x004a2910: the target list's first entry)
        let target_ready = id != Some(WeaponId::Sharkgun) || targets.first().is_some();
        let (shoot, starting, out_of_ammo) = match r.arsenal.in_hand_mut() {
            Some(w) => {
                // the recycle gate (0x004a1e40: never fired, or time - last fire +0x544 > recycle_time)
                w.timer -= dt;
                // can reload (0x004a22a0): spare ammo and the clip below the descriptor's clip size
                let can_reload = w.can_reload();
                // Reload (R, input 0xc): with vt+0x15c the reload starts (0x004a2360) and the reload anim with its load
                // sound is requested (+0xa34 |= 0x190000); nothing more while reloading (vt+0x164 0x004a2390)
                let manual = ctl.pressed(crate::input::Act::Reload) && can_reload;
                // the empty clip (0x0047a6af): +0xc00 run out, no shot this frame and the clip empty (vt+0x158
                // 0x004a2270): reload when it can, else fall back to the previous usable weapon (0x004a0be0)
                let empty = fire_delay <= 0.0 && w.clip == 0;
                let starting = !switching && !reloading && (manual || (empty && can_reload));
                // ready to fire (vt+0x138 0x004a23b0): not reloading, the clip not empty and the recycle gate
                let shoot = !switching && !reloading && !starting && fire && w.timer <= 0.0 && w.clip > 0 && target_ready;
                (shoot, starting, !switching && !reloading && empty && !can_reload)
            }
            None => (false, false, false),
        };
        if starting {
            r.reload_clock = Some((0.0, false));
            // the upper-body reload anim (trotter_reload, 1.97 s)
            r.reloading = 1.97;
        }
        if out_of_ammo {
            if let Some(prev) = r.arsenal.step(-1) {
                r.wchange = Some(0.0);
                r.switch_to = Some(prev);
            } else if let (true, Some(snd)) = (ctl.pressed(crate::input::Act::Fire), id.and_then(dry_fire_sound)) {
                // nothing usable: the dry-fire sound of the gun in hand
                sounds.write(crate::audio::PlaySound::ui(snd));
            }
        }
        // the reload runs on the reload anim's frames (30 per second, counted from its first frame, 1: AnimEventTime
        // game_05.cpp:1328) for the load sound; the clip refills when the weapon's reload timer (+0x55c =
        // clip_reload_time, 0x004a2360) runs out (0x004a2220 -> vt+0x168) or when the anim reaches frame 43 still
        // reloading (0x00475ff0 state 3), whichever comes first. The refill (0x004a22e0) moves min(clip size - clip,
        // spare) into the clip from the spare; the rifle's (0x004a29d0) leaves the spare as it is
        if let (Some((clock, said)), Some(id)) = (r.reload_clock, id) {
            let clock = clock + dt;
            let (frame, sound) = reload_sound(id);
            let mut said = said;
            if !said && clock >= (frame - 1.0) / 30.0 {
                said = true;
                sounds.write(crate::audio::PlaySound::at(sound, t.translation));
            }
            let done = r.arsenal.in_hand().is_some_and(|w| clock >= reload_done_at(w.spec.gun.clip_reload_time));
            if done {
                if let Some(w) = r.arsenal.in_hand_mut() {
                    w.refill();
                }
                r.reload_clock = None;
            } else {
                r.reload_clock = Some((clock, said));
            }
        }
        // the machine gun's fire sound, looping while the trigger is held: the bank's sequence plays its start
        // before the loop and its end on the stop (wpn_rome_machgun_fire_lp intro / outro, audio.rs)
        if mg && shoot && !r.mg_firing {
            r.mg_firing = true;
            sounds.write(crate::audio::PlaySound::at("wpn_rome_machgun_fire_lp", t.translation).looping(true));
        }
        if r.mg_firing && (!fire || !mg || switching || r.arsenal.in_hand().is_some_and(|w| w.clip == 0)) {
            r.mg_firing = false;
            stop.write(crate::audio::StopSound("wpn_rome_machgun_fire_lp".into()));
        }
        if let (true, Some(id)) = (shoot, id) {
            r.since_fire = 0.0;
            // a shot ends the reload anim (the fire request 0x8000 takes the state, 0x0047a555)
            r.reloading = 0.0;
            r.fire_delay = 0.5;
            let spec = {
                let w = r.arsenal.in_hand_mut().expect("a gun in hand");
                // vt+0x144 (0x004a2450): the last fire time (0x004a1f10) and one round less in the clip unless the
                // infinite ammo cheat (Game+0x175)
                if !infinite {
                    w.clip -= 1;
                }
                w.timer = w.spec.gun.recycle_time;
                w.spec.clone()
            };
            // Game+0x2718: the shots fired
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
            // with auto-aim (0x0047dbf0's use_target = vt+0x16c 0x004a2a40) the target's point replaces the
            // crosshair's (hud +0: the target pass, weapons/targeting.rs)
            let aim_at = if matches!(id, WeaponId::Machinegun | WeaponId::Rifle) { targets.first().map(|t| t.point).or(aim_point) } else { aim_point };
            let dir = match aim_at {
                Some(p) if (p - from).length() > 1.0 => (p - from).normalize_or(dir),
                _ => dir,
            };
            match id {
                WeaponId::Machinegun | WeaponId::Rifle => {
                    // the machine gun's scatter (0x0047f4e0): the target point moves by a random offset in [-s, s]
                    // per axis, s = min(Game+0xd68 x machinegun_spread_pct, machinegun_spread_pct) x distance x 0.01:
                    // none on the first shot, the full spread % after 1 s of holding Fire
                    let dir = if spec.spread > 0.0 {
                        let dist = aim_at.map_or(1000.0, |p| (p - from).length());
                        let s = mg_scatter(spec.spread, r.mg_held, dist);
                        let target = from + dir * dist + Vec3::new(rng.range(-s, s), rng.range(-s, s), rng.range(-s, s));
                        (target - from).normalize_or(dir)
                    } else {
                        dir
                    };
                    // the fire (vt+0x140 0x004a23e0): the projectile
                    spawn_projectile(&mut commands, &mut fx, &data.props(&spec.projectile), from, dir, true, Some(e));
                }
                // the guns whose projectiles their own modules make (weapons/)
                _ => {
                    fired.write(crate::weapons::GunFired { id, owner: e, muzzle: from, aim: aim_at.unwrap_or(from + dir * spec.range) });
                }
            }
            // the muzzle effect (vt+0x154 0x004a1f20: one-shot on muzzle_bone_name; the rapid-fire machine gun's is
            // started once, runs while firing and is released by the weapon update 0x004a1dd0 on the frame after the
            // last shot: its loop makes the flashes and the casings)
            if !spec.gun.muzzle_fx_name.is_empty() {
                let m = spec.gun.muzzle_fx_name.clone();
                let rotation = Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation;
                if !spec.gun.rapid_fire {
                    fx.write(crate::fx::StartFx { name: m, at: from, rotation, follow: None, position_only: false });
                } else {
                    r.muzzle_idle = 0;
                    if r.muzzle_fx.is_none() {
                        // the anchor rides with Roman (the hand's position at the shot, kept relative to him)
                        let local = Transform::from_translation(t.rotation.inverse() * (from - t.translation)).with_rotation(t.rotation.inverse() * rotation);
                        let anchor = commands.spawn((local, Visibility::default())).id();
                        commands.entity(e).add_child(anchor);
                        fx.write(crate::fx::StartFx { name: m, at: from, rotation, follow: Some(anchor), position_only: false });
                        r.muzzle_fx = Some(anchor);
                    }
                }
            }
            // (the rifle's fire sound comes from its muzzle fx, fx_muzzle_flash_hawkings op 0x39: the original's sound
            // log has one wpn_rome_rifle_fire per shot)
            if id.launcher() {
                let facing = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
                r.vel += recoil(facing);
                r.slow = 1.0;
            }
        }
        // Game+0xd68: + dt while Fire is held with weapon 0 (the machine gun) in hand, after this frame's shot; 0 when
        // Fire is up (0x004788a0 at 0x0047a555)
        if !switching {
            r.mg_held = if !fire { 0.0 } else if mg { r.mg_held + dt } else { r.mg_held };
        }
        // the held muzzle fx goes the frame after the firing stops (Fire up, a switch, a reload)
        if let Some(anchor) = r.muzzle_fx {
            let firing = fire && mg && !switching && r.reload_clock.is_none();
            r.muzzle_idle = if firing { 0 } else { r.muzzle_idle.saturating_add(1) };
            if r.muzzle_idle >= 2 {
                commands.entity(anchor).despawn();
                r.muzzle_fx = None;
            }
        }
        // nothing more while switching or reloading (game_06.cpp:2874 / 2884: vt+0x164 -> done)
        if switching {
            continue;
        }
        let bot_throw = std::mem::take(&mut bot.throw);
        // Next Special (Tab, input 0x10): the next usable special as pending with time 0 (0x004a0bb0), completed
        // on the next frame (0x004a17d0: the chat line "ui_change_special_weapon" and ui_weapon_put_away)
        if ctl.pressed(crate::input::Act::NextSpecial) {
            if let Some(s) = r.arsenal.next_special(1) {
                r.arsenal.special = s;
                change_line(&mut subs, &mut sounds, "ui_change_special_weapon", s.table().name_text);
            }
        }
        if r.reload_clock.is_some() {
            continue;
        }
        // the special in hand used up (its spare 0x004a1380 = 0): the previous usable special (0x004a0be0), with its
        // line (the original's capture topsyO); none left: it stays
        if r.arsenal.count(r.arsenal.special) == 0 && !topsy.active() && !queue.busy() {
            if let Some(s) = r.arsenal.next_special(-1) {
                r.arsenal.special = s;
                change_line(&mut subs, &mut sounds, "ui_change_special_weapon", s.table().name_text);
            }
        }
        // Fire Special (Mouse 2, input 9) with a usable special (0x004a1a50), by its id (jump table 0x0047a910); each
        // use counts a shot (Game+0x2718):
        // - the Topsy Turvy bomb (8): not in a jump (+0xa34 & 0xc0) and +0x580 clear (the contact callback's object,
        //   0x00477c90: meaning not settled, not ported), flag 0x800000 (topsy.rs)
        // - the thrown ones, Guy Fawkes (6), black hole (9), sticky bomb (10): 0x004a1aa0 -> 0x004a1930 queues it with
        //   a 0.4 s cooldown (the queue must be free, 0x004a1900); the throw anim is requested (+0xa34 |= 0x220000
        //   standing in state 0 / 6). It leaves the hand on the throw anim's "fire" note (weapons/mod.rs ThrowQueue).
        //   Guy Fawkes waits 10 s between uses (Game+0xae0); the black hole only while none is live (Game+0xadc)
        // - the knockout bomb (7, the punching glove): not ported (feature: bunch-of-fives)
        let special = r.arsenal.special;
        let wants = ctl.pressed(crate::input::Act::Special) || (bot_throw && special == SpecialId::StickyBomb);
        if wants && r.arsenal.count(special) > 0 {
            let throw = |r: &mut Roman, queue: &mut crate::weapons::ThrowQueue, stats: &mut PlayerStats| {
                r.since_throw = 0.0;
                queue.pending = Some((special, THROW_NOTE));
                stats.shots += 1;
            };
            match special {
                SpecialId::TopsyTurvy => {
                    if !topsy.active() && !r.jumping && r.jump_wind.is_none() {
                        if !infinite {
                            r.arsenal.spend(special);
                        }
                        topsy.start = true;
                        stats.shots += 1;
                    }
                }
                SpecialId::StickyBomb if !queue.busy() => throw(&mut r, &mut queue, &mut stats),
                SpecialId::GuyFawkes if !queue.busy() && queue.fawkes_wait <= 0.0 => {
                    queue.fawkes_wait = 10.0;
                    throw(&mut r, &mut queue, &mut stats);
                }
                SpecialId::BlackHole if !queue.busy() && holes.is_empty() => throw(&mut r, &mut queue, &mut stats),
                SpecialId::BunchOfFives => {
                    info!("EVENT player: the knockout bomb is not ported");
                }
                _ => {}
            }
        }
    }
}

/// The weapon-change anim's frame 13 (counted from 1 at 30 per second): the switch completes (0x00475ff0 state 4)
const SWITCH_AT: f32 = 12.0 / 30.0;

/// When a reload of `clip_reload_time` refills the clip: the weapon's timer (0x004a2220) or the reload anim's frame
/// 43 (0x00475ff0 state 3, 0x0059eedc), whichever comes first.
pub fn reload_done_at(clip_reload_time: f32) -> f32 {
    clip_reload_time.min(42.0 / 30.0)
}

/// The machine gun's scatter half-width at the target (0x0047f4e0): min(held x pct, pct) x distance x 0.01.
pub fn mg_scatter(spread_pct: f32, held: f32, distance: f32) -> f32 {
    (held * spread_pct).min(spread_pct) * distance * 0.01
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

/// The landing damage numbers (landing_damage_min / max_hurt, min / max_speed; the turbo set after a jumper pack's
/// flight, flag 2: 0x00478100 game_05.cpp:2177).
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct LandingDamage {
    pub min_hurt: f32,
    pub max_hurt: f32,
    pub min_speed: f32,
    pub max_speed: f32,
}

/// Landing damage (0x00478100 without the flag): t = (-vz - landing_damage_min_speed) / landing_damage_max_speed
/// (p_trotter 150 / 200; it divides by max_speed, not max - min: an original quirk kept), none when t <= 0, else
/// lerp(landing_damage_min_hurt 150, max_hurt 300, min(t, 1)).
pub fn landing_damage(peak_vz: f32, d: &LandingDamage) -> Option<f32> {
    let k = (-peak_vz - d.min_speed) / d.max_speed;
    if !(k > 0.0) {
        return None;
    }
    let k = k.min(1.0);
    Some((1.0 - k) * d.min_hurt + k * d.max_hurt)
}

/// The playtest state log's player fields (weapons, reload, switch, jump, specials, live sticky bombs).
fn player_state(state: Option<ResMut<crate::playtest::PlayState>>, q: Query<(&Roman, &Transform)>, bombs: Query<&crate::sticky::StickyBomb>, banter: Res<Banter>, held: Query<(&crate::level::HandWeapon, &Visibility)>) {
    let (Some(mut s), Ok((r, t))) = (state, q.single()) else { return };
    let m = &mut s.0;
    let r1 = |v: f32| ((v * 100.0).round() / 100.0) as f64;
    m.insert("weapon".into(), r.arsenal.in_hand_id().map_or("", crate::weapons::WeaponId::name).into());
    m.insert("guns".into(), r.arsenal.guns().map(|g| g.id.name()).collect::<Vec<_>>().join(",").into());
    let specials: serde_json::Map<String, serde_json::Value> = crate::weapons::SpecialId::ALL.iter().map(|s| (s.name().to_owned(), r.arsenal.count(*s).into())).collect();
    m.insert("specials".into(), specials.into());
    m.insert("reloading".into(), r.reload_clock.is_some().into());
    m.insert("switching".into(), r.wchange.is_some_and(|c| c < SWITCH_AT).into());
    m.insert("jumping".into(), r.jumping.into());
    m.insert("jump_wind".into(), r.jump_wind.is_some().into());
    m.insert("on_ground".into(), r.on_ground.into());
    m.insert("feet_x".into(), r1(t.translation.x).into());
    m.insert("feet_y".into(), r1(t.translation.y).into());
    m.insert("hspeed".into(), r1(Vec2::new(r.vel.x, r.vel.z).length()).into());
    m.insert("mg_held".into(), r1(r.mg_held).into());
    m.insert("special".into(), r.arsenal.special.name().into());
    m.insert("topsy_left".into(), r.arsenal.count(crate::weapons::SpecialId::TopsyTurvy).into());
    m.insert("dead".into(), r.dead.into());
    m.insert("landing".into(), r.landing.map_or("", |l| l.0).into());
    m.insert("sticky_live".into(), bombs.iter().count().into());
    m.insert("sticky_stuck".into(), bombs.iter().filter(|b| b.is_stuck()).count().into());
    m.insert("banter_lines".into(), banter.lines.into());
    let mut hand: Vec<&str> = held.iter().filter(|(_, v)| **v != Visibility::Hidden).map(|(w, _)| w.name()).collect();
    hand.sort();
    m.insert("hand".into(), hand.join(",").into());
    m.insert("sticky_fused".into(), bombs.iter().filter(|b| b.fuse.is_some()).count().into());
}

/// The idle lines' table 0x005ce350 (55 entries, read from the exe): Roman's, Q's and Jonesy's ambient / idle lines.
const IDLE_LINES: [&str; 55] = [
    "rom50044_Ambient", "rom50047_Ambient", "rom50048_Ambient", "rom50049_Ambient", "rom50050_Ambient", "rom50171_Ambient",
    "rom50178_BaseDefendAmb", "rom50187_BaseDefendAmb", "rom50189_BaseDefendAmb", "rom50190_BaseDefendAmb", "rom50191_BaseDefendAmb",
    "q1150029_Idle", "q1150030_Idle", "q1150027_Ambient", "q1150031_Ambient", "q1150033_Ambient", "q1150034_Ambient",
    "q1150025_Ambient", "q1150035_Ambient", "q1150036_Ambient", "q1150037_Ambient", "q1150041_Ambient", "q1150048_Ambient",
    "q1150049_Ambient", "q1154016_Ambient_Spec", "q1150175_Ambient_Spec", "q1150177_Ambient_Spec", "q1154004_Ambient_Spec",
    "jon50034_Idle", "jon50035_Idle", "jon50036_Idle", "jon50040_Idle", "jon50043_Idle", "jon50027_Ambient", "jon50030_Ambient",
    "jon50031_Ambient", "jon50032_Ambient", "jon50041_Ambient", "jon50046_Ambient", "jon50050_Ambient", "jon50051_Ambient",
    "jon50052_Ambient", "jon50053_Ambient", "jon50054_Ambient", "jon50055_Ambient", "jon50164_Ambient", "jon50182_Ambient",
    "jon50183_Ambient", "jon54001_Ambient_Spec", "jon54012_Ambient_Spec", "jon54014_Ambient_Spec", "jon54017_Ambient_Spec",
    "jon54019_Ambient_Spec", "jon50162_Ambient_Spec", "jon54005_Ambient_Spec",
];

/// The idle banter's clocks (Game+0xb0c / +0xafc: when Q / Jonesy last moved, with their positions +0xb10 / +0xb00;
/// Roman's idle time +0xe5c).
#[derive(Resource, Default)]
pub struct Banter {
    /// lines played (the playtest log's banter_lines)
    pub lines: u32,
    t: f32,
    q: Option<(Vec3, f32)>,
    jon: Option<(Vec3, f32)>,
    idle: f32,
}

/// The idle banter (0x0047b730, every player update): when Q or Jonesy has not moved for more than 30 s (0x005bccbc),
/// or Roman has been idle for more than 30 s (+0xe5c: dt added in the stance and fidget modes 0..5 of 0x00476a10,
/// cleared by any other mode and by firing, reloading, switching), one line of table 0x005ce350 plays (0x00493b70 cue
/// 0x37, filter 0x0047b520) and the clocks restart. A missing companion keeps its clock at now. Guesses: the filter
/// 0x0047b520 is not recovered: a line plays only when its speaker is there (as the dismay banter, ai.rs) and the
/// base-defence lines (_BaseDefendAmb) are left out; 0x00493b70's shuffle bag is a uniform pick.
#[allow(clippy::type_complexity)]
fn idle_banter(
    time: Res<Time>,
    mut st: ResMut<Banter>,
    roman: Query<(&Roman, &Transform)>,
    companions: Query<(&crate::ai::Companion, &Transform, Option<&Health>)>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    let st = &mut *st;
    st.t += dt;
    let now = st.t;
    let Ok((r, rt)) = roman.single() else { return };
    if r.dead {
        return;
    }
    let alive = |q: bool| companions.iter().find(|(c, _, h)| c.q == q && h.is_none_or(|h| h.0 > 0.0)).map(|(_, t, _)| t.translation);
    for (q, slot) in [(true, &mut st.q), (false, &mut st.jon)] {
        *slot = match (alive(q), *slot) {
            (Some(p), Some((last, at))) if p.distance(last) < 1e-3 => Some((p, at)),
            (Some(p), _) => Some((p, now)),
            (None, _) => None,
        };
    }
    let idle_mode = r.on_ground && r.move_local.length() < 3.0 && r.landing.is_none() && r.hit_anim <= 0.0 && r.jump_wind.is_none();
    let busy = r.since_fire < dt * 1.5 || r.reload_clock.is_some() || r.wchange.is_some() || r.since_throw < dt * 1.5;
    st.idle = if idle_mode && !busy { st.idle + dt } else { 0.0 };
    let stale = |s: Option<(Vec3, f32)>| s.is_some_and(|(_, at)| now - at > 30.0);
    if !(stale(st.q) || stale(st.jon) || st.idle > 30.0) {
        return;
    }
    let (qp, jp) = (alive(true), alive(false));
    let pool: Vec<(&str, Vec3)> = IDLE_LINES
        .iter()
        .filter(|n| !n.ends_with("_BaseDefendAmb"))
        .filter_map(|n| match &n[..3] {
            "rom" => Some((*n, rt.translation)),
            "jon" => jp.map(|p| (*n, p)),
            _ => qp.map(|p| (*n, p)),
        })
        .collect();
    if let Some(&(name, at)) = pool.get(((rng.rand() * pool.len() as f32) as usize).min(pool.len().saturating_sub(1))) {
        sounds.write(crate::audio::PlaySound::at(name, at));
    }
    for s in [&mut st.q, &mut st.jon] {
        if let Some((_, at)) = s.as_mut() {
            *at = now;
        }
    }
    st.idle = 0.0;
    st.lines += 1;
}

/// trotter_jump_into's length (4 frames at 30 per second): a standing jump's take-off delay (0x0047aaf0)
const JUMP_INTO: f32 = 4.0 / 30.0;

/// The surface under the player's feet (the ground query vt+0xb8, 0x00454b20; surface.rs): nothing within 4 units
/// below gives the plain fx.
fn ground_surface(physics: &Physics, at: Vec3) -> crate::surface::Surface {
    physics.surface_below(at, 4.0).unwrap_or_default()
}

/// An effect at the player's feet (0x00478800: Fx_Create with the position and, when the ground query vt+0xb8
/// answers, flag 0x800 and the surface: the fx's material variant, surface.rs).
fn ground_fx(physics: &Physics, fx: &mut MessageWriter<crate::surface::StartSurfaceFx>, at: Vec3, name: &str) {
    let fx_at = crate::fx::StartFx { name: name.to_owned(), at, rotation: crate::level::game_to_engine(), follow: None, position_only: false };
    fx.write(crate::surface::StartSurfaceFx { fx: fx_at, surface: ground_surface(physics, at) });
}

/// The stop and slide effects of the player update (0x004788a0 after the drive, 0x004799d5..0x00479bef):
/// - grounded (the grace +0x570 > 0) and the horizontal speed dropped by more than 20 (0x005bcd44) since the last
///   frame: "FX_fs_roman_stop" (0x00478800). The original compares per frame (about 60 per second in the captures);
///   the remaster scales the threshold by dt x 60 so it does not depend on the frame rate;
/// - touching the ground (contacts +0x584 > 0), faster than 10 (0x005bcb68) and the wanted velocity below 2 (its
///   square below 4, 0x005bcbe0): "FX_roman_slide" with the surface (vt+0xb8) runs, kept in +0x568 and released
///   (Fx_Release 0x0041d030) when that ends. The remaster releases it by despawning the anchor it follows.
fn ground_effects(mut commands: Commands, time: Res<Time>, physics: NonSend<Physics>, mut q: Query<(&mut Roman, &Transform)>, mut fx: MessageWriter<crate::surface::StartSurfaceFx>) {
    let dt = time.delta_secs();
    for (mut r, t) in &mut q {
        // the speed the update measured before its drive (+0x4bc): released at full speed, the slide fx starts on that
        // frame and the stop fx on the next (the original's trace: fol_bodyslide_snow_lp and fs_boot_heavy_snow_stop
        // together as Roman stops dead)
        let h = r.hspeed;
        if r.grace > 0.0 && !r.dead && h - r.prev_hspeed < -20.0 * (dt * 60.0).max(1e-3) {
            ground_fx(&physics, &mut fx, t.translation, "fx_fs_roman_stop");
        }
        r.prev_hspeed = h;
        let slide = r.on_ground && !r.dead && h > 10.0 && r.wish_h * r.wish_h < 4.0;
        match (slide, r.slide_fx) {
            (true, None) => {
                let anchor = commands.spawn(Transform::from_translation(t.translation)).id();
                let slide = crate::fx::StartFx { name: "fx_roman_slide".into(), at: t.translation, rotation: crate::level::game_to_engine(), follow: Some(anchor), position_only: true };
                fx.write(crate::surface::StartSurfaceFx { fx: slide, surface: ground_surface(&physics, t.translation) });
                r.slide_fx = Some(anchor);
            }
            (true, Some(a)) => {
                commands.entity(a).insert(Transform::from_translation(t.translation));
            }
            (false, Some(a)) => {
                commands.entity(a).despawn();
                r.slide_fx = None;
            }
            (false, None) => {}
        }
    }
}

/// Falling and landing (0x00478200 / 0x00478100; player.md "Landing and falling").
#[allow(clippy::too_many_arguments)]
/// `no_hits`: the hit handler ignores every message during the topsy states (0x0047afa0, +0xa34 & 0x7800000), the
/// fall's and the landing's damage included (both go through it, 0x00478100)
#[allow(clippy::too_many_arguments)]
/// True on the frame he lands.
fn fall_and_land(r: &mut Roman, t: &Transform, h: &mut Health, vy: f32, jumped: bool, dt: f32, physics: &Physics, fx: &mut MessageWriter<crate::surface::StartSurfaceFx>, moved: f32, no_hits: bool, mut jet: Option<&mut crate::jetpack::JetPack>) -> bool {
    let mut landed = false;
    let y = t.translation.y;
    r.grace = if r.on_ground { 0.5 } else { (r.grace - dt).max(0.0) };
    if r.grace > 0.0 {
        r.ground_ref = y;
    }
    // a jumper pack: its fall rules (0x00478200: fall_speed_turbo, turbo_death_falling_distance / time)
    let pack = jet.as_deref().filter(|j| j.pack).map(|j| (j.tuning.fall_speed, j.tuning.death_distance, j.tuning.death_time));
    let gliding = jet.as_deref().is_some_and(|j| j.flight == crate::jetpack::Flight::Glide);
    if !r.on_ground {
        r.peak_vz = r.peak_vz.min(vy);
        // falling: slower than fall_speed (-50; after a jump fall_speed_jump -70; with a pack fall_speed_turbo);
        // gliding never falls
        let threshold = match pack {
            Some((turbo, _, _)) => turbo,
            None if jumped => -70.0,
            None => r.fall_speed,
        };
        if vy <= 0.0 && vy < threshold && !gliding {
            r.falling = true;
            r.fall_fast_t += dt;
            // with the pack and nothing under him the wings may open (flag 0x10)
            if let Some(j) = jet.as_deref_mut() {
                j.fell();
            }
        }
    } else if r.grace > 0.0 && r.landing.is_none() {
        r.fall_fast_t = 0.0;
    }
    // death by falling: below z -1000, more than death_falling_distance (110) under the ground reference, or falling
    // fast for longer than death_falling_time (1 s): the whole health as damage (0x00478100 flag 1)
    let (drop, time) = pack.map_or(r.death_fall, |(_, d, t)| (d, t));
    let game_z = (crate::level::game_to_engine().inverse() * t.translation).z;
    if !r.dead && !no_hits && h.0 > 0.0 && (game_z < -1000.0 || y < r.ground_ref - drop || r.fall_fast_t > time) {
        info!("EVENT player: fell to his death (dropped {:.0}, {:.2} s falling)", r.ground_ref - y, r.fall_fast_t);
        h.0 = 0.0;
    }
    // landing: grounded after falling (or a jump), once: the land fx, the landing damage from the peak downward speed,
    // the landing anim (land_hard after more than fall_land_hard_time 0.6 s falling, else jump_land; after a jumper
    // pack's flight not falling, trotter_turbo_land or, faster than land_speed_turbo, trotter_turbo_skid: game_05.cpp
    // 1855-1876, lengths 0.467 / 0.633 s), the wings folded (0x00478200)
    if r.on_ground && vy <= 0.0 && (r.falling || jumped) && r.landing.is_none() && !r.dead {
        let turbo = jet.as_deref_mut().is_some_and(|j| j.landed());
        let damage = match jet.as_deref() {
            Some(j) if turbo => j.tuning.landing_damage,
            _ => r.landing_damage,
        };
        if let (Some(d), false) = (landing_damage(r.peak_vz, &damage), no_hits) {
            info!("EVENT player: landing damage {d:.0} (vz {:.0})", r.peak_vz);
            h.0 -= d;
        }
        ground_fx(physics, fx, t.translation, "fx_fs_roman_land");
        let hspeed = Vec2::new(r.vel.x, r.vel.z).length();
        r.landing = Some(match jet.as_deref() {
            Some(j) if turbo && !r.falling => {
                if hspeed > j.tuning.land_speed {
                    ("trotter_turbo_skid", 0.0, 0.633)
                } else {
                    ("trotter_turbo_land", 0.0, 0.467)
                }
            }
            _ if r.fall_fast_t > 0.6 => ("trotter_land_hard", 0.0, 2.633),
            _ => ("trotter_jump_land", 0.0, 0.633),
        });
        r.falling = false;
        r.peak_vz = 0.0;
        landed = true;
    } else if r.on_ground && vy <= 0.0 {
        // down without a landing (a short hop): the flight ends all the same
        if let Some(j) = jet.as_deref_mut() {
            j.landed();
        }
    }
    if let Some((n, el, len)) = r.landing {
        // moving ends it (game_05.cpp:1721-1726)
        let el = el + dt;
        r.landing = if el >= len || (moved > 0.01 && Vec2::new(r.vel.x, r.vel.z).length() > 3.0) { None } else { Some((n, el, len)) };
    }
    landed
}

/// Projectiles: one ray per frame along the path; damage on the first body hit (characters' Health, objects'
/// Destructible), a tracer line for the path.
#[allow(clippy::too_many_arguments)]
/// A projectile's direct hit on a character (Projectile_ApplyHitDamage 0x0047eec0): the part hit (the hit part's bone
/// "head" / "neck" -> head_shot_multiplier, "chest" / "belly" -> torso_shot_multiplier; without collision parts, the
/// skeleton bone nearest the hit point), the damage (a class's bullet share: the Q112's bullet_scale), the counts (hits,
/// head shots, kills), the damage message's source offered as a target (+0x2c). Returns whether it killed.
#[allow(clippy::too_many_arguments)]
fn strike(p: &Projectile, ce: Entity, h: &mut Health, anim: Option<&crate::skin::Animator>, grunt: bool, at: Vec3, hull_part: Option<String>, joints: &Query<&GlobalTransform>, stats: &mut PlayerStats, victims: &mut Query<&mut crate::ai::Grunt>, by_roman: bool) -> bool {
    let was_alive = h.0 > 0.0;
    let part = hull_part.or_else(|| {
        anim.and_then(|a| {
            a.skeleton
                .names
                .iter()
                .enumerate()
                // the body's main bones (helpers such as ref_*, camera targets and fingers are not parts)
                .filter(|(_, n)| BODY_PARTS.contains(&n.trim().to_lowercase().as_str()))
                .filter_map(|(i, n)| joints.get(a.joints[i]).ok().map(|j| (n.trim().to_lowercase(), j.translation().distance(at))))
                .min_by(|x, y| x.1.total_cmp(&y.1))
                .map(|(n, _)| n)
        })
    });
    let mult = match part.as_deref() {
        Some("head" | "neck") => p.head_mult,
        Some("chest" | "belly") => p.torso_mult,
        _ => 1.0,
    };
    if p.from_player && mult > 1.0 && matches!(part.as_deref(), Some("head" | "neck")) {
        stats.head_shots += 1;
    }
    h.0 -= p.damage * mult * victims.get(ce).map_or(1.0, |g| g.bullet_scale);
    if p.from_player && grunt && by_roman {
        stats.hits += 1;
    }
    if let Ok(mut g) = victims.get_mut(ce) {
        // the damage message's source (+0x2c): the shooter, offered to the grunt as a target (ai.rs think)
        if let Some(o) = p.owner {
            g.attacker = Some(o);
        }
        // a bullet's record has no 0x80 (allow_cartoon_death 0, p_player_rifle_bullet)
        g.cartoon = false;
    }
    if std::env::var("AIDBG").is_ok() {
        info!("AIDBG hit {ce:?} for {} -> {} part {:?}", p.damage * mult, h.0, part);
    }
    let killed = was_alive && h.0 <= 0.0;
    if p.from_player && killed {
        stats.kills += 1;
    }
    killed
}

fn projectiles(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut Projectile, &mut Transform, Option<&mut crate::weapons::sniper::Pierce>)>,
    mut chars: Query<(Entity, &crate::physics::Body, &mut Health, Option<&crate::skin::Animator>, Has<crate::ai::Grunt>), Without<Projectile>>,
    joints: Query<&GlobalTransform>,
    mut objects: Query<(&crate::level::Placed, &mut Destructible, Option<&crate::vehicles::boss::BulletScale>)>,
    mut roman: Query<(Entity, &mut Roman, &Transform, Option<&crate::skin::Animator>), Without<Projectile>>,
    mut roman_health: Query<(&mut Health, Option<&mut crate::fx::DamageShield>), (With<Roman>, Without<crate::physics::Body>, Without<Projectile>)>,
    mut companions: Query<(Entity, &mut crate::ai::Companion, &Transform, Option<&crate::skin::Animator>, &mut Health), (Without<Roman>, Without<crate::physics::Body>, Without<Projectile>)>,
    mut stats: ResMut<PlayerStats>,
    mut surface_fx: MessageWriter<crate::surface::StartSurfaceFx>,
    hulls: Option<Res<crate::hitparts::HitHulls>>,
    (rtime, topsy, mounted, mut noise, mut bombs, mut rng, mut sounds): (Res<Time<Real>>, Res<crate::topsy::Topsy>, Res<crate::turret::Mounted>, ResMut<crate::ai::Interests>, Query<(&Transform, &mut crate::sticky::StickyBomb), Without<Projectile>>, ResMut<crate::fx::FxStats>, MessageWriter<crate::audio::PlaySound>),
    mut victims: Query<&mut crate::ai::Grunt>,
    (mut pickups, traitors, binds, mut wstats): (MessageWriter<crate::pickup::SpawnPickup>, Query<(), With<crate::weapons::traitor::Traitor>>, Res<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>, ResMut<crate::weapons::WeaponStats>),
) {
    let dt = time.delta_secs();
    for (e, mut p, mut t, mut pierce) in &mut q {
        // an arrow falls (its physics body under the world's gravity) and passes Roman with a sound once
        if p.arrow {
            p.vel.y -= 64.348 * dt;
            if let (true, Ok((_, _, rt, _))) = (p.flyby, roman.single()) {
                // ("_1" when (rand() x 100) >> 15 < 50)
                if t.translation.distance(rt.translation) < 70.0 {
                    p.flyby = false;
                    let n = if rng.rand() < 0.5 { "wpn_arrow_norm_by_1" } else { "wpn_arrow_norm_by_2" };
                    sounds.write(crate::audio::PlaySound::at(n, t.translation));
                }
            }
            t.rotation = Transform::IDENTITY.looking_to(p.vel.normalize_or(Vec3::Z), Vec3::Y).rotation * crate::level::game_to_engine();
        }
        let step = p.vel * dt;
        let from = t.translation;
        p.life -= dt;
        // the noise of a player's shot (0x00404810; ai.rs Interests): its path and its end point
        noise.track(e, from + step, p.owner);
        let mut hit = physics.raycast_bullet(from, step.normalize_or(Vec3::Y), step.length().max(0.01));
        // the sniper's bullet pierces the grunts (SniperGunBullet's hit 0x0047f2a0, weapons/sniper.rs): the ones on this
        // step's path before whatever else stops it, each once, nearest first; the step's hit is then the first thing
        // past their capsules
        if let Some(pc) = pierce.as_deref_mut() {
            let dir = step.normalize_or(Vec3::Y);
            let len = step.length().max(0.01);
            let grunt_bodies: std::collections::HashSet<BodyId> = chars.iter().filter(|c| c.4).map(|c| c.1.id).collect();
            hit = crate::weapons::sniper::ray_past(&physics, from, dir, len, |b| grunt_bodies.contains(&b));
            let reach = hit.map_or(len, |(_, at, _)| at.distance(from));
            let to = from + dir * reach;
            let mut struck: Vec<(f32, Entity, Option<String>)> = Vec::new();
            for (ce, _, _, a, grunt) in chars.iter() {
                if !grunt || Some(ce) == p.owner || pc.hit.contains(&ce) {
                    continue;
                }
                let Some(a) = a else { continue };
                let found = match hulls.as_ref().filter(|h| !h.grunt.is_empty()) {
                    Some(h) => crate::hitparts::hit(&h.grunt, a, &joints, &binds, from, to).map(|(f, part)| (f, Some(part))),
                    None => ray_hits_parts(a, &joints, from, to - from).then_some((0.5, None)),
                };
                if let Some((f, part)) = found {
                    struck.push((f, ce, part));
                }
            }
            struck.sort_by(|a, b| a.0.total_cmp(&b.0));
            for (f, ce, part) in struck {
                if let Ok((_, _, mut h, anim, _)) = chars.get_mut(ce) {
                    let at = from + (to - from) * f;
                    if let Some(fx) = p.fx_end.clone() {
                        surface_fx.write(crate::surface::StartSurfaceFx { fx: crate::fx::StartFx { name: fx, at, rotation: Quat::IDENTITY, follow: None, position_only: false }, surface: crate::surface::Surface::of_property("grunt") });
                    }
                    strike(&p, ce, &mut h, anim, true, at, part, &joints, &mut stats, &mut victims, p.owner.is_some_and(|o| roman.contains(o)));
                    pc.hit.push(ce);
                    pc.count += 1;
                }
            }
        }
        // sticky bombs are in the bullets' list (+0xe90, 0x00481320): one on the path before the first other hit takes
        // the damage and goes off at once (0x00481b90: a direct hit's record has no area flag); the bullet ends there
        let reach = hit.map_or(step.length(), |(_, at, _)| at.distance(from));
        let bomb = bombs
            .iter_mut()
            // (the shooter's chain is skipped, +0x574: a bomb stuck on the shooter is attached to him)
            .filter(|(bt, b)| {
                if p.owner.is_some() && b.stuck_to() == p.owner {
                    return false;
                }
                let u = ((bt.translation - from).dot(step) / step.length_squared().max(1e-6)).clamp(0.0, 1.0);
                (from + step * u).distance(bt.translation) < crate::sticky::BOMB_RADIUS && (bt.translation - from).length() <= reach + crate::sticky::BOMB_RADIUS
            })
            .min_by(|a, b| a.0.translation.distance(from).total_cmp(&b.0.translation.distance(from)));
        if let Some((_, mut b)) = bomb {
            b.fuse = Some(0.0);
            commands.entity(e).despawn();
            continue;
        }
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
        // impact fx: fx_end_material with the variant of what it hit (surface.rs): a body's surface at the hit point,
        // a character's material ("grunt")
        let impact = |body: Option<(BodyId, Vec3)>, at: Vec3, rotation: Quat| -> Option<crate::surface::StartSurfaceFx> {
            let fx = crate::fx::StartFx { name: p.fx_end.clone()?, at, rotation, follow: None, position_only: false };
            let surface = body.map_or_else(|| crate::surface::Surface::of_property("grunt"), |(b, at)| physics.surface(b, at));
            Some(crate::surface::StartSurfaceFx { fx, surface })
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
            if let Some(f) = impact(None, from + step, Quat::IDENTITY) {
                surface_fx.write(f);
            }
            // the companions are teammates of the player (vt+0xe4 true): a grunt's shot hurts them, the player's and the
            // other companion's stop on them and do nothing (0x00430340: source and victim on the same team)
            if let Some((_, mut c, _, _, mut ch)) = companions.iter_mut().find(|(_, c, _, _, _)| c.q == q) {
                if p.from_player {
                    c.friendly = true;
                } else {
                    ch.0 -= p.damage * c.damage_scale(p.kind);
                    c.attacker = p.owner;
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        // a hit on Roman: PlayerObject vt+0xa8 0x00477c20 (the fxo_rocket_turret mark, bit 31 of +0xa34: no rocket
        // turret on lev_1_m1) passes it to the base hit, which reaches the hit handler 0x0047afa0 below
        if hit_roman {
            if let Some(f) = impact(None, from + step, Quat::IDENTITY) {
                surface_fx.write(f);
            }
            // ignored during the topsy states (0x0047afa0: +0xa34 & 0x7800000); the bullet still lands
            if let Some((mut h, shield)) = roman_health.single_mut().ok().filter(|_| !topsy.active()) {
                // a shield in front of him takes the whole hit first (the base turret's window, fx.rs DamageShield);
                // else halved while on a mounted gun (0x0047afa0: Game+0xac8 set -> damage * 0.5, 0x005bcb4c)
                // (the health set 0x0047af50 is the Health component)
                if !shield.is_some_and(|mut s| s.absorb(p.damage)) {
                    h.0 -= if mounted.0.is_some() { p.damage * 0.5 } else { p.damage };
                }
                if std::env::var("AIDBG").is_ok() {
                    // who hurt Roman: the shooter's kind and weapon, the distance it flew, his health after
                    let (kind, weapon) = p.owner.and_then(|o| victims.get(o).ok()).map_or(("?".to_string(), "?".to_string()), |g| (format!("{:?}", g.kind), g.kind.weapon().to_string()));
                    let d = p.owner.and_then(|o| joints.get(o).ok()).map_or(-1.0, |gt| gt.translation().distance(from));
                    info!("AIDBG romandmg bullet {:.0} by {kind} {weapon} {:?} d {:.0} health {:.0} t {:.2}", p.damage, p.owner, d, h.0, time.elapsed_secs());
                }
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
            // the hit handler (vt+0x138 0x0047eae0): moved to the hit point, area damage when it has any (none for the
            // bullets), the direct damage (0x0047eec0), the end effect (0x0047ed40: fx_end_material with the surface;
            // the bullets have no fx_end; its AI noise 0x00404810: ai.rs Interests, above) and removed
            // the fx's game +Z (its up) along the surface normal
            let rot = Quat::from_rotation_arc(Vec3::Y, normal.normalize_or(Vec3::Y)) * crate::level::game_to_engine();
            if let Some(f) = impact(Some((body, at)), at, rot) {
                surface_fx.write(f);
            }
            if let Some((ce, _, mut h, anim, grunt)) = chars.iter_mut().find(|(ce, b, _, _, _)| b.id == body && Some(*ce) != p.owner) {
                // same team (0x00430340: the source's and the victim's vt+0xe4 agree): a grunt's shot does no damage
                // to a grunt (the engine's earlier grunts killed each other with stray arrows) of his side (a Guy
                // Fawkes traitor is on the other: weapons/traitor.rs)
                if grunt && !p.from_player && traitors.contains(ce) == p.owner.is_some_and(|o| traitors.contains(o)) {
                    commands.entity(e).despawn();
                    continue;
                }
                strike(&p, ce, &mut h, anim, grunt, at, hull_part.clone(), &joints, &mut stats, &mut victims, p.owner.is_some_and(|o| roman.contains(o)));
            } else if let Some((_, mut d, scale)) = objects.iter_mut().find(|(pl, _, _)| pl.collider == Some(body)) {
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
                // Object_TakeDamage 0x004555e0 (fx.rs); a bullet's record (0x400) on a colossus is scaled by its
                // bullet_scale (Q112Boss_Slot52 0x00436600, vehicles/boss.rs)
                let amount = if p.no_buildings { p.damage * scale.map_or(1.0, |s| s.0) } else { p.damage };
                if d.take_damage(amount) && d.dying.is_none() {
                    d.chain_delay = 0.0;
                    d.dying = Some(0.0);
                }
            }
            // a piercing bullet stopped: its reward for the characters it went through (weapons/sniper.rs)
            if let (Some(pc), Ok((_, r, rt, _))) = (pierce.as_deref(), roman.single()) {
                wstats.pierced = wstats.pierced.max(pc.count);
                if let Some(name) = crate::weapons::sniper::reward(pc.count) {
                    wstats.health_drops += 1;
                    let facing = Quat::from_rotation_y(r.yaw) * Vec3::NEG_Z;
                    pickups.write(crate::pickup::SpawnPickup { name: name.into(), at: rt.translation + facing * 15.0 + Vec3::Y * 4.0, vel: Vec3::ZERO, hover: false });
                    info!("EVENT sniper: the bullet went through {} characters: {name}", pc.count);
                }
            }
            commands.entity(e).despawn();
            continue;
        }
        t.translation = from + step;
        // the lifetime (0x0047e4a0) runs out: removed (Projectile_Remove 0x0047ebd0: the despawn; its hit list
        // 0x0047ecb0 is not needed, a bullet ends at its first hit)
        if p.life <= 0.0 {
            commands.entity(e).despawn();
        }
    }
}

pub fn spawn_bomb(commands: &mut Commands, physics: &mut Physics, bombs: &BombAssets, data: &GameData, at: Vec3, velocity: Vec3, fuse: f32) {
    let p = data.props("p_player_tickerbomb");
    let d = Destructible {
        health: 1.0,
        max_health: 1.0,
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

#[cfg(test)]
mod tests {
    use super::*;

    /// Feedback 28: the way from the clicked Yes (below the centre) to the centre is never a look, however many frames
    /// the warp takes; once the cursor shows at the centre, its moves are; a cursor that never gets there is taken after
    /// MAX_FRAMES.
    #[test]
    fn cursor_settles_at_the_centre() {
        let mut s = CursorSettle::default();
        assert!(s.look(Some(Vec2::new(3.0, 4.0))), "not settling: every offset is look input");
        s.start();
        let yes = Some(Vec2::new(0.0, 180.0));
        for f in 0..10 {
            assert!(!s.look(yes), "frame {f}: the warp has not landed");
        }
        assert!(!s.look(Some(Vec2::new(0.5, 0.0))), "the frame the cursor shows at the centre");
        assert!(s.look(Some(Vec2::new(0.0, 6.0))), "then its moves are look input");
        s.start();
        assert!(!s.look(Some(Vec2::ZERO)) && !s.look(Some(Vec2::ZERO)), "the first two frames are always skipped");
        assert!(!s.look(Some(Vec2::ZERO)));
        assert!(s.look(Some(Vec2::ZERO)));
        s.start();
        let mut n = 0;
        while !s.look(yes) {
            n += 1;
            assert!(n <= CursorSettle::MAX_FRAMES, "a cursor that never centres is taken eventually");
        }
    }

    #[test]
    fn reload_refills_at_the_timer() {
        // rifle / machine gun clip_reload_time 1.0: the weapon's timer (0x004a2220) before anim frame 43
        assert!((reload_done_at(1.0) - 1.0).abs() < 1e-6);
        // a longer reload time: the anim's frame 43 (1.4 s) completes it first
        assert!((reload_done_at(1.5) - 1.4).abs() < 1e-6);
    }

    #[test]
    fn machine_gun_scatter_ramps_over_a_second() {
        // machinegun_spread_pct 2 at 100 units: 0 on the first shot, 1 after 0.5 s held, 2 from 1 s on
        assert_eq!(mg_scatter(2.0, 0.0, 100.0), 0.0);
        assert!((mg_scatter(2.0, 0.5, 100.0) - 1.0).abs() < 1e-5);
        assert!((mg_scatter(2.0, 1.0, 100.0) - 2.0).abs() < 1e-5);
        assert!((mg_scatter(2.0, 7.0, 100.0) - 2.0).abs() < 1e-5);
    }

    #[test]
    fn landing_damage_from_the_peak_fall_speed() {
        let d = LandingDamage { min_hurt: 150.0, max_hurt: 300.0, min_speed: 150.0, max_speed: 200.0 };
        assert_eq!(landing_damage(-150.0, &d), None);
        assert_eq!(landing_damage(-100.0, &d), None);
        // t = (250 - 150) / 200 = 0.5: 225
        assert!((landing_damage(-250.0, &d).unwrap() - 225.0).abs() < 1e-3);
        // clamped at t = 1: 300 (from 350 units/s on)
        assert!((landing_damage(-350.0, &d).unwrap() - 300.0).abs() < 1e-3);
        assert!((landing_damage(-900.0, &d).unwrap() - 300.0).abs() < 1e-3);
    }

    #[test]
    fn sticky_blast_fuse_range() {
        assert_eq!(crate::sticky::blast_fuse(0.0), 0.0);
        let top = crate::sticky::blast_fuse(0.999_999);
        assert!(top > 0.69 && top < 0.7, "{top}");
    }

    #[test]
    fn arrow_lands_on_its_target() {
        // p_grunt_normal_arrow speed 25 (x 3): 100 units away, T = 3 x 10 / 75 = 0.4 s
        let (from, to) = (Vec3::new(0.0, 5.0, 0.0), Vec3::new(100.0, 5.0, 0.0));
        let v = arrow_velocity(from, to, 75.0);
        let t = 0.4;
        let at = from + v * t - Vec3::Y * (0.5 * 64.348 * t * t);
        assert!(at.distance(to) < 1e-3, "{at}");
        assert!((v.x - 250.0).abs() < 1e-3);
    }

    #[test]
    fn segment_distance_basics() {
        let d = segment_distance(Vec3::ZERO, Vec3::X * 10.0, Vec3::new(5.0, 3.0, 0.0), Vec3::new(5.0, 9.0, 0.0));
        assert!((d - 3.0).abs() < 1e-5);
    }
}
