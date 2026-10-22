//! IC entries: grunts hidden in the scenery that come out by a scripted move (an "IC" mover anim) when their trigger
//! arrives. A host object makes them at load, one grunt_regular per non-zero IC property, at the property's reference
//! node (fn_00456180: ObjectSystem_CreateObject, the node's matrix, 0x0042d000: waiting in state 0x8c with the mover's
//! name at +0x4cc, its first frame held (Anim_Set 0x00408d90, flags 0x90), the trigger +0x52c, trigger_los +0x488 bit 6).
//! Hosts (their load functions and tables): buildings (TwigletGenerator_Slot84 0x0040ad80: doors, window dives, wall
//! climbs, roof jumps, wall jumps), trees (0x00496e90: the tree slides) and powder kegs (0x0041c180).
//!
//! - The trigger, or anything that wakes the grunt (a target seen with trigger_los, a hit), starts the move
//!   (0x0042f5c0's IC branch, mode 0x8000000): the host destroyed: no move, GoRagdoll with no record, quiet
//!   (vt+0x17c(0, 1): he goes limp where he is); the landing building of a roof jump (IC_building_tag) destroyed: he
//!   guards where he is (0x0042d0f0), the host his start object (+0x880); else the mover from
//!   where he waits (GenericAI_PlayICAnim 0x0042c410: physics off, state 0x64). The move is a [`Mover`]: its root track
//!   carries him, "anim_full" plays it on his skeleton, its fx / trigger notes are the generic ones (mover.rs).
//! - On the way his body breaks the village windows it passes (the dives: the original moves him with a velocity,
//!   0x00458510 in state 0x64, so his body hits the pane, a DefaultGameObject with physics on, which dies of the
//!   impact: oracle lev_5_m1 / lev_1_m1_b, a window's death and imp_turret_glass_break ~0.5 s into the dive; here a
//!   pane within reach of his body dies: guess at the impact rule).
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

/// How near a pane's origin his body's middle comes to break it (his capsule's radius and half a pane: guess).
const WINDOW_REACH: f32 = 8.0;

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
    /// knocked away by a host or landing destroyed
    pub knocked: u32,
    /// grunts a dying building took down with it (`building_deaths`)
    pub downed: u32,
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
            // the host destroyed: GoRagdoll with no damage record, quiet (0x0042f5c0's IC branch, game_02.cpp:1192-1196:
            // vt+0x17c(0, 1)): he dies limp where he waits (`knocks`)
            commands.entity(e).remove::<IcEntry>().insert(KnockAway { from: KnockFrom::Woken });
            info!("EVENT ic: grunt {e:?} goes down without his move ({}: host {} gone)", ic.spawn.mover, ic.spawn.host);
            continue;
        }
        if ic.spawn.landing.is_some_and(|l| !standing(l)) {
            // the landing roof gone: 0x0042d0f0 (0, 1), a guard where he waits; the host becomes his start object
            // (+0x880 = +0xadc, game_02.cpp:1198-1212): its later death knocks him out (`knocks`)
            commands.entity(e).remove::<IcEntry>().insert(StartObject { id: ic.spawn.host });
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
    mut windows: Query<(&GlobalTransform, &mut crate::fx::Destructible), With<crate::level::Window>>,
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
                // the panes his body passes through (within his capsule round its middle)
                let middle = pos + body.offset;
                for (wt, mut d) in &mut windows {
                    if d.health > 0.0 && d.dying.is_none() && wt.translation().distance(middle) < WINDOW_REACH {
                        crate::vehicles::kill(&mut d);
                        info!("EVENT ic: grunt {e:?} breaks a window");
                    }
                }
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
                    // his landing building stays his (+0xae0): its fall knocks him away (`knocks`)
                    if let Some(l) = ic.spawn.landing {
                        commands.entity(e).insert(IcAnchor { landing: l });
                    }
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

/// A grunt to be knocked off what held him: GoRagdoll (vt+0x17c) called straight from GenericAI_Update's prologue
/// (0x0043ee90, ai_06.cpp:279-300, 354-372) or the wake-up (0x0042f5c0), not through Character_Die.
#[derive(Component, Clone, Copy, Debug)]
pub struct KnockAway {
    pub from: KnockFrom,
}

/// Which link knocked him, and so GoRagdoll's record and quiet flag.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum KnockFrom {
    /// +0xae0, his landing building, destroyed (0x00425be0 with a0 = 0): the 0x83 record away from it, not quiet
    Landing(u32),
    /// +0xadc, the host of the IC move he is running, destroyed (0x00425be0 with a0 = 1): the 0x83 record away from
    /// it, quiet
    Host(u32),
    /// +0x880, the window / tower / roof object he holds, gone (ai_06.cpp:279-300): the 0x83 record along his facing,
    /// not quiet
    Start,
    /// an IC grunt woken with his host gone (0x0042f5c0: vt+0x17c(0, 1)): no record, quiet
    Woken,
}

/// The object a grunt holds (+0x880): a window / tower grunt's house (AI_ResolveStartObjects 0x00425e20, obj_tag), the
/// host of an IC grunt whose roof landing is gone (game_02.cpp:1198-1212). Its death knocks him out (`knocks`).
#[derive(Component, Clone, Copy, Debug)]
pub struct StartObject {
    /// its instance id
    pub id: u32,
}

/// The damage record of a knock's GoRagdoll (Character_GoRagdoll 0x00431310), for ai.rs `die`.
#[derive(Component, Clone, Copy, Debug)]
pub struct Knocked {
    /// the record's direction x amount x 0.15 on his 80 kg (units / s); zero without a record
    pub impulse: Vec3,
    /// the record's amount +0x24 (0 without a record)
    pub amount: f32,
    /// a0 = 1: no launch, no count of the bodies down, no impact branch
    pub quiet: bool,
    /// the record's attacker +0x2c (Game+0x748: the player), engine space; None without a record or a player
    pub attacker: Option<Vec3>,
}

/// What GoRagdoll does with a knock's record.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum RagdollGo {
    /// Character_Launch from the attacker at rand 2.5..3 x his move_speed, turned within `spread` degrees
    Launch { spread: f32 },
    /// the impact branch (amount over 10050): thrown with this velocity, flailing (anim 0x49 + rand, state 0x195)
    Thrown(Vec3),
    /// the ragdoll with this impulse; `limited`: only while at most the ragdoll limit of bodies are down (else the
    /// animated death 0x00427100)
    Ragdoll { impulse: Vec3, limited: bool },
}

/// GoRagdoll's impact threshold (K 0x005bcf34).
const IMPACT: f32 = 10050.0;

/// Character_GoRagdoll 0x00431310 (physics_01.cpp:3541-3630) for a knock: not quiet, with an attacker and at least 10 s
/// since the last launch (`launch_allowed`, Game+0xb34) the launch; else, not quiet with an attacker and an amount over
/// 10050, the impact branch: at the very time of a launch (`launched_now`) 25 % (rand x 100 >> 15 < 25: `roll`) a
/// launch spread 45 degrees, else the impulse x rand 2..5 (`scale`); otherwise the impulse alone. Quiet or without a
/// record: the ragdoll, whatever the count of bodies down (quiet skips the count).
pub fn go_ragdoll(k: &Knocked, launch_allowed: bool, launched_now: bool, roll: f32, scale: f32) -> RagdollGo {
    let loud = !k.quiet && k.attacker.is_some();
    if loud && launch_allowed {
        return RagdollGo::Launch { spread: 0.0 };
    }
    if loud && k.amount > IMPACT {
        if launched_now {
            if roll < 0.25 {
                return RagdollGo::Launch { spread: 45.0 };
            }
            return RagdollGo::Thrown(k.impulse * (2.0 + 3.0 * scale));
        }
        return RagdollGo::Thrown(k.impulse);
    }
    RagdollGo::Ragdoll { impulse: k.impulse, limited: !k.quiet }
}

/// A grunt that came out onto a building's roof (the landing +0xae0 held after his move).
#[derive(Component, Clone, Copy, Debug)]
pub struct IcAnchor {
    pub landing: u32,
}

/// The 0x83 record of a knock-away (0x00425be0, ai_01.cpp:2602-2640; the same in GenericAI_Update's +0x880 prologue,
/// ai_06.cpp:279-300): `across` (from the other object to him, or his facing) flat, 0.7..0.9 up (rand() x 6.1e-6 + 0.7,
/// K 0x005bcdf4 / 0x005bcd48), normalised; the amount 21666.67 + rand() x 0.661 (K 0x005bcdec / 0x005bcdf0).
/// Returns GoRagdoll's impulse, direction x amount x 0.15 on his 80 kg (units / s), and the amount.
pub fn knock_record(across: Vec3, r1: f32, r2: f32) -> (Vec3, f32) {
    let across = across.with_y(0.0).normalize_or_zero();
    let up = 0.7 + r1 * 32768.0 * 6.1e-6;
    let dir = (across + Vec3::Y * up).normalize_or(Vec3::Y);
    let amount = 21666.67 + r2 * 32768.0 * 0.661;
    (dir * amount * 0.15 / 80.0, amount)
}

/// The per-frame checks of GenericAI_Update's prologue, in its order: a grunt whose start object (+0x880) is gone is
/// knocked out along his facing (ai_06.cpp:279-300); one whose landing building (+0xae0) is destroyed is knocked away
/// from it (ai_06.cpp:354-364: 0x00425be0 with a0 = 0); one running an IC move whose host (+0xadc) is destroyed is knocked
/// away from it quietly (ai_06.cpp:366-371: a0 = 1); and the knock-aways asked for (`KnockAway`). Each is woken
/// (vt+0x1ac), loses his physics hold (0x004577b0(0)) and dies by GoRagdoll with the record (`Knocked`, ai.rs `die`).
/// (The original skips the landing's knock while the grunt is in state 0x29, the run along a route to a cover slot's
/// point after 0x28: no such state here, so it is not reproduced.)
#[allow(clippy::type_complexity)]
pub fn knocks(
    mut commands: Commands,
    mut q: Query<(Entity, &mut Grunt, &GlobalTransform, &mut crate::fx::Health, Option<&IcEntry>, Option<&IcAnchor>, Option<&KnockAway>, Option<&StartObject>)>,
    hosts: Query<(&crate::level::InstanceId, &GlobalTransform, Has<crate::level::Husk>, Option<&crate::fx::Destructible>)>,
    player: Query<&GlobalTransform, With<super::Player>>,
    mut rng: ResMut<crate::fx::FxStats>,
    mut stats: ResMut<IcStats>,
) {
    let standing = |id: u32| hosts.iter().any(|(i, _, husk, d)| i.0 == id && !husk && d.is_none_or(|d| d.dying.is_none() && d.health > 0.0));
    let at = |id: u32| hosts.iter().find(|(i, ..)| i.0 == id).map(|(_, t, ..)| t.translation());
    // the record's attacker: Game+0x748, the player
    let attacker = player.iter().next().map(|t| t.translation());
    for (e, mut g, gt, mut health, entry, anchor, asked, start) in &mut q {
        if g.mind == Mind::Dead {
            continue;
        }
        let landing = entry.and_then(|i| i.spawn.landing).or(anchor.map(|a| a.landing));
        let from = if let Some(a) = asked {
            a.from
        } else if start.is_some_and(|s| !standing(s.id)) {
            KnockFrom::Start
        } else if let Some(l) = landing.filter(|&l| !standing(l)) {
            KnockFrom::Landing(l)
        } else if let Some(i) = entry.filter(|i| i.running && !standing(i.spawn.host)) {
            KnockFrom::Host(i.spawn.host)
        } else {
            continue;
        };
        commands.entity(e).remove::<(IcEntry, IcAnchor, KnockAway, Mover, StartObject)>();
        let me = gt.translation();
        let across = match from {
            KnockFrom::Landing(id) | KnockFrom::Host(id) => Some(me - at(id).unwrap_or(me)),
            // 0x00453150: his facing
            KnockFrom::Start => Some(Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos())),
            KnockFrom::Woken => None,
        };
        let knocked = match across {
            Some(across) => {
                let (impulse, amount) = knock_record(across, rng.rand(), rng.rand());
                Knocked { impulse, amount, quiet: matches!(from, KnockFrom::Host(_)), attacker }
            }
            None => Knocked { impulse: Vec3::ZERO, amount: 0.0, quiet: true, attacker: None },
        };
        commands.entity(e).insert(knocked);
        g.fixed = None;
        g.last_hit = f32::MAX;
        health.0 = 0.0;
        stats.knocked += 1;
        info!("EVENT ic: grunt {e:?} knocked ({from:?}, {:.0} / s)", knocked.impulse.length());
    }
}

/// How high above the landscape a character's feet must be, within a dying building's bounds, to count as on it.
const ON_BUILDING: f32 = 3.0;

/// A destroyed building takes the characters up on it with it: each live grunt within its bounding sphere whose feet
/// are more than [`ON_BUILDING`] above the landscape (on its roof or floors, in a window, a tower grunt held at its
/// bone) is knocked away from it as GenericAI_Update knocks a grunt whose landing (+0xae0) or scripted host (+0xadc)
/// died (0x00425be0: he dies and flies). The original does this for those links only; a grunt who walked or climbed
/// up there without one stood on the ruin's old roof height until something moved him (the player, 2026-10-18: "any
/// grunt standing on an exploded house must die with it"; a grunt left standing on air). Grunts linked to an object (+0x880 / +0xae0 / +0xadc) are left to their link
/// (`knocks`); the others are knocked as by a landing (not quiet). The bodies round it (corpses,
/// debris, sleeping on what it was) are woken, so they fall onto the ruin.
#[allow(clippy::type_complexity)]
pub fn building_deaths(
    mut commands: Commands,
    mut died: MessageReader<crate::fx::Died>,
    buildings: Query<(&crate::level::InstanceId, &GlobalTransform, &crate::fx::Bounds), With<crate::level::Placed>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform), (Without<KnockAway>, Without<IcEntry>, Without<IcAnchor>, Without<StartObject>)>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut stats: ResMut<IcStats>,
) {
    for d in died.read() {
        let Ok((id, t, b)) = buildings.get(d.0) else { continue };
        let centre = t.transform_point(b.center);
        let reach = b.radius * t.scale().max_element();
        for (e, g, gt) in &grunts {
            let feet = gt.translation();
            if g.mind == Mind::Dead || feet.distance(centre) > reach {
                continue;
            }
            let ground = physics.raycast_landscape(feet + Vec3::Y, Vec3::NEG_Y, 1000.0);
            if ground.is_some_and(|gr| feet.y - gr.y > ON_BUILDING) {
                commands.entity(e).insert(KnockAway { from: KnockFrom::Landing(id.0) });
                stats.downed += 1;
                info!("EVENT ic: grunt {e:?} on building {} goes down with it", id.0);
            }
        }
        // wake what rests round it (Jolt does not wake a sleeping body when the static under it goes)
        let k = physics.meters_per_unit;
        let near: Vec<_> = physics.bodies.keys().copied().collect();
        for body in near {
            let close = physics.world.body(body).is_ok_and(|bd| (crate::physics::br(bd.position()) / k).distance(centre) <= reach + 10.0);
            if close {
                if let Ok(mut bd) = physics.world.body_mut(body) {
                    bd.activate();
                }
            }
        }
    }
}

/// The playtest state's `hidden`: grunts hidden at load, moves started and ended, grunts still waiting.
pub fn log(q: Query<&IcEntry>, stats: Res<IcStats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let waiting = q.iter().filter(|i| !i.running).count();
    let running = q.iter().filter(|i| i.running).count();
    // (not "ic": icobject.rs's)
    state.0.insert("hidden".into(), serde_json::json!({"started": stats.started, "ended": stats.ended, "waiting": waiting, "running": running, "knocked": stats.knocked, "downed": stats.downed}));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> crate::gamedata::Props {
        crate::gamedata::Props(v.as_object().cloned().unwrap_or_default())
    }

    /// Character_GoRagdoll 0x00431310 on a knock's record: a landing's (not quiet) launches once 10 s have passed, else
    /// is thrown (its amount is always over 10050); a host's (quiet) and the wake-up's (no record) ragdoll whatever
    /// the bodies down.
    #[test]
    fn knocks_go_ragdoll() {
        let (impulse, amount) = knock_record(Vec3::X, 0.5, 0.5);
        assert!(amount > IMPACT && impulse.x > 0.0 && impulse.y > 0.0);
        let landing = Knocked { impulse, amount, quiet: false, attacker: Some(Vec3::ZERO) };
        assert_eq!(go_ragdoll(&landing, true, false, 0.9, 0.0), RagdollGo::Launch { spread: 0.0 });
        assert_eq!(go_ragdoll(&landing, false, false, 0.9, 0.0), RagdollGo::Thrown(impulse));
        assert_eq!(go_ragdoll(&landing, false, true, 0.1, 0.0), RagdollGo::Launch { spread: 45.0 });
        assert_eq!(go_ragdoll(&landing, false, true, 0.9, 1.0), RagdollGo::Thrown(impulse * 5.0));
        let host = Knocked { quiet: true, ..landing };
        assert_eq!(go_ragdoll(&host, true, false, 0.0, 0.0), RagdollGo::Ragdoll { impulse, limited: false });
        let woken = Knocked { impulse: Vec3::ZERO, amount: 0.0, quiet: true, attacker: None };
        assert_eq!(go_ragdoll(&woken, true, true, 0.0, 0.0), RagdollGo::Ragdoll { impulse: Vec3::ZERO, limited: false });
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
