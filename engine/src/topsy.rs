//! The Topsy Turvy bomb (player_topsyturvy, from the pub): PlayerObject's topsy states (game_05.cpp 1790-1820,
//! game_06.cpp 2330) and the AI's reaction (fn_0042fe50, ai_03.cpp 2977):
//! - Fire Special with it selected (Tab picks the special): flag 0x800000, trotter_topsy_into (its notes: the
//!   fx_trotter_topsy fx, topsy_create attaches the bomb to the hand); then trotter_topsy_world_turn while the view
//!   turns over (the Camera_topsy track); the weapon fires (0x004a1aa0): every AI within topsy_turvy_radius (200) of
//!   Roman is thrown; the timer Game+0xc80 = topsy_turvy_duration (5 s) runs with Roman hanging
//!   (trotter_topsy_hanging), unable to turn; at 0 the bomb is removed and the world turns back.
//! - A thrown grunt rolls rand * 100: at or above topsy_turvy_fall_pct (50), with more than 3 s of the effect left,
//!   it hangs on (grunt_hang_into, grunt_hanging) until the world turns back; otherwise it lets go and falls into
//!   the sky (grunt_blown_up_into, grunt_blown_up, physics off) with a topsy_scream, and is dead when the world
//!   turns back.
//! Simplified, labelled: the view rolls 180 degrees about its axis over the turn instead of following the
//! Camera_topsy track; the world turns back over the same 0.97 s (the original's anim 0x23 is not identified).

use crate::levelreset::LevelAppExt;
use crate::ai::{Grunt, Mind};
use crate::fx::FxStats;
use crate::player::Roman;
use bevy::prelude::*;

#[derive(Clone, Copy, PartialEq, Debug, Default)]
pub enum Phase {
    #[default]
    Off,
    Into,
    Turn,
    Hang,
    Back,
}

/// The effect (Game+0xc80 and the player's topsy flags).
#[derive(Resource, Default)]
pub struct Topsy {
    pub phase: Phase,
    pub t: f32,
    /// topsy_turvy_duration
    pub duration: f32,
    /// the camera's roll, radians (0 upright, PI upside down)
    pub roll: f32,
    pub start: bool,
}

impl Topsy {
    pub fn active(&self) -> bool {
        self.phase != Phase::Off
    }
}

/// A grunt the bomb threw: hanging on, or falling into the sky.
#[derive(Component)]
pub struct Thrown {
    pub hangs: bool,
    pub t: f32,
    pub rise: f32,
    /// the hang starts with the hang_into anim (a grunt's 113; a monk goes straight to monk_hanging, 114)
    pub into: bool,
    /// a monk's hang ends on its own after this long (state 0xf4's +0x87c), then he teleports away
    pub until: Option<f32>,
}

/// A monk's knock-over (MonkObject damage 0x004371e0 with the topsy's flag 0x20, MonkObject_Update 0x00447500,
/// vt+0x1b0 0x00437240): he never lets go; on the ground he hangs at once (monk_hanging, anim 0x72, state 0xf4) for
/// +0x87c = topsy_turvy_duration + 1, counted down twice a frame (by state 0xf4 and by the update while +0x878 holds),
/// so half of it; then +0x878 = 0 and MonkObject_Bamf 0x00436bb0 (vt+0x240) teleports him round his target.
pub fn monk_hang(duration: f32) -> Thrown {
    Thrown { hangs: true, t: 0.0, rise: 0.0, into: false, until: Some((duration + 1.0) * 0.5) }
}

const INTO: f32 = 2.47;
const TURN: f32 = 0.97;

pub struct TopsyPlugin;

impl Plugin for TopsyPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<Topsy>()
            .init_level_resource::<TopsyCams>()
            .add_systems(Update, (run, thrown, log).chain().run_if(crate::playing))
            .add_systems(PostUpdate, camera.after(crate::camera::follow_camera).in_set(crate::camera::CameraPlace).before(bevy::transform::TransformSystems::Propagate).run_if(crate::playing));
    }
}

/// The anims' Camera_topsy tracks (trotter_topsy_into / _world_turn / _hanging; the view the original shows during the
/// effect, in Roman's model frame): loaded on first use.
#[derive(Resource, Default)]
struct TopsyCams {
    loaded: bool,
    into: Option<crate::campath::CamTrack>,
    turn: Option<crate::campath::CamTrack>,
    hang: Option<crate::campath::CamTrack>,
}

/// The view follows the anim's Camera_topsy node (Roman's frame) while the effect runs; turning back plays the world
/// turn's track backwards (guess: the original's anim 0x23 is not identified; its anim slots play reversed with flag
/// 0x40, game_05.cpp:1333).
fn camera(
    topsy: Res<Topsy>,
    mut cams: ResMut<TopsyCams>,
    lib: Res<crate::skin::AnimLibrary>,
    roman: Query<&Transform, (With<Roman>, Without<crate::player::FollowCam>)>,
    mut cam: Query<(&mut Transform, &mut Projection), With<crate::player::FollowCam>>,
) {
    if !topsy.active() {
        return;
    }
    if !cams.loaded {
        cams.loaded = true;
        let load = |n: &str| -> Option<crate::campath::CamTrack> {
            #[derive(serde::Deserialize)]
            struct F {
                cameras: std::collections::HashMap<String, crate::campath::CamTrack>,
            }
            let t = std::fs::read_to_string(lib.dir.join("anims").join(format!("{n}.json"))).ok()?;
            serde_json::from_str::<F>(&t).ok()?.cameras.remove("Camera_topsy")
        };
        cams.into = load("trotter_topsy_into");
        cams.turn = load("trotter_topsy_world_turn");
        cams.hang = load("trotter_topsy_hanging");
    }
    let (track, t) = match topsy.phase {
        Phase::Into => (cams.into.as_ref(), topsy.t),
        Phase::Turn => (cams.turn.as_ref(), topsy.t),
        Phase::Hang => (cams.hang.as_ref(), topsy.t),
        Phase::Back => (cams.turn.as_ref(), (TURN - topsy.t).max(0.0)),
        Phase::Off => (None, 0.0),
    };
    let (Some(track), Ok(rt), Ok((mut ct, mut proj))) = (track, roman.single(), cam.single_mut()) else { return };
    if let (Some((a, b, u)), Some((ra, rb, ru))) = (crate::campath::lerp_key(&track.t, t), crate::campath::lerp_key(&track.r, t)) {
        let p = Vec3::new(a[1], a[2], a[3]).lerp(Vec3::new(b[1], b[2], b[3]), u);
        let q = Quat::from_xyzw(ra[1], ra[2], ra[3], ra[4]).slerp(Quat::from_xyzw(rb[1], rb[2], rb[3], rb[4]), ru);
        let m = Mat4::from_rotation_translation(rt.rotation, rt.translation) * Mat4::from_rotation_translation(q.normalize(), p);
        let (_, rot, pos) = m.to_scale_rotation_translation();
        ct.translation = pos;
        ct.rotation = rot;
    }
    if let (Some((a, _, _)), Projection::Perspective(pp)) = (crate::campath::lerp_key(&track.fov, t), proj.as_mut()) {
        pp.fov = a[1];
    }
}

#[allow(clippy::too_many_arguments)]
fn run(
    mut commands: Commands,
    time: Res<Time>,
    mut topsy: ResMut<Topsy>,
    data: Res<crate::gamedata::GameData>,
    roman: Query<(&Roman, &GlobalTransform)>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, Option<&crate::physics::Body>), Without<Thrown>>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut rng: ResMut<FxStats>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: Option<ResMut<crate::mission::Mission>>,
    voices: crate::audio::PlayingVoices,
    (mut twiglets, mut twiglet_rng): (Query<(&mut crate::twiglet::Twiglet, &GlobalTransform, &crate::fx::Destructible)>, ResMut<crate::twiglet::TwigletRng>),
) {
    let dt = time.delta_secs();
    let g = data.props("p_globalsettings");
    let Ok((r, rt)) = roman.single() else { return };
    if topsy.start {
        topsy.start = false;
        if !topsy.active() {
            topsy.phase = Phase::Into;
            topsy.t = 0.0;
            topsy.duration = g.f("topsy_turvy_duration", 5.0);
            fx.write(crate::fx::StartFx { name: "fx_trotter_topsy".into(), at: rt.translation(), rotation: Quat::IDENTITY, follow: None, position_only: true });
            if let Some(m) = log.as_deref_mut() {
                m.log.push("topsy turvy: armed".into());
            }
        }
    }
    if !topsy.active() {
        return;
    }
    let _ = r;
    topsy.t += dt;
    match topsy.phase {
        Phase::Into if topsy.t >= INTO => {
            topsy.phase = Phase::Turn;
            topsy.t = 0.0;
        }
        Phase::Turn => {
            topsy.roll = std::f32::consts::PI * (topsy.t / TURN).min(1.0);
            if topsy.t >= TURN {
                // the weapon fires: every AI within the radius is thrown
                let radius = g.f("topsy_turvy_radius", 100.0);
                let fall_pct = g.f("topsy_turvy_fall_pct", 50.0);
                let me = rt.translation();
                let (mut hang, mut fall) = (0, 0);
                for (e, mut gr, gt, body) in &mut grunts {
                    // AreaDamage_Apply 0x0047f020 (from 0x004a1aa0, id 8, r = Game+0xc84 topsy_turvy_radius): a receiver
                    // within r of its bounding sphere's surface (grunt bounds radius about 4.66, as sticky.rs: guess)
                    if gr.mind == Mind::Dead || gt.translation().distance(me) - 4.66 > radius {
                        continue;
                    }
                    // a monk's damage slot (0x004371e0) never takes the base path: no roll, no fall
                    if gr.monk.is_some() {
                        hang += 1;
                        gr.mind = Mind::WaitTrigger;
                        commands.entity(e).insert(monk_hang(topsy.duration));
                        info!("EVENT topsy: monk {e:?} hangs");
                        continue;
                    }
                    let roll = rng.rand() * 100.0;
                    let hangs = roll >= fall_pct && topsy.duration > 3.0;
                    if !hangs {
                        if let Some(b) = body {
                            physics.remove(b.id);
                            commands.entity(e).remove::<crate::physics::Body>();
                        }
                        // the topsy scream (0x004387e0) keeps his scream handle: none while one plays
                        let set = gr.voice_set;
                        if let Some(req) = gr.speech.scream(&voices, crate::ai::bark_request("Topsy_Scream", set, gt.translation()).following(e)) {
                            sounds.write(req);
                        }
                        fall += 1;
                    } else {
                        hang += 1;
                    }
                    gr.mind = Mind::WaitTrigger;
                    commands.entity(e).insert(Thrown { hangs, t: 0.0, rise: 0.0, into: true, until: None });
                }
                // the twiglets in reach take it too (Twiglet_TakeDamage 0x0049f530: flag 0x20 stuns them; bounds radius
                // about 3: guess)
                let mut stunned = 0;
                for (mut t, gt, d) in &mut twiglets {
                    if d.health > 0.0 && gt.translation().distance(me) - 3.0 <= radius {
                        crate::twiglet::topsy_hit(&mut t, topsy.duration, &mut twiglet_rng);
                        stunned += 1;
                    }
                }
                if let Some(m) = log.as_deref_mut() {
                    m.log.push(format!("topsy turvy: {fall} fell, {hang} hung on, {stunned} twiglets stunned"));
                }
                topsy.phase = Phase::Hang;
                topsy.t = 0.0;
            }
        }
        Phase::Hang if topsy.t >= topsy.duration => {
            topsy.phase = Phase::Back;
            topsy.t = 0.0;
        }
        Phase::Back => {
            topsy.roll = std::f32::consts::PI * (1.0 - (topsy.t / TURN).min(1.0));
            if topsy.t >= TURN {
                topsy.phase = Phase::Off;
                topsy.roll = 0.0;
            }
        }
        _ => {}
    }
}

/// Thrown grunts: the falling ones rise into the sky (the world is upside down) and are dead when it turns back;
/// the ones that hung on carry on.
#[allow(clippy::too_many_arguments)]
fn thrown(
    mut commands: Commands,
    time: Res<Time>,
    topsy: Res<Topsy>,
    mut q: Query<(Entity, &mut Grunt, &mut Transform, &mut Thrown, &mut crate::fx::Health)>,
    mut stats: ResMut<crate::player::PlayerStats>,
) {
    let dt = time.delta_secs();
    for (e, mut g, mut t, mut th, mut h) in &mut q {
        th.t += dt;
        if th.hangs {
            g.pause = 1.0;
            // a monk's hang runs out: +0x878 = 0 and the bamf (vt+0x240): his pursuit with no specials left teleports
            if th.until.is_some_and(|u| th.t >= u) {
                commands.entity(e).remove::<Thrown>();
                if let Some(m) = g.monk.as_mut() {
                    m.special = 0;
                    m.bamf = true;
                }
                g.mind = Mind::Roam;
                info!("EVENT topsy: monk {e:?} lets go");
                continue;
            }
            if !topsy.active() {
                commands.entity(e).remove::<Thrown>();
                g.mind = Mind::Chase;
            }
            continue;
        }
        // falling "up": the game's gravity (64.348 units/s^2) toward the sky
        th.rise += 64.348 * dt;
        t.translation.y += th.rise * dt;
        if !topsy.active() || topsy.phase == Phase::Back {
            h.0 = 0.0;
            g.mind = Mind::Dead;
            g.dead_said = true;
            stats.kills += 1;
            // gone into the sky: removed with everything it holds (corpses.rs)
            commands.entity(e).remove::<Thrown>();
            commands.entity(e).insert((Visibility::Hidden, crate::corpses::Corpse::gone()));
        }
    }
}

/// The effect in the playtest state log: its phase and the AIs it holds (hanging on, falling into the sky; the monks
/// hanging).
fn log(topsy: Res<Topsy>, q: Query<(&Thrown, &Grunt)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let count = |f: &dyn Fn(&Thrown, &Grunt) -> bool| q.iter().filter(|(t, g)| f(t, g)).count();
    state.0.insert(
        "topsy".into(),
        serde_json::json!({
            "phase": format!("{:?}", topsy.phase),
            "hanging": count(&|t, _| t.hangs),
            "falling": count(&|t, _| !t.hangs),
            "monks_hanging": count(&|t, g| t.hangs && g.monk.is_some()),
        }),
    );
}
