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
        app.add_systems(Update, (roman, grunts, companions));
    }
}

fn roman(mut q: Query<(&Roman, &mut Animator)>, topsy: Res<crate::topsy::Topsy>) {
    for (r, mut a) in &mut q {
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
        let upper = if r.reloading > 0.0 {
            gait.replace("_aim", "_reload").replace("trotter_aim", "trotter_reload").replace("jump_idle", "jump_idle_reload")
        } else if r.since_fire < 0.35 {
            if gait == "trotter_aim" { "trotter_fire_rr".to_string() } else { gait.replace("_aim", "_fire").replace("jump_idle", "jump_fire_rr") }
        } else {
            gait.clone()
        };
        if r.dead {
            a.play(0, "trotter_fall_loop", true, Mask::All);
            a.stop(1);
            continue;
        }
        // `_b` / `_t` are the bottom and top of an aim-pitch blend (as the grunts' fire_bl / tl): the rifle points
        // ~50 degrees down in `_b` and ~47 up in `_t` (measured on the skeleton, docs/remaster-engine.md), legs alike.
        // The pose is the blend whose rifle follows the aim pitch: w = (pitch + 50) / 97 in degrees, with the
        // aim pitch = the view's (rest -8 degrees: w 0.43, a level rifle as in the original's captures).
        // The exact mapping in the player's anim code (0x00475ff0 / the blend +0x580) is not traced: derived.
        let aim_deg = (r.pitch + 0.15 - 0.141).to_degrees();
        let w = ((aim_deg + 50.0) / 97.0).clamp(0.0, 1.0);
        a.play(0, &format!("{upper}_b"), true, Mask::All);
        a.play(1, &format!("{upper}_t"), true, Mask::All);
        a.set_weight(1, w);
        a.sync(1, 0);
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

fn grunts(mut q: Query<(&Grunt, &mut Animator, &Transform, Option<&crate::topsy::Thrown>)>, targets: Query<&GlobalTransform>) {
    for (g, mut a, t, thrown) in &mut q {
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
        let limping = g.limp_pct > 0.0 && g.last_health < g.max_health * g.limp_pct / 100.0;
        let (name, looping) = match g.mind {
            Mind::Dead if g.ragdoll => ("grunt_blown_up".to_string(), false),
            Mind::Dead => (format!("grunt_die_{}", g.death_anim), false),
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
            Mind::Roam => ("grunt_march".to_string(), true),
            Mind::Guard | Mind::WaitTrigger => (if g.guard { "grunt_attention".to_string() } else { "grunt_stand_scan_1".to_string() }, true),
        };
        a.play(0, &name, looping, Mask::All);
        a.stop(1);
    }
}

fn companions(mut q: Query<(&Companion, &mut Animator, &Name)>) {
    for (c, mut a, name) in &mut q {
        let who = if name.as_str() == "molmer" { "molmer" } else { "q111" };
        let anim = if c.moving { format!("{who}_run") } else if c.fire_timer > 0.0 && c.fire_timer < 0.4 { format!("{who}_fire_tr") } else { format!("{who}_stand") };
        a.play(0, &anim, true, Mask::All);
    }
}
