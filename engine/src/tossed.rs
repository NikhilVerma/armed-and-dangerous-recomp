//! What a destroyed building throws out (BarricadeObject / BuildingObject destruction 0x0040cf40 -> 0x0040d390): a house
//! with toss_peasants (bit 0x800, 30 on lev_1_m4, 42 on lev_2_m4 and lev_3_m4_bd ...) throws one to three civilians
//! out (PeasantGenAI: peasant_man / peasant_woman at even odds, each on its prop_peasant_chair) and banks its
//! score_value_peasant_building (Kills when >= 0, Accidents below); one with toss_grunts (bit 0x20) throws out one to
//! three grunts (a grunt_regular, or a captain at even odds when the level has one) with a killing 0x8003 record (their
//! whole health, force 21666 up at 0.7..0.9) and banks its score_value. The husk the building leaves takes both bits
//! (0x0040cef0), so each stage of a multi-stage house throws again.
//!
//! The civilians' flight (0x00431020 from the player, speed 2.5..3 x move_speed 40, spread 75 degrees): from a point 40..60
//! beside the player, the ballistic velocity that lands on the civilian, applied to him (a downward one retried at
//! half the speed); physics off, the blown-up chair anim (0x49 / 0x4a), state 0x195: a body whoosh once within 50 of
//! Roman, then 8 s later he is gone (vt+0xd4). His scream: a cue of the PeasantBlownUp set (a new one once the
//! peasant_delay interval has passed since the last, else one of the "soon" set; the women's on vo_delay).
//!
//! Debt, labelled: the man's scream queues Roman's comment (0x00493b70 with the table 0x005fb6d0): not played; the fade
//! in (vt+0x100 over 1 s) is not drawn; the thrown grunts' 21666 push becomes a 40 units/s velocity for their ragdoll
//! (guess: impulse x 0.15 / mass 80, as ai.rs's death push). toss_grunts is set only on the rocket turrets, whose death
//! is 0x00497e60 (no toss): no level throws grunts.

use bevy::prelude::*;

/// A building's toss on its death (+0x460 bits 0x800 / 0x20) and the scores that go with it (+0x5b4 / +0x5b0).
#[derive(Component, Clone, Copy, Debug)]
pub struct TossOnDeath {
    pub peasants: bool,
    pub grunts: bool,
    pub peasant_score: f32,
    pub score: f32,
}

impl TossOnDeath {
    pub fn of(p: &crate::gamedata::Props) -> Option<Self> {
        let t = TossOnDeath {
            peasants: p.f("toss_peasants", 0.0) != 0.0,
            grunts: p.f("toss_grunts", 0.0) != 0.0,
            peasant_score: p.f("score_value_peasant_building", 0.0),
            score: p.f("score_value", 0.0),
        };
        (t.peasants || t.grunts).then_some(t)
    }
}

/// A grunt made to be thrown out dead (0x0040d390's 0x8003 record of his whole health), with his body's velocity.
#[derive(Component)]
pub struct Tossed(pub Vec3);

/// A thrown civilian (PeasantGenAI in state 0x195): flying, and gone after `left`.
#[derive(Component)]
pub struct ThrownPeasant {
    vel: Vec3,
    left: f32,
    whoosh: bool,
    /// the chair to hang on his "center" once his rig exists
    chair: bool,
}

/// The scream timers: the men's (Game+0xba4 last, +0xbb0 interval of peasant_delay_min..max) and the women's (Game+0xb44
/// / +0xb50, the vo_delay pair).
#[derive(Resource, Default)]
struct Screams {
    man: Option<(f32, f32)>,
    woman: Option<(f32, f32)>,
}

/// The tosses so far (the state log).
#[derive(Resource, Default)]
pub struct TossStats {
    pub peasants: u32,
    pub grunts: u32,
}

/// The world's gravity of the throw solver (0x00471a00: -64.35).
const GRAVITY: f32 = 64.348;

pub struct TossedPlugin;

impl Plugin for TossedPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.init_level_resource::<Screams>()
            .init_level_resource::<TossStats>()
            .add_systems(Update, (toss.after(crate::fx::FxSet), fly, chairs, kill_tossed, log).chain().run_if(crate::playing));
    }
}

/// The scream cues (0x0040d390's tables: men 12 new / 10 soon, women 9 / 9).
const MAN_NEW: [&str; 12] = ["pe150031", "pe150032", "pe150033", "pe150034", "pe150035", "pe150036", "pe250024", "pe250025", "pe250026", "pe250027", "pe250028", "pe250029"];
const MAN_SOON: [&str; 10] = ["pe150026", "pe150027", "pe150028", "pe150029", "pe150030", "pe250019", "pe250020", "pe250021", "pe250022", "pe250023"];
const WOMAN_NEW: [&str; 9] = ["pw150014", "pw150018", "pw150019", "pw150020", "pw150021", "pw250018", "pw250019", "pw250020", "pw250021"];
const WOMAN_SOON: [&str; 9] = ["pw150013", "pw150015", "pw150016", "pw150017", "pw250013", "pw250014", "pw250015", "pw250016", "pw250017"];

/// The ballistic velocity from `from` that lands on `to` at the horizontal speed `speed` (0x00471a00 with keep 1).
fn lob(from: Vec3, to: Vec3, speed: f32) -> Vec3 {
    let d = to - from;
    let h = d.with_y(0.0);
    let t = (h.length() / speed.max(1e-3)).max(1e-3);
    h / t + Vec3::Y * (d.y / t + 0.5 * GRAVITY * t)
}

/// 0x0040cf40 / 0x0040d390 on a building's death.
#[allow(clippy::too_many_arguments)]
fn toss(
    mut commands: Commands,
    mut died: MessageReader<crate::fx::Died>,
    q: Query<(&TossOnDeath, &GlobalTransform)>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    captains: Query<&crate::ai::Grunt>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    (mut rng, mut ms, mut stats, mut screams): (ResMut<crate::fx::FxStats>, ResMut<crate::stats::MissionStats>, ResMut<TossStats>, ResMut<Screams>),
    (mut spawn, mut sounds, time): (MessageWriter<crate::level::SpawnGrunt>, MessageWriter<crate::audio::PlaySound>, Res<Time>),
) {
    let now = time.elapsed_secs();
    let player = roman.single().ok().map(|t| t.translation());
    let gs = data.props("p_globalsettings");
    for d in died.read() {
        let Ok((t, gt)) = q.get(d.0) else { continue };
        let at = gt.translation();
        if t.peasants {
            ms.killed(t.peasant_score);
        }
        let mut throw = |peasants: bool, rng: &mut crate::fx::FxStats| {
            let n = 1 + (rng.rand() * 3.0) as u32 % 3;
            for _ in 0..n {
                // a random heading, 5 above the building's position
                let yaw = rng.rand() * std::f32::consts::TAU - std::f32::consts::PI;
                let start = at + Vec3::Y * 5.0;
                if peasants {
                    let man = rng.rand() * 100.0 < 50.0;
                    let template = if man { "peasant_man" } else { "peasant_woman" };
                    let Some(model) = data.templates.get(template).and_then(|t| t.model.clone()) else { continue };
                    let tr = Transform::from_translation(start).with_rotation(Quat::from_rotation_y(yaw) * crate::level::game_to_engine());
                    let Some(e) = kit.spawn(&mut commands, &model, tr, true) else { continue };
                    // 0x00431020 (the player, him, 2.5..3, 75, 0)
                    let speed = rng.range(2.5, 3.0) * 40.0;
                    let from = player.unwrap_or(start - Vec3::Z * 50.0);
                    let away = (start - from).with_y(0.0).normalize_or(Vec3::Z);
                    let dir = Quat::from_rotation_y(rng.range(-75.0, 75.0).to_radians()) * away;
                    let side = rng.range(40.0, 60.0) * if rng.rand() < 0.5 { 1.0 } else { -1.0 };
                    let origin = from + dir * side;
                    let mut vel = lob(origin, start, speed);
                    if vel.y < 0.0 {
                        vel = lob(origin, start, speed * 0.5);
                    }
                    commands.entity(e).insert((ThrownPeasant { vel, left: 8.0, whoosh: false, chair: true }, Name::new(template)));
                    // the scream: a new cue once its interval has passed, else a "soon" one
                    let (timer, range, new, soon): (_, _, &[&str], &[&str]) = if man {
                        (&mut screams.man, (gs.f("peasant_delay_min", 10.0), gs.f("peasant_delay_max", 10.0)), &MAN_NEW, &MAN_SOON)
                    } else {
                        (&mut screams.woman, (gs.f("vo_delay_min", 30.0), gs.f("vo_delay_max", 45.0)), &WOMAN_NEW, &WOMAN_SOON)
                    };
                    let (last, interval) = *timer.get_or_insert((if man { f32::NEG_INFINITY } else { 0.0 }, rng.range(range.0, range.1)));
                    let set = if now - last < interval {
                        soon
                    } else {
                        *timer = Some((now, rng.range(range.0, range.1)));
                        new
                    };
                    let cue = set[((rng.rand() * set.len() as f32) as usize).min(set.len() - 1)];
                    sounds.write(crate::audio::PlaySound::on(format!("{cue}_peasantblownup"), e));
                    stats.peasants += 1;
                } else {
                    let captain = rng.rand() * 100.0 < 50.0 && captains.iter().any(|g| g.kind == crate::ai::Kind::Captain);
                    let template = if captain { "captain" } else { "grunt_regular" };
                    // the killing record: its whole health, the push up at 0.7..0.9 (labelled guess for its size)
                    let up = rng.range(0.7, 0.9);
                    let push = (Quat::from_rotation_y(yaw) * Vec3::Z + Vec3::Y * up).normalize() * 21666.0 * 0.15 / 80.0;
                    spawn.write(crate::level::SpawnGrunt { at: start, template: template.into(), thrown: Some(push), ..default() });
                    stats.grunts += 1;
                }
            }
            info!("EVENT toss: a building throws out {n} {}", if peasants { "civilians" } else { "grunts" });
        };
        if t.peasants {
            throw(true, &mut rng);
        } else if t.grunts {
            throw(false, &mut rng);
            ms.killed(t.score);
        }
    }
}

/// The civilians' flight (state 0x195): ballistic, through everything, the body whoosh once near Roman, gone after 8 s.
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut ThrownPeasant, &mut Transform)>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    let dt = time.delta_secs();
    let player = roman.single().ok().map(|t| t.translation());
    for (e, mut p, mut t) in &mut q {
        p.vel.y -= GRAVITY * dt;
        t.translation += p.vel * dt;
        if !p.whoosh && player.is_some_and(|r| r.distance(t.translation) <= 50.0) {
            p.whoosh = true;
            let r = rng.rand() * 300.0;
            let name = if r < 100.0 { "fol_body_whoosh_1" } else if r < 200.0 { "fol_body_whoosh_2" } else { "fol_body_whoosh_3" };
            sounds.write(crate::audio::PlaySound::on(name, e));
        }
        p.left -= dt;
        if p.left <= 0.0 {
            commands.entity(e).despawn();
        }
    }
}

/// The blown-up anim (0x49 / 0x4a: peasant_blown_up_chair / _2) and the prop_peasant_chair at "center", once the
/// civilian's rig exists.
fn chairs(
    mut commands: Commands,
    mut q: Query<(Entity, &mut ThrownPeasant, &mut crate::skin::Animator)>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut rng: ResMut<crate::fx::FxStats>,
) {
    for (_, mut p, mut a) in &mut q {
        if !p.chair {
            continue;
        }
        let Some(&center) = a.skeleton.index.get("center").and_then(|&i| a.joints.get(i)) else { continue };
        p.chair = false;
        a.play(0, if rng.rand() < 0.5 { "peasant_blown_up_chair" } else { "peasant_blown_up_chair_2" }, true, crate::skin::Mask::All);
        if let Some(chair) = data.templates.get("prop_peasant_chair").and_then(|t| t.model.clone()).and_then(|m| kit.spawn(&mut commands, &m, Transform::IDENTITY, true)) {
            commands.entity(center).add_child(chair);
        }
    }
}

/// The thrown grunts take their record once their body exists: dead, their body moving (ai.rs die ragdolls a body
/// faster than 30).
fn kill_tossed(
    mut commands: Commands,
    mut q: Query<(Entity, &Tossed, &crate::physics::Body, &mut crate::fx::Health, &mut crate::ai::Grunt)>,
    mut physics: NonSendMut<crate::physics::Physics>,
) {
    let k = physics.meters_per_unit;
    for (e, t, body, mut h, mut g) in &mut q {
        g.last_hit = h.0.max(1.0);
        h.0 = 0.0;
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let _ = b.set_linear_velocity(crate::physics::jv(t.0 * k));
        }
        commands.entity(e).remove::<Tossed>();
    }
}

fn log(stats: Res<TossStats>, flying: Query<(), With<ThrownPeasant>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert("toss".into(), serde_json::json!({ "peasants": stats.peasants, "grunts": stats.grunts, "flying": flying.iter().count() }));
}
