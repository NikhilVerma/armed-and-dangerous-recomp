//! Arrows: the archers' projectiles fly with their model and stay stuck where they hit for a while.
//!
//! The original's ArrowProjectile (vtable 0x005a9a40, src/game/game_07.cpp): its property read (0x00482460) adds
//! linger_lifetime (+0x588, default 10; p_grunt_normal_arrow 2.5) to the projectile's; the launch (0x00482510) and
//! the fly-by sound (0x00482620) are player.rs's. The hit (0x00482780): the arrow is moved to the hit point, its trail
//! (fx_start, +0x5d8) released, its damage done, and it sticks to what it hit (0x0047e9f0: to the part of a character
//! it hit, attached at its matrix relative to the target's, else left in the world), marked stuck (+0x58c) and kept
//! for its linger time; removal (0x004824e0) releases the trail and runs the base remove.
//!
//! Here the projectile (player.rs `Projectile`) carries an [`ArrowShaft`] naming the model (ai.rs: the archer's
//! projectile template, grunt_normal_arrow -> grunt_arrow_1); its model is an entity of its own that follows the
//! projectile, and when the projectile hits (player.rs sends [`ArrowStuck`]) it stays at the hit point for the linger
//! time, attached to the bone of the part it hit (so it moves with that limb), or to the object it hit when no part
//! was hit (a prop, a character hit without collision parts). A projectile that ends otherwise (its lifetime) takes
//! its model with it.

use bevy::prelude::*;

/// Counts for the state log: models made, arrows stuck, arrows attached to a bone of the part they hit.
#[derive(Resource, Default)]
struct ArrowStats {
    made: u32,
    stuck: u32,
    on_part: u32,
}

/// On an arrow projectile: the model it flies with.
#[derive(Component, Clone)]
pub struct ArrowShaft {
    pub model: String,
}

/// An arrow's projectile hit at `at`, in `on` when it hit an object (a character, a prop), on its collision part
/// `part` (a bone name) when it hit one: its model stays there `linger` seconds.
#[derive(Message, Clone)]
pub struct ArrowStuck {
    pub arrow: Entity,
    pub at: Vec3,
    pub on: Option<Entity>,
    pub part: Option<String>,
    pub linger: f32,
}

/// An arrow's model: following its projectile in flight, then stuck with time left.
#[derive(Component)]
pub struct ArrowModel {
    projectile: Entity,
    /// stuck: seconds left
    left: Option<f32>,
}

pub struct ArrowsPlugin;

impl Plugin for ArrowsPlugin {
    fn build(&self, app: &mut App) {
        use crate::levelreset::LevelAppExt;
        app.add_level_message::<ArrowStuck>()
            .init_level_resource::<ArrowStats>()
            .add_systems(Update, dress)
            .add_systems(PostUpdate, (stick, follow, linger, log_state).chain().before(bevy::transform::TransformSystems::Propagate));
    }
}

/// A new arrow gets its model (the projectile's template's).
fn dress(mut commands: Commands, mut kit: crate::army::ModelKit, arrows: Query<(Entity, &ArrowShaft, &Transform), Added<ArrowShaft>>, mut stats: ResMut<ArrowStats>) {
    for (e, a, t) in &arrows {
        if let Some(m) = kit.spawn(&mut commands, &a.model, *t, false) {
            commands.entity(m).insert(ArrowModel { projectile: e, left: None });
            stats.made += 1;
        }
    }
}

/// The hit (0x00482780 -> 0x0047e9f0): the model at the hit point, kept for the linger time and attached at its place
/// relative to what it hit: the bone of the part hit (0x00458660 / 0x004eb670 the part's node and bone, 0x00453c50 its
/// matrix, Object_Attach 0x004539f0 with the bone), or the object itself for no part (record+0x10 = -1). A part whose
/// bone is not in the target's skeleton leaves the arrow where it is, unattached (0x0047e9f0 returns 0).
fn stick(
    mut commands: Commands,
    mut ev: MessageReader<ArrowStuck>,
    mut models: Query<(Entity, &mut ArrowModel, &mut Transform)>,
    targets: Query<(&GlobalTransform, Option<&crate::skin::Animator>)>,
    bones: Query<&GlobalTransform>,
    mut stats: ResMut<ArrowStats>,
) {
    for s in ev.read() {
        let Some((m, mut a, mut t)) = models.iter_mut().find(|(_, a, _)| a.projectile == s.arrow && a.left.is_none()) else { continue };
        a.left = Some(s.linger);
        stats.stuck += 1;
        t.translation = s.at;
        let Some((on, (g, animator))) = s.on.and_then(|on| targets.get(on).ok().map(|x| (on, x))) else { continue };
        debug!("arrow stuck in {on:?}, part {:?}, skinned {}", s.part, animator.is_some());
        let on_part = s.part.is_some() && animator.is_some();
        let parent = match (&s.part, animator) {
            (Some(part), Some(an)) => {
                let bone = an.skeleton.names.iter().position(|n| n.trim().eq_ignore_ascii_case(part)).and_then(|i| an.joints.get(i)).and_then(|&j| bones.get(j).ok().map(|bg| (j, *bg)));
                let Some(bone) = bone else { continue };
                bone
            }
            _ => (on, *g),
        };
        // (a parent scaled unevenly, a stretched prop, would shear the arrow as its child: left in the world)
        let sc = parent.1.scale();
        if (sc.max_element() - sc.min_element()).abs() > 1e-3 * sc.max_element().abs() {
            continue;
        }
        let local = parent.1.affine().inverse() * Transform::from_translation(s.at).with_rotation(t.rotation).with_scale(t.scale).to_matrix();
        *t = Transform::from_matrix(local);
        commands.entity(parent.0).add_child(m);
        stats.on_part += on_part as u32;
    }
}

/// In flight the model is where its projectile is; a projectile gone without a hit takes its model.
fn follow(mut commands: Commands, mut models: Query<(Entity, &ArrowModel, &mut Transform)>, projectiles: Query<&Transform, (With<ArrowShaft>, Without<ArrowModel>)>) {
    for (m, a, mut t) in &mut models {
        if a.left.is_some() {
            continue;
        }
        match projectiles.get(a.projectile) {
            Ok(p) => *t = *p,
            Err(_) => commands.entity(m).despawn(),
        }
    }
}

/// A stuck arrow is removed once its linger time has run out (0x004824e0).
fn linger(mut commands: Commands, time: Res<Time>, mut models: Query<(Entity, &mut ArrowModel)>) {
    for (m, mut a) in &mut models {
        if let Some(l) = a.left.as_mut() {
            *l -= time.delta_secs();
            if *l <= 0.0 {
                commands.entity(m).despawn();
            }
        }
    }
}

/// The state log's "arrows": models in flight (and arrow projectiles in flight: one model each), stuck, stuck in a
/// character, the most linger time left, the models made, the arrows stuck so far and those attached to a part's bone.
fn log_state(models: Query<(&ArrowModel, Has<ChildOf>)>, shafts: Query<(), With<ArrowShaft>>, stats: Res<ArrowStats>, play: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut p) = play else { return };
    let flying = models.iter().filter(|(a, _)| a.left.is_none()).count();
    let stuck = models.iter().filter(|(a, _)| a.left.is_some()).count();
    let in_body = models.iter().filter(|(a, c)| a.left.is_some() && *c).count();
    let linger_max = models.iter().filter_map(|(a, _)| a.left).fold(0.0f32, f32::max);
    p.0.insert(
        "arrows".into(),
        serde_json::json!({"flying": flying, "projectiles": shafts.iter().count(), "stuck": stuck, "in_body": in_body, "linger_max": linger_max, "made": stats.made, "stuck_total": stats.stuck, "on_part": stats.on_part}),
    );
}
