//! Which animation each Armed and Dangerous character plays (names from the anim bank and the template slots in
//! anims/index.json). Roman's anims come in bottom / top halves (`_b` drives the legs, `_t` the body above
//! "belly"); grunts, Q and Jonesy play whole-body anims. The original picks grunt anims by id through
//! vt+0x1a4 (docs/subsystems/grunt-ai.md 11); the state -> anim pairs here follow the states there.

use crate::ai::{Companion, Grunt, Mind};
use crate::player::Roman;
use crate::skin::{Animator, Mask};
use bevy::prelude::*;

pub struct CharAnimPlugin;

impl Plugin for CharAnimPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Twist>().add_systems(Update, (roman, grunts, companions, penguins));
        app.add_systems(PostUpdate, twist.after(crate::skin::AnimateSet).before(bevy::transform::TransformSystems::Propagate));
    }
}

/// Roman's standing torso twist and turn in place. The player's bone callbacks (PlayerObject_Load binds table
/// 0x005fb760: 0x004735f0 to "center" kind 0 and "belly" kind 1, 0x00472cc0 to "l_foot") read the look-at record
/// actor+0x68c, which 0x00475ff0 rebuilds at each mode change: anim-table kinds 2 (mode 0, the aim stance) and 3
/// (modes 0x1c / 0x1d, Trotter_turn_left / right) set flag 2 (kind 3 also flag 8), with the limit +0x24 =
/// normal_twist_limit (+0x560); the stored direction +8 (the legs' facing) is the previous record's for kinds
/// 1/2/3/5/8 and the actor's facing (0x00453150) otherwise (the fidgets are kind 12: no flag, no twist). Each frame
/// 0x004735f0 takes the angle between the facing and +8, drags +8 to within the limit (less 0.001) and, outside the
/// turn modes and the record's first frame, raises +0xa34 0x2000 with +0x980 = (+8 x facing).z; flag 8 scales the
/// limit by the anim's progress left, so the legs reach the facing as the turn anim ends. "center" turns by the
/// angle (the legs keep their facing), "belly" by minus it (the torso follows the view); actor+0x978 / +0x97c are
/// 0 for the player (game_05.cpp:831, no other writer found). The anim update 0x00476a10 (game_05.cpp:1915) plays
/// mode 0x1c when +0x980 >= 0, else 0x1d, standing, until the anim ends.
#[derive(Resource, Default)]
pub struct Twist {
    /// the stance's record has flag 2 this frame (the aim stance or a turn anim)
    pub active: bool,
    /// the legs' facing (record +8) as a yaw; None: the body's
    pub leg_yaw: Option<f32>,
    /// the torso's twist from the legs (record +0x28; + = the view left of the legs)
    pub angle: f32,
    /// the limit hit (+0xa34 0x2000) and its side (+0x980 > 0: left), for the next frame's anim update
    pub raised: Option<bool>,
    /// the turn anim playing: (left, seconds into it)
    pub turn: Option<(bool, f32)>,
    /// the record's first frame (record byte 0): no 0x2000
    pub fresh: bool,
    /// turn anims started (playtest evidence)
    pub turns: u32,
}

/// Trotter_turn_left / right_aim_b / _t: 29 frames at 30 fps
const TURN_LEN: f32 = 0.9667;

fn wrap(a: f32) -> f32 {
    (a + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI
}

/// The bone callback 0x004735f0 (flag 2) on Roman's "center" and "belly" after the anims set the joints.
fn twist(mut tw: ResMut<Twist>, q: Query<(&Roman, &Animator)>, mut joints: Query<&mut Transform>, mut state: Option<ResMut<crate::playtest::PlayState>>) {
    for (r, a) in &q {
        if !tw.active || r.dead {
            tw.leg_yaw = None;
            tw.angle = 0.0;
            tw.raised = None;
            tw.fresh = true;
        } else {
            // flag 8 (the turn modes): the limit times the anim's progress left (end - t) / (end - start)
            let lim = r.twist_limit * tw.turn.map_or(1.0, |(_, t)| (1.0 - t / TURN_LEN).max(0.0));
            let leg = *tw.leg_yaw.get_or_insert(r.yaw);
            let mut ang = wrap(r.yaw - leg);
            if ang.abs() > lim {
                ang -= (ang.abs() - lim + 0.001) * ang.signum();
                tw.leg_yaw = Some(r.yaw - ang);
                if !tw.fresh && tw.turn.is_none() {
                    tw.raised = Some(ang > 0.0);
                }
            }
            tw.fresh = false;
            tw.angle = ang;
            // the world's up in the model's space (the rig's parent turns by yaw + pi about it); "center" turns by
            // -angle about it (legs at leg_yaw), "belly" back by +angle about the same axis in center's frame
            let up = r.base.inverse() * Vec3::Y;
            let idx = |n: &str| a.skeleton.index.get(n).and_then(|&i| a.joints.get(i).copied());
            if let (Some(c), Some(b)) = (idx("center"), idx("belly")) {
                let mut crot = Quat::IDENTITY;
                if let Ok(mut jt) = joints.get_mut(c) {
                    jt.rotation = Quat::from_axis_angle(up, -ang) * jt.rotation;
                    crot = jt.rotation;
                }
                if let Ok(mut jt) = joints.get_mut(b) {
                    jt.rotation = Quat::from_axis_angle((crot.inverse() * up).normalize(), ang) * jt.rotation;
                }
            }
        }
        if let Some(s) = state.as_mut() {
            let d = |v: f32| (wrap(v).to_degrees() * 10.0).round() / 10.0;
            s.0.insert("twist_deg".into(), d(tw.angle).into());
            s.0.insert("yaw_deg".into(), d(r.yaw).into());
            s.0.insert("leg_yaw_deg".into(), d(tw.leg_yaw.unwrap_or(r.yaw)).into());
            s.0.insert("turn_anim".into(), tw.turn.map_or("", |(l, _)| if l { "left" } else { "right" }).into());
            s.0.insert("turns".into(), tw.turns.into());
        }
        break;
    }
}

/// Roman's idle (0x00476a10, game_05.cpp:1932-1948): standing, the idle time +0xc04 counts; past 5 s (0x005bcbdc),
/// each time the playing anim ends (the aim stance's 1.97 s loop or a fidget) a rand picks the stance (one half) or
/// one of trotter_idle_1..5 (mode 1 + rand * 5 >> 15); any other mode clears the time.
#[derive(Default)]
struct Idle {
    t: f32,
    left: f32,
    fidget: Option<u32>,
}

fn roman(mut q: Query<(&Roman, &mut Animator)>, topsy: Res<crate::topsy::Topsy>, time: Res<Time>, mut idle: Local<Idle>, mut rng: ResMut<crate::fx::FxStats>, mut tw: ResMut<Twist>) {
    for (r, mut a) in &mut q {
        // the twist's record flag 2 (set below for the aim stance and the turn anims)
        tw.active = false;
        // the Topsy Turvy states (anims 0x20..0x23)
        let t = match topsy.phase {
            crate::topsy::Phase::Into => Some(("trotter_topsy_into", false)),
            crate::topsy::Phase::Turn | crate::topsy::Phase::Back => Some(("trotter_topsy_world_turn", false)),
            crate::topsy::Phase::Hang => Some(("trotter_topsy_hanging", true)),
            crate::topsy::Phase::Off => None,
        };
        if let (Some((name, looping)), false) = (t, r.dead) {
            a.play(0, name, looping, Mask::All);
            a.stop(1);
            continue;
        }
        let speed = r.move_local.length();
        let (fwd, side) = (r.move_local.y, r.move_local.x);
        // locomotion: aim stance standing; walk / run (run animations from 12 units/s: anim_walk_run_switch)
        let gait = if !r.on_ground {
            "trotter_jump_idle"
        } else if speed < 3.0 {
            "trotter_aim"
        } else {
            let kind = if speed < 12.0 { "walk" } else { "run" };
            let dir = if side.abs() > fwd.abs() * 1.2 { if side > 0.0 { "_right" } else { "_left" } } else { "" };
            match (kind, dir) {
                ("walk", d) => return_name(&format!("trotter_walk{d}_aim")),
                (_, d) => return_name(&format!("trotter_run{d}_aim")),
            }
        };
        let gait = gait.to_string();
        // the upper body fires / reloads over the same gait
        let upper = if r.wchange.is_some() {
            // the weapon-change anim over the gait (trotter_wchange_t, trotter_run_wchange_t, ...)
            gait.replace("_aim", "_wchange_t").replace("trotter_wchange_t", "trotter_wchange_t").replace("jump_idle", "jump_idle_wchange_t")
        } else if r.reloading > 0.0 {
            gait.replace("_aim", "_reload").replace("trotter_aim", "trotter_reload").replace("jump_idle", "jump_idle_reload")
        } else if r.since_fire < 0.3 {
            // the fire state (game_05.cpp:1380): fire_sg for the rifle and the machine gun (weapon index 0..2), the
            // fire anims' length 0.3 s
            if gait == "trotter_aim" { "trotter_fire_sg".to_string() } else { gait.replace("_aim", "_fire").replace("jump_idle", "jump_fire_sg") }
        } else {
            gait.clone()
        };
        if r.dead {
            // the ragdoll drives the bones (ragdoll.rs); without one, the fall pose
            if !r.ragdolled {
                a.play(0, "trotter_fall_loop", true, Mask::All);
                a.stop(1);
            }
            continue;
        }
        // `_b` / `_t` are the bottom and top of an aim-pitch blend (as the grunts' fire_bl / tl): the rifle points
        // ~50 degrees down in `_b` and ~47 up in `_t` (measured on the skeleton, docs/remaster-engine.md), legs alike.
        // The pose is the blend whose rifle follows the aim pitch: w = (pitch + 50) / 97 in degrees, with the
        // aim pitch = the view's (rest -8 degrees: w 0.43, a level rifle as in the original's captures).
        // The exact mapping in the player's anim code (0x00475ff0 / the blend +0x580) is not traced: derived.
        // the sticky bomb throw: a whole-body anim, no aim-pitch pair (trotter_throw 0.633 s; running
        // trotter_run_throw 0.767 s)
        // the hit reaction (modes 0x1e / 0x1f: Trotter_hit_small / _med, full body, once)
        if r.hit_anim > 0.0 {
            a.play(0, if r.hit_med { "trotter_hit_med" } else { "trotter_hit_small" }, false, Mask::All);
            a.stop(1);
            continue;
        }
        // landing (modes 8 land_hard / 0x13 jump_land, whole body, once); falling (mode 7 fall_loop)
        if let Some((n, _, _)) = r.landing {
            a.play(0, n, false, Mask::All);
            a.stop(1);
            continue;
        }
        if r.falling && !r.on_ground && !r.jumping {
            a.play(0, "trotter_fall_loop", true, Mask::All);
            a.stop(1);
            continue;
        }
        let (throw, dur) = if speed < 3.0 { ("trotter_throw", 0.633) } else { ("trotter_run_throw", 0.767) };
        if r.since_throw < dur {
            a.play(0, throw, false, Mask::All);
            a.stop(1);
            continue;
        }
        // the aim blend (0x00475ff0, game_05.cpp:1499): w = (clamp(2 x pitch, -1, 1) + 1) / 2 of the look pitch, 0 at
        // rest (the engine's rest pitch is -0.15)
        let w = ((2.0 * (r.pitch + 0.15)).clamp(-1.0, 1.0) + 1.0) * 0.5;
        // turning in place (0x00476a10, game_05.cpp:1915): standing with the twist limit hit last frame, the turn
        // anim (mode 0x1c Trotter_turn_left / 0x1d _right; state 1 the _fire pair, other states link to _aim) plays
        // once, the legs catching up (Twist); any mode outside 0..5 clears the idle time
        let stance = gait == "trotter_aim" && r.on_ground && !r.dead;
        if !stance {
            tw.turn = None;
            tw.raised = None;
        } else if tw.turn.is_none() {
            if let Some(left) = tw.raised.take() {
                tw.turn = Some((left, 0.0));
                tw.fresh = true;
                tw.turns += 1;
            }
        }
        if let Some((left, t)) = tw.turn {
            *idle = Idle::default();
            let name = format!("trotter_turn_{}_{}", if left { "left" } else { "right" }, if r.since_fire < 0.3 { "fire" } else { "aim" });
            a.play(0, &format!("{name}_b"), false, Mask::All);
            a.play(1, &format!("{name}_t"), false, Mask::All);
            a.layers[0].time = t;
            a.set_weight(1, w);
            a.sync(1, 0);
            tw.active = true;
            let t = t + time.delta_secs();
            if t >= TURN_LEN {
                // the anim ended: +0xa34 0x2000 cleared, the aim stance's record from this one (the legs kept)
                tw.turn = None;
                tw.fresh = true;
            } else {
                tw.turn = Some((left, t));
            }
            continue;
        }
        if upper == "trotter_aim" && r.on_ground && !r.dead {
            let dt = time.delta_secs();
            idle.t += dt;
            idle.left -= dt;
            if idle.left <= 0.0 {
                idle.fidget = None;
                idle.left = 1.967;
                if idle.t > 5.0 && rng.rand() < 0.5 {
                    let k = ((rng.rand() * 5.0) as u32).min(4) + 1;
                    idle.fidget = Some(k);
                    idle.left = [3.3, 5.467, 4.8, 5.467, 3.3][k as usize - 1];
                }
            }
            if let Some(k) = idle.fidget {
                a.play(0, &format!("trotter_idle_{k}"), false, Mask::All);
                a.stop(1);
                continue;
            }
        } else {
            *idle = Idle::default();
        }
        a.play(0, &format!("{upper}_b"), true, Mask::All);
        a.play(1, &format!("{upper}_t"), true, Mask::All);
        a.set_weight(1, w);
        a.sync(1, 0);
        // the aim stance (mode 0, kind 2) twists; the fidgets (kind 12) and the moving modes do not
        tw.active = stance;
    }
}

fn return_name(s: &str) -> &'static str {
    // leak-free mapping of the few names used (avoids allocating per frame)
    match s {
        "trotter_walk_aim" => "trotter_walk_aim",
        "trotter_walk_left_aim" => "trotter_walk_left_aim",
        "trotter_walk_right_aim" => "trotter_walk_right_aim",
        "trotter_run_aim" => "trotter_run_aim",
        "trotter_run_left_aim" => "trotter_run_left_aim",
        "trotter_run_right_aim" => "trotter_run_right_aim",
        _ => "trotter_aim",
    }
}

#[allow(clippy::type_complexity)]
fn grunts(
    mut q: Query<(Entity, &mut Grunt, &mut Animator, &Transform, Option<&crate::topsy::Thrown>), Without<crate::ragdoll::Ragdoll>>,
    targets: Query<&GlobalTransform>,
    time: Res<Time>,
    lib: Res<crate::skin::AnimLibrary>,
    mut fidgets: Local<std::collections::HashMap<Entity, (f32, Option<String>)>>,
    mut seed: Local<u32>,
) {
    let dt = time.delta_secs();
    let mut rand = || {
        // MSVC rand(): 0..32767
        *seed = seed.wrapping_mul(214013).wrapping_add(2531011);
        (*seed >> 16) & 0x7fff
    };
    for (e, mut g, mut a, t, thrown) in &mut q {
        // a hit reaction: its anim once, then the state's anims again
        if g.mind != Mind::Dead {
            if let Some((name, _)) = g.hit.clone() {
                let playing = a.layers.first().is_some_and(|l| l.anim == name);
                if playing && a.finished(0, &lib) {
                    g.hit = None;
                } else {
                    if !playing {
                        a.play(0, &name, false, Mask::All);
                        a.layers[0].time = 0.0;
                    }
                    a.stop(1);
                    continue;
                }
            }
        } else {
            g.hit = None;
        }
        // thrown by the Topsy Turvy bomb: hanging on, or falling into the sky
        if let Some(th) = thrown {
            let (name, looping) = match (th.hangs, th.t < 1.0, th.t < 0.3) {
                (true, true, _) => ("grunt_hang_into", false),
                (true, false, _) => ("grunt_hanging", true),
                (false, _, true) => ("grunt_blown_up_into", false),
                (false, _, false) => ("grunt_blown_up", true),
            };
            a.play(0, name, looping, Mask::All);
            a.stop(1);
            continue;
        }
        // turning toward the target before firing (0x0042e050)
        if let (Some((name, _)), Mind::Fire) = (g.turn, g.mind) {
            a.play(0, name, false, Mask::All);
            a.stop(1);
            continue;
        }
        let limping = g.limp_pct > 0.0 && g.last_health < g.max_health * g.limp_pct / 100.0;
        let (name, looping) = match g.mind {
            Mind::Dead if g.ragdoll => ("grunt_blown_up".to_string(), false),
            Mind::Dead => (g.death_name.map(str::to_owned).unwrap_or_else(|| format!("grunt_die_{}", g.death_anim)), false),
            Mind::Fire => {
                // the four aim poses blended by the aim weights (fn_00428fc0, PlayAnim4 vt+0x1a0 with ids
                // 0x23/0x24/0x21/0x22): horizontal h (the grunt turns to face the target: on target 0.5), vertical
                // from the elevation to the target, acos(dz / len) * 57.3 - 90 degrees clamped to +-26.5 (0x005bcf0c /
                // 0x005bcf10): target above -> the top poses
                let v = g.target.and_then(|e| targets.get(e).ok()).map(|tg| {
                    let d = tg.translation() + Vec3::Y * 5.0 - (t.translation + Vec3::Y * 7.0);
                    let deg = (d.y / d.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
                    (deg.clamp(-26.5, 26.5) + 26.5) / 53.0
                }).unwrap_or(0.5);
                let h = 0.5;
                a.play(0, "grunt_fire_tl", true, Mask::All);
                a.set_pair(0, "grunt_fire_tr", h);
                a.play(1, "grunt_fire_bl", true, Mask::All);
                a.set_pair(1, "grunt_fire_br", h);
                a.set_weight(1, v);
                a.sync(1, 0);
                continue;
            }
            Mind::Reload => ("grunt_reload_tr".to_string(), false),
            Mind::Flee => ("grunt_run_scared".to_string(), true),
            Mind::Chase if limping => ("grunt_limp".to_string(), true),
            Mind::Chase => ("grunt_run_scan".to_string(), true),
            Mind::ToMarker => ("grunt_run_scan".to_string(), true),
            Mind::Guard | Mind::WaitTrigger | Mind::Roam if g.guard || g.mind == Mind::Roam => {
                // fidgets (0x0042be80 / 0x00427330): with no target the timer +0x494 (15 + rand * 0.000275 s, also
                // at load) counts down; on expiry, standing at attention (anim 0xf), one random fidget 0x12..0x1a
                // plays once (GruntObject 0x0043bd70: 18 shiver on a cold level (Game+0x2708 == 1: the world number,
                // 1 for lev_1_m1, as Music_Update's table row), else a random 19..26; 19 cough, 20 sneeze, 21 fly buzz,
                // 22 blow nose, 23..26 the
                // stand scans); when it ends (or the timer runs out again) attention resumes and the timer is redrawn.
                // The cough and shiver anims carry the fx notes that play the grunts' _ic sounds.
                let f = fidgets.entry(e).or_insert_with(|| (15.0 + rand() as f32 * 0.000275, None));
                f.0 -= dt;
                let done = f.1.is_some() && a.finished(0, &lib);
                if f.0 <= 0.0 || done {
                    f.0 = 15.0 + rand() as f32 * 0.000275;
                    f.1 = if f.1.is_some() {
                        None
                    } else {
                        let id = ((rand() * 9) >> 15) + 0x12;
                        Some(match id {
                            18 => "grunt_idle_shiver",
                            19 => "grunt_idle_cough",
                            20 => "grunt_idle_sneeze",
                            21 => "grunt_idle_fly_buzz",
                            22 => "grunt_idle_blow_nose",
                            23 | 25 => "grunt_stand_scan_1",
                            _ => "grunt_stand_scan_2",
                        }
                        .to_string())
                    };
                }
                match &f.1 {
                    Some(n) => (n.clone(), false),
                    None => ("grunt_attention".to_string(), true),
                }
            }
            Mind::Guard | Mind::WaitTrigger | Mind::Roam => ("grunt_stand_scan_1".to_string(), true),
        };
        a.play(0, &name, looping, Mask::All);
        a.stop(1);
    }
}

/// Penguins (PenguinObject_Update 0x0048f690, state 2 "wandering", where lev_1_m1's penguins start: no generic_txt,
/// 0x0048f140): anim 0x17 pngn_scan_chat at load; each time the anim ends a new one by r = rand() * 100 >> 15:
/// 0x18 pngn_scan_chat1 below 33, 0x17 below 66, else 0x19 (both pngn_scan_chat). pngn_scan_chat's note at 0
/// (`fx fx_penguin`) is the call. Not ported: physics on (knocked about), hits (state 3: screams 0x0048fd80,
/// pngn_blown_up), the idle state 1 (only with generic_txt).
fn penguins(
    mut q: Query<(&mut Animator, &crate::level::Placed, Option<&mut crate::level::Penguin>, &mut Visibility)>,
    lib: Res<crate::skin::AnimLibrary>,
    mut seed: Local<u32>,
    time: Res<Time>,
) {
    for (mut a, p, pg, mut vis) in &mut q {
        if p.template != "penguin" {
            continue;
        }
        // dead (0x0048fa20 -> GoRagdoll 0x00431310): pngn_blown_up (0.97 s; its note fx_penguin_blow_up screams), then
        // pngn_land_dead; the body lingers 5 s, then goes (the 0.5 s fade not drawn: guess)
        if let Some(mut pg) = pg {
            if let Some(t) = pg.dead.as_mut() {
                let first = *t == 0.0;
                *t += time.delta_secs();
                if first {
                    a.play(0, "pngn_blown_up", false, Mask::All);
                    a.layers[0].time = 0.0;
                } else if *t > 0.967 && a.layers.first().is_some_and(|l| l.anim == "pngn_blown_up") {
                    a.play(0, "pngn_land_dead", false, Mask::All);
                }
                if *t > 0.967 + 5.5 {
                    *vis = Visibility::Hidden;
                }
                continue;
            }
        }
        let start = a.layers.first().is_none_or(|l| l.anim.is_empty());
        if start || a.finished(0, &lib) {
            let name = if start {
                "pngn_scan_chat"
            } else {
                *seed = seed.wrapping_mul(214013).wrapping_add(2531011);
                let r = ((*seed >> 16) & 0x7fff) * 100 >> 15;
                if r < 33 { "pngn_scan_chat1" } else { "pngn_scan_chat" }
            };
            a.play(0, name, false, Mask::All);
            a.layers[0].time = 0.0;
        }
    }
}

/// The companions' anims (their tables 0x004876c0 / 0x0044ef20): run (run_reload while reloading on the move),
/// reload_br, the four fire poses blended by the aim elevation (as the grunts'), else the idle poses.
fn companions(mut q: Query<(&Companion, &mut Animator, &Name)>) {
    for (c, mut a, name) in &mut q {
        let who = if name.as_str() == "molmer" { "molmer" } else { "q111" };
        if c.dead.is_some() {
            a.play(0, &format!("{who}_die"), false, Mask::All);
            a.stop(1);
        } else if let Some((n, _)) = c.hit {
            a.play(0, &format!("{who}_{n}"), false, Mask::All);
            a.stop(1);
        } else if c.tea > 0.0 {
            a.play(0, "q111_drop_tea", false, Mask::All);
            a.stop(1);
        } else if c.throwing.is_some() {
            a.play(0, "molmer_throw_tnt", false, Mask::All);
            a.stop(1);
        } else if c.moving {
            a.play(0, &format!("{who}_{}", if c.reloading > 0.0 { "run_reload" } else { "run" }), true, Mask::All);
            a.stop(1);
        } else if c.reloading > 0.0 {
            a.play(0, &format!("{who}_reload_br"), false, Mask::All);
            a.stop(1);
        } else {
            let kind = if c.firing { "fire" } else { "idle" };
            a.play(0, &format!("{who}_{kind}_tl"), true, Mask::All);
            a.set_pair(0, &format!("{who}_{kind}_tr"), 0.5);
            a.play(1, &format!("{who}_{kind}_bl"), true, Mask::All);
            a.set_pair(1, &format!("{who}_{kind}_br"), 0.5);
            a.set_weight(1, 1.0 - c.aim_v);
            a.sync(1, 0);
        }
    }
}
