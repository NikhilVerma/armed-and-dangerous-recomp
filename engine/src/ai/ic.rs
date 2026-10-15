//! IC entries: grunts hidden in the scenery that come out by a scripted move (an "IC" mover anim) when their trigger
//! arrives. A host object makes them at load, one grunt_regular per non-zero IC property, at the property's reference
//! node (fn_00456180: ObjectSystem_CreateObject, the node's matrix, 0x0042d000: waiting in state 0x8c with the mover's
//! name at +0x4cc, its first frame held (Anim_Set 0x00408d90, flags 0x90), the trigger +0x52c, trigger_los +0x488 bit 6).
//! Hosts (their load functions and tables): buildings (TwigletGenerator_Slot84 0x0040ad80: doors, window dives, wall
//! climbs, roof jumps, wall jumps), trees (0x00496e90: the tree slides) and powder kegs (0x0041c180).
//!
//! - The trigger, or anything that wakes the grunt (a target seen with trigger_los, a hit), starts the move
//!   (0x0042f5c0's IC branch, mode 0x8000000): the host destroyed: no move (vt+0x17c, guess: he just wakes); the
//!   landing building of a roof jump (IC_building_tag) destroyed: he guards where he is (0x0042d0f0); else the mover from
//!   where he waits (GenericAI_PlayICAnim 0x0042c410: physics off, state 0x64). The move is a [`Mover`]: its root track
//!   carries him, "anim_full" plays it on his skeleton, its fx / trigger notes are the generic ones (mover.rs).
//! - Its end (state 0x64's end -> 0x65, ai_06.cpp:1960-1976): upright, his home where he stands, the stand set, state 1;
//!   with the note "ready_roof" (GenericAIObject_HandleEvent 0x0043d180) a guard on the roof (0x0042d0f0). "ragdoll"
//!   (the missed roof jump) ends the move there and lets him fall (guess: vt+0x17c's fall is not ported).
//!
//! Hooks outside this file: level.rs (the hosts' `hidden_grunts` at load, `SpawnGrunt::ic`), ai.rs (Mind::Scripted:
//! the think loop leaves him alone), charanim.rs (leaves his anims to this module).

use super::{Grunt, Mind};
use crate::mover::{Mover, MoverNote};
use bevy::prelude::*;

/// One hiding place of a host: the property holding its trigger, the reference node the grunt waits at, the mover he
/// plays, and whether the move lands on the building named by IC_building_tag (the roof jumps).
pub struct IcSource {
    pub prop: &'static str,
    pub node: &'static str,
    pub mover: &'static str,
    pub lands: bool,
}

const fn src(prop: &'static str, node: &'static str, mover: &'static str, lands: bool) -> IcSource {
    IcSource { prop, node, mover, lands }
}

/// TwigletGenerator_Slot84 0x0040ad80: the buildings' fifteen movers in its order (strings 0x005bb6a8 .. 0x005bb31c;
/// the four roof jumps share one node).
pub const BUILDING: [IcSource; 15] = [
    src("ic_window_dive_low_1", "ref_grunt_dive_window_low", "mover_grunt_dive_window_low", false),
    src("ic_window_dive_high_1", "ref_grunt_dive_window_high", "mover_grunt_dive_window_high", false),
    src("ic_climb_v2f_1", "ref_grunt_climb_v2f_1", "mover_grunt_climb_v2f_1", false),
    src("ic_roof_jump_v2f_1", "ref_grunt_roof_jump_2_2_miss", "mover_grunt_roof_jump_2_2", true),
    src("ic_roof_jump_v2f_v1f_1", "ref_grunt_roof_jump_2_2_miss", "mover_grunt_roof_jump_2_to_1", true),
    src("ic_roof_jump_miss_v2f_1", "ref_grunt_roof_jump_2_2_miss", "mover_grunt_roof_jump_2_2_miss", true),
    src("ic_roof_jump_slip_v2f_1", "ref_grunt_roof_jump_2_2_miss", "mover_grunt_jump_slip_vf2", true),
    src("ic_jump_wall_front_1", "ref_grunt_jump_wall_1", "mover_grunt_jump_wall_1", false),
    src("ic_jump_wall_front_2", "ref_grunt_jump_wall_2", "mover_grunt_jump_wall_2", false),
    src("ic_jump_wall_front_3", "ref_grunt_jump_wall_3", "mover_grunt_jump_wall_3", false),
    src("ic_jump_wall_back_1", "ref_grunt_jump_wall_4", "mover_grunt_jump_wall_1", false),
    src("ic_jump_wall_back_2", "ref_grunt_jump_wall_5", "mover_grunt_jump_wall_2", false),
    src("ic_jump_wall_back_3", "ref_grunt_jump_wall_6", "mover_grunt_jump_wall_3", false),
    src("ic_door_1", "ref_grunt_door_1", "mover_grunt_door_1", false),
    src("ic_door_2", "ref_grunt_door_2", "mover_grunt_door_2", false),
];

/// 0x00496e90: the trees' slides (strings 0x0058f7a0 .. 0x0058f71c).
pub const TREE: [IcSource; 3] = [
    src("ic_tree_slide_1", "ref_grunt_tree_1", "mover_grunt_tree_1", false),
    src("ic_tree_slide_2", "ref_grunt_tree_2", "mover_grunt_tree_2", false),
    src("ic_tree_slide_3", "ref_grunt_tree_3", "mover_grunt_tree_3", false),
];

/// 0x0041c180: the powder kegs' (strings 0x005b94a0 .. 0x005b9404).
pub const POWDER: [IcSource; 3] = [
    src("ic_powder_1", "ref_grunt_powder_1", "mover_grunt_powder_1", false),
    src("ic_powder_2", "ref_grunt_powder_2", "mover_grunt_powder_2", false),
    src("ic_powder_3", "ref_grunt_powder_3", "mover_grunt_powder_3", false),
];

/// The hiding places of a host of this behaviour.
pub fn sources(behavior: &str) -> &'static [IcSource] {
    match behavior {
        "building" => &BUILDING,
        "tree" => &TREE,
        "fx_explosion" => &POWDER,
        _ => &[],
    }
}

/// A grunt to hide at load: the mover, its trigger, whether he looks for targets while he waits (the host's
/// trigger_los), where he waits (the node's matrix, game space: the mover's origin), the host's instance id, and the
/// instance id of a roof jump's landing building.
#[derive(Clone, Debug)]
pub struct IcSpawn {
    pub mover: String,
    pub trigger: i64,
    pub trigger_los: bool,
    pub origin: Mat4,
    pub host: u32,
    pub landing: Option<u32>,
}

/// The grunts a host hides: each non-zero IC property whose node its reference anim has. `world` is the host's
/// game-space matrix, `node` a node's model-space matrix (game axes), `landing` the instance with a tag.
pub fn hidden_grunts(
    p: &crate::gamedata::Props,
    behavior: &str,
    host: u32,
    world: Mat4,
    node: impl Fn(&str) -> Option<Mat4>,
    landing: impl Fn(i64) -> Option<u32>,
) -> Vec<IcSpawn> {
    let trigger_los = p.i("trigger_los", 0) != 0;
    let lands_on = p.i("ic_building_tag", 0);
    sources(behavior)
        .iter()
        .filter_map(|s| {
            let trigger = p.i(s.prop, 0);
            if trigger == 0 {
                return None;
            }
            // (no node: fn_00456180 makes nothing)
            let m = node(s.node)?;
            Some(IcSpawn {
                mover: s.mover.to_owned(),
                trigger,
                trigger_los,
                origin: world * m,
                host,
                landing: (s.lands && lands_on != 0).then(|| landing(lands_on)).flatten(),
            })
        })
        .collect()
}

/// A hidden grunt: waiting at his node (holding his mover's first frame) or running it.
#[derive(Component, Clone, Debug)]
pub struct IcEntry {
    pub spawn: IcSpawn,
    pub running: bool,
}

/// What ends a move.
#[derive(Clone, Copy, PartialEq, Debug)]
enum End {
    /// state 0x65: upright, home here, state 1
    Ready,
    /// "ready_roof": a guard on the roof
    Roof,
}

#[derive(Resource, Default)]
pub struct IcStats {
    pub started: u32,
    pub ended: u32,
}

/// Waiting grunts hold their mover's first frame; the trigger or a wake-up starts the move.
#[allow(clippy::type_complexity)]
pub fn activate(
    mut commands: Commands,
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut q: Query<(Entity, &mut IcEntry, &mut Grunt, Option<&mut crate::skin::Animator>)>,
    hosts: Query<(&crate::level::InstanceId, Has<crate::level::Husk>, Option<&crate::fx::Destructible>)>,
    mut stats: ResMut<IcStats>,
) {
    let got: Vec<i64> = triggers.read().map(|t| t.0).filter(|t| *t != 0).collect();
    // an object by instance id is standing: present, not a husk, not dying
    let standing = |id: u32| hosts.iter().any(|(i, husk, d)| i.0 == id && !husk && d.is_none_or(|d| d.dying.is_none() && d.health > 0.0));
    for (e, mut ic, mut g, animator) in &mut q {
        if ic.running || g.mind == Mind::Dead {
            continue;
        }
        let woken = g.mind != Mind::WaitTrigger || got.contains(&ic.spawn.trigger);
        if !woken {
            // Anim_Set(mover, 0x90): set and held at its start
            if let Some(mut a) = animator {
                if a.layers.first().is_none_or(|l| l.anim != ic.spawn.mover) {
                    a.play(0, &ic.spawn.mover, false, crate::skin::Mask::All);
                    a.stop(1);
                }
                if let Some(l) = a.layers.get_mut(0) {
                    l.time = 0.0;
                    l.speed = 0.0;
                }
            }
            continue;
        }
        g.fixed = None;
        if !standing(ic.spawn.host) {
            // the host destroyed (vt+0x17c, guess): he just wakes where he is
            commands.entity(e).remove::<IcEntry>();
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam;
            }
            info!("EVENT ic: grunt {e:?} wakes without his move ({}: host {} gone)", ic.spawn.mover, ic.spawn.host);
            continue;
        }
        if ic.spawn.landing.is_some_and(|l| !standing(l)) {
            // the landing roof gone: 0x0042d0f0 (0, 1), a guard where he waits
            commands.entity(e).remove::<IcEntry>();
            g.guard = true;
            g.mind = Mind::Guard;
            info!("EVENT ic: grunt {e:?} guards (the roof of {} is gone)", ic.spawn.mover);
            continue;
        }
        ic.running = true;
        g.mind = Mind::Scripted;
        stats.started += 1;
        commands.entity(e).insert(Mover::new(&ic.spawn.mover, ic.spawn.origin));
        info!("EVENT ic: grunt {e:?} starts {}", ic.spawn.mover);
    }
}

/// A running move: the body follows the mover (physics off), the skeleton plays it; its end hands him back to the AI.
#[allow(clippy::type_complexity)]
pub fn run(
    mut commands: Commands,
    mut notes: MessageReader<MoverNote>,
    mut q: Query<(Entity, &IcEntry, &mut Grunt, &crate::physics::Body, &Transform, Option<&Mover>, Option<&mut crate::skin::Animator>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut stats: ResMut<IcStats>,
) {
    let mut roof: Vec<Entity> = Vec::new();
    let mut fall: Vec<Entity> = Vec::new();
    for n in notes.read() {
        match n.text.split_whitespace().next().unwrap_or("").to_lowercase().as_str() {
            "ready_roof" => roof.push(n.entity),
            "ragdoll" => fall.push(n.entity),
            _ => {}
        }
    }
    let k = physics.meters_per_unit;
    for (e, ic, mut g, body, tr, mover, animator) in &mut q {
        if !ic.running {
            continue;
        }
        let end = if g.mind == Mind::Dead {
            // killed during the move: it stops there (the death is ai.rs's)
            commands.entity(e).remove::<(Mover, IcEntry)>();
            continue;
        } else if roof.contains(&e) {
            Some(End::Roof)
        } else if fall.contains(&e) || mover.is_none_or(|m| m.ended) {
            Some(End::Ready)
        } else {
            None
        };
        let pos = tr.translation;
        match end {
            None => {
                // physics off (0x004577b0(0)): the body is put where the mover has him
                if let Ok(mut b) = physics.world.body_mut(body.id) {
                    let _ = b.set_position(crate::physics::jr((pos + body.offset) * k), oxijolt::prelude::Activation::Activate);
                    let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
                }
                // the IC anim on the whole skeleton at the mover's time ("anim_full", 0x0042c380)
                if let (Some(m), Some(mut a)) = (mover, animator) {
                    if a.layers.first().is_none_or(|l| l.anim != m.anim) {
                        a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
                        a.stop(1);
                    }
                    if let Some(l) = a.layers.get_mut(0) {
                        l.time = m.t;
                        l.speed = 0.0;
                    }
                }
            }
            Some(how) => {
                commands.entity(e).remove::<(Mover, IcEntry)>();
                stats.ended += 1;
                // upright (0x00454c90), facing as he ends
                g.yaw = yaw_of(tr.rotation);
                g.home = pos;
                g.goal = pos;
                if how == End::Roof {
                    g.guard = true;
                    g.mind = Mind::Guard;
                } else {
                    g.mind = Mind::Roam;
                }
                if let Ok(mut b) = physics.world.body_mut(body.id) {
                    let _ = b.set_position(crate::physics::jr((pos + body.offset) * k), oxijolt::prelude::Activation::Activate);
                    let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
                }
                info!("EVENT ic: grunt {e:?} ends {} ({how:?})", ic.spawn.mover);
            }
        }
    }
}

/// The playtest state's `ic`: grunts hidden at load, moves started and ended, grunts still waiting.
pub fn log(q: Query<&IcEntry>, stats: Res<IcStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let waiting = q.iter().filter(|i| !i.running).count();
    let running = q.iter().filter(|i| i.running).count();
    state.0.insert("ic".into(), serde_json::json!({"started": stats.started, "ended": stats.ended, "waiting": waiting, "running": running}));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> crate::gamedata::Props {
        crate::gamedata::Props(v.as_object().cloned().unwrap_or_default())
    }

    /// 0x0040ad80: each non-zero IC property with its node makes one grunt at world * node; a roof jump takes the
    /// IC_building_tag's building as its landing; no node, no grunt.
    #[test]
    fn a_building_hides_one_grunt_per_ic_property() {
        // lev_1_m1_b 187: ic_climb_v2f_1 1313, ic_door_1 1, trigger_los 1
        let p = props(serde_json::json!({ "ic_climb_v2f_1": 1313, "ic_door_1": 1, "trigger_los": 1, "ic_window_dive_low_1": 0 }));
        let world = Mat4::from_translation(Vec3::new(100.0, 0.0, 0.0));
        let node = |n: &str| (n != "ref_grunt_door_2").then(|| Mat4::from_translation(Vec3::new(0.0, 5.0, 0.0)));
        let g = hidden_grunts(&p, "building", 187, world, node, |_| None);
        assert_eq!(g.len(), 2);
        assert_eq!((g[0].mover.as_str(), g[0].trigger), ("mover_grunt_climb_v2f_1", 1313));
        assert_eq!((g[1].mover.as_str(), g[1].trigger), ("mover_grunt_door_1", 1));
        assert!(g.iter().all(|s| s.trigger_los && s.host == 187 && s.landing.is_none()));
        assert_eq!(g[0].origin.w_axis.truncate(), Vec3::new(100.0, 5.0, 0.0));
        // lev_1_m1_b 380: a roof jump onto the building of tag 704
        let p = props(serde_json::json!({ "ic_roof_jump_v2f_v1f_1": 703, "ic_building_tag": 704 }));
        let g = hidden_grunts(&p, "building", 380, Mat4::IDENTITY, |_| Some(Mat4::IDENTITY), |t| (t == 704).then_some(99));
        assert_eq!((g[0].mover.as_str(), g[0].landing), ("mover_grunt_roof_jump_2_to_1", Some(99)));
        // a tree's slide, a node missing
        let p = props(serde_json::json!({ "ic_tree_slide_2": 99 }));
        assert_eq!(hidden_grunts(&p, "tree", 47, Mat4::IDENTITY, |_| Some(Mat4::IDENTITY), |_| None)[0].mover, "mover_grunt_tree_2");
        assert!(hidden_grunts(&p, "tree", 47, Mat4::IDENTITY, |_| None, |_| None).is_empty());
        assert!(hidden_grunts(&p, "pub", 1, Mat4::IDENTITY, |_| Some(Mat4::IDENTITY), |_| None).is_empty());
    }
}

/// The AI's heading of an engine-space rotation (as level.rs reads a placed grunt's).
pub fn yaw_of(rotation: Quat) -> f32 {
    (rotation * crate::level::game_to_engine().inverse()).to_euler(EulerRot::YXZ).0
}
