//! The civilians a destroyed house throws out (toss_peasants; game_01.cpp:1900-2060, ai_04.cpp:2408-2480): a house
//! with toss_peasants (flag 0x800 of the generator's +0x460, read by 0x0040ad80) that is destroyed (the destruction slot
//! 0x0040cf40, ai_01.cpp:1861; the building's side, its score and the soldiers' toss, is crate::tossed, which sends
//! [`ThrowVillagers`]) throws out one to three villagers (0x0040d390 with peasants), each in a chair, screaming, flung
//! through the air until they land.
//!
//! Per villager (0x0040d390, in the original's order of rand() draws):
//! - "peasant_man" (rand * 100 >> 15 < 50) or "peasant_woman", made as class "peasantgenai" (PeasantGenAI, vtable
//!   0x005aa9c0; its constructor 0x004615d0, its load PeasantGenAI_Slot6 0x00435260: move_speed +0x948 = 40) at the
//!   house's position + 5 up, turned by a random angle;
//! - a "prop_peasant_chair" attached at its "center" (0x004539f0; +0xb58);
//! - launched from Roman (Character_Launch 0x00431020, ai/launch.rs: speed rand * 2^-16 + 2.5, spread 75 degrees);
//! - every anim it is asked for is Peasant_blown_up_chair or _2 (PeasantGenAI vt+0x1a4 0x0043c600: rand * 100 >> 15
//!   < 50 or not);
//! - a scream on it (vt+0xc4): a man's from twelve "new" cues when the peasant delay has passed since the last one
//!   (Game+0xba4, the interval Game+0xbb0 rolled from peasant_delay_min / max, 10 / 10; shared with the drop-off lines)
//!   else from ten "soon" ones; a woman's from nine and nine under the voice delay (Game+0xb44 / +0xb50: vo_delay, the
//!   one the grunts' Spotted / Fight barks use, ai.rs Barks); a man's scream is followed by a line of the tossed table
//!   (0x00493b70 on table 0x005fb6d0 with the speaker test 0x0040d250, ai/dialogue.rs).
//!
//! The flight (state 0x195, GenericAI_Update ai_06.cpp:3336): the body whoosh (fol_body_whoosh_1..3, rand * 300 by
//! hundreds) once within 50 of Roman; the countdown +0x490 (8 s) ends it (vt+0xd4: gone) if it has not landed by then.
//! It falls under the world's gravity until it meets something; then (0x0042e1c0's fall
//! block -> vt+0x17c, PeasantGenAI_Slot95 0x004353b0) the chair is let go with the villager's velocity and 5..8 up,
//! and the villager goes down as a ragdoll (GoRagdoll 0x00431310, quiet) whose body lies 5 s (corpses.rs). The chair
//! is still his (+0xb58): it goes with his body (PeasantGenAI_Slot55 0x00435330: stopped and released with him). His
//! fade passed on to it (Slot64 0x00435380) is not here (feature character-fade).

use crate::ai::launch::{LaunchClock, LaunchRolls};
use crate::physics::{Body, Physics, jv};
use crate::scene::ClonedParts;
use bevy::prelude::*;

/// A destroyed house's villagers to throw out (0x0040d390 with peasants), from its position (sent by crate::tossed).
#[derive(Message, Clone, Copy, Debug)]
pub struct ThrowVillagers {
    pub house: Vec3,
}

/// A thrown villager in flight (state 0x195), his chair and the anim he flails in.
#[derive(Component, Debug)]
pub struct Tossed {
    pub vel: Vec3,
    /// the countdown +0x490: gone (vt+0xd4) when it runs out in the air
    pub left: f32,
    /// the body whoosh played (once within 50 of Roman)
    pub whoosh: bool,
    pub chair: Option<Entity>,
    pub anim: &'static str,
    pub woman: bool,
}

/// PeasantGenAI's move_speed (Slot6 0x00435260: +0x948 = 40).
pub const MOVE_SPEED: f32 = 40.0;
/// The house's point the villagers start at: its position + 5 up (0x0040d390: K 0x005bcbdc).
pub const RISE: f32 = 5.0;
/// The flight's countdown (+0x490 in state 0x195): 8 s.
pub const FLIGHT: f32 = 8.0;
/// The body whoosh's range from Roman (K 0x005bccb8, squared in the original).
pub const WHOOSH_RANGE: f32 = 50.0;

/// The man's scream cues (0x005bb180.., twelve "new" ones; read from the exe).
pub const MAN_NEW: [&str; 12] = [
    "PE150031_PeasantBlownUp", "PE150032_PeasantBlownUp", "PE150033_PeasantBlownUp", "PE150034_PeasantBlownUp", "PE150035_PeasantBlownUp", "PE150036_PeasantBlownUp",
    "PE250024_PeasantBlownUp", "PE250025_PeasantBlownUp", "PE250026_PeasantBlownUp", "PE250027_PeasantBlownUp", "PE250028_PeasantBlownUp", "PE250029_PeasantBlownUp",
];
/// The man's ten "soon" cues (0x005bb060..).
pub const MAN_SOON: [&str; 10] = [
    "PE150026_PeasantBlownUp", "PE150027_PeasantBlownUp", "PE150028_PeasantBlownUp", "PE150029_PeasantBlownUp", "PE150030_PeasantBlownUp",
    "PE250019_PeasantBlownUp", "PE250020_PeasantBlownUp", "PE250021_PeasantBlownUp", "PE250022_PeasantBlownUp", "PE250023_PeasantBlownUp",
];
/// The woman's nine "new" cues (0x005baf70..).
pub const WOMAN_NEW: [&str; 9] = [
    "PW150014_PeasantBlownUp", "PW150018_PeasantBlownUp", "PW150019_PeasantBlownUp", "PW150020_PeasantBlownUp", "PW150021_PeasantBlownUp",
    "PW250018_PeasantBlownUp", "PW250019_PeasantBlownUp", "PW250020_PeasantBlownUp", "PW250021_PeasantBlownUp",
];
/// The woman's nine "soon" cues (0x005bae98..).
pub const WOMAN_SOON: [&str; 9] = [
    "PW150013_PeasantBlownUp", "PW150015_PeasantBlownUp", "PW150016_PeasantBlownUp", "PW150017_PeasantBlownUp", "PW250013_PeasantBlownUp",
    "PW250014_PeasantBlownUp", "PW250015_PeasantBlownUp", "PW250016_PeasantBlownUp", "PW250017_PeasantBlownUp",
];

/// A cue of a table by a roll in [0, 1) (0x0040d390's PickCue: rand * 100n >> 15, a hundred per entry, the last one
/// taking the rest).
pub fn pick_cue(table: &[&'static str], roll: f32) -> &'static str {
    let v = (roll * 100.0 * table.len() as f32) as usize;
    table[(v / 100).min(table.len() - 1)]
}

/// How many villagers a house throws out: (rand * 3 >> 15) + 1.
pub fn count_of(roll: f32) -> usize {
    ((roll * 3.0) as usize).min(2) + 1
}

/// The anim every request of a PeasantGenAI gets (0x0043c600).
pub fn chair_anim(roll: f32) -> &'static str {
    if roll < 0.5 { "Peasant_blown_up_chair" } else { "Peasant_blown_up_chair_2" }
}

/// The peasant delay (Game+0xba4 shared with the drop-off lines, PeasantTally; the interval Game+0xbb0).
#[derive(Resource, Default, Debug)]
pub struct TossVoice {
    interval: f32,
}

/// The counts for the state log's "tossed".
#[derive(Resource, Default, Debug)]
pub struct TossStats {
    pub houses: u32,
    pub thrown: u32,
    pub men: u32,
    pub landed: u32,
    pub screams: u32,
    pub lines: u32,
}

pub struct TossPlugin;

impl Plugin for TossPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.add_message::<ThrowVillagers>()
            .init_level_resource::<TossVoice>()
            .init_level_resource::<TossStats>()
            .add_systems(Update, (toss.after(crate::tossed::BuildingToss), fly, chairs_go, log).chain().run_if(crate::playing));
    }
}

/// A destroyed house's villagers (0x0040cf40 -> 0x0040d390 (house, husk, 1)); its score is banked by crate::tossed.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn toss(
    mut commands: Commands,
    mut throws: MessageReader<ThrowVillagers>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    (data, mut lib, mut anim_lib, mut binds, assets, mut meshes, mut materials): (
        Res<crate::gamedata::GameData>,
        ResMut<crate::scene::Library>,
        ResMut<crate::skin::AnimLibrary>,
        ResMut<Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>>,
        Res<AssetServer>,
        ResMut<Assets<Mesh>>,
        ResMut<Assets<StandardMaterial>>,
    ),
    (mut rng, mut clock, time): (ResMut<crate::fx::FxStats>, ResMut<LaunchClock>, Res<Time>),
    (mut tally, mut voice, mut barks, mut bags, mut stats): (ResMut<super::PeasantTally>, ResMut<TossVoice>, ResMut<crate::ai::Barks>, ResMut<crate::ai::dialogue::Bags>, ResMut<TossStats>),
    (mut companions, voices, mut sounds): (Query<(&mut crate::ai::Companion, &Transform, Option<&crate::fx::Health>)>, crate::audio::PlayingVoices, MessageWriter<crate::audio::PlaySound>),
) {
    let roman_at = roman.single().map(|t| t.translation()).ok();
    let g = data.props("p_globalsettings");
    let delay = (g.f("peasant_delay_min", 10.0), g.f("peasant_delay_max", 10.0));
    for throw in throws.read() {
        stats.houses += 1;
        let base = throw.house + Vec3::Y * RISE;
        let n = count_of(rng.rand());
        for _ in 0..n {
            let yaw = rng.rand() * std::f32::consts::TAU - std::f32::consts::PI;
            let woman = rng.rand() >= 0.5;
            let template = if woman { "peasant_woman" } else { "peasant_man" };
            let Some(villager) = spawn(&mut commands, template, base, yaw, &data, &mut lib, &mut anim_lib, &mut binds, &assets, &mut meshes, &mut materials) else { continue };
            // launched from Roman (0x00431020: speed 2.5 + rand * 2^-16, spread 75): the arc from him
            let speed = 2.5 + rng.rand() * 0.5;
            let rolls = LaunchRolls { angle: rng.rand(), side: rng.rand(), forward: rng.rand(), anim: rng.rand() };
            let launch = crate::ai::launch::launch(roman_at.unwrap_or(base), base, speed, MOVE_SPEED, 75.0, false, rolls);
            clock.stamp();
            let anim = chair_anim(rolls.anim);
            commands.entity(villager.0).insert(Tossed { vel: launch.velocity, left: FLIGHT, whoosh: false, chair: villager.1, anim, woman });
            // the scream on him (vt+0xc4): a man's under the peasant delay (Game+0xba4 / +0xbb0), a woman's under the
            // voice delay (Game+0xb44 / +0xb50)
            let now = time.elapsed_secs();
            let cue = if woman {
                let new = barks.take(crate::ai::Delay::Vo, now, &mut rng);
                pick_cue(if new { &WOMAN_NEW } else { &WOMAN_SOON }, rng.rand())
            } else if now - tally.dropoff_line < voice.interval {
                pick_cue(&MAN_SOON, rng.rand())
            } else {
                tally.dropoff_line = now;
                voice.interval = delay.0 + rng.rand() * (delay.1 - delay.0);
                pick_cue(&MAN_NEW, rng.rand())
            };
            sounds.write(crate::audio::PlaySound::on(cue, villager.0));
            stats.screams += 1;
            // a man's scream: a line of the tossed table by whoever can say it (0x00493b70 with 0x0040d250); which
            // list +0xf04 is (JON50172 / Q1150162 need it) is not traced: taken as empty
            if !woman {
                stats.men += 1;
                let present = |q: bool| {
                    companions.iter().find(|(c, _, h)| c.q == q && c.dead.is_none() && h.is_none_or(|h| h.0 > 0.0)).map(|(c, _, _)| crate::ai::dialogue::Present { speaking: c.speech.saying(&voices), still: 0.0 })
                };
                let cast = crate::ai::dialogue::Cast { roman_alive: true, jonesy: present(false), q: present(true), ..default() };
                if let Some((_, said)) = bags.tossed.pick(|| rng.rand(), |l| crate::ai::dialogue::tossed_speaker(l, &cast, false)) {
                    crate::ai::say(said, &mut companions, &voices, &mut sounds);
                    stats.lines += 1;
                }
            }
            stats.thrown += 1;
            info!("EVENT peasants: {template} thrown out ({:.0} / s)", launch.velocity.length());
        }
    }
}

/// A villager's model and its chair at the "center" bone (as level.rs's spawned grunts are made).
#[allow(clippy::too_many_arguments)]
fn spawn(
    commands: &mut Commands,
    template: &str,
    at: Vec3,
    yaw: f32,
    data: &crate::gamedata::GameData,
    lib: &mut crate::scene::Library,
    anim_lib: &mut crate::skin::AnimLibrary,
    binds: &mut Assets<bevy::mesh::skinning::SkinnedMeshInverseBindposes>,
    assets: &AssetServer,
    meshes: &mut Assets<Mesh>,
    materials: &mut Assets<StandardMaterial>,
) -> Option<(Entity, Option<Entity>)> {
    let name = data.templates.get(template).and_then(|t| t.model.clone())?;
    let model = lib.load_model(&name, false, assets, meshes, materials).cloned_parts()?;
    let world = Mat4::from_rotation_translation(Quat::from_rotation_y(yaw) * crate::level::game_to_engine(), at);
    let e = commands.spawn((Transform::from_matrix(world), GlobalTransform::from(Transform::from_matrix(world)), Visibility::default(), Name::new(template.to_owned()))).id();
    let mut ctx = crate::level::SkinCtx { lib: anim_lib, binds };
    let (_, _, center) = crate::level::attach_model(commands, e, &model, true, Some(&mut ctx), "center");
    let chair = data.templates.get("prop_peasant_chair").and_then(|t| t.model.clone()).and_then(|n| lib.load_model(&n, false, assets, meshes, materials));
    let chair = match (center, chair) {
        (Some(c), Some(m)) => Some(crate::level::attach_weapon(commands, c, m, Transform::IDENTITY)),
        _ => None,
    };
    Some((e, chair))
}

/// The flight (state 0x195) and the landing (0x0042e1c0 -> PeasantGenAI_Slot95 0x004353b0 -> GoRagdoll 0x00431310).
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn fly(
    mut commands: Commands,
    time: Res<Time>,
    mut q: Query<(Entity, &mut Tossed, &mut Transform, Option<&mut crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut physics: NonSendMut<Physics>,
    (mut rng, mut stats): (ResMut<crate::fx::FxStats>, ResMut<TossStats>),
    (roman, mut sounds): (Query<&GlobalTransform, With<crate::player::Roman>>, MessageWriter<crate::audio::PlaySound>),
) {
    let dt = time.delta_secs();
    let roman_at = roman.single().ok().map(|t| t.translation());
    for (e, mut tossed, mut t, mut a) in &mut q {
        // the body whoosh once near Roman (state 0x195: one of three by rand * 300)
        if !tossed.whoosh && roman_at.is_some_and(|r| r.distance(t.translation) <= WHOOSH_RANGE) {
            tossed.whoosh = true;
            let r = rng.rand() * 300.0;
            let name = if r < 100.0 { "fol_body_whoosh_1" } else if r < 200.0 { "fol_body_whoosh_2" } else { "fol_body_whoosh_3" };
            sounds.write(crate::audio::PlaySound::on(name, e));
        }
        // the countdown +0x490: still in the air when it runs out, gone (vt+0xd4) with his chair (chairs_go)
        tossed.left -= dt;
        if tossed.left <= 0.0 {
            commands.entity(e).despawn();
            continue;
        }
        // every anim request of PeasantGenAI is the chair anim (0x0043c600)
        if let Some(a) = a.as_deref_mut() {
            if a.layers.first().is_none_or(|l| l.anim != tossed.anim) {
                a.play(0, tossed.anim, true, crate::skin::Mask::All);
            }
        }
        let pos = t.translation;
        let step = tossed.vel * dt;
        // what it meets this frame (the landing's contact: 0x0042e1c0 runs the pending reaction of the state)
        let met = physics.raycast(pos + Vec3::Y, step.normalize_or(Vec3::NEG_Y), step.length() + 1.0).is_some_and(|(b, _, _)| !physics.ai_bodies.contains(&b));
        if met && tossed.vel.y <= 0.0 {
            // the chair let go (0x004353b0): detached, physics on, the villager's velocity with 5..8 up
            if let Some(c) = tossed.chair.take() {
                drop_chair(&mut commands, &mut physics, e, c, &joints, tossed.vel.with_y(5.0 + rng.rand() * 3.0));
            }
            // down as a ragdoll (GoRagdoll quiet: the down count not asked), its body lying 5 s
            if let Some(a) = a.as_deref_mut() {
                if let Some(rd) = crate::ragdoll::start_with(&mut physics, a, &joints, tossed.vel, super::RAGDOLL) {
                    a.layers.clear();
                    commands.entity(e).insert(rd);
                }
            }
            commands.entity(e).remove::<Tossed>().insert(crate::corpses::Corpse::ragdoll());
            stats.landed += 1;
            info!("EVENT peasants: a thrown villager landed");
            continue;
        }
        t.translation = pos + step;
        tossed.vel.y -= crate::ai::launch::GRAVITY * dt;
    }
}

/// The chair let go: out of the villager's hand at its place in the world, a dynamic box round its model.
fn drop_chair(commands: &mut Commands, physics: &mut Physics, villager: Entity, chair: Entity, at: &Query<&GlobalTransform>, vel: Vec3) {
    let Ok(gt) = at.get(chair) else { return };
    let world = gt.compute_transform();
    commands.entity(chair).remove::<ChildOf>().insert((Transform::from_translation(world.translation).with_rotation(world.rotation), ChairOf(villager)));
    let k = physics.meters_per_unit;
    // prop_peasant_chair_coll: about 2.4 x 2.4 x 4 (guess from the model's size)
    let centre = Vec3::new(0.0, 0.0, 2.0);
    let Ok(shape) = oxijolt::prelude::Shape::new_box(jv(Vec3::new(1.2, 1.2, 2.0) * k)) else { return };
    let settings = oxijolt::prelude::BodySettings::new_dynamic().mass(10.0).friction(0.8).restitution(0.2).angular_damping(0.4);
    if let Some(id) = physics.add_dynamic(chair, &shape, world.translation + world.rotation * centre, world.rotation, vel, settings) {
        commands.entity(chair).insert(Body { id, offset: centre, sync_rotation: true });
    }
}

/// A villager's chair (+0xb58), let go at the landing.
#[derive(Component, Clone, Copy, Debug)]
pub struct ChairOf(pub Entity);

/// The chair goes with its villager (PeasantGenAI_Slot55 0x00435330: the carried object stopped and released when he
/// is destroyed).
fn chairs_go(mut commands: Commands, chairs: Query<(Entity, &ChairOf, Option<&Body>)>, alive: Query<(), With<Transform>>, mut physics: NonSendMut<Physics>) {
    for (e, of, body) in &chairs {
        if alive.contains(of.0) {
            continue;
        }
        if let Some(b) = body {
            physics.remove(b.id);
        }
        commands.entity(e).despawn();
    }
}

/// The state log's "tossed".
fn log(stats: Res<TossStats>, flying: Query<(), With<Tossed>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "tossed".into(),
        serde_json::json!({"houses": stats.houses, "thrown": stats.thrown, "men": stats.men, "flying": flying.iter().count(), "landed": stats.landed, "screams": stats.screams, "lines": stats.lines}),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rolls() {
        assert_eq!((count_of(0.0), count_of(0.34), count_of(0.67), count_of(0.9999)), (1, 2, 3, 3));
        assert_eq!(pick_cue(&MAN_NEW, 0.0), "PE150031_PeasantBlownUp");
        assert_eq!(pick_cue(&MAN_NEW, 0.9999), "PE250029_PeasantBlownUp");
        assert_eq!(pick_cue(&WOMAN_SOON, 0.5), "PW250013_PeasantBlownUp");
        assert_eq!((chair_anim(0.49), chair_anim(0.5)), ("Peasant_blown_up_chair", "Peasant_blown_up_chair_2"));
    }
}
