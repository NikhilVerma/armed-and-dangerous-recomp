//! The body fall: the thud and dust of a character thrown through the air hitting the ground.
//!
//! The original: a character launched by a blast or a heavy hit (Character_Die 0x004319e0 with a record of flag 2,
//! GoRagdoll 0x00431310 over 10050, Character_Launch 0x00431020: anim 0x49 grunt_blown_up, state 0x195; the turbo
//! death 0x197, the launch at the player 0x19b) flies until its landing, which the deferred reaction 0x0042e1c0 turns
//! into a ragdoll death and FxBodyfall_Spawn 0x0043a8d0: the character's held sound (+0xb4c, its scream) is stopped
//! and "fx_bodyfall" starts at its position with flag 0x800, the material variant of the ground under it
//! (Fx_Create: fx_bodyfall_grass / _dirt / _snow / _stone / _wood / _mud / _sand / _ice / _shallow / _deep, the base
//! when the bank has none).
//!
//! The remaster ragdolls such a character at once (ai.rs die: a death whose body is moving faster than 30 units a
//! second, the launch speeds), so the flight is the ragdoll's: [`Launched`] marks it, and its body's first touch of
//! the ground (descending, the ground within 3 units under the pelvis) starts fx_bodyfall with the ground's variant
//! (surface.rs) and stops its scream.

use crate::physics::Physics;
use bevy::prelude::*;

/// The speed (game units a second) of a dying body that counts as launched (ai.rs die: the ragdoll threshold).
pub const LAUNCH_SPEED: f32 = 30.0;
/// The mass the launch's impact force x 0.15 is divided by into a speed (guess: the original applies it as an
/// impulse to the Havok body, Body_ApplyImpulse 0x00458440, whose mass is not traced): a sticky bomb's 15000 throws a
/// grunt off at 45 units a second, a powder keg's 30000 at 90.
pub const LAUNCH_MASS: f32 = 50.0;
/// The ground within this far under the pelvis is a landing.
const LAND_HEIGHT: f32 = 3.0;
/// A launch that never lands (off the world) is forgotten after this long.
const GIVE_UP: f32 = 8.0;

/// A dying character thrown through the air: its pelvis height last frame and the time since the launch.
#[derive(Component, Default)]
pub struct Launched {
    last_y: Option<f32>,
    age: f32,
}

/// Counts for the state log.
#[derive(Resource, Default)]
struct BodyfallStats {
    launched: u32,
    landed: u32,
    last: String,
}

pub struct BodyfallPlugin;

impl Plugin for BodyfallPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.init_level_resource::<BodyfallStats>().add_systems(Update, (count, land, log_state).chain());
    }
}

fn count(added: Query<(), Added<Launched>>, mut stats: ResMut<BodyfallStats>) {
    stats.launched += added.iter().count() as u32;
}

/// The landing (0x0042e1c0 -> FxBodyfall_Spawn 0x0043a8d0).
#[allow(clippy::too_many_arguments)]
fn land(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    mut q: Query<(Entity, &mut Launched, &GlobalTransform, Option<&crate::ragdoll::Ragdoll>, Option<&crate::skin::Animator>, Option<&crate::ai::Grunt>)>,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::surface::StartSurfaceFx>,
    mut hush: MessageWriter<crate::audio::StopVoice>,
    mut stats: ResMut<BodyfallStats>,
) {
    let dt = time.delta_secs();
    for (e, mut l, gt, rd, anim, grunt) in &mut q {
        l.age += dt;
        if l.age > GIVE_UP {
            commands.entity(e).remove::<Launched>();
            continue;
        }
        // the pelvis: the ragdoll's first part (its "center" bone), else the body's own place
        let at = match (rd, anim) {
            (Some(r), Some(a)) => r.bones.first().and_then(|&b| a.joints.get(b)).and_then(|j| joints.get(*j).ok()).map_or(gt.translation(), |j| j.translation()),
            _ => gt.translation(),
        };
        let descending = l.last_y.is_some_and(|y| at.y < y);
        l.last_y = Some(at.y);
        if l.age < 0.1 || !descending {
            continue;
        }
        // the ground under him (the landscape: the ray would meet his own ragdoll's parts first)
        let Some(ground) = physics.raycast_landscape(at, -Vec3::Y, LAND_HEIGHT) else { continue };
        let surface = physics.landscape.map(|l| physics.surface(l, ground)).unwrap_or_default();
        let name = surface.variant("fx_bodyfall", |_| true);
        info!("EVENT bodyfall: {e:?} landed ({name})");
        // his scream stops (+0xb4c: audio vt+0xd8)
        if let Some(id) = grunt.and_then(|g| g.speech.scream_voice()) {
            hush.write(crate::audio::StopVoice { id, immediate: false });
        }
        fx.write(crate::surface::StartSurfaceFx { fx: crate::fx::StartFx { name: "fx_bodyfall".into(), at: ground, rotation: Quat::IDENTITY, follow: None, position_only: false }, surface });
        stats.landed += 1;
        stats.last = name;
        commands.entity(e).remove::<Launched>();
    }
}

fn log_state(stats: Res<BodyfallStats>, play: Option<ResMut<crate::playtest::PlayState>>) {
    if let Some(mut p) = play {
        p.0.insert("bodyfall".into(), serde_json::json!({"launched": stats.launched, "landed": stats.landed, "last": stats.last}));
    }
}
