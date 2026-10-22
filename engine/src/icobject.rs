//! Scripted objects of the defence levels:
//! - ICObject (behaviour object_ic, ctor 0x004617c0, vtable 0x005a1b60; load 0x00436820): the bombers flying over the
//!   base (shrub_bomber) and the flak (p_flak). Hidden when "invisible"; waits for receive_trigger_activate (state 0x8c,
//!   mode trigger_ic), then plays its generic_txt as a mover (vt+0x238 0x00436980 -> 0x0042c410) from where it stands.
//!   Its script's notes (GenericAIObject_HandleEvent 0x0043d180): "visible" / "invisible", "fx NAME", "powderkeg",
//!   "health", "token_or_health", "turret_upgrade" (an object made at its matrix: powderkeg_bomber, shootable_health,
//!   shootable_token, shootable_turret_upgrade; 0x004324d0), "deactivate".
//! - The drops: kegs to shoot (fx_explosion: area damage among the army) and pickups to shoot (max_health 1,
//!   takes_bullet_damage; death_timeout 20 s): on_death_player_health (shootable_health: +50), the turret's ammo upgrade
//!   (turret_upgrade: Game+0xdd1, defence.rs). They fall at their "gravity" 0.5 (the custom-gravity list World+0xf1c,
//!   the getter 0x00450fa0: the body's gravity factor) with the speed trap's linear cast (movable.rs).
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
    /// hidden until its trigger (ShrubPatrol: load 0x00436630 vt+0xf4(0), trigger 0x004367c0 vt+0xf4(1))
    show_on_start: bool,
}

/// An AnimPlayer: its anim, triggers, and where in the anim it is and which way it plays.
#[derive(Component)]
pub struct AnimPlayer {
    anim: String,
    activate: i64,
    backward: i64,
    /// the time into the anim and the play direction: 0 held, 1 forward (+0x460 bit 0), -1 backward (bit 1)
    t: f32,
    direction: f32,
    /// a model without a skeleton whose parts ride one bone (the gate: base_gate_l5_m5_0 on b_gate): the bone the anim
    /// moves them by
    rigid_bone: Option<String>,
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

/// What the scripted objects did (the playtest state log's "ic").
#[derive(Resource, Default, Debug)]
struct IcStats {
    started: u32,
    drops: u32,
    /// the template of the last drop
    last_drop: &'static str,
    shot: u32,
    gates: u32,
}

/// A shot turret upgrade (Game+0xdd1): the base turret's ammo upgrade starts (defence.rs).
#[derive(Message, Clone, Copy)]
pub struct TurretUpgrade;

pub struct IcObjectPlugin;

impl Plugin for IcObjectPlugin {
    fn build(&self, app: &mut App) {
        app.add_level_message::<TurretUpgrade>()
            .init_level_resource::<IcStats>()
            // (drops read fx::Died before the husk swap removes the entity: before mission::deaths)
            .add_systems(Update, (setup, triggers, notes, drops, anim_players, log).chain().after(crate::fx::FxSet).before(crate::mission::deaths).run_if(crate::playing));
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
    lib: Res<crate::scene::Library>,
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
                commands.entity(e).insert(IcObject { activate: p.i("receive_trigger_activate", 0), script: p.s("generic_txt").unwrap_or("none").to_lowercase(), started: false, show_on_start: false });
            }
            // the defence levels' AIs waiting in mode trigger_ic for their script (GenericAI state 0x8c): the goliath and the
            // shrub patrols (other trigger_ic classes, e.g. the saucer, belong to their own modules)
            b @ ("goliath" | "shrubpatrol") if p.s("mode") == Some("trigger_ic") && p.s("generic_txt").is_some_and(|g| !g.eq_ignore_ascii_case("none")) => {
                let hidden = b == "shrubpatrol";
                if hidden {
                    *vis = Visibility::Hidden;
                }
                commands.entity(e).insert(IcObject { activate: p.i("receive_trigger_activate", 0), script: p.s("generic_txt").unwrap_or("none").to_lowercase(), started: false, show_on_start: hidden });
            }
            "anim_player" => {
                // Anim_Set(anim, 0x90) at load: set, held at its start
                let anim = p.s("anim").unwrap_or("none").to_lowercase();
                if let Some(mut a) = animator {
                    if anim != "none" {
                        a.play(0, &anim, false, crate::skin::Mask::All);
                    }
                }
                let rigid_bone = lib.models.get(&inst.model).and_then(|m| {
                    m.skeleton.is_none().then(|| m.binds.iter().find_map(|b| if let crate::scene::PartBind::Bone(b) = b { Some(b.clone()) } else { None })).flatten()
                });
                commands.entity(e).insert(AnimPlayer { anim, activate: p.i("receive_trigger_activate", 0), backward: p.i("receive_trigger_bkwd", 0), t: 0.0, direction: 0.0, rigid_bone });
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
    mut ics: Query<(Entity, &mut IcObject, &GlobalTransform, &mut Visibility)>,
    mut players: Query<&mut AnimPlayer>,
    mut stats: ResMut<IcStats>,
) {
    let ids: Vec<i64> = events.read().map(|e| e.0).filter(|t| *t != 0).collect();
    for t in ids {
        for (e, mut ic, gt, mut vis) in &mut ics {
            if ic.started || ic.activate != t || ic.script == "none" {
                continue;
            }
            ic.started = true;
            stats.started += 1;
            if ic.show_on_start {
                *vis = Visibility::Inherited;
            }
            // the object's matrix, game axes (engine -> game: the inverse of game_to_engine)
            let to_game = Mat4::from_quat(crate::level::game_to_engine().inverse());
            let (_, r, p) = (to_game * gt.to_matrix()).to_scale_rotation_translation();
            commands.entity(e).insert(Mover::new(&ic.script, Mat4::from_rotation_translation(r, p)));
            info!("EVENT ic: object {e:?} starts {}", ic.script);
        }
        // forward from where it is (0x20000 once it went backward), backward likewise (0x20040)
        for mut ap in &mut players {
            if ap.anim == "none" {
                continue;
            }
            if t == ap.activate && ap.direction <= 0.0 {
                ap.direction = 1.0;
                stats.gates += 1;
                info!("EVENT ic: anim player plays {}", ap.anim);
            } else if t == ap.backward && ap.direction >= 0.0 {
                ap.direction = -1.0;
                info!("EVENT ic: anim player plays {} backward", ap.anim);
            }
        }
    }
}

/// AnimPlayers advance their anim in their direction and rest at its ends: a skinned model's anim layer is held at
/// the time; a rigid one's parts (no skeleton, every part on one bone) move by that bone's track (its key at the time
/// over its key at the start, in the model's frame).
fn anim_players(
    time: Res<Time>,
    mut q: Query<(&mut AnimPlayer, Option<&mut crate::skin::Animator>, Option<&Children>)>,
    mut parts: Query<&mut Transform, With<Mesh3d>>,
    mut lib: ResMut<crate::skin::AnimLibrary>,
) {
    let dt = time.delta_secs();
    for (mut ap, animator, children) in &mut q {
        if ap.anim == "none" {
            continue;
        }
        let Some(anim) = lib.anim(&ap.anim) else { continue };
        ap.t = (ap.t + ap.direction * dt).clamp(0.0, anim.duration);
        if let Some(mut a) = animator {
            if let Some(l) = a.layers.get_mut(0).filter(|l| l.anim == ap.anim) {
                l.speed = 0.0;
                l.time = ap.t;
            }
            continue;
        }
        let (Some(bone), Some(children)) = (ap.rigid_bone.as_deref(), children) else { continue };
        let key = |t: f32| Mat4::from_rotation_translation(anim.rotation(bone, t).unwrap_or(Quat::IDENTITY), anim.translation(bone, t).unwrap_or(Vec3::ZERO));
        let delta = key(ap.t) * key(0.0).inverse();
        for c in children.iter() {
            if let Ok(mut tr) = parts.get_mut(c) {
                *tr = Transform::from_matrix(delta);
            }
        }
    }
}

/// The ICObjects' script notes beyond the generic ones (mover.rs): the drops.
#[allow(clippy::too_many_arguments)]
fn notes(
    mut commands: Commands,
    mut notes: MessageReader<MoverNote>,
    ics: Query<&GlobalTransform, With<IcObject>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut stats: ResMut<IcStats>,
    opts: Res<crate::options::GameOptions>,
) {
    for n in notes.read() {
        let Ok(gt) = ics.get(n.entity) else { continue };
        let word = n.text.split_whitespace().next().unwrap_or("").to_lowercase();
        let health = || ("shootable_health", DropKind::Health(data.template_props("shootable_health").f("on_death_player_health", 50.0)));
        let (template, kind) = match word.as_str() {
            "powderkeg" => ("powderkeg_bomber", DropKind::Keg),
            "health" => health(),
            // token_or_health (ai_05.cpp:1631-1634): health once the level's token is found (0x004a0720(level, 2)),
            // else the token (the defence levels' token: their health bombers, mover_sb_health)
            "token_or_health" if crate::pickup::level_token_found(&opts, &kit.dir()) => health(),
            "token" | "token_or_health" => ("shootable_token", DropKind::Token),
            "turret_upgrade" => ("shootable_turret_upgrade", DropKind::TurretUpgrade),
            _ => continue,
        };
        let p = data.template_props(template);
        let Some(model) = data.templates.get(template).and_then(|t| t.model.clone()) else { continue };
        let tr = Transform::from_translation(gt.translation()).with_rotation(gt.compute_transform().rotation);
        let Some(e) = kit.spawn(&mut commands, &model, tr, true) else { continue };
        let k = physics.meters_per_unit;
        let Ok(shape) = oxijolt::prelude::Shape::new_sphere(3.0 * k) else { continue };
        // its property set's gravity (0.5: the getter 0x00450fa0) and speed trap (anti-tunnelling, 0x00452790: a linear
        // cast), as a movable object's (movable.rs)
        let mp = crate::movable::MovableProps::of(&p);
        let mut s = oxijolt::prelude::BodySettings::new_dynamic().mass(20.0).gravity_factor(mp.gravity);
        if mp.speed_trap != 0 {
            s = s.motion_quality(oxijolt::prelude::MotionQuality::LinearCast);
        }
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
        stats.drops += 1;
        stats.last_drop = template;
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
    mut stats: ResMut<IcStats>,
    (mut opts, mut ms, texts, difficulty, mut cheats, mut mission, lib): (
        ResMut<crate::options::GameOptions>,
        ResMut<crate::stats::MissionStats>,
        Res<crate::gamemenu::Texts>,
        Res<crate::gamedata::Difficulty>,
        ResMut<crate::cheats::Cheats>,
        Option<ResMut<crate::mission::Mission>>,
        Res<crate::skin::AnimLibrary>,
    ),
) {
    for d in died.read() {
        let Ok((_, drop)) = q.get(d.0) else { continue };
        stats.shot += 1;
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
            // on_death_unlock_level (game_03.cpp:938-948): the level's token, unless found already (0x004a0720), as a
            // token pickup gives it (0x004a07a0 with category 2, Game+0x2720)
            DropKind::Token => {
                let found = crate::pickup::level_token_found(&opts, &lib.dir);
                if !found {
                    let key = crate::pickup::shot_token_key(&lib.dir);
                    crate::pickup::award_token(
                        Some(key),
                        crate::pickup::TokenReward { opts: &mut opts, stats: &mut ms, texts: &texts, difficulty: *difficulty, cheats: &mut cheats, mission: mission.as_deref_mut() },
                    );
                }
                info!("EVENT ic: token shot (level token {})", if found { "already found" } else { "found" });
            }
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

fn log(stats: Res<IcStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    if stats.started == 0 && stats.gates == 0 {
        return;
    }
    state.0.insert("ic".into(), serde_json::json!({ "started": stats.started, "drops": stats.drops, "last_drop": stats.last_drop, "shot": stats.shot, "gates": stats.gates }));
}
