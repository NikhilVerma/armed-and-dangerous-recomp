//! Scripted objects of the defence levels:
//! - ICObject (behaviour object_ic, ctor 0x004617c0, vtable 0x005a1b60; load 0x00436820): the bombers flying over the
//!   base (shrub_bomber) and the flak (p_flak). Hidden when "invisible"; waits for receive_trigger_activate (state 0x8c,
//!   mode trigger_ic), then plays its generic_txt as a mover (vt+0x238 0x00436980 -> 0x0042c410) from where it stands.
//!   Its script's notes (GenericAIObject_HandleEvent 0x0043d180): "visible" / "invisible", "fx NAME", "powderkeg",
//!   "health", "token_or_health", "turret_upgrade" (an object made at its matrix: powderkeg_bomber, shootable_health,
//!   shootable_token, shootable_turret_upgrade; 0x004324d0), "deactivate".
//! - The drops: kegs to shoot (fx_explosion: area damage among the army) and pickups to shoot (max_health 1,
//!   takes_bullet_damage; death_timeout 20 s): on_death_player_health (shootable_health: +50), the turret's ammo upgrade
//!   (turret_upgrade: Game+0xdd1, defence.rs). They fall under the world's gravity (their "gravity" 0.5: guess that it
//!   scales the fall: not ported, full gravity).
//! - AnimPlayer (behaviour anim_player, vtable 0x005ab788; load 0x0040a700, trigger 0x0040a7d0): its "anim" set and held
//!   at the start (flags 0x90); receive_trigger_activate plays it forward, receive_trigger_bkwd backward (0x20040), each
//!   from where it is. The bases' gates (base_gate_l5_m5).
//!
//! Hooks outside this file: level.rs `destructible` (pub(crate)), main.rs (plugin).

use bevy::prelude::*;

use crate::levelreset::LevelAppExt;
use crate::mover::{Mover, MoverNote};

/// An ICObject waiting for its trigger or running its script.
#[derive(Component)]
pub struct IcObject {
    activate: i64,
    script: String,
    started: bool,
}

/// An AnimPlayer: its anim, triggers, and which way it last played.
#[derive(Component)]
pub struct AnimPlayer {
    anim: String,
    activate: i64,
    backward: i64,
    /// +0x460 bit 0 (forward) / bit 1 (backward)
    state: u8,
}

/// A dropped object waiting to be shot (or to fall to the ground and time out).
#[derive(Component)]
pub struct Drop {
    kind: DropKind,
    left: f32,
    body: oxijolt::prelude::BodyId,
}

#[derive(Clone, Copy, PartialEq, Debug)]
enum DropKind {
    Keg,
    Health(f32),
    Token,
    TurretUpgrade,
}

/// A shot turret upgrade (Game+0xdd1): the base turret's ammo upgrade starts (defence.rs).
#[derive(Message, Clone, Copy)]
pub struct TurretUpgrade;

pub struct IcObjectPlugin;

impl Plugin for IcObjectPlugin {
    fn build(&self, app: &mut App) {
        app.add_level_message::<TurretUpgrade>()
            .add_systems(Update, (setup, triggers, notes, drops, anim_players).chain().run_if(crate::playing));
    }
}

/// ICObject / AnimPlayer components for the placed objects (once each).
fn setup(
    mut commands: Commands,
    launch: Res<crate::Launch>,
    data: Res<crate::gamedata::GameData>,
    mut q: Query<(Entity, &crate::level::InstanceId, &mut Visibility, Option<&mut crate::skin::Animator>), (Without<IcObject>, Without<AnimPlayer>)>,
    mut scene: crate::levelreset::PerLevel<Option<crate::scene::SceneFile>>,
    mut done: crate::levelreset::PerLevel<std::collections::HashSet<Entity>>,
) {
    let done = done.get();
    if q.iter().all(|(e, ..)| done.contains(&e)) {
        return;
    }
    let scene = scene.get().get_or_insert_with(|| crate::scene::read_scene(&launch.scene));
    for (e, id, mut vis, animator) in &mut q {
        if !done.insert(e) {
            continue;
        }
        let Some(inst) = scene.instances.iter().find(|i| i.id == id.0) else { continue };
        let p = data.instance_props(&inst.template, &inst.props);
        match p.s("behavior").unwrap_or("") {
            "object_ic" => {
                if p.f("invisible", 0.0) != 0.0 {
                    *vis = Visibility::Hidden;
                }
                commands.entity(e).insert(IcObject { activate: p.i("receive_trigger_activate", 0), script: p.s("generic_txt").unwrap_or("none").to_lowercase(), started: false });
            }
            "anim_player" => {
                let anim = p.s("anim").unwrap_or("none").to_lowercase();
                // Anim_Set(anim, 0x90) at load: set, held at its start
                if let Some(mut a) = animator {
                    if anim != "none" {
                        a.play(0, &anim, false, crate::skin::Mask::All);
                        if let Some(l) = a.layers.get_mut(0) {
                            l.speed = 0.0;
                        }
                    }
                }
                commands.entity(e).insert(AnimPlayer { anim, activate: p.i("receive_trigger_activate", 0), backward: p.i("receive_trigger_bkwd", 0), state: 0 });
            }
            _ => {}
        }
    }
}

/// The objects' trigger slots: an ICObject starts its script (0x004367c0-style activation of state 0x8c), an AnimPlayer
/// plays forward / backward (0x0040a7d0).
fn triggers(
    mut commands: Commands,
    mut events: MessageReader<crate::mission::SendTrigger>,
    mut ics: Query<(Entity, &mut IcObject, &GlobalTransform)>,
    mut players: Query<(&mut AnimPlayer, &mut crate::skin::Animator)>,
) {
    let ids: Vec<i64> = events.read().map(|e| e.0).filter(|t| *t != 0).collect();
    for t in ids {
        for (e, mut ic, gt) in &mut ics {
            if ic.started || ic.activate != t || ic.script == "none" {
                continue;
            }
            ic.started = true;
            // the object's matrix, game axes (engine -> game: the inverse of game_to_engine)
            let to_game = Mat4::from_quat(crate::level::game_to_engine().inverse());
            let (_, r, p) = (to_game * gt.to_matrix()).to_scale_rotation_translation();
            commands.entity(e).insert(Mover::new(&ic.script, Mat4::from_rotation_translation(r, p)));
            info!("EVENT ic: object {e:?} starts {}", ic.script);
        }
        for (mut ap, mut a) in &mut players {
            if ap.anim == "none" {
                continue;
            }
            if t == ap.activate && ap.state & 1 == 0 {
                let from = a.layers.first().map(|l| l.time).unwrap_or(0.0);
                a.play(0, &ap.anim, false, crate::skin::Mask::All);
                if let Some(l) = a.layers.get_mut(0) {
                    l.speed = 1.0;
                    if ap.state & 2 != 0 {
                        l.time = from;
                    }
                }
                ap.state = 1;
                info!("EVENT ic: anim player plays {}", ap.anim);
            } else if t == ap.backward && ap.state & 2 == 0 {
                if let Some(l) = a.layers.get_mut(0) {
                    l.speed = -1.0;
                }
                ap.state = 2;
                info!("EVENT ic: anim player plays {} backward", ap.anim);
            }
        }
    }
}

/// An AnimPlayer's anim rests at its ends (a one-shot anim held by the skin module): nothing to do per frame but keep
/// a held anim from restarting.
fn anim_players(mut q: Query<(&AnimPlayer, &mut crate::skin::Animator)>) {
    for (ap, mut a) in &mut q {
        if ap.state == 0 {
            if let Some(l) = a.layers.get_mut(0) {
                l.speed = 0.0;
            }
        }
    }
}

/// The ICObjects' script notes beyond the generic ones (mover.rs): visibility and drops.
#[allow(clippy::too_many_arguments)]
fn notes(
    mut commands: Commands,
    mut notes: MessageReader<MoverNote>,
    mut ics: Query<(&mut Visibility, &GlobalTransform), With<IcObject>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut physics: NonSendMut<crate::physics::Physics>,
) {
    for n in notes.read() {
        let Ok((mut vis, gt)) = ics.get_mut(n.entity) else { continue };
        let word = n.text.split_whitespace().next().unwrap_or("").to_lowercase();
        let (template, kind) = match word.as_str() {
            "visible" => {
                *vis = Visibility::Inherited;
                continue;
            }
            "invisible" => {
                *vis = Visibility::Hidden;
                continue;
            }
            "powderkeg" => ("powderkeg_bomber", DropKind::Keg),
            // token_or_health: a token when the level's is still to find (0x004a0720(level, 2)): guess, health always
            "health" | "token_or_health" => ("shootable_health", DropKind::Health(data.template_props("shootable_health").f("on_death_player_health", 50.0))),
            "token" => ("shootable_token", DropKind::Token),
            "turret_upgrade" => ("shootable_turret_upgrade", DropKind::TurretUpgrade),
            _ => continue,
        };
        let p = data.template_props(template);
        let Some(model) = data.templates.get(template).and_then(|t| t.model.clone()) else { continue };
        let tr = Transform::from_translation(gt.translation()).with_rotation(gt.compute_transform().rotation);
        let Some(e) = kit.spawn(&mut commands, &model, tr, true) else { continue };
        let k = physics.meters_per_unit;
        let Ok(shape) = oxijolt::prelude::Shape::new_sphere(3.0 * k) else { continue };
        let s = oxijolt::prelude::BodySettings::new_dynamic().mass(20.0);
        let Some(body) = physics.add_dynamic(e, &shape, tr.translation, tr.rotation, Vec3::ZERO, s) else { continue };
        if let Some(m) = p.s("material") {
            physics.materials.insert(body, m.to_owned());
        }
        let mut d = crate::level::destructible(&p).unwrap_or(crate::fx::Destructible {
            health: p.f("max_health", 1.0),
            max_health: p.f("max_health", 1.0),
            fx_death: None,
            husk: None,
            area_damage: 0.0,
            radius_inner: 0.0,
            radius_outer: 0.0,
            impact_force: 0.0,
            chain: false,
            start_delay: 0.0,
            min_delay: 0.0,
            acceleration: 1.0,
            dying: None,
            chain_delay: 0.0,
            class: crate::fx::DClass::Default,
            score: 0.0,
            takes_damage: true,
            takes_bullet: true,
            player_damage: true,
            teammate_damage: false,
            badguy_damage: false,
        });
        if kind != DropKind::Keg {
            d.takes_damage = true;
            d.takes_bullet = true;
        }
        commands.entity(e).insert((
            crate::physics::Body { id: body, offset: Vec3::ZERO, sync_rotation: true },
            crate::level::Placed { template: template.into(), collider: Some(body), husk: None, triggers: default() },
            d,
            crate::fx::Bounds { center: Vec3::ZERO, radius: 3.0 },
            Drop { kind, left: p.f("death_timeout", 20.0), body },
        ));
        info!("EVENT ic: {word} dropped");
    }
}

/// Dropped objects: shot (Died: their reward) or timed out (death_timeout).
#[allow(clippy::too_many_arguments)]
fn drops(
    mut commands: Commands,
    time: Res<Time>,
    mut died: MessageReader<crate::fx::Died>,
    mut q: Query<(Entity, &mut Drop)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut roman: Query<(&mut crate::fx::Health, &crate::player::Roman)>,
    mut upgrade: MessageWriter<TurretUpgrade>,
) {
    for d in died.read() {
        let Ok((_, drop)) = q.get(d.0) else { continue };
        physics.remove(drop.body);
        match drop.kind {
            DropKind::Health(h) => {
                if let Ok((mut hp, r)) = roman.single_mut() {
                    hp.0 = (hp.0 + h).min(r.max_health.max(hp.0));
                }
                info!("EVENT ic: health shot (+{h})");
            }
            DropKind::TurretUpgrade => {
                upgrade.write(TurretUpgrade);
                info!("EVENT ic: turret upgrade shot");
            }
            DropKind::Token => info!("EVENT ic: token shot"),
            DropKind::Keg => {}
        }
    }
    let dt = time.delta_secs();
    for (e, mut drop) in &mut q {
        drop.left -= dt;
        if drop.left <= 0.0 {
            physics.remove(drop.body);
            commands.entity(e).despawn();
        }
    }
}
