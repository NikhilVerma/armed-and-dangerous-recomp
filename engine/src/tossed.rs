//! What a destroyed building throws out (BarricadeObject / BuildingObject destruction 0x0040cf40 -> 0x0040d390): a house
//! with toss_peasants (bit 0x800, 30 on lev_1_m4, 42 on lev_2_m4 and lev_3_m4_bd ...) throws one to three civilians
//! out (PeasantGenAI: peasant_man / peasant_woman at even odds, each on its prop_peasant_chair) and banks its
//! score_value_peasant_building (Kills when >= 0, Accidents below); one with toss_grunts (bit 0x20) throws out one to
//! three grunts (a grunt_regular, or a captain at even odds when the level has one) with a killing 0x8003 record (their
//! whole health, force 21666 up at 0.7..0.9) and banks its score_value. The husk the building leaves takes both bits
//! (0x0040cef0), so each stage of a multi-stage house throws again.
//!
//! The civilians themselves (spawn, scream, flight in state 0x195, landing) are crate::peasant::toss: this module banks
//! the house's score and sends it [`crate::peasant::toss::ThrowVillagers`].
//!
//! Debt, labelled: the thrown grunts' 21666 push becomes a 40 units/s velocity for their ragdoll
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

/// The smoking wounded troop (0x0040c850 -> 0x0042dc20): made at the first free exit slot of a destroyed building's
/// ruin (its ref_spawn_00, walking out to ref_exit_00: the barracks' ruin grunt_barracks_mini_d keeps the barracks'
/// reference anim grunt_barracks_mini_0), a quarter of his health, fx_smoking_grunt on his chest, the burnt skin
/// b_grunt_burnt (+0x488 bit 31, 0x00426290), and at the door the charge at the player (bit 0x100: 0x0042ce70).
#[derive(Component, Default)]
pub struct SmokingTroop {
    wounded: bool,
    skin: bool,
    charged: bool,
}

/// A grunt made to be thrown out dead (0x0040d390's 0x8003 record of his whole health), with his body's velocity.
#[derive(Component)]
pub struct Tossed(pub Vec3);

/// The tosses so far (the state log).
#[derive(Resource, Default)]
pub struct TossStats {
    pub grunts: u32,
}

/// The building's toss on its death (the villagers' systems run after it).
#[derive(SystemSet, Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub struct BuildingToss;

pub struct TossedPlugin;

impl Plugin for TossedPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.init_level_resource::<TossStats>()
            .add_systems(Update, (toss.after(crate::fx::FxSet).in_set(BuildingToss), ruins, kill_tossed, smoking, burnt_skin, log).chain().run_if(crate::playing));
    }
}

/// 0x0040cf40 / 0x0040d390 on a building's death: toss_peasants banks score_value_peasant_building (Kills when >= 0,
/// Accidents below) and throws the villagers (peasant/toss.rs); toss_grunts throws one to three soldiers out dead and
/// banks score_value.
fn toss(
    mut died: MessageReader<crate::fx::Died>,
    q: Query<(&TossOnDeath, &GlobalTransform)>,
    captains: Query<&crate::ai::Grunt>,
    (mut rng, mut ms, mut stats): (ResMut<crate::fx::FxStats>, ResMut<crate::stats::MissionStats>, ResMut<TossStats>),
    (mut spawn, mut villagers): (MessageWriter<crate::level::SpawnGrunt>, MessageWriter<crate::peasant::toss::ThrowVillagers>),
) {
    for d in died.read() {
        let Ok((t, gt)) = q.get(d.0) else { continue };
        let at = gt.translation();
        if t.peasants {
            ms.killed(t.peasant_score);
            villagers.write(crate::peasant::toss::ThrowVillagers { house: at });
            info!("EVENT toss: a building throws out civilians");
        } else if t.grunts {
            let n = 1 + (rng.rand() * 3.0) as u32 % 3;
            for _ in 0..n {
                // a random heading, 5 above the building's position
                let yaw = rng.rand() * std::f32::consts::TAU - std::f32::consts::PI;
                let start = at + Vec3::Y * crate::peasant::toss::RISE;
                let captain = rng.rand() * 100.0 < 50.0 && captains.iter().any(|g| g.kind == crate::ai::Kind::Captain);
                let template = if captain { "captain" } else { "grunt_regular" };
                // the killing record: its whole health, the push up at 0.7..0.9 (labelled guess for its size)
                let up = rng.range(0.7, 0.9);
                let push = (Quat::from_rotation_y(yaw) * Vec3::Z + Vec3::Y * up).normalize() * 21666.0 * 0.15 / 80.0;
                spawn.write(crate::level::SpawnGrunt { at: start, template: template.into(), thrown: Some(push), ..default() });
                stats.grunts += 1;
            }
            ms.killed(t.score);
            info!("EVENT toss: a building throws out {n} grunts");
        }
    }
}

/// A destroyed object whose ruin (its husk, a "building") has an exit slot sends the smoking troop out of it (0x0040cf40
/// when the building throws no civilians: 0x0040c850, the first free kind-5 slot: ref_exit_00, made at ref_spawn_00).
fn ruins(
    mut died: MessageReader<crate::fx::Died>,
    q: Query<(&crate::level::Placed, &GlobalTransform, Option<&TossOnDeath>)>,
    data: Res<crate::gamedata::GameData>,
    kit: crate::army::ModelKit,
    mut spawn: MessageWriter<crate::level::SpawnGrunt>,
) {
    for d in died.read() {
        let Ok((pl, gt, toss)) = q.get(d.0) else { continue };
        if toss.is_some_and(|t| t.peasants) {
            continue;
        }
        let Some(husk) = pl.husk.as_deref() else { continue };
        let hp = data.template_props(husk);
        // the ruin's slots: a valid building with more than one part (0x0040ad80), its reference anim's nodes
        if hp.s("behavior") != Some("building") || hp.f("valid", 0.0) == 0.0 {
            continue;
        }
        let Some(anim) = hp.s("ref_anim_name").map(str::to_owned).or_else(|| data.templates.get(husk).and_then(|t| t.default_anim.clone())) else { continue };
        let dir = kit.dir();
        let (Some(spawn_at), Some(exit)) = (crate::level::ref_node(&dir, &anim, "ref_spawn_00"), crate::level::ref_node(&dir, &anim, "ref_exit_00")) else { continue };
        let world = gt.to_matrix();
        let at = world.transform_point3(spawn_at.w_axis.truncate());
        let exit = world.transform_point3(exit.w_axis.truncate());
        spawn.write(crate::level::SpawnGrunt { at, template: "grunt_regular".into(), exit: Some(exit), smoking: true, ..default() });
        info!("EVENT toss: a smoking troop comes out of the ruin {husk}");
    }
}

/// The smoking troop's wounds (a quarter of his health, fx_smoking_grunt on his chest) and, out of the door, his charge.
fn smoking(
    mut q: Query<(&mut SmokingTroop, &mut crate::ai::Grunt, &mut crate::fx::Health, Option<&crate::skin::Animator>)>,
    roman: Query<Entity, With<crate::player::Roman>>,
    mut fx: MessageWriter<crate::fx::StartFx>,
) {
    for (mut s, mut g, mut h, a) in &mut q {
        if !s.wounded {
            let Some(chest) = a.and_then(|a| a.skeleton.index.get("chest").and_then(|&i| a.joints.get(i)).copied()) else { continue };
            s.wounded = true;
            h.0 *= 0.25;
            fx.write(crate::fx::StartFx { name: "fx_smoking_grunt".into(), at: Vec3::ZERO, rotation: Quat::IDENTITY, follow: Some(chest), position_only: false });
        }
        if !s.charged && g.exit.is_none() && g.mind != crate::ai::Mind::Dead {
            s.charged = true;
            if let Ok(r) = roman.single() {
                g.target = Some(r);
                g.mind = crate::ai::Mind::Chase;
            }
        }
    }
}

/// The burnt skin (0x00426290 with +0x488 bit 31): his skin's texture (the b_grunt* maps) becomes b_grunt_burnt.
fn burnt_skin(
    mut q: Query<(Entity, &mut SmokingTroop)>,
    children: Query<&Children>,
    mut parts: Query<&mut MeshMaterial3d<crate::gamematerial::GameMaterial>>,
    mut materials: ResMut<Assets<crate::gamematerial::GameMaterial>>,
    assets: Res<AssetServer>,
) {
    for (e, mut s) in &mut q {
        if s.skin {
            continue;
        }
        let mut any = false;
        for c in children.iter_descendants(e) {
            let Ok(mut m) = parts.get_mut(c) else { continue };
            any = true;
            let Some(mat) = materials.get(&m.0) else { continue };
            let skin = assets.get_path(&mat.base).is_some_and(|p| p.path().file_stem().is_some_and(|f| f.to_string_lossy().to_lowercase().starts_with("b_grunt")));
            if !skin {
                continue;
            }
            let mut burnt = mat.clone();
            burnt.base = assets.load(crate::graphics::texture_path("b_grunt_burnt"));
            m.0 = materials.add(burnt);
        }
        s.skin = any;
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

fn log(
    stats: Res<TossStats>,
    smoking: Query<(&SmokingTroop, &crate::ai::Grunt, &crate::fx::Health)>,
    state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mut state) = state else { return };
    let live: Vec<_> = smoking.iter().filter(|(_, g, _)| g.mind != crate::ai::Mind::Dead).collect();
    state.0.insert(
        "toss".into(),
        serde_json::json!({
            "grunts": stats.grunts,
            "smoking": live.len(),
            "smoking_charging": live.iter().filter(|(s, ..)| s.charged).count(),
            "smoking_health": live.first().map(|(_, _, h)| h.0),
            "smoking_burnt": live.iter().filter(|(s, ..)| s.skin).count(),
        }),
    );
}
