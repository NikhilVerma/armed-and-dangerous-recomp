//! The army's ladders as objects (GruntLadderObject, behaviour grunt_ladder, vtable 0x005a3528; template grunt_ladder,
//! model grunt_ladder_0): the one a grunt carries to the wall and the one he plants there. The carrying and climbing
//! grunts' scripts are army.rs's (AI_ClimbLadder 0x0042d710, the ic_* arrivals); this module draws the ladder.
//!
//! - The ladder object (GruntLadderObject_Construct 0x00461610; Slot6 0x00434580: Ai_InitGeneric, then out of every
//!   AI and target list, World+0xe8c / +0xe7c / +0xe9c / +0xe90 / +0xe94 / +0xea4: no AI sees it, no bullet or blast
//!   reaches it; Slot7 0x00434670: collision description 0x4000; 0x00496e10 its load, 0x00496e70 / 0x00496e80 empty
//!   slots).
//! - Carried (0x004298a0: made at the grunt and held at +0xab4; 0x0042d650: Object_Attach to him, "ladder_run" on it):
//!   while an army grunt runs his ladder spline with the ladder not yet planted; he runs with anim 0x31
//!   (grunt_ladder_run, 0x00427210 with +0xab4 set: charanim.rs).
//! - Planted (GenericAI_PlayICAnim 0x0042c410 with +0x484 0x80000 and a held ladder, ai_03.cpp:1225-1233: detached,
//!   0x00429870, placed at the grunt's matrix moved by 0.458 along its x axis, 24.015 along its y and 12.894 up
//!   (K 0x005bcf40 / 0x005bcf3c / 0x005bcf38), "mover_ladder_plant" played on it): here a mover of that anim from there
//!   (its notes: "invincible" at 0, the event 0x00428220 that makes it immune, +0x484 0x100000, which this object is
//!   anyway; "fx FX_ladder_lean"; "ic_pause" at its end, where it stays). It stays for the level (no "vulnerable" note
//!   in the data).
//! - Guess: the carried ladder hangs at the grunt's origin in its ref pose (Object_Attach's offset is not traced).

use super::Grunt;
use crate::army::ArmyGrunt;
use bevy::prelude::*;

/// The ladder's model (grunt_ladder_0, its joint attributes dropped: drawn in its rest pose), loaded with a level that
/// has the template (level.rs).
#[derive(Resource, Default)]
pub struct LadderAssets(pub Vec<(Handle<Mesh>, Handle<StandardMaterial>)>);

/// A grunt carrying a ladder (+0xab4): the ladder model's entity.
#[derive(Component)]
pub struct CarriedLadder(pub Entity);

/// Ladders picked up and planted so far (playtest log).
#[derive(Resource, Default, Debug)]
pub struct LadderStats {
    pub carried: u32,
    pub planted: u32,
}

/// A planted ladder (its spline's tag).
#[derive(Component)]
pub struct PlantedLadder(pub i64);

/// The plant's offset in the grunt's frame (game axes: x, y, z up).
pub const PLANT_OFFSET: Vec3 = Vec3::new(0.458, 24.015, 12.894);

fn model(commands: &mut Commands, assets: &LadderAssets, parent: Entity) {
    commands.entity(parent).with_children(|p| {
        for (mesh, mat) in &assets.0 {
            p.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone())));
        }
    });
}

/// The carried ladder on and off; a ladder planted when its spline gets one (army.rs's plant script ended).
#[allow(clippy::type_complexity)]
pub fn ladders(
    mut commands: Commands,
    assets: Option<Res<LadderAssets>>,
    army: Res<crate::army::Army>,
    grunts: Query<(Entity, &Grunt, &ArmyGrunt, &GlobalTransform, Option<&CarriedLadder>)>,
    planted: Query<&PlantedLadder>,
    mut stats: ResMut<LadderStats>,
) {
    let Some(assets) = assets.filter(|a| !a.0.is_empty()) else { return };
    let standing: Vec<i64> = planted.iter().map(|p| p.0).collect();
    for (e, g, ag, gt, carried) in &grunts {
        let tag = ag.ladder;
        let is_planted = tag.is_some_and(|t| army.is_planted(t));
        let carrying = g.mind != super::Mind::Dead && tag.is_some() && !is_planted;
        match (carrying, carried) {
            (true, None) => {
                let l = commands.spawn((Transform::IDENTITY, Visibility::default())).id();
                model(&mut commands, &assets, l);
                commands.entity(e).add_child(l).insert(CarriedLadder(l));
                stats.carried += 1;
            }
            (false, Some(c)) => {
                commands.entity(c.0).despawn();
                commands.entity(e).remove::<CarriedLadder>();
                // planted by him: the ladder stands where he put it, played into its lean
                if let Some(t) = tag.filter(|t| is_planted && !standing.contains(t)) {
                    let game = Mat4::from_quat(crate::level::game_to_engine()).inverse() * gt.to_matrix();
                    let origin = game * Mat4::from_translation(PLANT_OFFSET);
                    let l = commands
                        .spawn((crate::mover::engine_transform(origin), Visibility::default(), PlantedLadder(t), crate::mover::Mover::new("mover_ladder_plant", origin)))
                        .id();
                    model(&mut commands, &assets, l);
                    stats.planted += 1;
                    info!("EVENT ai: ladder {t} stands at the wall");
                }
            }
            _ => {}
        }
    }
}

/// The playtest state's `ladders`: carried now, planted.
pub fn log(carried: Query<(), With<CarriedLadder>>, planted: Query<(), With<PlantedLadder>>, stats: Res<LadderStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "ladders".into(),
        serde_json::json!({"carried": carried.iter().count(), "standing": planted.iter().count(), "picked_up": stats.carried, "planted": stats.planted}),
    );
}
