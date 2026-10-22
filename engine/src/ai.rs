//! Grunt AI ported from the recovered state machine (GenericAI_Update 0x0043ee90 and its helpers, documented in
//! docs/subsystems/grunt-ai.md), on the game's own per-instance parameters, for the classes lev_1_m1 makes (`Kind`:
//! GruntObject, BowGruntObject, CaptainObject) and the companions Q / Jonesy (`Companion`).
//! - The state +0x460 is `Mind` (GenericAI_SetState 0x004262c0); the anims by id through the class tables (`anim_name_with`),
//!   played by charanim.rs as PlayAnim 0x004262d0 (one) / PlayAnim4 0x00426480 (the four-pose sets) do.
//! - States: idle / roaming (1), guard (0x79, become guard 0x0042d0f0, its flag cleared by 0x0042cff0), wait for a
//!   trigger (0x8c; the trigger handler 0x0042f870 runs in mission.rs's dispatch), chase (0x3c), firing (0xa) with the
//!   fire cycle 0x00428980, reload (0x32), flee (0xc8), the marker splines (0x96 / 0x97, `SplineRun`), the wander
//!   (0x3d / 0x5b), home (0x50, after 0x0042bc60), the taunt (0x6e), the captain's grenade (0x10), the hit reactions
//!   (0x12f..0x132, 300..0x12e), the trenches (0xa0..0xa6), the mounted gun (0x7b / 0x7d / 0x7c), death.
//! - Targets: AI_FindTarget 0x00426920 (`search`: the noises of the player's shots, `Interests`, then the cone
//!   Object_IsInViewCone 0x00455d90 and the sight ray), the alert groups, retaliation 0x0042c820.
//! Functions this port does not cite are explained in docs/port/ai.csv (replaced by the engine, or not needed).

pub mod aim;
pub mod assault;
pub mod base_area;
pub mod body;
pub mod bully;
pub mod coverslot;
pub mod carried;
pub mod close_kill;
pub mod dialogue;
pub mod fall;
pub mod grenade;
pub mod ic;
pub mod jumper;
pub mod launch;
pub mod ladder;
pub mod max_fire;
pub mod melee;
pub mod monk;
pub mod q112;
pub mod notes;
pub mod paratroop;
pub mod retreat;
pub mod rocket;
pub mod squad;
pub mod tower;

pub use jumper::{JumpMarker, Jumper};

use crate::levelreset::LevelAppExt;
use crate::fx::{FxStats, Health};
use crate::physics::{Body, Physics, bv, jv};
use bevy::prelude::*;
use oxijolt::prelude::*;
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Mind {
    /// state 1
    Roam,
    /// states 0x79 / 0xdd
    Guard,
    /// state 0x8c
    WaitTrigger,
    /// state 0x3c (and 2 / 0x50: waiting, going home)
    Chase,
    /// state 0xa with the fire cycle
    Fire,
    /// state 0x32
    Reload,
    /// state 0xc8: running from a sticky bomb (the scare 0x0042fb60), `Grunt::flee_bomb` when it is on him
    Flee,
    /// state 0xca: a guard (or a gunner) with a sticky bomb stuck on him (the scare 0x0042fb60): he stands, physics
    /// frozen (ai_06.cpp:535 -> 0x00454c90), playing grenade_stuck_1 / 2 (`Grunt::anim_id` 0x10 / 0x11) once, until
    /// the bomb goes off
    Cower,
    /// state 0x96: running to a marker (trigger_marker mode, alarm.rs)
    ToMarker,
    /// states 0xa0..0xa5: going to and using a trench's slot (`Grunt::cover`, its phase)
    Cover,
    /// states 0x7b / 0x7d / 0x7c: going to, manning and leaving a mounted gun (`Grunt::gun`)
    Gunner,
    Dead,
    /// state 0x50: walking back to the home point +0x4c0 (0x0042bc60 after a lost target, state 2's end); there the
    /// stand set and state 1 (ai_06.cpp:1833)
    Home,
    /// state 0x3d: AI_Wander 0x0042d430 (a marker's scatter, a hide marker with no cover found): walk to the point,
    /// then home = there, the stand set and state 1 (ai_06.cpp:1383)
    Wander,
    /// state 0x6e: a taunt anim (0x32..0x34) after the last clip (0x00428980, ai_02.cpp:1955-1981), facing the target;
    /// its end as the clip's end (0x0042b8e0) or, a guard, 0x7a (ai_06.cpp:1979-2012)
    Taunt,
    /// state 0x10: the close attack (anim 0x27): the captain's grenade throw (0x00427910 roll < 30 with +0x484 flag 1)
    Throw,
    /// states 0xdc / 0xde: a squad member following his bone of the squad (or idle while it waits): squad.rs
    InSquad,
    /// states 0xc4 / 0xc5 / 0xbd: a jumper captain flying to a jump marker (jumper.rs `Flight`)
    Jump,
    /// state 0xf0: a monk's teleport flight (MonkObject_Bamf 0x00436bb0) to `Grunt::goal`
    Teleport,
    /// state 0xf1: a monk casting the basket rain (anim 0x26 cast_spell_1); 0xf2 / 0xf3: the twiglet summon (anim 0x25
    /// cast_spell_2, monk.rs `Spell`)
    Cast,
    /// state 0x64: an IC move (GenericAI_PlayICAnim 0x0042c410) carries him: a hidden grunt coming out (ic.rs)
    Scripted,
    /// state 0x118: a bully holding his peasant (bully.rs), standing until a target or the peasant's death ends it
    Bully,
    /// states 0x1e / 0x1f: running to a generic cover slot, a tree's or a building's (coverslot.rs, `Grunt::slot`)
    ToSlot,
    /// state 0xf: held by a companion who comes to kill him (GruntObject_Slot66 0x00434ea0; close_kill.rs), standing
    Held,
    /// state 0x11: waiting for a turn to fire under the mission's shooter limit (max_fire.rs), facing the target with
    /// the idle bases 0x74..0x76
    MaxFireWait,
    /// state 0x82: falling (ai/fall.rs), anim 0x61 (0x62 under a parachute); a landing resumes `Grunt::fall`'s state
    Falling,
    /// state 0xb: a close attack (ai/melee.rs `close_attack`), anim 0x5f / 0x60 looped (`anim_id`)
    CloseAttack,
    /// state 0xae: the pre-success retreat (ai/retreat.rs), running along his route to `Grunt::goal`; it never ends
    /// (he stands at its end); the run anim, or run_scared (0xb) with `Grunt::scared_run`
    Retreat,
}

/// The grunt classes a lev_1_m1 template makes (counted in the coverage runs: BowGruntObject_Slot6 0x004347b0 runs 9
/// times per level load = the 9 `grunt` instances, GruntObject_Slot6 0x00434fb0 for `grunt_regular`; `captain` is
/// CaptainObject, vtable 0x005acb78, spawned by alarm generator 215). Their vtables differ in the anim table (vt+0x1a4),
/// the weapon (0x004a0ef0 index: 0xe bow at ref_lhand, 0xf rifle at ref_rhand, 0x10 captain_mg at ref_rhand) and the
/// yes / no slots below (0x00433fa0 = yes, 0x00435250 = no).
#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum Kind {
    /// GruntObject 0x005a9cf0 (`grunt_regular`)
    #[default]
    Grunt,
    /// BowGruntObject 0x005abb88 (`grunt`)
    Bow,
    /// CaptainObject 0x005acb78 (`captain`)
    Captain,
    /// SniperCaptain 0x005ae868 (`captain_sniper`, behavior "captain_sniper"): CaptainObject's vtable but for the init
    /// (0x004348a0: the captain_snipergun, weapon index 0x12, at ref_chest), the anim table (0x0043b110), no close attack
    /// (vt+0x1e0 0x00435250) and SetTarget (vt+0x23c 0x00434930: the gun raised to the eye, `Grunt::aiming`)
    Sniper,
    /// Q112Object 0x005a2440 (`q112`): a heavy chaingun grunt (init 0x00435090: q112_chaingun, index 0x13, at
    /// ref_chaingun; the fire anim loops; bullets scaled by bullet_scale), anims 0x0043c330
    Q112,
    /// MonkObject 0x005aac28 (`monk`): monk_gun (0x19) at ref_shoot_00 (init 0x004369a0), anims 0x0043cb40, the special
    /// roll vt+0x220 0x00437130 and the teleport MonkObject_Bamf 0x00436bb0 as its hiding-spot search
    Monk,
}

impl Kind {
    pub fn of(template: &str) -> Kind {
        Kind::from_template(template).unwrap_or(Kind::Grunt)
    }
    /// The class a template makes, for the grunt-class templates (ObjectSystem_CreateObject 0x0045f020 by the property
    /// set's "behavior", else the template name): None for anything else
    pub fn from_template(template: &str) -> Option<Kind> {
        match template {
            "grunt" => Some(Kind::Bow),
            "grunt_regular" => Some(Kind::Grunt),
            "captain" => Some(Kind::Captain),
            "captain_sniper" => Some(Kind::Sniper),
            "q112" => Some(Kind::Q112),
            "monk" => Some(Kind::Monk),
            _ => None,
        }
    }
    /// CaptainObject and SniperCaptain share the captain's slots but for those listed on `Sniper`
    pub fn captain_like(self) -> bool {
        matches!(self, Kind::Captain | Kind::Sniper)
    }
    /// vt+0x1f4: may take a trench (0x00429f00); the archers, the Q112 and the monk may not
    pub fn uses_trenches(self) -> bool {
        !matches!(self, Kind::Bow | Kind::Q112 | Kind::Monk)
    }
    /// vt+0x218: the turn anims before firing (0x0042e050): the grunts and archers only
    pub fn turns(self) -> bool {
        matches!(self, Kind::Grunt | Kind::Bow)
    }
    /// vt+0x1d8: the taunt after the last burst: not the Q112 nor the monk
    pub fn taunts(self) -> bool {
        !matches!(self, Kind::Q112 | Kind::Monk)
    }
    /// vt+0x1f0: may man a mounted gun (0x0042a100); the captains (and snipers), the Q112 and the monk may not
    pub fn uses_guns(self) -> bool {
        matches!(self, Kind::Grunt | Kind::Bow)
    }
    /// vt+0x1e0: the close attack of the engage roll (0x00427910: anim 0x27, state 0x10), the captain's grenade
    pub fn close_attack(self) -> bool {
        self == Kind::Captain
    }
    /// vt+0x1fc: the fire spray from a trench (anim 0x68, state 0xa6); the sniper's vtable keeps the captain's
    pub fn spray(self) -> bool {
        self.captain_like()
    }
    /// vt+0x200: the all-fours reaction when the health falls past allfours_pct (anim 0x35, state 300); the Q112's
    /// knees
    pub fn allfours(self) -> bool {
        self.captain_like() || self == Kind::Q112
    }
    /// vt+0x1cc: the chase engages on the ray alone (state 0x3c); without it the target must first be within
    /// +0x5d8 x K(0x005bcc38) (the archers, the Q112)
    pub fn chase_ray_only(self) -> bool {
        !matches!(self, Kind::Bow | Kind::Q112)
    }
    /// The weapon template held (0x004a0ef0's index -> the weapon table, game_01.cpp:600)
    pub fn weapon(self) -> &'static str {
        match self {
            Kind::Grunt => "grunt_rifle",
            Kind::Bow => "grunt_bow",
            Kind::Captain => "captain_mg",
            Kind::Sniper => "captain_snipergun",
            Kind::Q112 => "q112_chaingun",
            Kind::Monk => "monk_gun",
        }
    }
    /// The bone level.rs hangs the weapon model from: the sniper's gun at ref_chest (0x004348a0: 0x004a0ea0 with
    /// "ref_chest"), the rest at ref_rhand (the bow's 0xe names ref_lhand; the engine has always drawn it from
    /// ref_rhand: not compared)
    pub fn weapon_bone(self) -> &'static str {
        match self {
            Kind::Sniper => "ref_chest",
            Kind::Q112 => "ref_chaingun",
            Kind::Monk => "ref_shoot_00",
            _ => "ref_rhand",
        }
    }
}

/// Game+0x2708 == 1 (world 1: the snow levels lev_1_*), from the level's folder name: id 18 is the shiver there
pub fn cold_level(dir: &std::path::Path) -> bool {
    dir.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.starts_with("lev_1_"))
}

/// The weapon template a grunt template holds (Kind::weapon): level.rs attaches its model to the hand.
pub fn weapon_template(template: &str) -> Option<&'static str> {
    Kind::from_template(template).map(Kind::weapon)
}


/// What the class anim tables read of the object besides the id (0x0043b610, 0x0043b110).
#[derive(Clone, Copy, Default, Debug)]
pub struct AnimFlags {
    /// Game+0x2708 == 1 (world 1): id 18 is the shiver
    pub cold: bool,
    /// +0x484 0x8000000: a rocket captain (rocket_captain): ids 0..3 / 33..36 the rocket sets
    pub rocket: bool,
    /// state 0xbd: a jumper captain gliding: ids 0..3 / 33..36 the glide sets
    pub glide: bool,
    /// +0x588 held (the sniper's SetTarget 0x00434930): ids 0..3 the aim poses, else captain_sniper_stand
    pub aiming: bool,
    /// 0x0044e600: the level starts Roman in a turret (a base defence: lev_2_m4, lev_3_m4_bd, lev_5_m5): the
    /// grunt's run ids 8 / 9 are grunt_run_no_sound
    pub base_defence: bool,
}

/// The anim name for an anim id (the class's vt+0x1a4: GruntObject 0x0043bd70, BowGruntObject 0x0043abc0,
/// CaptainObject 0x0043b610; anim_01.cpp). `r` is a uniform 0..1 draw for the ids that pick by rand (bow run 10,
/// deaths 80..85, the idle 18 off a cold level); `cold` is Game+0x2708 == 1 (world 1: lev_1_m1). Ids the class has no
/// anim for give its error anim (the original's "grunt_anim_error"); ids 8 / 9 give grunt_run (grunt_run_no_sound on a
/// base-defence level, 0x0044e600).
/// With the object's state (`AnimFlags`): `Grunt::anim`.
pub fn anim_name_with(kind: Kind, id: u32, r: f32, fl: AnimFlags) -> &'static str {
    let cold = fl.cold;
    let idle18 = |k: Kind| -> &'static str {
        if cold {
            return if k.captain_like() { "captain_idle_shiver" } else { "grunt_idle_shiver" };
        }
        // this virtual again with (rand << 3 >> 15) + 0x13: 19..26
        anim_name_with(k, 19 + (r * 8.0) as u32 % 8, 0.0, fl)
    };
    // the captain's aim / fire sets: rocket (+0x484 0x8000000) before glide (state 0xbd) for 0..3, glide first for 33..36
    let captain_set = |id: u32| -> &'static str {
        const IDLE: [&str; 4] = ["captain_idle_tl", "captain_idle_tr", "captain_idle_bl", "captain_idle_br"];
        const ROCKET: [&str; 4] = ["captain_rocket_tl", "captain_rocket_tr", "captain_rocket_bl", "captain_rocket_br"];
        const GLIDE: [&str; 4] = ["captain_jumper_glide_tl", "captain_jumper_glide_tr", "captain_jumper_glide_bl", "captain_jumper_glide_br"];
        const FIRE: [&str; 4] = ["captain_fire_tl", "captain_fire_tr", "captain_fire_bl", "captain_fire_br"];
        const RFIRE: [&str; 4] = ["captain_rocket_fire_tl", "captain_rocket_fire_tr", "captain_rocket_fire_bl", "captain_rocket_fire_br"];
        const GFIRE: [&str; 4] = ["captain_jumper_glide_fire_tl", "captain_jumper_glide_fire_tr", "captain_jumper_glide_fire_bl", "captain_jumper_glide_fire_br"];
        match id {
            0..=3 if fl.rocket => ROCKET[id as usize],
            0..=3 if fl.glide => GLIDE[id as usize],
            0..=3 => IDLE[id as usize],
            _ if fl.glide => GFIRE[(id - 33) as usize],
            _ if fl.rocket => RFIRE[(id - 33) as usize],
            _ => FIRE[(id - 33) as usize],
        }
    };
    match kind {
        Kind::Grunt => match id {
            0 => "grunt_stand_tl",
            1 => "grunt_stand_tr",
            2 => "grunt_stand_bl",
            3 => "grunt_stand_br",
            4 => "grunt_turn",
            5 => "grunt_turn_180",
            6 => "grunt_turn_left_90",
            7 => "grunt_turn_right_90",
            8 | 9 if fl.base_defence => "grunt_run_no_sound",
            8 | 9 => "grunt_run",
            10 => "grunt_run_scan",
            11 | 12 => "grunt_run_scared",
            14 => "grunt_march",
            15 => "grunt_attention",
            16 => "grunt_grenade_stuck_1",
            17 => "grunt_grenade_stuck_2",
            18 => idle18(kind),
            19 => "grunt_idle_cough",
            20 => "grunt_idle_sneeze",
            21 => "grunt_idle_fly_buzz",
            22 => "grunt_idle_blow_nose",
            23 | 25 => "grunt_stand_scan_1",
            24 | 26 => "grunt_stand_scan_2",
            33 => "grunt_fire_tl",
            34 => "grunt_fire_tr",
            35 => "grunt_fire_bl",
            36 => "grunt_fire_br",
            40 => "grunt_reload_tl",
            41 => "grunt_reload_tr",
            42 => "grunt_reload_bl",
            43 => "grunt_reload_br",
            44 => "grunt_reload_hide",
            45 => "grunt_peek_1",
            46 => "grunt_peek_2",
            49 => "grunt_ladder_run",
            50 => "grunt_taunt1",
            51 | 52 => "grunt_taunt2",
            57 | 59 => "grunt_hit_fall_1",
            58 | 60 => "grunt_hit_fall_2",
            61 => "grunt_hit_window",
            62 => "grunt_window_blown_out",
            63 => "grunt_bow_hit_1",
            64 => "grunt_hit_mgun_2",
            65 => "grunt_hit_mgun_3",
            66 => "grunt_hit_4",
            67 | 119 => "grunt_hit_1",
            68 | 120 => "grunt_hit_2",
            69..=72 | 121 | 122 => "grunt_hit_3",
            73 => "grunt_blown_up",
            74 => "grunt_blown_up_2",
            75 | 123 => "grunt_hit_long",
            76 | 124 => "grunt_hit_long_2",
            77 => "grunt_dazed_1",
            78 => "grunt_dazed_2",
            79 => "grunt_dazed_3",
            80 => "grunt_die_1",
            81 => "grunt_die_2",
            82 => "grunt_die_3",
            83 => "grunt_die_4",
            84 => "grunt_die_5",
            85 => "grunt_die_6",
            86 => "grunt_barricade_death",
            87 => "grunt_hit_in_back",
            88 => "grunt_sharkbite",
            89 => "grunt_limp",
            91 => "grunt_crash_land",
            93 => "grunt_blown_up_die",
            95 => "grunt_bow_hand_to_hand",
            96 => "grunt_bow_hand_to_hand_2",
            97 | 98 => "grunt_bow_fall",
            101 => "grunt_barricade_breathe",
            102 => "grunt_barricade_reload",
            103 => "grunt_barricade_peek",
            105 => "grunt_fcannon_sit",
            106 => "grunt_fcannon_fire",
            113 => "grunt_hang_into",
            114 => "grunt_hanging",
            115 => "grunt_blown_up_into",
            116 => "grunt_idle_base_1",
            117 => "grunt_idle_base_2",
            118 => "grunt_idle_base_3",
            125 => "grunt_scare",
            127 => "mover_grunt_mdie_1",
            128 => "mover_grunt_mdie_2",
            129 => "mover_grunt_qdie_1",
            130 => "mover_grunt_qdie_2",
            131 => "grunt_stig_mgun_fire_tl",
            132 => "grunt_stig_mgun_fire_tr",
            133 => "grunt_stig_mgun_fire_bl",
            134 => "grunt_stig_mgun_fire_br",
            135 => "grunt_stig_mgun_idle_tl",
            136 => "grunt_stig_mgun_idle_tr",
            137 => "grunt_stig_mgun_idle_bl",
            138 => "grunt_stig_mgun_idle_br",
            139 => "mover_grunt_tower_climb",
            _ => "grunt_anim_error",
        },
        Kind::Bow => match id {
            0 => "grunt_bow_idle_tl",
            1 => "grunt_bow_idle_tr",
            2 => "grunt_bow_idle_bl",
            3 => "grunt_bow_idle_br",
            4 => "grunt_bow_turn",
            5 => "grunt_turn_180",
            6 => "grunt_turn_left_90",
            7 => "grunt_turn_right_90",
            8 => "grunt_bow_run",
            9 => "grunt_bow_run_2",
            // (rand * 100 >> 15) >= 50: run_2
            10 => {
                if r >= 0.5 {
                    "grunt_bow_run_2"
                } else {
                    "grunt_bow_run"
                }
            }
            11 | 12 => "grunt_run_scared",
            14 => "grunt_march",
            15 => "grunt_bow_attention",
            16 => "grunt_grenade_stuck_1",
            17 => "grunt_grenade_stuck_2",
            18 => idle18(kind),
            19 => "grunt_idle_cough",
            20 => "grunt_idle_sneeze",
            21 => "grunt_idle_fly_buzz",
            22 => "grunt_idle_blow_nose",
            23 | 25 => "grunt_bow_attention_scan",
            24 | 26 => "grunt_bow_attention_scan_2",
            33 => "grunt_bow_fire_tl",
            34 => "grunt_bow_fire_tr",
            35 => "grunt_bow_fire_bl",
            36 => "grunt_bow_fire_br",
            40 => "grunt_bow_reload_tl",
            41 => "grunt_bow_reload_tr",
            42 => "grunt_bow_reload_bl",
            43 => "grunt_bow_reload_br",
            47 => "grunt_bow_run_fire",
            49 => "grunt_ladder_run",
            50 => "grunt_bow_taunt1",
            51 | 52 => "grunt_bow_taunt2",
            57 | 59 => "grunt_bow_hit_fall_1",
            58 | 60 => "grunt_bow_hit_fall_2",
            61 => "grunt_hit_window",
            62 => "grunt_window_blown_out",
            63 => "grunt_bow_hit_1",
            64 => "grunt_hit_mgun_2",
            65 => "grunt_hit_mgun_3",
            66 => "grunt_hit_4",
            67 => "grunt_bow_hit_small",
            68 => "grunt_bow_hit_small_2",
            69..=72 => "grunt_bow_hit_med",
            73 => "grunt_blown_up",
            74 => "grunt_blown_up_2",
            75..=79 => "grunt_hit_long",
            // rand * 300 >> 15: < 100 die_1, < 200 die_2, else die_3
            80..=85 => {
                if r < 1.0 / 3.0 {
                    "grunt_bow_die_1"
                } else if r < 2.0 / 3.0 {
                    "grunt_bow_die_2"
                } else {
                    "grunt_bow_die_3"
                }
            }
            86 => "grunt_barricade_death",
            87 => "grunt_hit_in_back",
            88 => "grunt_sharkbite",
            89 => "grunt_limp",
            91 => "grunt_crash_land",
            93 => "grunt_blown_up_die",
            95 => "grunt_bow_hand_to_hand",
            96 => "grunt_bow_hand_to_hand_2",
            97 | 98 => "grunt_bow_fall",
            105 => "grunt_fcannon_sit",
            106 => "grunt_fcannon_fire",
            113 => "grunt_hang_into",
            114 => "grunt_hanging",
            115 => "grunt_blown_up_into",
            116 => "grunt_idle_base_1",
            117 => "grunt_idle_base_2",
            118 => "grunt_idle_base_3",
            125 => "grunt_scare",
            127 => "mover_grunt_mdie_1",
            128 => "mover_grunt_mdie_2",
            129 => "mover_grunt_qdie_1",
            130 => "mover_grunt_qdie_2",
            131 => "grunt_stig_mgun_fire_tl",
            132 => "grunt_stig_mgun_fire_tr",
            133 => "grunt_stig_mgun_fire_bl",
            134 => "grunt_stig_mgun_fire_br",
            135 => "grunt_stig_mgun_idle_tl",
            136 => "grunt_stig_mgun_idle_tr",
            137 => "grunt_stig_mgun_idle_bl",
            138 => "grunt_stig_mgun_idle_br",
            139 => "mover_grunt_tower_climb",
            _ => "grunt_bow_anim_error",
        },
        // the rocket (+0x484 0x8000000) and jumper glide (state 0xbd) variants of ids 0..3 / 33..36 are not reached:
        // lev_1_m1's captains are neither (rocket_captain 0; generator 215 sets no num_jumper_captain)
        Kind::Captain => match id {
            0..=3 | 33..=36 => captain_set(id),
            4 => "captain_turn",
            8 | 9 => "captain_run",
            10 => "captain_run_scan",
            11 | 12 => "captain_run_scared",
            14 => "captain_march",
            15 => "captain_attention",
            16 => "captain_grenade_stuck_1",
            17 => "captain_grenade_stuck_2",
            18 => idle18(kind),
            19 => "captain_idle_cough",
            20 => "captain_idle_sneeze",
            21 => "captain_idle_ball_adjust",
            22 => "captain_idle_check_time",
            23 => "captain_stand_scan_1",
            24 => "captain_stand_scan_2",
            25 => "captain_stand_scan_3",
            26 => "captain_stand_scan_4",
            39 => "captain_grenade_throw",
            40 => "captain_reload_tl",
            41 => "captain_reload_tr",
            42 => "captain_reload_bl",
            43 => "captain_reload_br",
            50 => "captain_taunt1",
            51 => "captain_taunt2",
            52 => "captain_taunt3",
            53 => "captain_allfours_hit",
            54 => "captain_allfours_breathe",
            55 | 93 => "captain_allfours_death",
            56 => "captain_allfours_stand_up",
            57 => "captain_hit_fall_1",
            58 => "captain_hit_fall_2",
            59 => "captain_hit_fall_3",
            60 => "captain_hit_fall_4",
            61 => "captain_hit_window",
            62 => "captain_window_blown_out",
            63 => "captain_hit_1",
            64 => "captain_hit_2",
            65 => "captain_hit_mgun_3",
            66 => "captain_hit_mgun_4",
            67 | 123 => "captain_hit_small",
            68 | 124 => "captain_hit_small_2",
            69 | 71 | 121 => "captain_hit_med",
            70 | 72 | 122 => "captain_hit_med_2",
            73 => "captain_blown_up",
            74 => "captain_blown_up_2",
            75 | 76 | 119 => "captain_hit_long",
            77..=79 | 120 => "captain_hit_long_2",
            // rand * 200 >> 15 < 100: 0x005b3be0 "captain_death_1", else 0x005b3bd0 "captain_death_2" (the exe's strings)
            80..=85 => {
                if r < 0.5 {
                    "captain_death_1"
                } else {
                    "captain_death_2"
                }
            }
            86 => "captain_barricade_death",
            87 => "captain_hit_in_back",
            88 => "captain_sharkbite",
            89 => "captain_limp",
            91 => "captain_jumper_hit_ground",
            92 => "captain_jumper_fall",
            94 => "captain_jumper_hit_ground_get_up",
            97 => "captain_fall",
            98 => "captain_parachute",
            101 => "captain_barricade_hide",
            102 => "captain_barricade_reload",
            103 => "captain_barricade_peek",
            104 => "captain_barricade_fire_spray",
            107 => "captain_jumper_forward",
            108 => "captain_jumper_land",
            109 => "captain_jumper_glide_tl",
            110 => "captain_jumper_glide_tr",
            111 => "captain_jumper_glide_bl",
            112 => "captain_jumper_glide_br",
            113 => "captain_hang_into",
            114 => "captain_hanging",
            115 => "captain_blown_up_into",
            116 => "captain_idle_base_1",
            117 => "captain_idle_base_2",
            118 => "captain_idle_base_3",
            _ => "captain_error_anim",
        },
        // Q112Object 0x0043c330 (the ids it does not list: q112_anim_error)
        Kind::Q112 => match id {
            0 => "q112_aim_tl",
            1 => "q112_aim_tr",
            2 => "q112_aim_bl",
            3 => "q112_aim_br",
            4 => "q112_turn",
            8 | 9 => "q112_run",
            10 => "q112_walk_scan",
            14 => "q112_march",
            15 => "q112_stand_attention",
            18 | 21 | 22 => "q112_stand_scan_3",
            19 | 23 | 25 => "q112_stand_scan_1",
            20 | 24 | 26 => "q112_stand_scan_2",
            33 => "q112_fire_tl",
            34 => "q112_fire_tr",
            35 => "q112_fire_bl",
            36 => "q112_fire_br",
            40 => "q112_reload_tl",
            41 => "q112_reload_tr",
            42 => "q112_reload_bl",
            43 => "q112_reload_br",
            53 => "q112_hit_to_knees",
            54 | 75..=79 => "q112_hit_knees_breathe",
            55 => "q112_hit_knees_die",
            56 => "q112_hit_knees_standup",
            67 | 68 => "q112_hit_med",
            69 | 70 => "q112_hit_big",
            71 => "q112_hit_head",
            72 => "q112_hit_head_2",
            73 | 74 | 97 | 98 => "q112_fall",
            80 | 83 => "q112_die",
            81 | 84 => "q112_die_2",
            82 | 85 => "q112_die_3",
            89 => "q112_walk",
            95 | 96 => "q112_gun_hit",
            99 => "q112_land",
            _ => "q112_anim_error",
        },
        // MonkObject 0x0043cb40 (the rest: monk_anim_error)
        Kind::Monk => match id {
            0..=3 | 15 | 40..=43 => "monk_stand",
            4 | 8..=10 | 14 | 89 => "monk_walk",
            18 | 21 | 22 => "monk_scan_3",
            19 | 23 | 25 => "monk_scan_1",
            20 | 24 | 26 => "monk_scan_2",
            33..=36 => "monk_fire",
            37 => "monk_cast_spell_2",
            38 => "monk_cast_spell_1",
            71 | 72 => "monk_hit_head_shot",
            80 | 83 => "monk_die_1",
            81 | 84 => "monk_die_2",
            82 | 85 | 93 => "monk_die_3",
            92 | 97 | 98 => "monk_fall",
            107 => "monk_turbo",
            113 => "monk_hang_into",
            114 => "monk_hanging",
            115 => "monk_blown_up_into",
            _ => "monk_anim_error",
        },
        // SniperCaptain 0x0043b110: the captain's table but for these (the exe's names are matched case-blind:
        // "Captain_sniper_aim_tl" is the export's captain_sniper_aim_tl)
        Kind::Sniper => match id {
            0..=3 if fl.aiming => ["captain_sniper_aim_tl", "captain_sniper_aim_tr", "captain_sniper_aim_bl", "captain_sniper_aim_br"][id as usize],
            0..=3 | 15 => "captain_sniper_stand",
            23 => "captain_sniper_scan_1",
            24 => "captain_sniper_scan_2",
            33 => "captain_sniper_fire_tl",
            34 => "captain_sniper_fire_tr",
            35 => "captain_sniper_fire_bl",
            36 => "captain_sniper_fire_br",
            40 => "captain_sniper_reload_tl",
            41 => "captain_sniper_reload_tr",
            42 => "captain_sniper_reload_bl",
            43 => "captain_sniper_reload_br",
            5..=7 | 13 | 27..=32 | 37 | 38 | 44..=49 | 90 | 95 | 96 | 99 | 100 | 105..=107 | 109..=112 => "sniper_captain_error_anim",
            _ => match anim_name_with(Kind::Captain, id, r, AnimFlags { rocket: false, glide: false, ..fl }) {
                "captain_error_anim" => "sniper_captain_error_anim",
                n => n,
            },
        },
    }
}

/// The mounted-gun states (GenericAI_Update ai_06.cpp:583-797).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum GunPhase {
    /// 0x7b: running to the gun (reserved: flag 0x10)
    ToGun,
    /// 0x7d: on it (attached at ref_grunt_08, +0x484 0x8000, the gun's flag 4)
    On,
    /// 0x7c: got off, walking to ref_grunt_exit; then the hiding-spot search
    Off,
}

/// A grunt's mounted gun (+0x898) and the gunner's own counters and aim.
#[derive(Clone, Copy, Debug)]
pub struct Gun {
    pub turret: Entity,
    pub phase: GunPhase,
    /// the gun's origin (ref_grunt_08: the gunner's root; his anims put his body 7.5 behind), ref_grunt_exit, the
    /// gun's heading (yaw in engine terms, and its forward), the gunner's body centre offset there
    pub origin: Vec3,
    pub exit: Vec3,
    pub yaw: f32,
    pub fwd: Vec3,
    pub lift: Vec3,
    /// +0x494: the delay before a burst sequence (stig_fire_delay)
    pub timer: f32,
    /// aim weights +0x580 / +0x584 (0x00428fc0)
    pub aim: aim::AimBlend,
    /// the counters' tables: the grunt's stig_fire_* for a kind-1 gun, forge_fire_* for a forge cannon (0x00427600)
    pub table: StigTable,
    /// +0x5b4 clips, +0x5bc rounds, +0x5b8 burst rounds, +0x5c8 clip pause, +0x5cc burst delay
    pub clips: i64,
    pub rounds: i64,
    pub burst: i64,
    pub pause: f32,
    pub delay: f32,
    /// the fire set (0x83..0x86) playing, its time (its "fire" notes at 0.067 / 0.233 of 0.3 s)
    pub firing: bool,
    pub anim_t: f32,
    /// +0x48c flag 1: the counters ran out together (the aim snaps, firing continues out of the arc)
    pub snap: bool,
    pub shots: u32,
    /// "fire" notes a forge cannon held back (0x0043ec60: the target behind or within 100)
    pub held: u32,
}

impl Gun {
    /// Going to this gun (state 0x7b)
    fn to(gv: &GunView, table: StigTable) -> Gun {
        Gun {
            table,
            turret: gv.e,
            phase: GunPhase::ToGun,
            origin: gv.origin,
            exit: gv.exit,
            yaw: gv.yaw,
            fwd: gv.fwd,
            lift: Vec3::ZERO,
            timer: 0.0,
            aim: aim::AimBlend::default(),
            clips: 0,
            rounds: 0,
            burst: 0,
            pause: 0.0,
            delay: 0.0,
            firing: false,
            anim_t: 0.0,
            snap: false,
            shots: 0,
            held: 0,
        }
    }
}

/// The gunner's tables (Ai_InitGeneric ai_01.cpp:2376-2381, kind 1 guns): stig_fire_num_clips 3..5, _clip_delay 1..1,
/// _num_rounds 10..10, _num_bursts 5..5, _burst_delay 0.25..0.5, _delay 0.5..0.75; lev_1_m1 sets none.
#[derive(Clone, Copy, Debug)]
pub struct StigTable {
    pub clips: (i64, i64),
    pub clip_delay: (f32, f32),
    pub rounds: (i64, i64),
    pub bursts: (i64, i64),
    pub burst_delay: (f32, f32),
    pub delay: (f32, f32),
}

/// A trench (BarricadeObject, vtable 0x005a7068, behavior "barracade" = class 141, game_03.cpp:3067; lev_1_m1's
/// `trench` 37..41). Its type (vt+0x138 0x004563c0) is 3, so its load (ai_01.cpp:1019) gives it one ground slot with
/// limit 1: one grunt at a time. vt+0x14c 0x0040dcb0 gives its ref_exit, vt+0x150 0x0040dd10 its ref_barracade (point
/// = the node's translation, direction = -(its y axis)); vt+0x148 0x0040c100 (free: no occupant, count < limit),
/// vt+0x158 0x0040c400 (occupy), vt+0x15c 0x0040c4a0 (free the slot).
#[derive(Component)]
pub struct Barricade {
    /// the object's position (vt+0x54: the placement), ref_exit, ref_barracade and its facing (horizontal)
    pub origin: Vec3,
    pub exit: Vec3,
    pub spot: Vec3,
    pub dir: Vec3,
    /// the slot's occupant (+0x4b0[0]); no code path frees it when the occupant dies (static reading: Character_Die
    /// 0x004319e0 / 0x00427100 call neither vt+0x188 nor vt+0x18c), so a dead grunt keeps his trench
    pub occupant: Option<Entity>,
    /// its collision mesh: the cover's own line-of-sight test passes through it (0x00426fd0's `exclude`)
    pub collider: Option<BodyId>,
}

/// The barricade states (GenericAI_Update 0x0043ee90, ai_06.cpp:2055-2218; anim ids by GruntObject 0x0043bd70).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CoverPhase {
    /// 0xa0: running to ref_exit (the path of 0x0046f6d0)
    ToExit,
    /// 0xa1: from there to ref_barracade; on arrival +0x484 |= 0x20000, anim 0x66
    ToSpot,
    /// 0xa2: grunt_barricade_reload (0x66, 1.333 s) facing the slot's direction; at its end the fire counters are
    /// rolled (0x00427600) and 0xa3 follows
    Duck,
    /// 0xa3: grunt_barricade_breathe (0x65, looped) for 1..6 s (0 when the target is outside 80 degrees of the slot's
    /// direction); then 90 % stand up (0xa4), else peek (0xa5)
    Breathe,
    /// 0xa5: grunt_barricade_peek (0x67, 2.633 s, no fire notes), then back to 0xa3 (counters left) or 0xa2
    Peek,
    /// 0xa4: the stand set (ids 2, 3, 0, 1: grunt_stand_*, 1.967 s) turning to the target; at its end engage
    /// (0x00427910): in range the fire state 0xa (keeping the trench), else 0xa2; no target 0xa2
    Stand,
    /// 0xa6: the fire spray over the trench (anim 0x68, vt+0x1fc: the captain's captain_barricade_fire_spray 1.967 s,
    /// a shot at each of its "fire" notes); its end as the peek's
    Spray,
}

/// A grunt's trench (+0x904 with its slot +0x914 = 0, ref_barracade +0x934, the slot direction +0x908).
#[derive(Clone, Copy, Debug)]
pub struct Cover {
    pub trench: Entity,
    pub phase: CoverPhase,
    pub timer: f32,
    pub exit: Vec3,
    pub spot: Vec3,
    pub dir: Vec3,
}

#[derive(Component)]
pub struct Grunt {
    pub mind: Mind,
    pub home: Vec3,
    pub goal: Vec3,
    pub timer: f32,
    pub base: Quat,
    pub yaw: f32,
    pub move_speed: f32,
    pub patrol_speed: f32,
    pub limp_speed: f32,
    pub limp_pct: f32,
    pub max_health: f32,
    pub scan_distance: f32,
    pub hear_distance: f32,
    /// engagement range +0x5d8, drawn once from `fire_range` (fire_range_min..max +0x5d0 / +0x5d4; the rocket
    /// captain's rkt_*, the jumper's jumper_*)
    pub range: f32,
    pub fire_range: (f32, f32),
    pub alert_group: i64,
    pub guard: bool,
    pub trigger_los: bool,
    pub search_timer: f32,
    pub target: Option<Entity>,
    /// alert-group hand-over (+0x578 / +0x57c)
    pub pending: Option<(Entity, f32)>,
    /// trigger_marker mode (ai_01.cpp:2498) and the troops (0x0042db50 / 0x0042dd50): marker_tag_min..max, taken once
    /// by the spline run (`spline`, AI_FollowMarkerSpline 0x0042d1b0)
    pub marker_tags: Option<(i64, i64)>,
    /// a barracks troop walking out to the exit slot's point (state 0x5a, set by 0x0042db50); then his marker run
    /// (marker tags set: 0x0042d1b0) or 20..50 ahead within 45 degrees (state 0x5b) and idle
    pub exit: Option<Vec3>,
    /// a route to the goal over the ground when the straight line is blocked (the original walks the nav grid,
    /// 0x0046f6d0; here nav.rs A*, re-planned every second)
    route: Vec<Vec3>,
    replan: f32,
    /// a marker run's start (state 0x96: 0x00470aa0 plans the route on the grid before 0x00470c30 walks it): the time
    /// left before he sets off (`ROUTE_PLAN`)
    pub route_wait: f32,
    /// his own entity: his bullets' owner (the damage record's source +0x2c)
    pub me: Option<Entity>,
    /// score_value: to the Kills money when he dies (Game+0x2724)
    pub score: f32,
    /// the source of the last damage taken (the hit message's +0x2c), offered as a target (0x0042c820)
    pub attacker: Option<Entity>,
    /// game time a target was last adopted by an offer (+0x514)
    pub offer_t: f32,
    /// the fire counters' ranges (0x00427600 from +0x58c..+0x5b0): `fire_num_clips` clips before he moves on,
    /// `fire_num_rounds` shots per clip, `fire_num_bursts` shots per burst (at most the rounds left),
    /// `fire_burst_delay` between bursts, `fire_clip_delay` after a clip's reload
    pub clips: (i64, i64),
    pub rounds: (i64, i64),
    pub bursts: (i64, i64),
    pub burst_delay: (f32, f32),
    pub clip_delay: (f32, f32),
    pub inaccuracy_angle: f32,
    pub inaccuracy_height: f32,
    pub accuracy: (f32, f32),
    pub infinite_accuracy: (f32, f32),
    pub infinite: bool,
    /// shots left in the clip (+0x5bc)
    pub left_rounds: i64,
    /// shots left in the burst (+0x5b8)
    pub left_burst: i64,
    /// clips left (+0x5b4)
    pub left_clips: i64,
    pub pause: f32,
    pub bullet_speed: f32,
    pub bullet_damage: f32,
    /// the weapon's and its projectile's property sets (p_grunt_rifle / p_grunt_bow and their projectiles)
    pub weapon: crate::gamedata::Props,
    pub bullet: crate::gamedata::Props,
    pub bow: bool,
    pub hit_ragdoll: f32,
    pub last_health: f32,
    /// a guard's first fire cycle after a turn toward its target is dry (no shot: the counters are empty), then the
    /// reload: measured in the original
    /// (hook AI dump, second encounter: the guards play fire (anim 0x23) at 740..760, reload (0x2a) by 780..820, and
    /// their first shots come at 800..837; the sound log has none in between), as fire-timing note #7 predicted
    pub dry_first: bool,
    /// a turn toward the target before firing (0x0042e050: target more than ~45 degrees off: grunt_turn_180 0.6 s
    /// behind (dot < -0.7), grunt_turn_left / right_90 0.5 s, the archers' grunt_bow_turn 0.633 s): the anim, time left
    pub turn: Option<(&'static str, f32)>,
    /// the aim weights on foot (+0x580 / +0x584, 0x00428fc0 from the turn 0x0042df70): the fire set's and the slot's
    /// stand set's blend
    pub aim: aim::AimBlend,
    /// +0x484 bit 14: turning to the target (0x0042df70: set when the aim is off target, cleared once he faces it)
    pub turning: bool,
    pub last_hit: f32,
    /// the fling of a blast that killed him (Character_GoRagdoll 0x00431310: a record with an owner and an impact
    /// over 10050 throws the body along its direction at impact x 0.15 on his 80 kg), units/s, engine axes
    pub blast_fling: Option<Vec3>,
    /// hit_level_1 / hit_level_2 (+0x9e8 / +0x9ec): the damage limits of the small and medium hit reactions
    pub hit_level_1: f32,
    pub hit_level_2: f32,
    /// a window / tower grunt's place (physics off there: 0x00425e20), held each frame
    pub fixed: Option<Vec3>,
    /// the hit reaction playing (states 0x12f small, 0x130 medium, 0x131 big): its anim, and its kind 1..3; the grunt
    /// neither moves nor fires meanwhile; charanim.rs clears it when the anim ends
    pub hit: Option<(String, u8)>,
    pub hurt_voice_timer: f32,
    pub dead_said: bool,
    pub voice_set: u8,
    /// his own voice handles (+0xb4c scream, +0xb50 line): audio.rs Speech
    pub speech: crate::audio::Speech,
    pub ray_timer: f32,
    /// died by a hit of at least hit_ragdoll (blown away as a body) rather than a death anim
    pub ragdoll: bool,
    /// which death anim (grunt_die_1..6) when not ragdolled
    pub death_anim: u8,
    /// the death anim Character_Die picked instead of grunt_die_N (window, ledge)
    pub death_name: Option<&'static str>,
    /// ledge_death (+0x484 bit 31): falls off his ledge when he dies
    pub ledge_death: bool,
    /// can_be_sharked (default 1; clear: +0x48c 0x100, Ai_InitGeneric 0x004247f0): the land shark may bite him
    pub can_be_sharked: bool,
    /// a window or tower grunt (+0x488 & 0x200080: modes *_window / *_tower)
    pub window: bool,
    /// the last damage record had flag 0x80 (allow_cartoon_death: the sticky bomb's)
    pub cartoon: bool,
    /// where that flagged record came from (its attacker +0x2c: the bomb), engine space: GoRagdoll's launch flies him
    /// away from it (ai/launch.rs)
    pub cartoon_from: Option<Vec3>,
    /// the last damage record had flag 4: a head shot (Projectile_ApplyHitDamage 0x0047eec0, set by player.rs strike);
    /// the hit reaction answers it with the head-hit anim (0x00430340)
    pub head_hit: bool,
    /// chance in % of a pickup on death (pickup_drop_percent, default 100)
    pub drop_percent: f32,
    /// pickup_drop_specific (+0xb2c, Ai_InitGeneric 0x004247f0): the pickup he drops (a weapon table pickup name, or
    /// pickup_health_1 / _2: the Q112's set says pickup_health_2); None: one picked for the player's needs (0x00475a90)
    pub drop_specific: Option<&'static str>,
    /// the trench held (+0x904) and its phase (states 0xa0..0xa5)
    pub cover: Option<Cover>,
    /// +0x484 0x20000: in the trench's slot (set on arrival, 0xa1; cleared on leaving, 0x004273a0 / 0x004274e0): the
    /// target search sees all round (AI_FindTarget ai_02.cpp:771), the clip's end and a lost line of sight duck
    /// (0xa2) instead of reloading or pursuing, and the death anim is 0x56 grunt_barricade_death (0x00427100)
    pub at_barricade: bool,
    /// +0x918 / +0x91c / +0x920: the covers left last (0x004273a0); the search skips the first two
    pub cover_history: [Option<Entity>; 3],
    /// the generic cover slot held (+0x904 / +0x914 of a tree or a building: coverslot.rs)
    pub slot: Option<coverslot::SlotHold>,
    /// a hiding-spot search asked for from outside the think loop (vt+0x240(1, 0, 0)), run at his next think as at a
    /// reload's end (flag 4 set): a breach runner the closing gate stops (army.rs), the playtest's `cover` step
    pub force_hide: bool,
    /// +0x484 0x20: his runs are the scared run (anim 0xb run_scared): the pre-success retreat's 20 % (ai/retreat.rs)
    pub scared_run: bool,
    /// a flee (state 0xc8) with the sticky bomb on him (+0xb54 held): its timer does not run, he runs to his point
    pub flee_bomb: bool,
    /// this frame's hit had record flag 0x100 (a base turret round, player.rs `DamageKind::BaseTurret`): the long hit
    pub long_hit_round: bool,
    /// +0x484 flag 4: the fire counters are rolled (0x00427600: at init, a reload's end, the trench's duck); cleared at a
    /// clip's end (0x00428980)
    pub fire_ready: bool,
    /// +0x484 0x200 (FindHidingSpot's a2, a hide marker's search): in the trench the breathing is not cut short by a
    /// target outside the slot's arc, and without a target the grunt peeks instead of standing up
    pub hold: bool,
    /// the mounted gun held (+0x898) and its state
    pub gun: Option<Gun>,
    pub stig: StigTable,
    /// the forge cannon's tables (+0x8cc..+0x8f8, forge_fire_*)
    pub forge: StigTable,
    /// the class (vtable): anim table, weapon, the yes / no slots
    pub kind: Kind,
    /// the run / limp anims' playback rates by his speed (anim_run_* / anim_limp_*: skin.rs RunRates, charanim.rs)
    pub run_rates: crate::skin::RunRates,
    /// his arrows' model (an archer's projectile template, grunt_normal_arrow: grunt_arrow_1; arrows.rs), none for bullets
    pub arrow_model: Option<String>,
    /// the rest stance: attention (anim 0xf, with the fidgets 0x00427330: attention_* modes at start) or ready (the
    /// stand set 2, 3, 0, 1 by PlayAnim4: ready_* modes, and every return to rest after a fight, which plays the set:
    /// 0x0042bc60, 0x7a, the reload's end, L_442860)
    pub attention: bool,
    /// the marker spline followed (+0x570 its head, +0x450 the index, +0x558..+0x55c the tag range): `None` while not
    /// on one; `started` once the walk to the head (state 0x96) arrived and the markers are followed (0x97)
    pub spline: Option<SplineRun>,
    /// +0x488 bit 1 marker_looping (wrap at the spline's end), bit 0 marker_patrol: read by Ai_InitGeneric
    pub looping: bool,
    /// +0x488 bit 0 marker_patrol: a spline walked at patrol_speed (+0x954) where its markers name no speed
    pub patrol: bool,
    /// +0x488 0x200: a troop of an alarm generator (0x0042dd50) charges the player when it has no marker tags
    pub charge: bool,
    /// mode trigger_charge (+0x488 0x100): his activation (0x0042f5c0) is a charge (0x0042ce70)
    pub trigger_charge: bool,
    /// receive_trigger_kill (+0x530): this trigger kills him (the trigger handler 0x0042f870 -> vt+0xd4); 0 none
    pub kill_trigger: i64,
    /// trigger_no_damage (+0x48c 0x4000, read for mode trigger_charge, ai_01.cpp:2465): while it is set, damage is
    /// ignored (the monk's damage slot 0x004371e0; the other classes' slot 0x00430340 does not look at it, so only a
    /// monk keeps it); his activation clears it (0x0042f5c0)
    pub no_damage: bool,
    /// a charge (0x0042ce70) calls the pursuit (vt+0x184) at once, whatever the range: the next decision is the
    /// pursuit (a jumper's take-off, 0x004295d0), engaging comes after it
    pub pursue: bool,
    /// the remembered-point list +0x958 exists: a charge (0x0042ce70 -> 0x004246c0(1)) makes it (a new grunt has none,
    /// game_02.cpp:1669), and while it does the target is kept at any range (0x0043ecf0); the engage's in-range branch
    /// with +0x488 0x100 (trigger_charge) removes it (0x00427910, ai_02.cpp:1183-1187). Every charge sets it (also
    /// level.rs's spawned troops, squad.rs's attack, tossed.rs's smoking troop, bully.rs).
    pub charged: bool,
    /// the heading the marker being walked to must be faced in before it counts as reached: a face_dir marker hands
    /// the next marker's heading to the route (AIMarker_Advance 0x0044b980 -> the node's flag 8), and AIRoute_CheckReached
    /// 0x0046f900 then waits, turning, until the facing's dot with it is at least 0.98 (K 0x005bcd30)
    pub marker_face: Option<Vec3>,
    /// one-shot anim playing (taunt 0x6e, throw 0x10): its id and time left
    pub anim_id: u32,
    pub anim_left: f32,
    /// the grenade (captain) left the hand this throw
    pub thrown: bool,
    /// the hit reaction that ran (states 0x12f small 1, 0x130 medium 2, 0x131 big 3, 0x132 long 4, 0x12c..0x12e all
    /// fours 5..7): its exit runs when the anim ends (ai_06.cpp:3191-3412)
    pub reacting: u8,
    /// the anim id of the last reaction (+0x498: the medium hits alternate 0x43 / 0x44)
    pub hit_id: u32,
    /// +0x484 flag 1: the close attack (the captain's grenade) is allowed; set when the fire counters are rolled
    /// afresh (0x00427600 with flag 4 clear), cleared by the throw (0x00428340)
    pub throw_armed: bool,
    /// long_hit_pct +0x9e4 (60), allfours_pct +0x9e0 (20): health percentages for the long hit / the all-fours fall
    pub long_hit_pct: f32,
    pub allfours_pct: f32,
    /// the priority target tags (+0x858 / +0x85c: an alarm troop's generator's priority_target_tag_min / max,
    /// 0x0042cc60; a window grunt's): AI_FindTarget takes the first live object of these tags (not on lev_1_m1)
    pub priority_tags: (i64, i64),
    /// Game+0x2708 == 1: a world-1 (cold) level, where id 18 is the shiver (level.rs sets it from the level's folder)
    pub cold: bool,
    /// 0x0044e600: a base-defence level (the mission's start_in_turret_tag; `mark_base_defence` sets it)
    pub base_defence: bool,
    /// +0x484 0x8000000 (rocket_captain): the rocket launcher and its aim / fire sets
    pub rocket: bool,
    /// +0x488 0x4000000 with +0x588 held: the sniper has his gun raised at a target (SetTarget 0x00434930)
    pub aiming: bool,
    /// a jumper captain's turbo pack (+0xb08, 0x00425d30) and its flight
    pub jumper: Option<Jumper>,
    /// mode turret: the instance id of the gun his obj_tag names (level.rs), taken by the first frame
    pub start_gun: Option<u32>,
    /// his squad and his bone in it (+0x9bc / +0x9c0, joined by 0x0042de60)
    pub squad: Option<squad::SquadLink>,
    /// moving this frame (charanim.rs: the march of a squad member)
    pub walking: bool,
    /// a one-shot anim that holds the grunt still while it plays (the sniper's attach / detach, state 0x104; the
    /// jumper's landing 0xbf): its name and time left
    pub pose: Option<(&'static str, f32)>,
    /// the fire anim's length (one shot each), the reload's: 0 until think reads them from the class's anims
    pub fire_len: f32,
    pub reload_len: f32,
    /// seconds since his last shot (charanim.rs: a one-shot fire anim, then the aim set)
    pub shot_age: f32,
    /// the share of a bullet's damage he takes (player.rs; the Q112's bullet_scale)
    pub bullet_scale: f32,
    /// a monk's special counter and teleport (monk.rs)
    pub monk: Option<monk::Monk>,
    /// a guard engaging his target from his guard state (0x79 with +0x588 held: the fire cycle runs there)
    pub guard_firing: bool,
    /// a guard's reload set playing at a clip's end (anim 0x2a with flag 4 clear)
    pub reloading: bool,
    /// mode bully: his peasant and the scares (bully.rs)
    pub bully: Option<bully::Bully>,
    /// the Q112's own parts: the trail it keeps of its target, its eye loop (q112.rs; its swing is ai/melee.rs)
    pub q112: Option<q112::Q112>,
    /// his place under the mission's shooter limit (+0x488 0x20000 / 0x100000, max_fire.rs)
    pub shooter: max_fire::Shooter,
    /// handed the turn by a shooter that stopped (0x0042bb10 -> vt+0x230(0)): engage again this frame
    pub retry_engage: bool,
    /// a fall in progress (state 0x82) and the falling watchdog's timer (+0x890, ai/fall.rs)
    pub fall: Option<fall::Fall>,
    pub fall_watch: Option<f32>,
}

impl Grunt {
    /// GenericAIObject slot 94 0x0042f8d0 (vt+0x178): he cannot take `other` as a target now ("busy"): in an IC move
    /// (0x00426630), dead, on a gun (+0x898), `other` himself, or in one of the states 0xc..0x10 (here the hold by a
    /// companion 0xf and the grenade throw 0x10), 0x7c, 0x8c (waiting for his trigger), 0x96 (to a spline's head),
    /// 0xdc / 0xde (a squad member), 0xf0 (a monk's teleport). Not modelled: the states with no counterpart here
    /// (0x5a, 0xaa, 0xac, 0xe7..0xeb, 0x197, 0x19a..0x19d), +0x894, the turn state 0x31, the parent rule, +0x484 bits 19
    /// and 28, the ride along a spline (0x97 with 0x0044b4b0).
    pub fn busy(&self, me: Entity, other: Entity) -> bool {
        me == other || self.busy_now()
    }

    /// [`Grunt::busy`] without the `other == self` test: busy whoever asks (a scare by a bomb, a hit with no source).
    pub fn busy_now(&self) -> bool {
        self.gun.is_some()
            || matches!(self.mind, Mind::Dead | Mind::Scripted | Mind::Held | Mind::Throw | Mind::WaitTrigger | Mind::InSquad | Mind::Teleport)
            || (self.mind == Mind::ToMarker && self.spline.is_some_and(|r| !r.started))
    }

    /// The aim error's scale at 2D distance `d` (0x004284e0, ai_02.cpp:1690-1700): from the accuracy ranges, the
    /// infinite ones with +0x484 0x400000; d <= min 0.1, beyond max 1, between max(0.1, (d^2 - min^2) / (max^2 - min^2)).
    fn aim_error_scale(&self, d: f32) -> f32 {
        let (lo, hi) = if self.infinite { self.infinite_accuracy } else { self.accuracy };
        if d <= lo {
            0.1
        } else if d >= hi {
            1.0
        } else {
            ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1)
        }
    }

    /// The engage's close attack (0x00427910 with allow_close_attack, ai_02.cpp:1199-1208): a class with one (vt+0x1e0,
    /// the captain's grenade), in range, the counters rolled (flag 4) and armed (flag 1), and `roll` (rand * 200 >> 15
    /// < 30) passing; the roll is drawn only when the rest holds.
    fn engage_throws(&self, in_range: bool, roll: impl FnOnce() -> bool) -> bool {
        self.kind.close_attack() && in_range && self.fire_ready && self.throw_armed && roll()
    }

    /// Starts the close attack the engage chose (anim 0x27, state 0x10).
    fn start_throw(&mut self, lib: &mut crate::skin::AnimLibrary) {
        self.anim_id = 0x27;
        self.anim_left = anim_len(lib, self.anim(0x27, 0.0));
        self.thrown = false;
        self.mind = Mind::Throw;
    }

    /// The anim of an id by the class's table and this grunt's state (vt+0x1a4, `anim_name_with`)
    pub fn anim(&self, id: u32, r: f32) -> &'static str {
        anim_name_with(self.kind, id, r, self.anim_flags())
    }
    pub fn anim_flags(&self) -> AnimFlags {
        AnimFlags {
            cold: self.cold,
            rocket: self.rocket,
            glide: self.jumper.is_some_and(|j| j.flight == Some(jumper::Flight::Glide)),
            aiming: self.aiming,
            base_defence: self.base_defence,
        }
    }
}

/// A grunt's marker spline run (AI_FollowMarkerSpline 0x0042d1b0).
#[derive(Clone, Copy, Debug)]
pub struct SplineRun {
    /// the spline's tag and the marker index on it (+0x450)
    pub tag: i64,
    pub index: usize,
    /// state 0x97 (following) once the head is reached; state 0x96 before
    pub started: bool,
}

/// What the AI hunts: Roman.
#[derive(Component)]
pub struct Player;

/// A death decided by what killed him (a weapon's own death record): the anim his death plays on slot 0 instead of
/// the usual pick, never a ragdoll (the land shark's bite: 0x1000 -> anim 0x58 grunt_sharkbite, state 0x191;
/// weapons/landshark.rs).
#[derive(Component, Clone, Copy, Debug)]
pub struct DeathAnim(pub &'static str);

/// An anim a weapon's effect holds a live character in, by the class's anim id (the black hole's pull: 0x49 / 0x4a
/// grunt_blown_up / _2 looped; ai_06.cpp:3159): charanim.rs plays it over the state's own.
#[derive(Component, Clone, Copy, Debug)]
pub struct AnimOverride {
    pub id: u32,
    pub looping: bool,
}

#[derive(Resource, Default)]
pub struct AiStats {
    pub ms: f32,
    pub alive: usize,
    pub dead: usize,
    pub shots: usize,
    pub enabled: bool,
    pub states: [usize; 32],
    /// grunts holding a target: the player, a companion (playtest log, for comparisons with the hook's AI dump)
    pub targets: [usize; 2],
    /// marker arrivals so far, taunts, grenades thrown (playtest log)
    pub markers: usize,
    pub taunts: usize,
    pub throws: usize,
    /// bursts ended with rounds left in the clip, pursuits started by a lost line of sight (playtest log)
    pub bursts: usize,
    pub sight_pursuits: usize,
    /// shots by class (playtest log "ai.shots_by"): the snipers', the rocket captains', the jumpers' in a glide
    pub sniper_shots: usize,
    pub rocket_shots: usize,
    pub glide_shots: usize,
    /// jumps to a marker started, landings
    pub jumps: usize,
    pub landings: usize,
    /// deaths flung by a blast (`Grunt::blast_fling`) and the fastest fling (units/s)
    pub blast_flings: usize,
    pub max_fling: f32,
    /// monks' teleports begun (MonkObject_Bamf 0x00436bb0)
    pub teleports: usize,
    /// squads halted by a target, members sent to charge (squad.rs)
    pub squad_halts: usize,
    pub squad_charged: usize,
    /// a gunner's shots at the led point (flag 1, ai/aim.rs)
    pub led_shots: usize,
    /// grunts killed by their receive_trigger_kill (0x0042f870)
    pub trigger_kills: usize,
    /// scares by a sticky bomb that sent a grunt running (0x0042fb60), and how far he was sent at most
    pub flees: usize,
    pub max_flee: f32,
}

/// Debug (playtest `ai_force`): forced outcomes of the AI's next rolls, each consumed by the behaviour it names ("throw":
/// the next captain's engage roll throws, ai.rs; "fight": the next companion's close kill coin passes, ai/close_kill.rs).
/// Kept, not consumed: "no_cover" (no generic cover slots), "companions_hold" (the companions take no target: a
/// test of an enemy's own behaviour that their fire would disturb) and "grunts_hold" (the grunts look for no target:
/// a test whose thrown bomb their bullets would shoot down, 0x00481320).
#[derive(Resource, Default)]
pub struct AiForce(pub Vec<String>);

impl AiForce {
    /// Takes one forced `what`, if queued.
    pub fn take(&mut self, what: &str) -> bool {
        match self.0.iter().position(|w| w == what) {
            Some(i) => {
                self.0.remove(i);
                true
            }
            None => false,
        }
    }
}

/// Sticky bomb stuck at a point (game_07.cpp:1240: AI within 25 units are scared).
#[derive(Message, Clone, Copy)]
pub struct StickyStuck(pub Vec3, pub Option<Entity>);

pub struct AiPlugin;

/// The kill meter and the "dismay" banter (MissionObject_Update, game_02.cpp:2473-2494): every bad guy killed adds 1
/// (Game+0xbb8, ai_04.cpp:594), the meter drains 0.5 / s (+0xbb4); when the cooldown (+0xbc8) has run out and the
/// meter is at 3 (+0xbbc) or more, one line of table 0x005ce570 plays (Roman, Jonesy, Q: "Ha ha, did you see that?!"
/// ...; 0x00493b70), the meter is cleared and the cooldown restarts at 30..45 s (+0xbc0 / +0xbc4).
#[derive(Resource, Default)]
pub struct KillMeter {
    pub meter: f32,
    pub cooldown: f32,
    /// lines said (the playtest log's dismay.lines)
    pub lines: u32,
}

fn dismay(
    time: Res<Time>,
    mut km: ResMut<KillMeter>,
    mut rng: ResMut<FxStats>,
    mut companions: Query<(&mut Companion, &Transform, Option<&Health>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut bags, voices, log): (ResMut<dialogue::Bags>, crate::audio::PlayingVoices, Option<ResMut<crate::playtest::PlayState>>),
) {
    let dt = time.delta_secs();
    km.cooldown -= dt;
    if km.cooldown < 0.0 && km.meter >= 3.0 {
        km.cooldown = rng.range(30.0, 45.0);
        km.meter = 0.0;
        // one line of table 0x005ce570 whose speaker can say it now (0x00493b70 with the test 0x0044da10: Roman
        // always, Jonesy / Q while there and not talking; dialogue.rs)
        let present = |q: bool| {
            companions.iter().find(|(c, _, h)| c.q == q && c.dead.is_none() && h.is_none_or(|h| h.0 > 0.0)).map(|(c, _, _)| dialogue::Present { speaking: c.speech.saying(&voices), still: 0.0 })
        };
        let cast = dialogue::Cast { roman_alive: true, jonesy: present(false), q: present(true), ..default() };
        if let Some((_, said)) = bags.dismay.pick(|| rng.rand(), |l| dialogue::dismay_speaker(l, &cast)) {
            say(said, &mut companions, &voices, &mut sounds);
            km.lines += 1;
            if let Some(mut log) = log {
                log.0.insert("dismay".into(), serde_json::json!({"lines": km.lines, "speaker": format!("{:?}", said.speaker).to_lowercase(), "line": said.line}));
            }
        }
    }
    km.meter = (km.meter - dt * 0.5).max(0.0);
}

/// Plays a banter line a speaker test accepted: Roman's without a position, a companion's at him, kept as his line
/// (+0xb50).
pub fn say(said: dialogue::Said, companions: &mut Query<(&mut Companion, &Transform, Option<&Health>)>, voices: &crate::audio::PlayingVoices, sounds: &mut MessageWriter<crate::audio::PlaySound>) {
    let who = match said.speaker {
        dialogue::Speaker::Roman => None,
        dialogue::Speaker::Jonesy => Some(false),
        dialogue::Speaker::Q => Some(true),
    };
    match who.and_then(|q| companions.iter_mut().find(|(c, ..)| c.q == q)) {
        Some((mut c, t, _)) => {
            let req = c.speech.line(voices, dialogue::request(said, Some(t.translation)));
            sounds.write(req);
        }
        None => {
            sounds.write(dialogue::request(said, None));
        }
    }
}

impl Plugin for AiPlugin {
    fn build(&self, app: &mut App) {
        app.insert_level_resource(|| AiStats { enabled: true, ..default() })
            .init_level_resource::<KillMeter>()
            .init_level_resource::<Barks>()
            .add_message::<StickyStuck>()
            .init_resource::<grenade::GrenadeAssets>()
            .init_level_resource::<grenade::GrenadeStats>()
            .init_level_resource::<rocket::RocketHullStats>()
            .init_level_resource::<AiForce>()
            .init_level_resource::<fall::FallStats>()
            .init_level_resource::<assault::Assault>()
            .init_level_resource::<retreat::RetreatStats>()
            .init_level_resource::<ladder::LadderStats>()
            .init_level_resource::<notes::ScriptEndStats>()
            .init_level_resource::<base_area::BaseAreaStats>()
            .add_message::<notes::ScriptEnd>()
            .init_level_resource::<paratroop::ParatroopStats>()
            .init_resource::<fall::ParachuteAssets>()
            .init_level_resource::<aim::TargetMotion>()
            .init_level_resource::<Interests>()
            .init_level_resource::<bully::BullyLines>()
            .init_level_resource::<ic::IcStats>()
            .init_level_resource::<launch::LaunchClock>()
            .add_systems(Update, (launch::tick, launch::log_state).run_if(crate::playing))
            .add_systems(Update, monk_smoke.run_if(crate::playing))
            .init_level_resource::<dialogue::Bags>()
            .add_plugins(q112::Q112Plugin)
            .init_level_resource::<max_fire::ShooterLimit>()
            .add_systems(Update, mark_base_defence.before(think).run_if(crate::playing))
            .add_systems(
                Update,
                (
                    (difficulty_health, body::bumps, squad::update_squads, interests, mark_army_targets, search, ic::activate, (ic::knocks, base_area::cull).chain(), scare, max_fire::sync_limit, aim::track, think, max_fire::upkeep, fall::run, ic::run, paratroop::rappel, notes::from_movers, notes::apply, bully::bullies, (trigger_kills, die, dismay).chain()).chain(),
                    (
                        (body::apply_rules, log_cover, log_ai, log_placed, squad::log_squads, ic::log).chain(),
                        // the later levels' infantry (projectiles, falls, drops, the wall assault) and their playtest logs
                        (grenade::fly, jumper_packs, rocket::shoot_down, fall::drop_parachutes, jumper::free_markers, assault::sync, assault::recruit, (ladder::ladders, ladder::carry_anim).chain(), retreat::run, ic::building_deaths).chain(),
                        (melee::close_hits, melee::log, max_fire::log, grenade::log, rocket::log, fall::log, paratroop::log, jumper::log, notes::log, assault::log, ladder::log, retreat::log, base_area::log).chain(),
                    )
                        .chain(),
                )
                    .chain()
                    .after(crate::fx::FxSet)
                    .after(crate::turret::setup),
            );
    }
}

/// What 0x0044e600 answers (a base-defence level: the level starts Roman in a turret, the mission's
/// start_in_turret_tag) where a new AI reads it:
/// - the anim tables (`Grunt::base_defence`): set on each new AI, and on all of them when the mission (re)loads;
/// - CaptainObject_Slot8 0x00434da0 (also the sniper captain's), with Roman there: a new captain engages at his
///   greatest range (+0x5d8 = +0x5d4, no roll). Its other write there, +0x5c8 = 2.0 (the clip pause), is cleared by
///   his first engagement's counter roll (0x00427600 with flag 4 of +0x484 clear zeroes +0x5c8) before anything reads
///   it, so it has no effect.
fn mark_base_defence(mission: Option<Res<crate::mission::Mission>>, mut grunts: Query<&mut Grunt>, player: Query<(), With<Player>>) {
    let on = mission.as_ref().is_some_and(|m| m.start_in_turret);
    let all = mission.as_ref().is_some_and(|m| m.is_changed());
    for mut g in &mut grunts {
        let new = g.is_added();
        if (all || new) && g.base_defence != on {
            g.base_defence = on;
        }
        if new && on && !player.is_empty() && matches!(g.kind, Kind::Captain | Kind::Sniper) {
            g.range = g.fire_range.1;
        }
    }
}

/// The playtest state's `cover`: trenches held, grunts in a slot, the first held cover's phase, deaths in a trench.
fn log_cover(grunts: Query<&Grunt>, trenches: Query<(&Barricade, &crate::level::InstanceId)>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let held = trenches.iter().filter(|(b, _)| b.occupant.is_some()).count();
    // each trench by instance id: held or not ("cover.slots.37")
    let slots: serde_json::Map<String, serde_json::Value> = trenches.iter().map(|(b, i)| (i.0.to_string(), b.occupant.is_some().into())).collect();
    let live = grunts.iter().filter(|g| g.mind != Mind::Dead);
    let in_slot = live.clone().filter(|g| g.at_barricade).count();
    let going = live.clone().filter(|g| g.cover.is_some() && !g.at_barricade).count();
    let phase = live.clone().find_map(|g| g.cover.map(|c| format!("{:?}", c.phase))).unwrap_or_default();
    let firing = live.filter(|g| g.at_barricade && g.mind == Mind::Fire).count();
    let deaths = grunts.iter().filter(|g| g.mind == Mind::Dead && g.death_name == Some("grunt_barricade_death")).count();
    // the first grunt with a mounted gun: its phase, shots, aim weights
    let gunner = grunts.iter().filter(|g| g.mind != Mind::Dead).find_map(|g| g.gun);
    state.0.insert(
        "gunner".into(),
        match gunner {
            Some(gn) => serde_json::json!({"phase": format!("{:?}", gn.phase), "shots": gn.shots, "h": gn.aim.h, "v": gn.aim.v, "firing": gn.firing}),
            None => serde_json::json!({"phase": "", "shots": 0}),
        },
    );
    state.0.insert("cover".into(), serde_json::json!({"held": held, "in_slot": in_slot, "going": going, "phase": phase, "firing": firing, "deaths": deaths, "slots": slots}));
}

/// The playtest state's `ai`: the live grunts by state, the reactions, spline runs, captains, grenades, noises, and the
/// grunt nearest Roman (its state, class, stance, reaction, whether it targets him).
/// The state log's "grunt_ids" (playtest expects, e.g. "grunt_ids.286.mind": "Dead"): every placed AI (a level
/// instance, not a troop made in play) by instance id: game-space position, mind, health. A placed AI whose body
/// was removed (corpses.rs) stays listed as dead: {"mind": "Dead", "removed": true}.
fn log_placed(
    grunts: Query<(&Grunt, &GlobalTransform, &crate::level::InstanceId, Option<&crate::fx::Health>, Has<crate::skin::Animator>, Has<crate::physics::Body>)>,
    corpses: Option<Res<crate::corpses::Corpses>>,
    state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mut state) = state else { return };
    let removed = corpses.iter().flat_map(|c| c.removed_instances.iter()).map(|id| (id.to_string(), serde_json::json!({"mind": "Dead", "removed": true})));
    let out: serde_json::Map<String, serde_json::Value> = removed.chain(grunts
        .iter()
        .map(|(g, t, id, h, animated, body)| {
            let p = t.translation();
            let pos = [p.x.round(), (-p.z).round(), p.y.round()];
            // (rigged: the crosshair and bullets test his parts; a body: his capsule)
            (id.0.to_string(), serde_json::json!({"pos": pos, "mind": format!("{:?}", g.mind), "health": h.map(|h| h.0.round()), "rigged": animated, "body": body, "gun_shots": g.gun.map(|gn| gn.shots), "gun_held": g.gun.map(|gn| gn.held), "slot": g.slot.map(|s| s.arrived)}))
        }))
        .collect();
    state.0.insert("grunt_ids".into(), out.into());
}

fn log_ai(
    grunts: Query<(Entity, &Grunt, &GlobalTransform)>,
    roman: Query<(Entity, &GlobalTransform), With<Player>>,
    grenades: Query<(), With<grenade::Grenade>>,
    noise: Res<Interests>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    stats: Res<AiStats>,
) {
    let Some(mut state) = state else { return };
    let (re, rp) = roman.single().map(|(e, t)| (Some(e), t.translation())).unwrap_or((None, Vec3::ZERO));
    let live: Vec<(Entity, &Grunt, Vec3)> = grunts.iter().filter(|(_, g, _)| g.mind != Mind::Dead).map(|(e, g, t)| (e, g, t.translation())).collect();
    let mut minds = serde_json::Map::new();
    for (_, g, _) in &live {
        let k = format!("{:?}", g.mind);
        let n = minds.get(&k).and_then(|v| v.as_u64()).unwrap_or(0) + 1;
        minds.insert(k, n.into());
    }
    let count = |f: &dyn Fn(&Grunt) -> bool| live.iter().filter(|(_, g, _)| f(g)).count();
    let near = live.iter().min_by(|a, b| a.2.distance(rp).total_cmp(&b.2.distance(rp))).map(|(_, g, p)| {
        serde_json::json!({
            "mind": format!("{:?}", g.mind),
            "kind": format!("{:?}", g.kind),
            "attention": g.attention,
            "reaction": g.hit.as_ref().map(|h| h.0.clone()).unwrap_or_default(),
            "reacting": g.reacting,
            "targets_player": g.target.is_some() && g.target == re,
            "spline": g.spline.map(|r| format!("{}:{}", r.tag, r.index)).unwrap_or_default(),
            "dist": p.distance(rp).round(),
            // the on-foot aim weights' distance from the centre and the turn to the target (ai/aim.rs, 0x0042df70)
            "aim_off": ((g.aim.h - 0.5).abs() * 100.0).round() / 100.0,
            "turning": g.turning,
        })
    });
    let mut ai = serde_json::json!({
            "live": live.len(),
            "minds": minds,
            "reacting": count(&|g| g.hit.is_some()),
            "long_hits": count(&|g| g.reacting == 4),
            "spline": count(&|g| g.spline.is_some()),
            // charged grunts holding Roman beyond 2 x scan_distance (kept by the charge's list +0x958, 0x0043ecf0)
            "charged_far": live.iter().filter(|(_, g, p)| g.charged && g.target.is_some() && g.target == re && (*p - rp).with_y(0.0).length() > 2.0 * g.scan_distance).count(),
            "captains": count(&|g| g.kind == Kind::Captain),
            "snipers": count(&|g| g.kind == Kind::Sniper),
            "aiming": count(&|g| g.aiming),
            "rockets": count(&|g| g.rocket),
            "jumpers": count(&|g| g.jumper.is_some()),
            "flying": count(&|g| g.jumper.is_some_and(|j| j.flight.is_some())),
            "sniper_shots": stats.sniper_shots,
            "rocket_shots": stats.rocket_shots,
            "glide_shots": stats.glide_shots,
            "jumps": stats.jumps,
            "bullies": count(&|g| g.bully.is_some()),
            "on_guns": count(&|g| g.gun.is_some_and(|x| x.phase == GunPhase::On)),
            "scares": live.iter().filter_map(|(_, g, _)| g.bully).map(|b| b.scares).sum::<u32>(),
            "q112": count(&|g| g.kind == Kind::Q112),
            "monks": count(&|g| g.kind == Kind::Monk),
            "teleporting": count(&|g| g.mind == Mind::Teleport),
            "landings": stats.landings,
            "blast_flings": stats.blast_flings,
            "max_fling": stats.max_fling,
            "teleports": stats.teleports,
            "archers": count(&|g| g.kind == Kind::Bow),
            "attention": count(&|g| g.attention),
            "targets_player": live.iter().filter(|(_, g, _)| g.target.is_some() && g.target == re).count(),
            "grenades": grenades.iter().count(),
            "noise": noise.list.len(),
            "markers": stats.markers,
            "taunts": stats.taunts,
            "throws": stats.throws,
            "shots": stats.shots,
            "bursts": stats.bursts,
            "sight_pursuits": stats.sight_pursuits,
            "led_shots": stats.led_shots,
            "near": near.unwrap_or_default(),
    });
    // (a second map: one json! of all of them passes the macro's recursion limit)
    let more = serde_json::json!({
        "trigger_kills": stats.trigger_kills,
        "flees": stats.flees,
        "max_flee": stats.max_flee.round(),
        "priority": count(&|g| g.priority_tags.0 != 0),
        "unlimited_guards": count(&|g| g.infinite),
        "no_damage": count(&|g| g.no_damage),
        "pending": count(&|g| g.pending.is_some()),
    });
    if let (Some(a), Some(m)) = (ai.as_object_mut(), more.as_object()) {
        a.extend(m.clone());
    }
    state.0.insert("ai".into(), ai);
}

/// Ai_InitGeneric 0x004247f0 + the class init (GruntObject_Slot6 0x00434fb0, BowGruntObject_Slot6 0x004347b0,
/// CaptainObject_Slot6 0x00434cd0 with its slot 8 0x00434da0: the range rolled again, AI_ResolveStartObjects 0x00425e20):
/// parameters with the code's defaults, the weapon (the bow at ref_lhand 0xe, the rifle 0xf / the captain's machine gun
/// 0x10 at ref_rhand; a captain's +0x484 0x1000: the fire set loops, 0x00428250), the voice set, the mode's start state.
pub fn make_grunt(p: &crate::gamedata::Props, data: &crate::gamedata::GameData, kind: Kind, home: Vec3, yaw: f32, rng: &mut crate::fxvm::CrtRand) -> Grunt {
    let mode = p.s("mode").unwrap_or("attention_roaming").to_owned();
    // a rocket captain (CaptainObject_Slot6 0x00434cd0 -> RocketCaptain_ReadProperties 0x00434ac0): the rkt_fire_* set
    // replaces the fire parameters (defaults: range 200 / 350, accuracy 50 / 150, infinite 400 / 8000) and the weapon is
    // the rocket launcher (index 0x11); a jumper captain (0x00425d30: the turbo pack +0xb08) fires from
    // jumper_fire_range_min / max (400 / 450)
    let rocket = kind == Kind::Captain && p.i("rocket_captain", 0) != 0;
    let jumper = kind == Kind::Captain && p.i("jumper_captain", 0) != 0;
    let fk = |k: &str| if rocket { format!("rkt_{k}") } else { k.to_owned() };
    let (rmin, rmax) = if rocket {
        (p.f("rkt_fire_range_min", 200.0), p.f("rkt_fire_range_max", 350.0))
    } else if jumper {
        (p.f("jumper_fire_range_min", 400.0), p.f("jumper_fire_range_max", 450.0))
    } else {
        (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0))
    };
    // the weapon by class (0x004a0ef0's index into the weapon table); projectile speed x3 (Projectile_Load)
    let bow = kind == Kind::Bow;
    // (by the templates' property sets: captain_sniper_bullet's is p_captain_sniperbullet)
    let weapon = data.template_props(if rocket { "captain_rocketlauncher" } else { kind.weapon() });
    let proj = data.template_props(weapon.s("projectile_name").unwrap_or("grunt_rifle_bullet"));
    let move_speed = p.f("move_speed", 46.0);
    let guard = mode.contains("guard") || mode.contains("window") || mode.contains("tower");
    let mind = if mode.starts_with("trigger") { Mind::WaitTrigger } else if guard { Mind::Guard } else { Mind::Roam };
    let max_health = p.f("max_health", 100.0);
    // the voice set: Ai_InitGeneric's coin (ai_01.cpp:2438: rand x 100 >> 15 < 50 sets +0x484 bit 21, set 0, else set
    // 1); GruntObject / BowGruntObject_Slot6 roll it again three ways (ai_04.cpp:2073, 2343: < 100 bit 21, < 200
    // +0x488 bit 29 (set 2), else neither), the other classes (captains, the sniper, the monk, the Q112) keep the coin
    let coin = if rng.rand() * 100 >> 15 < 50 { 0 } else { 1 };
    let voice_set = match kind {
        Kind::Grunt | Kind::Bow => match rng.rand() * 300 >> 15 {
            0..100 => 0,
            100..200 => 2,
            _ => 1,
        },
        _ => coin,
    };
    let arrow_model = (proj.s("behavior") == Some("proj_arrow"))
        .then(|| data.templates.get(weapon.s("projectile_name").unwrap_or("")).and_then(|t| t.model.clone()))
        .flatten();
    Grunt {
        run_rates: crate::skin::RunRates::from_props(p),
        arrow_model,
        mind,
        home,
        goal: home,
        timer: rng.r(15.0, 24.0),
        base: crate::level::game_to_engine(),
        yaw,
        move_speed,
        patrol_speed: p.f("patrol_speed", move_speed * 0.25),
        limp_speed: p.f("limp_speed", move_speed * 0.35),
        limp_pct: p.f("limp_pct", 0.0),
        max_health,
        scan_distance: p.f("scan_distance", 300.0),
        hear_distance: p.f("hear_distance", 20.0),
        range: rmin + rng.rand() as f32 * (rmax - rmin) / 32768.0,
        fire_range: (rmin, rmax),
        alert_group: p.i("alert_group", 0),
        guard,
        trigger_los: p.i("trigger_los", 1) != 0,
        // +0x490 starts at 0: the first search runs on the first frame
        search_timer: 0.0,
        target: None,
        pending: None,
        score: p.f("score_value", 0.0),
        me: None,
        marker_tags: (p.s("mode") == Some("trigger_marker")).then(|| (p.i("marker_tag_min", 0), p.i("marker_tag_max", 0))),
        exit: None,
        route: Vec::new(),
        replan: 0.0,
        route_wait: 0.0,
        attacker: None,
        offer_t: f32::NEG_INFINITY,
        clips: (p.i(&fk("fire_num_clips_min"), 1), p.i(&fk("fire_num_clips_max"), 1)),
        rounds: (p.i(&fk("fire_num_rounds_min"), 1), p.i(&fk("fire_num_rounds_max"), 1)),
        bursts: (p.i(&fk("fire_num_bursts_min"), 1), p.i(&fk("fire_num_bursts_max"), 1)),
        burst_delay: (p.f(&fk("fire_burst_delay_min"), 0.0), p.f(&fk("fire_burst_delay_max"), 0.0)),
        clip_delay: (p.f(&fk("fire_clip_delay_min"), 0.0), p.f(&fk("fire_clip_delay_max"), 0.0)),
        // the exe reads fire_inaccuracy_angle (default 15), not the data's fire_inaccuracy_xy
        inaccuracy_angle: p.f(&fk("fire_inaccuracy_angle"), 15.0),
        inaccuracy_height: p.f(&fk("fire_inaccuracy_height"), 10.0),
        accuracy: (p.f(&fk("fire_accuracy_min"), 50.0), p.f(&fk("fire_accuracy_max"), 150.0)),
        infinite_accuracy: (p.f(&fk("fire_infinite_accuracy_min"), 400.0), p.f(&fk("fire_infinite_accuracy_max"), if rocket { 8000.0 } else { 800.0 })),
        infinite: false,
        left_rounds: 0,
        left_burst: 0,
        left_clips: 0,
        pause: 0.0,
        bullet_speed: proj.f("speed", if bow { 25.0 } else { 150.0 }) * 3.0,
        bullet_damage: proj.f("damage", if bow { 10.0 } else { 15.0 }),
        bullet: proj.clone(),
        weapon,
        bow,
        hit_ragdoll: p.f("hit_ragdoll", 50.0),
        last_health: max_health,
        dry_first: false,
        turn: None,
        aim: aim::AimBlend::default(),
        turning: false,
        last_hit: 0.0,
        blast_fling: None,
        hit_level_1: p.f("hit_level_1", 10.0),
        hit_level_2: p.f("hit_level_2", 25.0),
        hit: None,
        fixed: None,
        hurt_voice_timer: 0.0,
        dead_said: false,
        speech: crate::audio::Speech::default(),
        voice_set,
        ray_timer: 0.0,
        ragdoll: false,
        death_anim: (rng.rand() % 6) as u8 + 1,
        death_name: None,
        ledge_death: p.i("ledge_death", 0) != 0,
        can_be_sharked: p.i("can_be_sharked", 1) != 0,
        window: mode.contains("window") || mode.contains("tower"),
        cartoon: false,
        cartoon_from: None,
        head_hit: false,
        drop_percent: p.f("pickup_drop_percent", 100.0),
        drop_specific: p.s("pickup_drop_specific").and_then(drop_specific),
        cover: None,
        at_barricade: false,
        cover_history: [None; 3],
        slot: None,
        force_hide: false,
        scared_run: false,
        flee_bomb: false,
        long_hit_round: false,
        fire_ready: true,
        hold: false,
        gun: None,
        stig: StigTable {
            clips: (p.i("stig_fire_num_clips_min", 3), p.i("stig_fire_num_clips_max", 5)),
            clip_delay: (p.f("stig_fire_clip_delay_min", 1.0), p.f("stig_fire_clip_delay_max", 1.0)),
            rounds: (p.i("stig_fire_num_rounds_min", 10), p.i("stig_fire_num_rounds_max", 10)),
            bursts: (p.i("stig_fire_num_bursts_min", 5), p.i("stig_fire_num_bursts_max", 5)),
            burst_delay: (p.f("stig_fire_burst_delay_min", 0.25), p.f("stig_fire_burst_delay_max", 0.5)),
            delay: (p.f("stig_fire_delay_min", 0.5), p.f("stig_fire_delay_max", 0.75)),
        },
        // (ai_01.cpp:2382-2387: 1 clip, no clip delay, 1 round, bursts of 1, no burst delay, delay 5..7)
        forge: StigTable {
            clips: (p.i("forge_fire_num_clips_min", 1), p.i("forge_fire_num_clips_max", 1)),
            clip_delay: (p.f("forge_fire_clip_delay_min", 0.0), p.f("forge_fire_clip_delay_max", 0.0)),
            rounds: (p.i("forge_fire_num_rounds_min", 1), p.i("forge_fire_num_rounds_max", 1)),
            bursts: (p.i("forge_fire_num_bursts_min", 1), p.i("forge_fire_num_bursts_max", 1)),
            burst_delay: (p.f("forge_fire_burst_delay_min", 0.0), p.f("forge_fire_burst_delay_max", 0.0)),
            delay: (p.f("forge_fire_delay_min", 5.0), p.f("forge_fire_delay_max", 7.0)),
        },
        kind,
        // attention_* modes stand at attention (0x0042d0f0 with a0 = 1: anim 0xf), ready_* with the stand set; the
        // roaming default plays anim 0xf (vt+0x19c(0xf, 1), ai_01.cpp:2444); trigger_charge waits in the stand set
        // (vt+0x1a0(2, 3, 0, 1), ai_01.cpp:2480)
        attention: !(mode.starts_with("ready") || mode == "trigger_charge"),
        spline: None,
        looping: p.i("marker_looping", 0) != 0,
        patrol: p.i("marker_patrol", 0) != 0,
        charge: false,
        trigger_charge: mode == "trigger_charge",
        kill_trigger: p.i("receive_trigger_kill", 0),
        no_damage: kind == Kind::Monk && mode == "trigger_charge" && p.i("trigger_no_damage", 0) != 0,
        pursue: false,
        charged: false,
        marker_face: None,
        anim_id: 0,
        anim_left: 0.0,
        thrown: false,
        reacting: 0,
        hit_id: 0,
        throw_armed: true,
        long_hit_pct: p.f("long_hit_pct", 60.0),
        allfours_pct: p.f("allfours_pct", 20.0),
        priority_tags: (0, 0),
        cold: true,
        base_defence: false,
        rocket,
        jumper: jumper.then(|| Jumper::read(p)),
        start_gun: None,
        squad: None,
        walking: false,
        aiming: false,
        pose: None,
        // the fire anim's length a shot (captain_fire_* 0.633 s looped by +0x484 0x1000, grunt_fire_* / grunt_bow_fire_*
        // 0.467 s); the sniper's and the rocket captain's (one-shot: no 0x00428250(1)) from their anims (think)
        fire_len: match kind {
            Kind::Grunt | Kind::Bow => 0.467,
            Kind::Captain if !rocket => 0.633,
            _ => 0.0,
        },
        // the reload set's length (grunt_reload_br 1.133 s, grunt_bow_reload_* 1.3 s, captain_reload_* 2.3 s); the
        // sniper's from its anim (think)
        reload_len: match kind {
            Kind::Grunt => 1.133,
            Kind::Bow => 1.3,
            Kind::Captain => 2.3,
            _ => 0.0,
        },
        // bullet_scale +0xb58 (Q112 init 0x00435090, default 0.5): bullets (damage flag 0x400) hurt him this much
        // (vt+0xd0 0x00435170); the other classes take them whole
        bullet_scale: if kind == Kind::Q112 { p.f("bullet_scale", 0.5) } else { 1.0 },
        monk: (kind == Kind::Monk).then(|| monk::Monk::new(rng)),
        guard_firing: false,
        reloading: false,
        bully: None,
        shot_age: f32::INFINITY,
        q112: (kind == Kind::Q112).then(q112::Q112::default),
        shooter: max_fire::Shooter::None,
        retry_engage: false,
        fall: None,
        fall_watch: None,
    }
}

/// pickup_drop_specific's name as Ai_InitGeneric 0x004247f0 resolves it (case-insensitive): one of the weapon table's
/// pickups (Game+0x2890 + i x 0x150), else pickup_health_1 / pickup_health_2; anything else is no specific drop.
fn drop_specific(name: &str) -> Option<&'static str> {
    crate::weapontable::WEAPON_TABLE
        .iter()
        .filter_map(|w| w.pickup)
        .chain(["pickup_health_1", "pickup_health_2"])
        .find(|p| p.eq_ignore_ascii_case(name))
}

/// The length of an anim of the scene's export (seconds; 1 when it is missing).
fn anim_len(lib: &mut crate::skin::AnimLibrary, name: &str) -> f32 {
    lib.anim(name).map(|a| a.duration).unwrap_or(1.0)
}

/// A grunt bark of a category (Fight, Hurt, Dead, Taunt, Help, Sticky_Scream, ...): audio.rs picks a line of
/// that category and voice set from the bank (audio_01.cpp; index = ((rand*range)>>15)/100).
pub fn voice(category: &str, set: u8) -> String {
    // "Cat:{}:pool" puts the set before a pool selector (audio.rs resolve)
    if category.contains("{}") {
        return format!("@grunt:{}", category.replace("{}", &set.to_string()));
    }
    format!("@grunt:{category}:{set}")
}

/// The barks' shared timers (game_02.cpp:2057-2110), all started at level load and re-rolled when used:
/// death_delay Game+0xb64 30..45 s (hurt / dead / sticky), vo_delay Game+0xb44 30..45 s (spotted / fight),
/// taunt_delay Game+0xb84 300..360 s.
#[derive(Resource, Default)]
pub struct Barks {
    /// (last time, interval) each; an interval of 0 is rolled on first use
    death: (f32, f32),
    vo: (f32, f32),
    taunt: (f32, f32),
    captain: CaptainLines,
}

/// A captain's cues dealt from fixed tables (read from the exe), not from every bank line of the category.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CaptainCue {
    /// slot +0x144 0x00437690: CA1 0x005ce5b4 / CA2 0x005ce5c0, 3 lines each
    Spotted,
    /// slot +0x13c 0x00437860: CA1 0x005ce5cc, 6 lines / CA2 0x005ce5e4, 3 lines
    Fight,
}

/// The captains' cue tables, each dealt by its own shuffle bag (Audio_PlayRandomSound 0x00493b70; the used counts
/// 0x0063a19c / 0x0063a1a0 Spotted, 0x0063a1a4 / 0x0063a1a8 Fight): no line again until the table is spent.
struct CaptainLines {
    spotted: [dialogue::LineBag; 2],
    fight: [dialogue::LineBag; 2],
}

impl Default for CaptainLines {
    fn default() -> Self {
        use dialogue::LineBag;
        CaptainLines {
            spotted: [LineBag::new(&["CA150014_Spotted", "CA150015_Spotted", "CA150016_Spotted"]), LineBag::new(&["CA250014_Spotted", "CA250015_Spotted", "CA250016_Spotted"])],
            fight: [
                LineBag::new(&["CA150004_Fight", "CA150005_Fight", "CA150006_Fight", "CA150007_Fight", "CA150008_Fight", "CA150009_Fight"]),
                LineBag::new(&["CA250004_Fight", "CA250005_Fight", "CA250012_Fight"]),
            ],
        }
    }
}

#[derive(Clone, Copy)]
pub enum Delay {
    Death,
    Vo,
    Taunt,
}

impl Barks {
    /// Whether the delay has passed (then it restarts with a new interval).
    pub fn take(&mut self, d: Delay, now: f32, rng: &mut FxStats) -> bool {
        let (t, range) = match d {
            Delay::Death => (&mut self.death, (30.0, 45.0)),
            Delay::Vo => (&mut self.vo, (30.0, 45.0)),
            Delay::Taunt => (&mut self.taunt, (300.0, 360.0)),
        };
        if t.1 == 0.0 {
            t.1 = rng.range(range.0, range.1);
        }
        if now - t.0 < t.1 {
            return false;
        }
        *t = (now, rng.range(range.0, range.1));
        true
    }

    /// A captain's cue line: the next of his voice set's bag (set 0 the CA1 table, +0x484 0x200000; else CA2).
    pub fn captain_line(&mut self, cue: CaptainCue, set: u8, rng: &mut FxStats) -> &'static str {
        let i = usize::from(set != 0);
        let bag = match cue {
            CaptainCue::Spotted => &mut self.captain.spotted[i],
            CaptainCue::Fight => &mut self.captain.fight[i],
        };
        bag.pick(|| rng.rand(), |_| Some(())).map_or("", |(line, ())| line)
    }

    /// The line of a Spotted / Fight cue by the speaker's class: a captain's from his table's bag, others' from the bank
    /// (`class_voice`).
    pub fn cue_line(&mut self, cue: CaptainCue, kind: Kind, set: u8, rng: &mut FxStats) -> String {
        match kind {
            Kind::Captain | Kind::Sniper => self.captain_line(cue, set, rng).to_owned(),
            _ => class_voice(kind, if cue == CaptainCue::Spotted { "Spotted" } else { "Fight" }, set),
        }
    }

    /// Debug (playtest `barks_ready`): every shared delay counts as passed.
    pub fn all_ready(&mut self) {
        for t in [&mut self.death, &mut self.vo, &mut self.taunt] {
            *t = (-10000.0, 1.0);
        }
    }

    /// Whether the delay has passed, without restarting it (the taunt's test, 0x00428980 ai_02.cpp:1958: the timer is
    /// only restarted when the taunt plays).
    pub fn ready(&mut self, d: Delay, now: f32, rng: &mut FxStats) -> bool {
        let (t, range) = match d {
            Delay::Death => (&mut self.death, (30.0, 45.0)),
            Delay::Vo => (&mut self.vo, (30.0, 45.0)),
            Delay::Taunt => (&mut self.taunt, (300.0, 360.0)),
        };
        if t.1 == 0.0 {
            t.1 = rng.range(range.0, range.1);
        }
        now - t.0 >= t.1
    }
}

/// A bark on the speaker, following him (the voice methods' Say: audio +0xc4 with the object).
fn bark(sounds: &mut MessageWriter<crate::audio::PlaySound>, category: &str, set: u8, at: Vec3, who: Entity) {
    sounds.write(bark_request(category, set, at).following(who));
}

/// A cue's line by the speaker's class: captains and sniper captains their own lists (CaptainObject / SniperCaptain
/// slots +0x144 0x00437690 Spotted, +0x13c 0x00437860 Fight: the CA1 lists with +0x484 bit 21, the grunts' GR1 bit,
/// else CA2, dealt by Audio_PlayRandomSound 0x00493b70), the others the grunt sets.
pub fn class_voice(kind: Kind, category: &str, set: u8) -> String {
    match kind {
        Kind::Captain | Kind::Sniper => format!("@captain:{category}:{}", if set == 0 { 0 } else { 1 }),
        _ => voice(category, set),
    }
}

/// A bark's request, for the barks kept as a voice handle (audio.rs Speech), starting at `at`. The character voice
/// methods play on the speaker (audio_01.cpp Say, audio +0xc4): add `.following(speaker)`; only DeadAtPosition's
/// first and default sets play at a fixed point (0x00439890, audio +0xcc).
pub fn bark_request(category: &str, set: u8, at: Vec3) -> crate::audio::PlaySound {
    crate::audio::PlaySound::at(voice(category, set), at).looping(false)
}

/// The "ai_interest" noises of the player (AIInterestObject, vtable 0x005a7820, in the list World+0xee0): posted by
/// 0x00404810 only for Roman's own projectiles (Projectile_GetOwner == Game+0x748): one following each projectile with
/// an fx_start (0x0047ee30: radius 200, timer 0, alive while the projectile is: the update 0x00404680 drops it when its
/// target is gone and the timer is 0) and one at its end point (0x0047ed40, at every end: radius 200, 2 s); a repeat of
/// the same owner and target only refreshes the entry (0x00404750 / 0x00404790). AI_FindTarget's step 2 takes the
/// owner (the player) when a grunt's ref_hit_00 lies within the radius (3D) and the player within 2 x scan_distance
/// (2D, vt+0x180 0x0043ecf0), with no cone or line-of-sight test: shots flying or landing near a grunt alert him.
/// Nothing is posted while 0x0044e600 holds (Roman there and the mission's start_in_turret_tag Game+0x26cc set: the
/// defence levels) or while Roman is down (the owner's vt+0xd8); his noises already posted go with him (0x00404680).
#[derive(Resource, Default)]
pub struct Interests {
    list: Vec<Interest>,
    /// the projectiles in flight (tracked by player.rs's update), their last position (where their end interest goes)
    /// and owner
    flying: std::collections::HashMap<Entity, (Vec3, Option<Entity>, bool)>,
    /// 0x00404810's gates this frame (`interests`): a start-in-turret level, or Roman gone or down
    muted: bool,
}

struct Interest {
    pos: Vec3,
    radius: f32,
    timer: f32,
    follow: Option<Entity>,
}

impl Interests {
    /// A noise of the player's at a point (debug: the playtest `noise` step; the bullets' own come from `interests`).
    pub fn post(&mut self, pos: Vec3, radius: f32, timer: f32) {
        self.list.push(Interest { pos, radius, timer, follow: None });
    }

    /// A projectile's position this frame (player.rs projectiles, before its hit test): the noises follow it and take
    /// its last one as the end point.
    pub fn track(&mut self, e: Entity, pos: Vec3, owner: Option<Entity>) {
        if self.muted {
            return;
        }
        let seen = self.flying.get(&e).is_some_and(|f| f.2);
        self.flying.insert(e, (pos, owner, seen));
    }

    /// A projectile removed without its end effect (a sticky bomb's lifetime out in flight): no end noise; its
    /// following one ends
    pub fn forget(&mut self, e: Entity) {
        self.flying.remove(&e);
        self.list.retain(|i| i.follow != Some(e));
    }

    /// A projectile of Roman's that is not a player.rs `Projectile` (his sticky bomb, his rockets): its start effect's noise,
    /// following it while it lives (0x0047ee30 -> 0x00404810(200, 0, the projectile)); posted once
    pub fn follow(&mut self, e: Entity, pos: Vec3) {
        if !self.muted && !self.list.iter().any(|i| i.follow == Some(e)) {
            self.list.push(Interest { pos, radius: 200.0, timer: 0.0, follow: Some(e) });
        }
    }

    /// ... and its end effect's noise at a point for 2 s (0x0047ed40 -> 0x00404810(200, 2, -, the position))
    pub fn end_at(&mut self, e: Entity, pos: Vec3) {
        self.list.retain(|i| i.follow != Some(e));
        if self.muted {
            return;
        }
        self.list.push(Interest { pos, radius: 200.0, timer: 2.0, follow: None });
    }
}

/// AIInterestObject_Update 0x00404680 and the posting of 0x00404810 for Roman's projectiles.
fn interests(
    time: Res<Time>,
    mut it: ResMut<Interests>,
    shots: Query<(Entity, &Transform, &crate::player::Projectile)>,
    roman: Query<(Entity, &crate::player::Roman)>,
    followed: Query<&Transform>,
    mission: Option<Res<crate::mission::Mission>>,
    (rockets, data): (Query<(Entity, &Transform, &crate::vehicles::rockets::Rocket), Added<crate::vehicles::rockets::Rocket>>, Res<crate::gamedata::GameData>),
) {
    let dt = time.delta_secs();
    let it = &mut *it;
    // the owner (+0x478, always Roman) gone or down (vt+0xd8 false): every noise removes itself (0x00404680); in a
    // start-in-turret level 0x00404810 posts none (0x0044e600)
    let owner = roman.single().ok().filter(|(_, r)| !r.dead).map(|(e, _)| e);
    it.muted = owner.is_none() || mission.is_some_and(|m| m.start_in_turret);
    let Some(re) = owner.filter(|_| !it.muted) else {
        it.list.clear();
        it.flying.clear();
        return;
    };
    // Roman's rockets and shells (the Gurner, the vindaloo, a forge cannon he mans: vehicles/rockets.rs): the start
    // effect's noise when it has one (0x0047ee30 needs fx_start and fx_start_bone_name: the vindaloo's); their end
    // noise is posted by the hit (`end_at`)
    for (e, t, r) in &rockets {
        let p = data.template_props(&r.template);
        let has = |k: &str| p.s(k).is_some_and(|s| !s.is_empty());
        if r.owner == Some(re) && has("fx_start") && has("fx_start_bone_name") {
            it.follow(e, t.translation);
        }
    }
    // the projectiles that ended since last frame: an interest at their end (2 s)
    // (0x00404810 posts for Roman's own projectiles only)
    it.flying.retain(|_, f| f.1 == Some(re));
    let gone: Vec<(Entity, Vec3)> = it.flying.iter().filter(|(e, _)| shots.get(**e).is_err()).map(|(e, f)| (*e, f.0)).collect();
    for (e, at) in gone {
        it.flying.remove(&e);
        it.list.push(Interest { pos: at, radius: 200.0, timer: 2.0, follow: None });
        if std::env::var("AIDBG").is_ok() {
            let g = crate::level::game_to_engine().inverse() * at;
            info!("AIDBG noise end {:.0} {:.0} {:.0}", g.x, g.y, g.z);
        }
    }
    let new: Vec<(Entity, Vec3)> = it.flying.iter_mut().filter(|(_, f)| !f.2).map(|(e, f)| {
        f.2 = true;
        (*e, f.0)
    }).collect();
    for (e, at) in new {
        // the start effect's interest (fx_start: both of Roman's bullets have one), following the projectile
        it.list.push(Interest { pos: at, radius: 200.0, timer: 0.0, follow: Some(e) });
    }
    it.list.retain_mut(|i| {
        if let Some(f) = i.follow {
            match followed.get(f) {
                Ok(t) => i.pos = t.translation,
                Err(_) => {
                    i.follow = None;
                    if i.timer == 0.0 {
                        return false;
                    }
                }
            }
        }
        if i.timer > 0.0 {
            i.timer -= dt;
            if i.timer < 0.0 {
                return false;
            }
        }
        true
    });
}

/// An object of the mission's ai_target_tag (Game+0xae8, mission.rs): while the tag is set a grunt's target search
/// takes only these, at any range and without the view cone or the line of sight (AI_FindTarget 0x00426920), and one
/// never drops it for distance (vt+0x180, 0x0043ecf0, holds for it). lev_bonus_m1's Roman: the whole army has him as
/// target from the first frame.
#[derive(Component)]
pub struct ArmyTarget;

/// Marks the placed objects of the mission's ai_target_tag as they are made.
fn mark_army_targets(mut commands: Commands, mission: Option<Res<crate::mission::Mission>>, made: Query<(Entity, &crate::level::InstanceId), Added<crate::level::InstanceId>>) {
    let Some(m) = mission.filter(|m| m.ai_target_tag != 0) else { return };
    for (e, id) in &made {
        if m.has_tag(id.0, m.ai_target_tag) {
            commands.entity(e).insert(ArmyTarget);
        }
    }
}

/// A dropped troop's landing orders from its IC anim's notes (GenericAIObject_HandleEvent 0x0043d180, the freefall
/// events: state 0x82 then the mode +0x4ac): freefall_attack_village LO HI takes the objects of tags LO..HI as its
/// priority targets (+0x858 / +0x85c, the houses), freefall_attack the player (mode 0x13). False when no note says.
pub fn drop_orders<'a>(g: &mut Grunt, notes: impl Iterator<Item = &'a str>) -> bool {
    for line in notes.flat_map(|n| n.split(['\r', '\n'])) {
        let w: Vec<&str> = line.split_whitespace().collect();
        let int = |i: usize| w.get(i).and_then(|v| v.parse::<i64>().ok()).unwrap_or(0);
        match w.first().map(|c| c.to_lowercase()).as_deref() {
            Some("freefall_attack_village") => {
                g.priority_tags = (int(1), int(2));
                g.target = None;
                g.mind = Mind::Roam;
                g.search_timer = 0.0;
                return true;
            }
            Some("freefall_attack") => return false,
            _ => {}
        }
    }
    false
}

/// AI_FindTarget's first step (0x00426920): the priority tags [+0x858, +0x85c] walked from the low end: the first live
/// object of a tag is the target at once, at any range; a tag no object carries any more moves the low end up, and
/// past the high end the walk is over (both 0). A tag whose objects are all dying is passed over this time.
fn priority_target(tags: &mut (i64, i64), mission: &crate::mission::Mission, live: &std::collections::HashMap<u32, Entity>) -> Option<Entity> {
    let mut tag = tags.0;
    while tags.0 != 0 && tag <= tags.1 {
        let mut ids = mission.tagged(tag).peekable();
        if ids.peek().is_none() {
            tags.0 += 1;
            if tags.0 > tags.1 {
                *tags = (0, 0);
                return None;
            }
        } else if let Some(e) = ids.find_map(|i| live.get(&i).copied()) {
            return Some(e);
        }
        tag += 1;
    }
    None
}

/// A waiting grunt's trigger (receive_trigger_activate: fn_0042f870 -> 0x0042f5c0), by his mode: trigger_marker (+0x488
/// 0x10) runs his marker spline (think: `marker_tags`), trigger_ic (0x8000000) plays his IC move (ic.rs), trigger_charge
/// (0x100) charges the player (0x0042ce70). `ic`: he has an IC move.
/// The activation clears trigger_no_damage first (+0x48c 0x4000). (The original runs it on any state; the id +0x52c is
/// cleared by it, and every way out of the wait passes it, so only a waiting grunt has an id left to match.)
pub fn activate(g: &mut Grunt, ic: bool, player: Option<Entity>) {
    if g.mind != Mind::WaitTrigger {
        return;
    }
    g.no_damage = false;
    g.mind = Mind::Roam;
    if g.marker_tags.is_none() && !ic {
        g.target = player;
        g.pursue = g.trigger_charge;
        g.charged |= g.trigger_charge;
        // (the charge 0x0042ce70 drops a hand-over under way)
        if g.trigger_charge {
            g.pending = None;
        }
    }
}

/// How long a target taken by an offer is protected from the next offer (K 0x005bcb68 against +0x514).
const OFFER_HOLD: f32 = 10.0;
/// The engagement and scan ranges a guard takes for good once offered a target beyond his range (0x0042c790).
const GUARD_UNLIMITED: f32 = 99999.0;

/// GenericAI_OfferTarget 0x0042c820 (vt+0x1b4): `cand` (with its position) is offered to a grunt standing at `me` whose
/// current target stands at `current`. Ignored while the current target was taken by an offer less than `OFFER_HOLD`
/// ago, or while on the way to a cover (+0x904 without +0x484 0x20000; the towers' +0x488 0x200000 exception is not
/// modelled: no tower grunt takes a trench). Otherwise taken when he has no target or `cand` is no farther (3D) than
/// the current one, and stamped (+0x514); with `engage` (a2) the taken target is engaged at once (0x00454c90 and
/// vt+0x230: here the wander and the scared run, which would otherwise run their course, end; the other states engage
/// on their own). Not ignored, it ends with 0x0042c790: a guard (+0x484 bit 25) whose target is beyond his
/// engagement range takes the infinite accuracy ranges (+0x484 bit 22) and 99999 as both his ranges, for good.
/// (The waiting grunt's activation on the way, +0x52c with +0x484 0x20 clear, has no caller here: both callers skip
/// a busy grunt, and a waiting one, state 0x8c, is busy.) Returns whether he took it.
fn offer_target(g: &mut Grunt, cand: (Entity, Vec3), current: Option<Vec3>, me: Vec3, now: f32, engage: bool) -> bool {
    if (g.target.is_some() && now - g.offer_t < OFFER_HOLD) || (g.cover.is_some() && !g.at_barricade) {
        return false;
    }
    let take = g.target.is_none() || current.is_none_or(|c| (cand.1 - me).length_squared() <= (c - me).length_squared());
    if take {
        g.target = Some(cand.0);
        g.offer_t = now;
        if engage && matches!(g.mind, Mind::Wander | Mind::Flee) {
            g.mind = Mind::Roam;
        }
    }
    let held = if take { Some(cand.1) } else { current };
    if g.guard && held.is_some_and(|p| (p - me).with_y(0.0).length() > g.range) {
        g.infinite = true;
        g.range = GUARD_UNLIMITED;
        g.scan_distance = GUARD_UNLIMITED;
    }
    take
}

/// Whether a member of `group` takes part in its alert (0x0042c700, from SetTarget 0x0042cc70 with a1 and the group id
/// +0x574, a grunt's or a squad's (squad.rs), and from a death 0x00431720): alive, not the alerter and not the target himself, not a traitor (+0x488
/// 0x4000, the Guy Fawkes' swap), with no hand-over under way (+0x578 run out). Whether he holds a target does not
/// matter: the hand-over's end offers it (0x0042c820).
pub(crate) fn alerted(g: &Grunt, me: Entity, (group, target, alerter): (i64, Entity, Entity), traitor: bool) -> bool {
    group != 0 && g.alert_group == group && me != alerter && me != target && !traitor && g.pending.is_none() && g.mind != Mind::Dead
}

/// The AI's trigger handler 0x0042f870 (slot 14), its kill: the id of receive_trigger_kill (+0x530) is his death
/// (vt+0xd4, Character_Die with an empty record of kind 0x8000: no hit and no killer, so the death anim). The volumes
/// of lev_2_m2 (900), lev_4_m2 (802) and lev_4_m3 (2345) clear their placed grunts this way (mission.json lists these
/// edges as "destroyed"). (Its activate id +0x52c is mission.rs's `WakeAi` -> [`activate`].)
fn trigger_kills(mut sent: MessageReader<crate::mission::SendTrigger>, mut grunts: Query<(&mut Grunt, &mut Health)>, mut stats: ResMut<AiStats>) {
    for crate::mission::SendTrigger(t) in sent.read() {
        for (mut g, mut h) in &mut grunts {
            if *t != 0 && g.kill_trigger == *t && g.mind != Mind::Dead && h.0 > 0.0 {
                h.0 = 0.0;
                g.last_hit = 0.0;
                g.attacker = None;
                g.blast_fling = None;
                g.cartoon = false;
                // (not damage: a monk's trigger_no_damage does not keep him alive)
                g.no_damage = false;
                stats.trigger_kills += 1;
                info!("EVENT ai: grunt killed by trigger {t}");
            }
        }
    }
}

/// One entry of the looker's list +0x604 that AI_FindTarget walks: the player's side (Roman and his companions; for a
/// loyal grunt also the Guy Fawkes traitors, for a traitor the loyal grunts).
#[derive(Clone, Copy)]
pub struct TargetCandidate {
    pub entity: Entity,
    /// its feet (the distances are 2D)
    pub pos: Vec3,
    /// its ref_hit_00, where the sight ray aims
    pub hit: Vec3,
    /// the player (Game+0x748): taken at once inside hear_distance
    pub player: bool,
    /// of the mission's ai_target_tag (`ArmyTarget`)
    pub army: bool,
}

/// The looker as AI_FindTarget sees it (a grunt, or a squad's anchor: SquadObject is a GenericAIObject and its scan
/// 0x0042be80 calls the same function with its own +0x5dc).
pub struct Looker<'a> {
    pub pos: Vec3,
    /// its ref_hit_00 (0x00428280): where the sight ray starts
    pub eye: Vec3,
    /// its flat facing (the view cone's axis)
    pub fwd: Vec3,
    /// the range (a0: +0x5dc, scan_distance)
    pub scan: f32,
    /// +0x5e0, hear_distance
    pub hear: f32,
    /// +0x484 0x20000 (at a barricade): no cone test
    pub at_barricade: bool,
    /// on a mounted gun (+0x484 0x8000): its origin and heading, candidates outside 80 degrees of it skipped
    pub gun: Option<(Vec3, Vec3)>,
    /// bodies the sight ray passes (its own)
    pub skip: &'a [BodyId],
    /// step 2: a noise of the player's within its radius of the looker
    pub heard: bool,
    /// the mission's ai_target_tag (Game+0xae8) is set (the looker a bad guy, vt+0xe8)
    pub tagged: bool,
}

/// AI_FindTarget 0x00426920 (ai_02.cpp:660-826), the one target search of grunts and squads: (step 2) a noise of the
/// player's heard takes the player within 2 x scan (no cone, no sight); then the walk of the list: with the mission's
/// ai_target_tag only its objects, at any range (1e8) with neither cone nor sight; else within scan (2D) and no
/// farther than the best so far, the player inside hear_distance at once, the others inside the 190-degree cone (or
/// within hear_distance; no cone at a barricade), within 80 degrees of a mounted gun's heading, and in sight
/// (0x00426fd0). Returns the 2D distance, the target and whether it was taken by the noise or the hearing range.
pub fn find_target(l: &Looker, cands: &[TargetCandidate], physics: &Physics) -> Option<(f32, Entity, bool)> {
    let mut best: Option<(f32, Entity, bool)> = None;
    if l.heard {
        if let Some(c) = cands.iter().find(|c| c.player) {
            let d = (c.pos - l.pos).with_y(0.0).length();
            if d <= 2.0 * l.scan {
                best = Some((d, c.entity, true));
            }
        }
    }
    for c in cands {
        let to = (c.pos - l.pos).with_y(0.0);
        let d = to.length();
        if l.tagged {
            if c.army && best.is_none_or(|b| d < b.0) {
                best = Some((d, c.entity, false));
            }
            continue;
        }
        if d > l.scan || best.is_some_and(|b| d > b.0) {
            continue;
        }
        if c.player && d < l.hear {
            return Some((d, c.entity, true));
        }
        // Object_IsInViewCone 0x00455d90 (hear_distance 0 would take a 180-degree cone or 100 units instead: no
        // property set or placement has 0)
        let cone = in_view_cone(to, l.fwd) || d < l.hear || l.at_barricade;
        // on a mounted gun only what lies within 80 degrees of the gun's heading, seen from the gun (0x0042b860 with
        // 0x00453150's heading), before the sight test (ai_02.cpp:786-792)
        let gun_arc = l.gun.is_none_or(|(origin, fwd)| within_arc(c.pos, origin, fwd, 80.0));
        if cone && gun_arc && sight_ray(physics, l.eye, c.hit, l.skip) {
            best = Some((d, c.entity, false));
        }
    }
    best
}

/// AI_FindTarget 0x00426920 and the alert-group hand-over: every 0.75..1.25 s (2..4 s on a start-in-turret level);
/// the player inside hear_distance is taken at once; otherwise within scan_distance, inside the 190-degree cone, with line of sight. With the mission's
/// ai_target_tag (Game+0xae8) a grunt (a bad guy, vt+0xe8) takes only candidates of that tag, at any range (1e8) and
/// with neither the cone nor the line of sight (ai_02.cpp:662, 721-731, 761).
#[allow(clippy::too_many_arguments)]
fn search(
    time: Res<Time>,
    mut rng: ResMut<FxStats>,
    physics: NonSend<Physics>,
    player: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), With<Player>>,
    companions: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), (With<Companion>, Without<Player>)>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, &Body, Option<&crate::skin::Animator>)>,
    // the ref_hit_00 joints' world points from the rig's own transforms: a grunt made this frame (the level's first,
    // an IC host's hidden grunts) has no propagated GlobalTransform on his joints yet, and his first search would
    // look from the world origin (lev_1_m1_b's IC grunts saw Roman one search late)
    joints: bevy::transform::helper::TransformHelper,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stats: ResMut<AiStats>,
    mut barks: ResMut<Barks>,
    noise: Res<Interests>,
    squads: Query<&squad::Squad>,
    (army, mission, objects, people): (
        Query<(), With<ArmyTarget>>,
        Option<Res<crate::mission::Mission>>,
        Query<(Entity, &crate::level::InstanceId, &GlobalTransform), With<crate::fx::Destructible>>,
        Query<(Entity, &crate::level::InstanceId, &Health), Or<(With<Player>, With<Companion>)>>,
    ),
    traitors: Query<(), With<crate::weapons::traitor::Traitor>>,
    positions: Query<&GlobalTransform>,
    force: Res<AiForce>,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Ok((pe, _, _)) = player.single() else { return };
    // (a playtest's ai_force "grunts_hold", kept: the grunts look for no target)
    let hold = force.0.iter().any(|w| w == "grunts_hold");
    // the live placed objects by id (a destroyed one has lost its Destructible) and the living placed characters (Roman,
    // Jonesy, Q: lev_5_m2's barracks troops go for tag 1, Roman; lev_4_m2's alarm troops for 600, Jonesy): the
    // priority targets' candidates (Object_FindByTag takes any object)
    let live: std::collections::HashMap<u32, Entity> = if grunts.iter().any(|g| g.1.priority_tags.0 != 0) {
        objects.iter().map(|(e, i, _)| (i.0, e)).chain(people.iter().filter(|(_, _, h)| h.0 > 0.0).map(|(e, i, _)| (i.0, e))).collect()
    } else {
        Default::default()
    };
    let global_tag = mission.as_ref().map_or(0, |m| m.ai_target_tag);
    // the re-arm delay (ai_02.cpp:803-810): 0.75..1.25 s, or 2..4 s while 0x0044e600 holds (Roman there, the mission's
    // start_in_turret_tag set: the defence levels' grunts look half as often)
    let rearm = if mission.as_ref().is_some_and(|m| m.start_in_turret) { (2.0, 4.0) } else { (0.75, 1.25) };
    stats.targets = [0, 0];
    for (_, g, _, _, _) in &grunts {
        if let Some(t) = g.target.filter(|_| g.mind != Mind::Dead) {
            stats.targets[if t == pe { 0 } else { 1 }] += 1;
        }
    }
    // the candidates (the AI's list +0x604: the player and his companions; the original's grunts take Jonesy and Q
    // too, hook AI dump fightT): each with its position and its ref_hit_00 (the point the sight ray aims at)
    let hit_point = |t: &GlobalTransform, a: Option<&crate::skin::Animator>| {
        a.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.compute_global_transform(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(t.translation() + Vec3::Y * 7.0)
    };
    let cands: Vec<TargetCandidate> = player
        .iter()
        .chain(companions.iter())
        .map(|(e, t, a)| TargetCandidate { entity: e, pos: t.translation(), hit: hit_point(t, a), player: e == pe, army: army.contains(e) })
        .collect();
    // the Guy Fawkes traitors (weapons/traitor.rs; 0x00430140 swaps the AI's lists +0x604 / +0x608 and joins the other
    // side's): a traitor hunts the live grunts still loyal; the loyal hunt the traitors as well as the player's side
    let sides: Vec<(Entity, Vec3, Vec3, bool)> = if traitors.is_empty() {
        Vec::new()
    } else {
        grunts.iter().filter(|g| g.1.mind != Mind::Dead).map(|(e, _, t, _, a)| (e, t.translation(), hit_point(t, a), traitors.contains(e))).collect()
    };
    if std::env::var("AIDBG").is_ok() && time.elapsed_secs() < 0.5 {
        info!("AIDBG candidates player {:?} companions {:?}", pe, companions.iter().map(|c| c.0).collect::<Vec<_>>());
    }
    let mut spotted: Vec<(i64, Entity, Entity)> = Vec::new();
    for (ge, mut g, t, gbody, ga) in &mut grunts {
        if g.mind == Mind::Dead {
            continue;
        }
        if let Some((tgt, left)) = g.pending {
            if left - dt <= 0.0 {
                // the hand-over's end (ai_06.cpp:318-340): a grunt free to engage (vt+0x178 0x0042f8d0 false) is offered
                // the target (vt+0x1b4(target, 0, 1): the offer's hold, nearer test and engage); a busy one lets it go.
                // (A busy squad member, states 0xdc / 0xde, is also released to a guard where he stands, 0x0042d0f0 and
                // state 0xdd: not ported, squad.rs keeps him.)
                g.pending = None;
                let at = |e: Entity| positions.get(e).ok().map(|p| p.translation());
                if let (false, Some(tp)) = (g.busy(ge, tgt), at(tgt)) {
                    let current = g.target.and_then(at);
                    offer_target(&mut g, (tgt, tp), current, t.translation(), now, true);
                }
            } else {
                g.pending = Some((tgt, left - dt));
            }
        }
        // a squad member does not look for targets himself while the squad is not halted (0x0042f8d0: states 0xdc /
        // 0xde are busy); the squad scans (squad.rs)
        if g.squad.is_some_and(|l| squads.get(l.squad).is_ok_and(|s| !s.members_engage())) {
            continue;
        }
        g.search_timer -= dt;
        if g.search_timer > 0.0 || hold {
            continue;
        }
        // a guard (0x0042ec20, every frame of state 0x79) releases its target each time the search timer runs out and
        // searches again: one that loses the cone or the line of sight loses its target within 0.75..1.25 s
        if g.guard && g.target.is_some() {
            g.target = None;
        }
        // a priority target (a house) that is gone (destroyed: its husk keeps the entity but has lost the tag) leaves
        // him free to search
        let gone = |t: Entity| objects.get(t).map_or(true, |(_, i, _)| mission.as_deref().is_some_and(|m| m.lost(i.0)));
        if g.priority_tags.0 != 0 && g.target.is_some_and(|t| t != pe && !cands.iter().any(|c| c.entity == t) && gone(t)) {
            g.target = None;
        }
        if g.target.is_some() {
            continue;
        }
        if let Some(m) = mission.as_deref().filter(|_| g.priority_tags.0 != 0) {
            let mut tags = g.priority_tags;
            let found = priority_target(&mut tags, m, &live);
            if std::env::var("AIDBG").is_ok() {
                info!("AIDBG priority {ge:?} tags {:?} -> {:?} found {found:?}", g.priority_tags, tags);
            }
            g.priority_tags = tags;
            if tags.0 == 0 {
                // the walk over: the scan range back to range x 1.5 (+0x5dc = +0x5d8 x 0x005bce20)
                g.scan_distance = g.range * 1.5;
            }
            if let Some(tgt) = found {
                // its 2D distance becomes the scan range +0x5dc: the target is kept (2 x scan_distance) however far it
                // is, the walk to the next village included
                if let Ok(tt) = positions.get(tgt) {
                    g.scan_distance = (tt.translation() - t.translation()).with_y(0.0).length();
                }
                g.target = Some(tgt);
                g.search_timer = rng.range(rearm.0, rearm.1);
                continue;
            }
        }
        // waiting for a trigger: only trigger_los grunts look (state 0x8c)
        if g.mind == Mind::WaitTrigger && !g.trigger_los {
            continue;
        }
        g.search_timer = rng.range(rearm.0, rearm.1);
        let me = t.translation();
        // AI_FindTarget's walk of +0x604: within scan_distance (2D), the closest that passes; the player inside
        // hear_distance is taken at once; otherwise inside the 190-degree cone (or within hear_distance) with line of
        // sight
        let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
        // its own ref_hit_00 (0x00428280 with "ref_hit_00"): where the sight ray starts (raised by 5)
        let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.compute_global_transform(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(me + Vec3::Y * 6.0);
        // a traitor walks the loyal grunts (and hears no noise of the player's); the loyal the player's side and the
        // traitors
        let traitor = traitors.contains(ge);
        let own: Vec<TargetCandidate> = if traitor {
            sides.iter().filter(|s| !s.3 && s.0 != ge).map(|s| TargetCandidate { entity: s.0, pos: s.1, hit: s.2, player: false, army: army.contains(s.0) }).collect()
        } else {
            cands.iter().copied().chain(sides.iter().filter(|s| s.3).map(|s| TargetCandidate { entity: s.0, pos: s.1, hit: s.2, player: false, army: army.contains(s.0) })).collect()
        };
        let looker = Looker {
            pos: me,
            eye: own_hit,
            fwd,
            scan: g.scan_distance,
            hear: g.hear_distance,
            at_barricade: g.at_barricade,
            gun: g.gun.filter(|gn| gn.phase == GunPhase::On).map(|gn| (gn.origin, gn.fwd)),
            skip: &[gbody.id],
            // step 2: a noise within its radius of his ref_hit_00 (3D)
            heard: !traitor && noise.list.iter().any(|i| i.pos.distance(own_hit) <= i.radius),
            tagged: global_tag != 0,
        };
        if let Some((d2, tgt, heard)) = find_target(&looker, &own, &physics) {
            // the Spotted cue: a new target when there was none (SetTarget 0x0042cc70 -> vt+0x144), under vo_delay
            // (30..45 s, from level load: the first fight has no line)
            if g.target.is_none() && barks.take(Delay::Vo, time.elapsed_secs(), &mut rng) {
                let line = barks.cue_line(CaptainCue::Spotted, g.kind, g.voice_set, &mut rng);
                sounds.write(crate::audio::PlaySound::at(line, me).looping(false));
            }
            g.target = Some(tgt);
            if g.mind == Mind::WaitTrigger {
                // activate 0x0042f5c0 with the target found (state 0x8c, trigger_los): trigger_charge charges it
                // (0x0042ce70: the pursuit at once); the marker run of trigger_marker is not ported here
                g.mind = Mind::Roam;
                g.pursue = g.trigger_charge;
                g.no_damage = false;
                g.charged |= g.trigger_charge;
            }
            if std::env::var("AIDBG").is_ok() { info!("AIDBG spotted {:?} by {:?} at {:?} d {:.0} heard {} mode {:?}", tgt, ge, me, d2, heard, g.mind); }
            if g.alert_group != 0 {
                spotted.push((g.alert_group, tgt, ge));
            }
        }
    }
    // SetTarget's group alert (0x0042cc70 with a1: every search that finds a target sets it so)
    for alert in spotted {
        for (ge, mut g, _, _, _) in &mut grunts {
            if alerted(&g, ge, alert, traitors.contains(ge)) {
                g.pending = Some((alert.1, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// Line of sight as 0x00426fd0: from the source raised by 5 (0x005bcbdc) to the target point (its ref_hit_00), true
/// when nothing is hit on the way; bodies in `skip` (the looker's own) are passed through, and so are the AI
/// characters (the query excludes the looker's list +0x608 = World+0xe7c: oracle lev_2_m2, grunt 32 sees Roman past
/// grunt 16 at the load).
pub fn sight_ray(physics: &Physics, from_hit: Vec3, target: Vec3, skip: &[BodyId]) -> bool {
    let mut from = from_hit + Vec3::Y * 5.0;
    let dir = (target - from).normalize_or(Vec3::Y);
    let mut left = (target - from).length();
    while let Some((b, at, _)) = physics.raycast(from, dir, left) {
        if !skip.contains(&b) && !physics.ai_bodies.contains(&b) {
            return false;
        }
        let step = at.distance(from) + 0.05;
        from = at + dir * 0.05;
        left -= step;
        if left <= 0.0 {
            break;
        }
    }
    true
}

/// What is under a character's feet within `depth` (his own body passed through: the ray starts inside his sphere).
pub fn ground_below(physics: &Physics, feet: Vec3, depth: f32, own: BodyId) -> Option<BodyId> {
    let mut from = feet + Vec3::Y * 1.0;
    let mut left = depth + 1.0;
    while let Some((b, at, _)) = physics.raycast(from, -Vec3::Y, left) {
        if b != own {
            return Some(b);
        }
        left -= from.y - at.y + 0.05;
        from = at - Vec3::Y * 0.05;
        if left <= 0.0 {
            break;
        }
    }
    None
}

/// A clear line between two characters' eyes (the squad's scan, a walk's straight leg): nothing in the way but AI
/// characters, which the AI's queries pass (0x00426fd0 excludes World+0xe7c; a squad's own members stand round its
/// anchor).
pub fn line_of_sight(physics: &Physics, from_feet: Vec3, to_feet: Vec3) -> bool {
    let eye = from_feet + Vec3::Y * 8.0;
    let aim = to_feet + Vec3::Y * 7.0;
    let v = aim - eye;
    let dir = v.normalize_or(Vec3::Y);
    let (mut from, mut left) = (eye + dir * 4.0, (v.length() - 8.0).max(0.1));
    while let Some((b, at, _)) = physics.raycast(from, dir, left) {
        if !physics.ai_bodies.contains(&b) {
            return false;
        }
        left -= at.distance(from) + 0.05;
        if left <= 0.0 {
            break;
        }
        from = at + dir * 0.05;
    }
    true
}

/// How far the grunt a sticky bomb is stuck to runs from the player (K 0x005bcf00).
const SCARE_FLEE_STUCK: f32 = 250.0;
/// How far the grunts near it run from the bomb's carrier, or the player (K 0x005bcb60).
const SCARE_FLEE_NEAR: f32 = 100.0;

/// One scare, the arguments of GenericAIObject slot 51 0x0042fb60 (vt+0xcc) as StickyBombProjectile_Stick 0x004817b0
/// sends them: the bomb is stuck on him (the attacker), the object to run from when it is not (a1: the bomb's
/// carrier), and whether it passes to his squad (a2).
#[derive(Clone, Copy)]
struct Scare {
    who: Entity,
    bomb_on_him: bool,
    from: Option<Entity>,
    to_squad: bool,
}

/// A monk's smoke started (MonkObject_Init 0x004369a0).
#[derive(Component)]
struct MonkSmoke;

/// MonkObject_Init 0x004369a0 (ai_05.cpp:186-205): a monk (with his model, vt+0xd8) carries five smoke effects for
/// good, each on a node of his (Fx_Create 0x0041cc60 with mode 4, the node and the monk): fx_monk_smoke_sleeve at
/// ref_sleeve_00 and ref_sleeve_01, fx_monk_smoke_body_0 / _1 / _2 at ref_body_00 / _01 / _02. Started once his
/// skeleton's joints exist.
fn monk_smoke(mut commands: Commands, monks: Query<(Entity, &Grunt, &crate::skin::Animator), Without<MonkSmoke>>, joints: Query<&GlobalTransform>, mut fx: MessageWriter<crate::fx::StartFx>) {
    const SMOKE: [(&str, &str); 5] = [
        ("fx_monk_smoke_sleeve", "ref_sleeve_00"),
        ("fx_monk_smoke_sleeve", "ref_sleeve_01"),
        ("fx_monk_smoke_body_0", "ref_body_00"),
        ("fx_monk_smoke_body_1", "ref_body_01"),
        ("fx_monk_smoke_body_2", "ref_body_02"),
    ];
    for (e, g, a) in &monks {
        if g.kind != Kind::Monk || g.mind == Mind::Dead {
            continue;
        }
        let nodes: Vec<Option<(Entity, Vec3)>> = SMOKE
            .iter()
            .map(|(_, n)| a.skeleton.index.get(*n).and_then(|&i| a.joints.get(i)).and_then(|&j| joints.get(j).ok().map(|t| (j, t.translation()))))
            .collect();
        if a.joints.is_empty() {
            continue;
        }
        commands.entity(e).insert(MonkSmoke);
        for ((name, node), at) in SMOKE.iter().zip(nodes) {
            let Some((j, at)) = at else {
                warn!("ai: monk {e:?} has no {node} for {name}");
                continue;
            };
            fx.write(crate::fx::StartFx { name: (*name).into(), at, rotation: Quat::IDENTITY, follow: Some(j), position_only: false });
        }
        info!("EVENT ai: monk {e:?} smoke on");
    }
}

/// Scare 0x0042fb60, sent by a sticky bomb sticking (0x004817b0, game_07.cpp:1240-1257): first to the object it
/// stuck on (attacker = the bomb, a2 = 1), then to every grunt within 25 units of it (a1 = that object, a2 = 1). For
/// each, in order (ai_03.cpp:2889-2962):
/// - dead: nothing;
/// - a squad member (+0x9bc) leaves his squad and its alert group, his guard flag cleared (0x00454750, 0x0042cc10,
///   0x0042cff0); with a2 the scare first goes to the squad's other members (vt+0xcc(0, the bomb's carrier when it
///   is he, 0)), who leave it the same way;
/// - else the carrier on a mounted gun (+0x898 with +0x484 0x8000) becomes a guard (+0x484 bit 25);
/// - else nothing while busy (vt+0x178, `Grunt::busy`) or already fleeing (state 0xc8);
/// - a guard (bit 25, `Grunt::guard`): only the carrier reacts, cowering (anim 0x10 + (rand() * 2 >> 15)
///   grenade_stuck_1 / 2, his sticky scream vt+0x164, state 0xca: he stands, `Mind::Cower`);
/// - the rest scream (the carrier vt+0x164 0x00438810 Sticky, the others vt+0x160 0x004383a0 General) and flee
///   (state 0xc8): from the player when they carry the bomb or it stuck on nothing, else from its carrier, 250
///   (K 0x005bcf00) / 100 (K 0x005bcb60) units along the 2D direction, the held cover left (vt+0x18c; think lets a
///   trench go), the timer +0x490 = 3 + rand() * 2 / 32768 s (K 0x005bce30, 0x005bcb7c), which only a grunt without
///   the bomb counts down (state 0xc8, ai_06.cpp:2494): the carrier runs to his point.
///
/// Not modelled: +0xb54 (the bomb held: on arriving the carrier sets it off at once, Call_00481ab0 ai_06.cpp:2520; the
/// turbo-pack carrier's escape, state 0xc9), the flags +0x484 bit 16 / +0x488 bit 0 cleared.
#[allow(clippy::too_many_arguments)]
fn scare(
    mut stuck: MessageReader<StickyStuck>,
    mut rng: ResMut<FxStats>,
    player: Query<&GlobalTransform, With<Player>>,
    places: Query<&GlobalTransform>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform)>,
    squads: Query<&squad::Squad>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut barks, time, voices): (ResMut<Barks>, Res<Time>, crate::audio::PlayingVoices),
    mut stats: ResMut<AiStats>,
) {
    let ppos = player.single().map(|p| p.translation()).unwrap_or_default();
    for s in stuck.read() {
        let carrier = s.1.filter(|&o| grunts.contains(o));
        let mut work: std::collections::VecDeque<Scare> = carrier.map(|o| Scare { who: o, bomb_on_him: true, from: None, to_squad: true }).into_iter().collect();
        work.extend(
            grunts
                .iter()
                .filter(|(ge, _, t)| Some(*ge) != s.1 && t.translation().distance(s.0) < 25.0)
                .map(|(ge, ..)| Scare { who: ge, bomb_on_him: false, from: s.1, to_squad: true }),
        );
        while let Some(sc) = work.pop_front() {
            let Ok((ge, mut g, t)) = grunts.get_mut(sc.who) else { continue };
            if g.mind == Mind::Dead {
                continue;
            }
            let mut guard = g.guard;
            if let Some(link) = g.squad.take() {
                if sc.to_squad {
                    let members = squads.get(link.squad).map(|sq| sq.members.clone()).unwrap_or_default();
                    // (passed on before his own reaction: the recursion of vt+0xcc)
                    for &m in members.iter().rev().filter(|&&m| m != ge) {
                        work.push_front(Scare { who: m, bomb_on_him: false, from: sc.bomb_on_him.then_some(ge), to_squad: false });
                    }
                }
                g.alert_group = 0;
                g.guard = false;
                guard = false;
            } else if sc.bomb_on_him && g.gun.is_some_and(|x| x.phase == GunPhase::On) {
                guard = true;
            } else if g.busy_now() || g.mind == Mind::Flee {
                continue;
            }
            if guard && !sc.bomb_on_him {
                continue;
            }
            // the screams keep his scream handle: none starts while it plays; the sticky one (0x00438810: death_delay,
            // else General_Scream) then leaves death_delay as it is
            let at = t.translation();
            let scream = if !sc.bomb_on_him || !barks.ready(Delay::Death, time.elapsed_secs(), &mut rng) {
                Some("General_Scream")
            } else if !g.speech.screaming(&voices) {
                barks.take(Delay::Death, time.elapsed_secs(), &mut rng);
                Some("Sticky_Scream")
            } else {
                None
            };
            let set = g.voice_set;
            if let Some(req) = scream.and_then(|cat| g.speech.scream(&voices, bark_request(cat, set, at).following(ge))) {
                sounds.write(req);
            }
            if guard {
                g.anim_id = 0x10 + (rng.rand() * 2.0) as u32 % 2;
                g.mind = Mind::Cower;
                info!("EVENT ai: grunt {ge:?} cowers (sticky bomb)");
                continue;
            }
            let from = match sc.from {
                Some(o) if !sc.bomb_on_him => places.get(o).map_or(ppos, |p| p.translation()),
                _ => ppos,
            };
            let away = (at - from).with_y(0.0).normalize_or(Vec3::X);
            let dist = if sc.bomb_on_him { SCARE_FLEE_STUCK } else { SCARE_FLEE_NEAR };
            g.goal = at + away * dist;
            stats.flees += 1;
            stats.max_flee = stats.max_flee.max(dist);
            g.timer = rng.range(3.0, 5.0);
            g.flee_bomb = sc.bomb_on_him;
            g.mind = Mind::Flee;
            info!("EVENT ai: grunt {ge:?} flees (sticky bomb{})", if sc.bomb_on_him { " on him" } else { "" });
        }
    }
}

/// 0x0042b860: `point` lies within `angle` degrees of `facing` seen from `from` (2D; both directions normalised, a
/// zero vector giving 90 degrees).
pub fn within_arc(point: Vec3, from: Vec3, facing: Vec3, angle: f32) -> bool {
    let a = (point - from).with_y(0.0).normalize_or_zero();
    let f = facing.with_y(0.0).normalize_or_zero();
    a.dot(f).clamp(-1.0, 1.0).acos().to_degrees() < angle
}

/// A trench as the frame's searches see it (occupancy written back after the grunts' loop).
struct TrenchView {
    e: Entity,
    origin: Vec3,
    exit: Vec3,
    spot: Vec3,
    dir: Vec3,
    occupant: Option<Entity>,
    collider: Option<BodyId>,
}

/// The barricade candidate of the hiding-spot search, 0x00429f00 (needs vt+0x1f4: true for grunts, 0x00433fa0): the
/// trench (vt+0x138 == 3) nearest the grunt (2D, from its origin) whose slot 0 is free (vt+0x148), that is not one of
/// the last two left (+0x918 / +0x91c), within scan_distance (+0x5dc) of the grunt; with a target also within the
/// engagement range (+0x5d8) of the target, the target within 80 degrees of ref_barracade's direction seen from
/// ref_barracade (0x0042b860), and a clear line from ref_barracade + 5 (and + 5 again in 0x00426fd0) to the target's
/// ref_hit_00, the trench itself passed through. Without a target only the first three tests (need_target with no
/// target returns nothing before the walk).
fn pick_trench(g: &Grunt, me: Entity, pos: Vec3, target: Option<(Vec3, Vec3)>, need_target: bool, trenches: &[TrenchView], physics: &Physics, own: BodyId) -> Option<usize> {
    // vt+0x1f4 (0x00429f00's first test): the archers take no trench
    if (need_target && target.is_none()) || !g.kind.uses_trenches() {
        return None;
    }
    let mut best: Option<(usize, f32)> = None;
    for (i, t) in trenches.iter().enumerate() {
        if t.occupant.is_some_and(|o| o != me) || g.cover_history[..2].contains(&Some(t.e)) {
            continue;
        }
        let d = (t.origin - pos).with_y(0.0).length_squared();
        if d > g.scan_distance * g.scan_distance || best.is_some_and(|b| !(b.1 > d)) {
            continue;
        }
        if let Some((tpos, thit)) = target {
            if (t.origin - tpos).with_y(0.0).length_squared() > g.range * g.range {
                continue;
            }
            if !within_arc(tpos, t.spot, t.dir, 80.0) {
                continue;
            }
            let skip: Vec<BodyId> = t.collider.into_iter().chain([own]).collect();
            if !sight_ray(physics, t.spot + Vec3::Y * 5.0, thit, &skip) {
                continue;
            }
        }
        best = Some((i, d));
    }
    best.map(|b| b.0)
}

impl Hide {
    /// the state a found spot starts
    fn mind(&self) -> Option<Mind> {
        match self {
            Hide::Cover => Some(Mind::Cover),
            Hide::Gunner => Some(Mind::Gunner),
            Hide::Teleport => Some(Mind::Teleport),
            Hide::Engage => Some(Mind::Fire),
            Hide::Rest => Some(Mind::Roam),
            Hide::Slot => Some(Mind::ToSlot),
            _ => None,
        }
    }
}

/// A mounted gun as the frame's searches see it (written back after the grunts' loop).
struct GunView {
    e: Entity,
    origin: Vec3,
    exit: Vec3,
    yaw: f32,
    fwd: Vec3,
    /// 0x004987a0: ai_can_use, no kickout time left (+0x614, turret.rs `damage`), no player on it (flag 8)
    usable: bool,
    reserved: Option<Entity>,
    ai: Option<Entity>,
    collider: Option<BodyId>,
    /// the placed gun's instance id (a turret-mode grunt's obj_tag resolves to it, level.rs)
    instance: Option<u32>,
    /// a kind-2 gun (vt+0x168 != 1: the forge cannon, two pivots): the forge_fire_* tables
    forge: bool,
}

/// The frame's cover objects.
struct Spots {
    trenches: Vec<TrenchView>,
    guns: Vec<GunView>,
    /// the generic cover list World+0xed4: trees and valid buildings (coverslot.rs)
    slots: Vec<coverslot::SlotView>,
    /// a base-defence level (0x0044e600: Roman starts in a turret): the search takes no gun, trench or cover slot
    defence: bool,
    /// the player's position and hit point (Game+0x748: the cover slots' reference without a target)
    player: Option<(Vec3, Vec3)>,
}

/// The mounted-gun candidate of the hiding-spot search, 0x0042a100 (needs vt+0x1f0: true for grunts): the free gun
/// (0x004987a0: usable, flags 0x14 clear: no AI on it or walking to it) nearest the grunt (2D) within scan_distance; with a
/// target also within the engagement range of the target, the target within 65 degrees of the gun's heading
/// (0x00453150) seen from the gun, and a clear line from the gun + 5 (+ 5 in 0x00426fd0) to the target's ref_hit_00.
fn pick_gun(g: &Grunt, pos: Vec3, target: Option<(Vec3, Vec3)>, need_target: bool, guns: &[GunView], physics: &Physics, own: BodyId) -> Option<usize> {
    // vt+0x1f0 (0x0042a100's first test): the captains man no gun
    if (need_target && target.is_none()) || !g.kind.uses_guns() {
        return None;
    }
    let mut best: Option<(usize, f32)> = None;
    for (i, t) in guns.iter().enumerate() {
        if !t.usable || t.reserved.is_some() || t.ai.is_some() {
            continue;
        }
        let d = (t.origin - pos).with_y(0.0).length_squared();
        if d > g.scan_distance * g.scan_distance || best.is_some_and(|b| !(b.1 > d)) {
            continue;
        }
        if let Some((tpos, thit)) = target {
            if (t.origin - tpos).with_y(0.0).length_squared() > g.range * g.range || !within_arc(tpos, t.origin, t.fwd, 65.0) {
                continue;
            }
            let skip: Vec<BodyId> = t.collider.into_iter().chain([own]).collect();
            if !sight_ray(physics, t.origin + Vec3::Y * 5.0, thit, &skip) {
                continue;
            }
        }
        best = Some((i, d));
    }
    best.map(|b| b.0)
}

/// The gunner's counters (0x00427600 with a gun, from its table `Gun::table`): the first time (flag 4 clear) clips,
/// rounds, burst and no pause; later rounds, burst and the clip pause; the burst delay cleared, the burst at most the
/// rounds.
fn gun_roll(g: &mut Grunt, gn: &mut Gun, rng: &mut FxStats) {
    let int = |rng: &mut FxStats, (lo, hi): (i64, i64)| rng.range(lo as f32, hi as f32 + 0.999) as i64;
    let st = gn.table;
    if !g.fire_ready {
        g.fire_ready = true;
        gn.clips = int(rng, st.clips);
        gn.rounds = int(rng, st.rounds);
        gn.burst = int(rng, st.bursts);
        gn.pause = 0.0;
    } else {
        gn.rounds = int(rng, st.rounds);
        gn.burst = int(rng, st.bursts);
        gn.pause = rng.range(st.clip_delay.0, st.clip_delay.1);
    }
    gn.delay = 0.0;
    gn.burst = gn.burst.min(gn.rounds);
}

/// The gun's firing cycle 0x004292f0: the clip pause (idle), the burst delay, the fire set while rounds and burst last;
/// a spent burst re-rolls the burst and its delay; a spent clip counts the clips down (a pause between, flag 4 cleared
/// after the last). Returns firing. Not modelled: a kind-2 gun (the forge cannon) also keeps its fire anim until it
/// ends (bit 1 of 0x00408800(4)+0x1c) once the counters run out; its shots are the same.
fn gun_cycle(g: &mut Grunt, gn: &mut Gun, rng: &mut FxStats, dt: f32) -> bool {
    if gn.pause > 0.0 {
        gn.firing = false;
        gn.pause -= dt;
        return false;
    }
    if !gn.firing {
        if gn.delay > 0.0 {
            gn.delay -= dt;
            return false;
        }
        if gn.rounds > 0 && gn.burst > 0 {
            gn.firing = true;
            gn.anim_t = 0.0;
            return true;
        }
    } else if gn.rounds > 0 && gn.burst > 0 {
        return true;
    }
    if gn.rounds > 0 {
        let st = gn.table;
        gn.burst = rng.range(st.bursts.0 as f32, st.bursts.1 as f32 + 0.999) as i64;
        gn.delay = rng.range(st.burst_delay.0, st.burst_delay.1);
        gn.burst = gn.burst.min(gn.rounds);
    } else {
        gn.clips -= 1;
        if gn.clips > 0 {
            gn.pause = rng.range(gn.table.clip_delay.0, gn.table.clip_delay.1);
        } else {
            g.fire_ready = false;
        }
    }
    gn.firing = false;
    false
}

/// The gunner's aim 0x00428fc0 (vt+0x190, window in degrees) from the gun's heading (ai/aim.rs `AimBlend::track`);
/// without a target flag 1 is cleared. The flagged case's predicted target point (0x004580a0) is the caller's `target`.
fn gun_aim(gn: &mut Gun, from: Vec3, target: Option<Vec3>, window: f32, dt: f32) -> bool {
    if target.is_none() {
        gn.snap = false;
    }
    gn.aim.track(gn.fwd, from, target, window, gn.snap, dt)
}

/// Off a gun (0x004266b0): the gun's flags 4 / 0x10 cleared, the body back on its feet, +0x8000 / flag 1 cleared.
/// Returns the gun when the grunt was on it (its aim back to the centre: the corner set at 0.5, 0.5).
fn leave_gun(g: &mut Grunt, me: Entity, body: &mut Body, guns: &mut [GunView]) -> Option<Entity> {
    let gn = g.gun.take()?;
    // 0x0042cff0: the guarding flag (+0x484 bit 25) goes with the gun
    g.guard = false;
    for v in guns.iter_mut().filter(|v| v.e == gn.turret) {
        if v.ai == Some(me) {
            v.ai = None;
        }
        if v.reserved == Some(me) {
            v.reserved = None;
        }
    }
    if gn.phase == GunPhase::On {
        body.offset = gn.lift;
        g.fixed = None;
        return Some(gn.turret);
    }
    None
}

/// What the hiding-spot search chose.
enum Hide {
    /// a trench taken (state 0xa0 next)
    Cover,
    /// a mounted gun taken (state 0x7b next)
    Gunner,
    /// the reload in place (anims 0x2a/0x2b/0x28/0x29, state 0x32)
    Reload,
    /// pursue (vt+0x184 0x004295d0)
    Pursue,
    /// a monk's teleport (MonkObject_Bamf 0x00436bb0: state 0xf0 to `Grunt::goal`)
    Teleport,
    /// a monk with specials left re-engages (vt+0x230 0x00427910: in range the fire state)
    Engage,
    /// ... else state 1
    Rest,
    /// a generic cover slot taken (states 0x1e / 0x1f next: coverslot.rs)
    Slot,
}

/// Leaves a held trench: vt+0x188 0x004273a0 (history: +0x918 = the trench, the older two cleared, as without flag
/// 0x10, which only the generic cover slots set) or vt+0x18c 0x004274e0 (history cleared); the slot is freed (vt+0x15c)
/// and +0x484 0x20000 cleared.
fn leave_cover(g: &mut Grunt, me: Entity, trenches: &mut [TrenchView], history: bool) {
    if let Some(c) = g.cover.take() {
        if let Some(t) = trenches.iter_mut().find(|t| t.e == c.trench && t.occupant == Some(me)) {
            t.occupant = None;
        }
        g.cover_history = if history { [Some(c.trench), None, None] } else { [None; 3] };
    } else {
        // with nothing held both clear the history (0x004273a0 pushes the empty +0x904 after clearing it)
        g.cover_history = [None; 3];
    }
    g.at_barricade = false;
}

/// GenericAI_FindHidingSpot 0x0042a7b0 (vt+0x240) as lev_1_m1's grunts reach it: (a0 need_target, a1 leaving, a2 hold) =
/// (1, 0, 0) at the clip's end through 0x0042b8e0 and at a reload's end (state 0x32), (1, 1, 0) leaving a trench, (0, 0, 1) on
/// arriving at a marker whose action is hide (Marker_ProcessArrival 0x0044ba30; nothing found: AI_Wander 60..100):
/// - a clip's end (+0x484 flag 4 clear) with a target: 90 % (0x2d0000) the reload in place, the search skipped
///   (0x004531e0(target), true for an airborne target, taken false: guess);
/// - pickup_grab_pct (+0x86c, default 0 at 0x0042533f; no property set of the 24 levels has the key) % 0x0042a500, a
///   grab of a pickup: dead in the shipped game (docs/port/ai.csv);
/// - a held cover is left (vt+0x188); need_target without a target: nothing found;
/// - the candidates: the nearest free mounted gun (0x0042a100, `pick_gun`; docs/subsystems/turret.md) and the
///   nearest trench (0x00429f00, `pick_trench`); a trench nearer than the gun wins; then the generic cover slots of the
///   list World+0xed4 (kinds 1 ground / 6 enter of objects other than type 3), which lev_1_m1 has none of (its
///   barracks have only ref_exit / ref_spawn nodes; the trenches are type 3, skipped there);
/// - a trench: the path to its ref_exit must exist (0x0046f6d0 == 1: a straight line or a nav route here), then the slot
///   is taken (vt+0x158) and state 0xa0 starts; +0x484 0x200 = hold (a2);
/// - nothing: flag 4 set -> pursue (vt+0x184), else the reload in place (state 0x32).
#[allow(clippy::too_many_arguments)]
fn find_hiding_spot(g: &mut Grunt, me: Entity, (need_target, leaving, hold): (bool, bool, bool), pos: Vec3, target: Option<(Vec3, Vec3)>, spots: &mut Spots, physics: &Physics, own: BodyId, rng: &mut FxStats) -> Hide {
    // a monk's search is MonkObject_Bamf 0x00436bb0: with specials left he re-engages (in range) or rests (the flag 4
    // it tests after clearing it is always set again by the counter roll 0x00427600 between); at 0 he teleports round
    // the target, else round the player (Game+0x748); with no spot found he still goes, back onto his own place
    if let Some(m) = g.monk.as_mut() {
        if m.special > 0 {
            return match target {
                Some((p, _)) if (p - pos).with_y(0.0).length() <= g.range => Hide::Engage,
                _ => Hide::Rest,
            };
        }
        let Some(around) = target.map(|(p, _)| p).or(spots.player.map(|(p, _)| p)) else { return Hide::Rest };
        m.flight_t = 0.0;
        g.goal = monk::bamf_point(around, physics, rng).unwrap_or(pos);
        return Hide::Teleport;
    }
    let flag4 = g.fire_ready;
    if !flag4 && target.is_some() && rng.rand() < 0.9 {
        return Hide::Reload;
    }
    leave_cover(g, me, &mut spots.trenches, true);
    if let Some(h) = g.slot {
        // vt+0x188: a slot left counts in the history (+0x918) as a trench does
        coverslot::release(&mut g.slot, me, &mut spots.slots);
        g.cover_history = [Some(h.obj), None, None];
    }
    g.hold = false;
    // (with a turbo pack, +0xb08: the jumper captain, always the reload, ai_03.cpp:521-540)
    let fail = if flag4 && g.jumper.is_none() { Hide::Pursue } else { Hide::Reload };
    // a base-defence level (0x0044e600, ai_03.cpp:219-224) tries only the breach and the ways over the wall (the
    // assault, ai/assault.rs, its own timed search) and otherwise fails: no gun, trench or cover slot
    if spots.defence {
        return fail;
    }
    // the two candidates by 2D squared distance from the grunt: a trench nearer than the gun wins, else the gun
    let gi = pick_gun(g, pos, target, need_target, &spots.guns, physics, own);
    let ti = pick_trench(g, me, pos, target, need_target, &spots.trenches, physics, own);
    let d2 = |p: Vec3| (p - pos).with_y(0.0).length_squared();
    let ti = match (gi, ti) {
        (Some(gi), Some(ti)) => (d2(spots.trenches[ti].origin) < d2(spots.guns[gi].origin)).then_some(ti),
        (_, t) => t,
    };
    // the generic cover slots (the list World+0xed4: trees, valid buildings; coverslot.rs `pick`), for those whose
    // vt+0x1cc holds (not the archers): nearer (3D, squared) than a quarter of the chosen gun's or trench's 2D squared
    // distance, else than twice scan_distance squared; then within scan_distance; the slot taken (vt+0x158), its
    // point's path needed (0x0046f6b0), +0x494 = 5 s and state 0x1e / 0x1f
    // (none while holding a turbo pack, +0xb08, ai_03.cpp:329)
    if g.kind != Kind::Bow && g.jumper.is_none() && (!need_target || target.is_some()) {
        let won = ti.map(|i| d2(spots.trenches[i].origin)).or(gi.map(|i| d2(spots.guns[i].origin)));
        let limit2 = won.map_or((2.0 * g.scan_distance).powi(2), |d| d * 0.25);
        // (no target: the slots are judged against the player, Game+0x748, ai_03.cpp:222)
        let seeker = coverslot::Seeker {
            me,
            pos,
            target: target.or(spots.player),
            history: [g.cover_history[0], g.cover_history[1]],
            skip_last_both: false,
            physics,
        };
        // the last object left (+0x918) is skipped when leaving one (a1, 0x0042b8e0's slot branch), else 25 % of the
        // time (ai_03.cpp:287-295)
        let skip_last = leaving || rng.rand() < 0.25;
        if let Some((i, slot, point, dir)) = coverslot::pick(&seeker, &spots.slots, limit2, skip_last) {
            let reachable = point.distance(pos) < g.scan_distance && (line_of_sight(physics, pos, point) || crate::nav::path(physics, pos, point, &[]).is_some());
            if reachable {
                spots.slots[i].occupants[slot] = Some(me);
                g.slot = Some(coverslot::SlotHold { obj: spots.slots[i].e, slot, point, dir, look: 0.0, give_up: 5.0, arrived: false });
                g.route.clear();
                g.hold = hold;
                return Hide::Slot;
            }
        }
    }
    if let (Some(i), None) = (gi, ti) {
        // the gun: the path to it (0x0046f6b0 with the gun's collision off; here to ref_grunt_exit: guess), reserved
        // (vt+0x15c(1)), state 0x7b
        let gv = &spots.guns[i];
        if !line_of_sight(physics, pos, gv.exit) && crate::nav::path(physics, pos, gv.exit, &[]).is_none() {
            return fail;
        }
        spots.guns[i].reserved = Some(me);
        let gv = &spots.guns[i];
        g.gun = Some(Gun::to(gv, if gv.forge { g.forge } else { g.stig }));
        g.route.clear();
        g.hold = hold;
        return Hide::Gunner;
    }
    let trenches = &mut spots.trenches;
    let Some(i) = ti else { return fail };
    let exit = trenches[i].exit;
    if !line_of_sight(physics, pos, exit) && crate::nav::path(physics, pos, exit, &[]).is_none() {
        return fail;
    }
    trenches[i].occupant = Some(me);
    let t = &trenches[i];
    g.cover = Some(Cover { trench: t.e, phase: CoverPhase::ToExit, timer: 0.0, exit: t.exit, spot: t.spot, dir: t.dir });
    g.route.clear();
    g.hold = hold;
    Hide::Cover
}

/// The barricade anims' lengths (lev_1_m1_big03_anim): grunt_barricade_reload, _peek, the stand set.
const DUCK_TIME: f32 = 1.333;
const PEEK_TIME: f32 = 2.633;
const STAND_TIME: f32 = 1.967;

/// Duck at the trench: anim 0x66 and state 0xa2 (0x00427910 out of range, 0x00428980 line of sight lost, 0x0042b8e0
/// target within the slot's 80 degrees).
fn duck(g: &mut Grunt) {
    if let Some(c) = g.cover.as_mut() {
        c.phase = CoverPhase::Duck;
        c.timer = DUCK_TIME;
        g.mind = Mind::Cover;
        g.turn = None;
    }
}

/// 0x0042b8e0 for a grunt in a trench's slot (+0x484 0x20000): the target `p` within 80 degrees of the slot's direction
/// (0x0042b860) -> duck (anim 0x66, state 0xa2); else leave the trench (vt+0x188) and search again (vt+0x240(1, 1, 0)).
#[allow(clippy::too_many_arguments)]
fn barricade_clip_end(g: &mut Grunt, me: Entity, p: Vec3, pos: Vec3, tgt: Option<(Vec3, Vec3)>, spots: &mut Spots, physics: &Physics, own: BodyId, rng: &mut FxStats) {
    let c = g.cover.expect("at a barricade without a cover");
    if within_arc(p, c.spot, c.dir, 80.0) {
        duck(g);
    } else {
        leave_cover(g, me, &mut spots.trenches, true);
        if let Some(m) = find_hiding_spot(g, me, (true, true, false), pos, tgt, spots, physics, own, rng).mind() {
            g.mind = m;
        }
    }
}

/// The pursuit's entry (vt+0x184 0x004295d0, state 0x3c): the pause cleared, a point around the target and the
/// delay to its first ray.
fn pursue(g: &mut Grunt, target: Vec3, limping: bool, rng: &mut FxStats) {
    g.pause = 0.0;
    pursuit_point(g, target, limping, rng);
    g.mind = Mind::Chase;
}

/// The pursuit's point within +-20 of the target and the delay to its next ray (+0x490: 0.2..0.4 s, 0.27..0.54 s
/// limping, flag 2 of +0x484).
fn pursuit_point(g: &mut Grunt, target: Vec3, limping: bool, rng: &mut FxStats) {
    g.ray_timer = if limping { rng.range(0.27, 0.54) } else { rng.range(0.2, 0.4) };
    g.goal = target + Vec3::new(rng.range(-20.0, 20.0), 0.0, rng.range(-20.0, 20.0));
}

/// The reload set's length (anims 0x2a, 0x2b, 0x28, 0x29 by PlayAnim4: the engine times the one it plays):
/// grunt_reload_br 1.133 s, grunt_bow_reload_* 1.3 s, captain_reload_* 2.3 s.
/// The state the hiding-spot search's answer starts (0x0042a7b0): a trench (0xa0), a gun (0x7b), the reload in place
/// (anims 0x2a.., state 0x32), or the pursuit (vt+0x184 0x004295d0, state 0x3c; without a target it ends in 0x0042bc60
/// at once: home).
fn hide_to_mind(g: &mut Grunt, h: Hide) -> Mind {
    match h {
        Hide::Cover => Mind::Cover,
        Hide::Gunner => Mind::Gunner,
        Hide::Reload => {
            g.pause = g.reload_len;
            Mind::Reload
        }
        Hide::Pursue => Mind::Chase,
        Hide::Teleport => Mind::Teleport,
        Hide::Engage => Mind::Fire,
        Hide::Rest => Mind::Roam,
        Hide::Slot => Mind::ToSlot,
    }
}

/// 0x0042bc60, the reaction to losing the target (dropped beyond 2 x scan_distance, dead, the clip's end without it):
/// a guard plays the stand set and re-guards (0x7a); in a trench's slot duck (0xa2); else a new search at once
/// (+0x490 = 0: the search system's next tick) and, nothing found: with the fire counters spent (flag 4 clear) the
/// reload (0x32, 0xc1 holding a cover), else leave the cover and walk home (+0x4c0, anim 0xa run_scan, state 0x50).
/// (The pursuit of a still-live enemy, vt+0x184 with +0x1e8 true, has no target to follow and ends here again: home.)
fn lost_target(g: &mut Grunt, me: Entity, trenches: &mut [TrenchView]) -> Mind {
    g.target = None;
    if g.guard {
        g.attention = false;
        return Mind::Guard;
    }
    if g.at_barricade {
        duck(g);
        return Mind::Cover;
    }
    g.search_timer = 0.0;
    if !g.fire_ready {
        g.pause = g.reload_len;
        return Mind::Reload;
    }
    leave_cover(g, me, trenches, true);
    g.attention = false;
    // (a home it cannot reach: the walk ends after a minute, a guard of the engine's: the original's route fails)
    g.timer = 60.0;
    Mind::Home
}

/// AI_Wander 0x0042d430 / state 0x5b's point: walk there (state 0x3d), the spline dropped (+0x570) and +0x488 bit 15
/// cleared; the walk's time limit is the engine's (twice the straight run plus 2 s).
pub(crate) fn wander_to(g: &mut Grunt, from: Vec3, to: Vec3, speed: f32) {
    g.spline = None;
    g.goal = to;
    g.timer = to.distance(from) / speed.max(1.0) * 2.0 + 2.0;
    g.mind = Mind::Wander;
}

/// AI_Wander 0x0042d430 (min, max): the facing turned by rand * K(0x005bce90) - K(0x005bce8c) = -45..+45 degrees, the
/// point at a random distance min..max along it (one roll per axis, as the original).
fn wander(g: &mut Grunt, pos: Vec3, (min, max): (f32, f32), speed: f32, rng: &mut FxStats) {
    let a = g.yaw + rng.range(-std::f32::consts::FRAC_PI_4, std::f32::consts::FRAC_PI_4);
    let dir = Vec3::new(a.sin(), 0.0, a.cos());
    let to = pos + Vec3::new(dir.x * rng.range(min, max), 0.0, dir.z * rng.range(min, max));
    wander_to(g, pos, to, speed);
}

/// AI_FollowMarkerSpline 0x0042d1b0 (first, last tag): a tag picked in the range, its spline's head; the walk there
/// (state 0x96, anim 0x00427210). (+0x484 bit 16 marker_startloc, riding the spline from its start, is no lev_1_m1
/// grunt's.) A tag without a spline: the original removes the grunt ("AI : Can't find marker spline group"); here
/// he stays (none on lev_1_m1).
/// Seconds a marker run waits for its route before the first step (oracle, agent-startfix: lev_2_m2 grunt 16 and
/// lev_bonus_m3's squad set off ~18 frames after the unpause in the original; the grid route request 0x00470aa0 /
/// 0x00470c30 of state 0x96 is the guess at why).
pub const ROUTE_PLAN: f32 = 0.3;

/// A route node counts as reached within this distance in the plane (AIRoute_CheckReached 0x0046f900: squared
/// distance under K(0x005bce5c) = 6.25). (Marker routes never ask its stop-exactly flag 0x40: AIMarker_Advance passes 0.)
const MARKER_REACHED: f32 = 2.5;

/// Object_IsInViewCone 0x00455d90's radians-to-degrees factor (K 0x005bcdb8 = 57.339447, slightly above 180 / pi).
const VIEW_CONE_DEGREES: f32 = 57.339447;

/// Object_IsInViewCone 0x00455d90 with the AI's 190-degree cone: 190 x 0.5 > VIEW_CONE_DEGREES x acos(dot) of the
/// heading `fwd` and the direction `to` (both in the plane). Shared by the grunts' search and the squads' scan.
fn in_view_cone(to: Vec3, fwd: Vec3) -> bool {
    to.normalize_or_zero().dot(fwd).clamp(-1.0, 1.0).acos() * VIEW_CONE_DEGREES < 95.0
}

/// The facing a node's heading needs before it counts as reached (AIRoute_CheckReached 0x0046f900: K 0x005bcd30).
const MARKER_FACING_DOT: f32 = 0.98;

fn follow_spline(g: &mut Grunt, markers: &crate::alarm::Markers, tags: (i64, i64), r: f32) {
    match markers.pick_spline(tags, r) {
        Some(tag) => {
            // (the first leg asks no heading: AI_FollowMarkerSpline 0x0042d1b0)
            g.spline = Some(SplineRun { tag, index: 0, started: false });
            g.marker_face = None;
            g.mind = Mind::ToMarker;
            g.route_wait = ROUTE_PLAN;
        }
        None => {
            g.spline = None;
        }
    }
}

/// The yaw rate of GenericAI_TurnToTarget 0x0042df70 (its 0x00454e60 call's rate, 16.0; ai/body.rs `turn_toward`).
const TURN_TO_TARGET_RATE: f32 = 16.0;
/// The aim window of 0x0042df70's vt+0x190 call from the fire cycle and the slot's stand (45 degrees).
const TURN_TO_TARGET_WINDOW: f32 = 45.0;

/// A charge, 0x0042ce70(0): the player as target unless one is held (vt+0x23c), the pursuit (vt+0x184: the state
/// returned), the search timer +0x490 = 1.5..2.0 s, and any group hand-over under way dropped (+0x578 = 0, +0x57c
/// released: it does not reach him afterwards).
fn charge(g: &mut Grunt, player: Option<Entity>, rng: &mut FxStats) -> Mind {
    g.target = g.target.or(player);
    // (the charge's point list +0x958 keeps the target at any range, 0x0043ecf0)
    g.charged = true;
    g.search_timer = rng.range(1.5, 2.0);
    g.pending = None;
    Mind::Chase
}

/// Marker_ProcessArrival 0x0044ba30 at the grunt's current marker (index `marker` into `Markers::markers`): its trigger
/// (Object_SendTrigger 0x00461870), its alarm (alarm.rs: fx_siren and the runner's Help bark, once), then by its flags:
/// hide (vt+0x240(0, 0, 1), else AI_Wander 60..100), ready_guard / attention_guard (0x0042d0f0: a guard there, stand set
/// or attention), trigger_charge (0x0042ce70(0): the player), trigger_charge_tag (no shipped marker has it, nor its
/// obj_charge_* fields: taken as the plain charge), ic_* (none on lev_1_m1: stands), a positive scatter_min (AI_Wander scatter_min..max); else the next marker:
/// the spline switches when the marker names marker_next_min..max (AIMarker_NextTag 0x0044b830), else index + 1, at the
/// end wrapping with marker_looping (+0x488 bit 1) or stopping: the stand set and state 1 with home = here
/// (ai_06.cpp:1797-1817). Before the action: the flags deactivate (removed, ai/notes.rs `End::Remove`; the breach
/// spline's last marker on the defence levels) and kill (his death). Returns the state it starts, or None
/// to go on along the spline.
#[allow(clippy::too_many_arguments)]
fn marker_arrival(
    g: &mut Grunt,
    me: Entity,
    marker: usize,
    markers: &crate::alarm::Markers,
    (pos, tgt, physics, own): (Vec3, Option<(Vec3, Vec3)>, &Physics, BodyId),
    spots: &mut Spots,
    rng: &mut FxStats,
    (arrive, triggers, ends): (&mut MessageWriter<crate::alarm::MarkerArrival>, &mut MessageWriter<crate::mission::SendTrigger>, &mut MessageWriter<notes::ScriptEnd>),
    player: Option<Entity>,
) -> Option<Mind> {
    let mk = &markers.markers[marker];
    info!("EVENT ai: grunt reached marker {} ({})", mk.id, mk.action);
    if mk.trigger != 0 {
        triggers.write(crate::mission::SendTrigger(mk.trigger));
    }
    if mk.alarm_tag != 0 {
        arrive.write(crate::alarm::MarkerArrival { marker, at: pos, voice_set: g.voice_set });
    }
    // the kill / deactivate flags come first; the spline is not advanced
    if let Some(end) = mk.ends_ai() {
        g.spline = None;
        ends.write(notes::ScriptEnd { entity: me, end });
        return Some(Mind::Roam);
    }
    let speed = g.move_speed;
    let stand = |g: &mut Grunt| {
        g.spline = None;
        g.home = pos;
        g.attention = false;
        Some(Mind::Roam)
    };
    if mk.flags & crate::alarm::Marker::HIDE != 0 {
        g.spline = None;
        let h = find_hiding_spot(g, me, (false, false, true), pos, tgt, spots, physics, own, rng);
        if let Some(m) = h.mind() {
            return Some(m);
        }
        wander(g, pos, (60.0, 100.0), speed, rng);
        return Some(Mind::Wander);
    }
    if mk.flags & (crate::alarm::Marker::READY_GUARD | crate::alarm::Marker::ATTENTION_GUARD) != 0 {
        // 0x0042d0f0: the guard point +0x4c0, +0x484 bit 25, a fidget timer 15..24 s, state 0x79
        g.spline = None;
        g.guard = true;
        g.home = pos;
        g.attention = mk.flags & crate::alarm::Marker::ATTENTION_GUARD != 0;
        return Some(Mind::Guard);
    }
    if mk.flags & (crate::alarm::Marker::TRIGGER_CHARGE | crate::alarm::Marker::TRIGGER_CHARGE_TAG) != 0 {
        g.spline = None;
        g.attention = false;
        return Some(charge(g, player, rng));
    }
    if mk.flags & crate::alarm::Marker::IC != 0 {
        return stand(g);
    }
    if mk.scatter.0 > 0.0 {
        wander(g, pos, mk.scatter, speed, rng);
        return Some(Mind::Wander);
    }
    let run = g.spline?;
    let next = if mk.next.0 != 0 { mk.next.0 + ((rng.rand() * (mk.next.1 - mk.next.0 + 1) as f32) as i64) } else { run.tag };
    if next == run.tag {
        let mut i = run.index + 1;
        if i >= markers.spline_len(run.tag) {
            if !g.looping {
                return stand(g);
            }
            i = 0;
        }
        g.spline = Some(SplineRun { index: i, started: true, ..run });
        // face_dir (+0x490 bit 7) of the marker reached: the next one's heading (0x00453150) is to be faced there
        g.marker_face = mk.face_dir.then(|| markers.spline_marker(run.tag, i).map(|m| m.dir.with_y(0.0).normalize_or_zero())).flatten();
        return None;
    }
    // another spline (0x0044c030 + 0x00426640): its first marker; a missing one is logged ("Marker : Switching to
    // spline that does not exists") and the run stays where it is
    if markers.spline_len(next) > 0 {
        g.spline = Some(SplineRun { tag: next, index: 0, started: true });
        // (the face flag is the new spline's own, Marker_GetFlags on it)
        g.marker_face = markers.spline_marker(next, 0).filter(|m| m.face_dir).map(|m| m.dir.with_y(0.0).normalize_or_zero());
    }
    None
}

/// The end of a hit reaction (GenericAI_Update ai_06.cpp:3191-3412). Returns the state it starts.
/// - 0x12f (small): health at most long_hit_pct % of max -> a long hit (anims 0x4b..0x4f by rand * 500 >> 15, state
///   0x132); else as 0x132's end;
/// - 0x132 (long): in the slot duck (0xa2); a guard back to guarding (stand set, 0x7a); else leave any cover (vt+0x18c)
///   and with flag 4 pursue (vt+0x184), else the hiding-spot search (1, 0, 0);
/// - 0x130 / 0x131 (medium / big): the slot duck; a guard 0x7a; else leave the cover; with a target and flag 4 engage
///   (vt+0x230) when it is in range (2D, +0x5d8) and in sight, else pursue; without, the search (1, 0, 0);
/// - 300 (all fours, anim 0x35): alive -> anim 0x36 looped for 3..7 s (0x12d), then 0x38 standing up (0x12e), whose end
///   ducks in the slot, re-guards a guard, else searches (1, 0, 0).
#[allow(clippy::too_many_arguments)]
fn reaction_end(
    g: &mut Grunt,
    me: Entity,
    health: f32,
    has_target: bool,
    in_range: bool,
    sight: impl Fn() -> bool,
    spots: &mut Spots,
    (pos, tgt, physics, own): (Vec3, Option<(Vec3, Vec3)>, &Physics, BodyId),
    rng: &mut FxStats,
    lib: &mut crate::skin::AnimLibrary,
) -> Option<Mind> {
    let kind = std::mem::take(&mut g.reacting);
    let search = |g: &mut Grunt, spots: &mut Spots, rng: &mut FxStats| {
        let h = find_hiding_spot(g, me, (true, false, false), pos, tgt, spots, physics, own, rng);
        hide_to_mind(g, h)
    };
    let common = |g: &mut Grunt, spots: &mut Spots, rng: &mut FxStats| -> Mind {
        if g.at_barricade {
            duck(g);
            return Mind::Cover;
        }
        g.attention = false;
        if g.guard {
            return Mind::Guard;
        }
        leave_cover(g, me, &mut spots.trenches, false);
        if g.fire_ready {
            if has_target { Mind::Chase } else { lost_target(g, me, &mut spots.trenches) }
        } else {
            search(g, spots, rng)
        }
    };
    match kind {
        1 if health / g.max_health <= g.long_hit_pct / 100.0 => {
            let id = 0x4b + ((rng.rand() * 500.0) as u32 / 100).min(4);
            g.hit = Some((g.anim(id, rng.rand()).to_owned(), 4));
            g.hit_id = id;
            g.reacting = 4;
            None
        }
        1 | 4 => Some(common(g, spots, rng)),
        2 | 3 => {
            if g.at_barricade {
                duck(g);
                return Some(Mind::Cover);
            }
            g.attention = false;
            if g.guard {
                return Some(Mind::Guard);
            }
            leave_cover(g, me, &mut spots.trenches, false);
            Some(if has_target && g.fire_ready {
                if in_range && sight() { Mind::Fire } else { Mind::Chase }
            } else {
                search(g, spots, rng)
            })
        }
        5 => {
            g.hit = Some((g.anim(0x36, 0.0).to_owned(), 6));
            g.reacting = 6;
            g.anim_left = rng.range(3.0, 7.0);
            None
        }
        6 => {
            let n = g.anim(0x38, 0.0);
            g.hit = Some((n.to_owned(), 7));
            g.reacting = 7;
            let _ = anim_len(lib, n);
            None
        }
        7 => {
            if g.at_barricade {
                duck(g);
                return Some(Mind::Cover);
            }
            g.attention = false;
            if g.guard {
                return Some(Mind::Guard);
            }
            Some(search(g, spots, rng))
        }
        _ => None,
    }
}

/// What ended the fire cycle's frame.
#[derive(PartialEq)]
enum FireEnd {
    None,
    /// a burst over with more of the clip left (0x00428980, ai_02.cpp:1928-1937: the close attack's other moment)
    Burst,
    /// a clip spent with more clips left: the reload in place (ai_02.cpp:1938-1952, state 0x32)
    Reload,
    /// the last clip spent (0x00428980 -> 0x0042b8e0); `close_attack` false when a blocked taunt skips the fallbacks
    Clip { close_attack: bool },
    /// no line of sight to the target (los_lost)
    Sight,
    /// the last clip ended in a taunt (state 0x6e)
    Taunt,
}

#[allow(clippy::too_many_arguments)]
fn think(
    mut commands: Commands,
    time: Res<Time>,
    mut stats: ResMut<AiStats>,
    mut rng: ResMut<FxStats>,
    mut physics: NonSendMut<Physics>,
    targets: Query<&GlobalTransform>,
    anims: Query<&crate::skin::Animator, Without<Grunt>>,
    mut grunts: Query<(Entity, &mut Grunt, &mut Body, &mut Transform, &mut Health, Option<&crate::skin::Animator>, Has<crate::topsy::Thrown>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    rtime: Res<Time<Real>>,
    (mut barks, voices): (ResMut<Barks>, crate::audio::PlayingVoices),
    (markers, mut arrive, mut trenches, mut guns, mounted, data, mission): (
        Res<crate::alarm::Markers>,
        MessageWriter<crate::alarm::MarkerArrival>,
        Query<(Entity, &mut Barricade)>,
        Query<(Entity, &mut crate::turret::Turret, &GlobalTransform, &crate::skin::Animator, (Option<&crate::level::Placed>, Option<&crate::level::InstanceId>)), Without<Grunt>>,
        Res<crate::turret::Mounted>,
        Res<crate::gamedata::GameData>,
        Option<Res<crate::mission::Mission>>,
    ),
    (mut lib, player_q, mut triggers, grenade_assets, squads, mut jmarks, army, placed, mut limit, mut force, motion, mut close, mut covers, traitors): (
        ResMut<crate::skin::AnimLibrary>,
        Query<Entity, With<Player>>,
        MessageWriter<crate::mission::SendTrigger>,
        Option<Res<grenade::GrenadeAssets>>,
        Query<&squad::Squad>,
        Query<(Entity, &mut JumpMarker, &Transform), Without<Grunt>>,
        Query<(), With<ArmyTarget>>,
        Query<&crate::level::Placed>,
        ResMut<max_fire::ShooterLimit>,
        ResMut<AiForce>,
        Res<aim::TargetMotion>,
        (MessageWriter<melee::CloseHit>, ResMut<melee::CloseStats>, MessageWriter<notes::ScriptEnd>),
        Query<(Entity, &mut coverslot::CoverObject, &GlobalTransform, Option<&crate::level::Placed>), Without<Grunt>>,
        Query<(), With<crate::weapons::traitor::Traitor>>,
    ),
    (topsy, glove, suction): (Option<Res<crate::topsy::Topsy>>, Option<Res<crate::weapons::glove::Glove>>, Option<Res<crate::weapons::blackhole::Suction>>),
) {
    let t0 = Instant::now();
    // the weapon effects that hold the bad guys' grenades (the "throw" note, 0x0043d180, ai_05.cpp:1282-1295): the
    // Topsy Turvy's timer (Game+0xc80 > 0), the knockout glove on (Game+0xcbc), a black hole sucking (World+0xf00)
    let throw_held = topsy.is_some_and(|t| t.active()) || glove.is_some_and(|g| g.active()) || suction.is_some_and(|s| s.active);
    let player_e = player_q.single().ok();
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    let mut states = [0usize; 32];
    let views: Vec<TrenchView> = trenches
        .iter()
        .map(|(e, b)| TrenchView { e, origin: b.origin, exit: b.exit, spot: b.spot, dir: b.dir, occupant: b.occupant, collider: b.collider })
        .collect();
    let gun_views: Vec<GunView> = guns
        .iter()
        .map(|(e, t, gt, a, (placed, inst))| {
            let (rot, origin) = (gt.rotation(), gt.translation());
            let (yaw, _, _) = (rot * crate::level::game_to_engine().inverse()).to_euler(EulerRot::YXZ);
            GunView {
                e,
                origin,
                exit: t.grunt_exit.map(|i| t.bone_world(gt, a, i).w_axis.truncate()).unwrap_or(origin),
                yaw,
                fwd: t.forward(gt, a).with_y(0.0).normalize_or_zero(),
                usable: t.ai_can_use && t.stayout <= 0.0 && mounted.0 != Some(e),
                reserved: t.reserved,
                ai: t.ai,
                collider: placed.and_then(|p| p.collider),
                instance: inst.map(|i| i.0),
                forge: t.two_pivot(),
            }
        })
        .collect();
    // (a playtest's ai_force "no_cover", kept: no generic cover slots, for a test that needs its grunts where they are)
    let slot_views: Vec<coverslot::SlotView> = if force.0.iter().any(|w| w == "no_cover") {
        Vec::new()
    } else {
        covers.iter().map(|(e, c, gt, pl)| coverslot::SlotView::of(e, c, gt, pl.and_then(|p| p.collider))).collect()
    };
    let defence = mission.is_some_and(|m| m.start_in_turret);
    let player = player_e.and_then(|e| targets.get(e).ok().map(|t| (e, t.translation()))).map(|(e, p)| {
        let hit = anims.get(e).ok().and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation());
        (p, hit.unwrap_or(p + Vec3::Y * 7.0))
    });
    let mut spots = Spots { trenches: views, guns: gun_views, slots: slot_views, defence, player };
    // the jump markers (World+0xf10) as the jumpers' choice sees them; their occupants written back at the end
    let mut jviews: Vec<jumper::MarkerView> =
        jmarks.iter().map(|(e, m, t)| jumper::MarkerView { e, pos: t.translation, tag: m.tag, free: m.occupant.is_none() }).collect();
    let mut joccupy: Vec<(Entity, Option<Entity>)> = Vec::new();
    // targets taken this frame that alert the taker's group: (group, target, taker)
    let mut alerts: Vec<(i64, Entity, Entity)> = Vec::new();
    for (ge, mut g, mut body, mut tr, mut h, ga, thrown) in &mut grunts {
        g.me = Some(ge);
        // a slot is held only on the run to it and in the fight there (states 0x1e.. / 0x34 / 0x33); anything else lets
        // it go (vt+0x18c), as the death does (0x0040cd40 / 0x00497430 free it from the object's side)
        if g.slot.is_some() && !matches!(g.mind, Mind::ToSlot | Mind::Fire | Mind::Reload) {
            coverslot::release(&mut g.slot, ge, &mut spots.slots);
        }
        if g.mind == Mind::Dead {
            dead += 1;
            continue;
        }
        alive += 1;
        states[g.mind as usize] += 1;
        // held by the Topsy Turvy (hanging on, or falling into the sky: topsy.rs): his states wait until it lets go
        if thrown {
            continue;
        }
        // a trench held by a grunt another system took elsewhere (a scare, a throw): let it go (guess: as vt+0x18c)
        if g.cover.is_some() && !matches!(g.mind, Mind::Cover | Mind::Fire | Mind::Reload) && !(g.mind == Mind::Throw && g.at_barricade) {
            leave_cover(&mut g, ge, &mut spots.trenches, false);
        }
        // likewise a gun (0x004266b0)
        if g.gun.is_some() && !matches!(g.mind, Mind::Gunner | Mind::Cower) {
            if let Some(t) = leave_gun(&mut g, ge, &mut body, &mut spots.guns) {
                if let Ok((_, mut tu, ..)) = guns.get_mut(t) {
                    tu.ai_aim(0.5, 0.5);
                }
            }
        }
        // mode turret (+0x488 0x40000000): AI_ResolveStartObjects 0x00425e20 puts him on the gun of his obj_tag at once
        // (+0x898, the gun's flag 4, +0x484 0x8000 | 0x200: he holds it until his first firing cycle starts, state 0x7d)
        if let Some(id) = g.start_gun.take() {
            match spots.guns.iter().position(|v| v.instance == Some(id)) {
                Some(i) if spots.guns[i].ai.is_none() && spots.guns[i].reserved.is_none() => {
                    let gv = &spots.guns[i];
                    g.gun = Some(Gun::to(gv, if gv.forge { g.forge } else { g.stig }));
                    g.hold = true;
                    g.mind = Mind::Gunner;
                    spots.guns[i].reserved = Some(ge);
                    // (the start path attaches him with no walk: here he is put at the gun's exit, and the 0x7b arrival
                    // below mounts him this frame)
                    let at = spots.guns[i].exit;
                    tr.translation = at;
                    if let Ok(mut b) = physics.world.body_mut(body.id) {
                        let _ = b.set_position(crate::physics::jr((at + body.offset) * k), Activation::Activate);
                    }
                }
                // occupied: "Can't start in turret (turret occupied)" and the grunt removed (vt+0xdc)
                Some(_) => {
                    warn!("ai: grunt {ge:?} can't start in turret {id} (occupied): removed");
                    g.mind = Mind::Dead;
                    physics.remove(body.id);
                    commands.entity(ge).despawn();
                    continue;
                }
                // the guns not set up yet this frame (turret.rs `setup` runs in the same Update): he asks again next
                // frame (the order of the systems decided it before: the first frame could miss them)
                None if spots.guns.is_empty() => g.start_gun = Some(id),
                // a gun the engine does not make (the forge cannons): he stands guard where he is (debt)
                None => {
                    g.guard = true;
                    g.mind = Mind::Guard;
                }
            }
        }
        // hurt: bark at most every 4 s (0x004394c0); a hurt grunt waiting for a trigger with trigger_los activates
        // (ai_04.cpp:420)
        g.hurt_voice_timer -= dt;
        // trigger_no_damage (a monk waiting for his trigger): the damage slot 0x004371e0 returns before taking it
        if g.no_damage && h.0 < g.last_health {
            h.0 = g.last_health;
            g.attacker = None;
            g.blast_fling = None;
            g.cartoon = false;
        }
        let long_round = std::mem::take(&mut g.long_hit_round);
        if h.0 < g.last_health && h.0 > 0.0 {
            g.last_hit = g.last_health - h.0;
            // the hit reaction (GenericAIObject_Slot52 0x00430340, ai_04.cpp:444-535; anim names by the class's table,
            // `anim_name_with`), in its order: on a gun (+0x484 0x8000) none; the all-fours fall (vt+0x200: the captain)
            // when the health crosses allfours_pct (+0x9e0) %: anim 0x35, state 300; then (vt+0x204: all three) damage
            // <= hit_level_1 a small hit (not while one or a long hit plays, states 0x12f / 0x132): one of ids
            // 0x3f..0x42 by rand * 400 >> 15, state 0x12f; <= hit_level_2 a medium hit, 0x43 / 0x44 alternating on the
            // anim playing (the first by rand * 200 >> 15 < 100), state 0x130; else a big hit 0x45 + (rand * 2 >> 15),
            // state 0x131. Before them a record with flag 0x100 (a base turret round, `long_hit_round`) plays a long hit
            // 0x77 + (rand * 6 >> 15), state 0x131, unless one plays (state 0x131 already); after the all-fours fall a
            // head shot (the record's flag 4: player.rs strike, `head_hit`) plays the head-hit anim 0x47 +
            // (rand * 2 >> 15) as a big hit (state 0x131).
            // Before any of it (ai_04.cpp:411-425): a busy grunt (vt+0x178 0x0042f8d0, `Grunt::busy` with the source:
            // on or going to a gun, an IC move, held, throwing, a squad member, waiting for his trigger, ...) has no
            // reaction and makes no offer; only one waiting for his trigger (state 0x8c) with trigger_los and a source
            // takes it as his target and wakes (vt+0x23c, 0x0042f5c0). The barks above it still play.
            let d = g.last_hit;
            // a busy grunt (vt+0x178 0x0042f8d0 with the source: on a gun, waiting for his trigger, carried, held, on the
            // way to a spline's head, in a squad's march ...) takes the damage and barks, and that is all (ai_04.cpp:420)
            let busy = g.attacker.map_or_else(|| g.busy_now(), |a| g.busy(ge, a));
            let head = std::mem::take(&mut g.head_hit);
            let (before, after) = (g.last_health / g.max_health, h.0 / g.max_health);
            let fours = g.allfours_pct / 100.0;
            let react: Option<(u32, u8)> = if busy {
                None
            } else if long_round {
                (g.reacting != 3 || g.hit.is_none()).then(|| (0x77 + (rng.rand() * 6.0) as u32 % 6, 3))
            } else if g.kind.allfours() && before > fours && after <= fours {
                Some((0x35, 5))
            } else if head {
                Some((0x47 + (rng.rand() * 2.0) as u32 % 2, 3))
            } else if d <= g.hit_level_1 {
                (!matches!(g.reacting, 1 | 4) || g.hit.is_none()).then(|| (0x3f + ((rng.rand() * 400.0) as u32 / 100).min(3), 1))
            } else if d <= g.hit_level_2 {
                let id = match (g.hit.is_some(), g.hit_id) {
                    (true, 0x43) => 0x44,
                    (true, 0x44) => 0x43,
                    _ if rng.rand() < 0.5 => 0x43,
                    _ => 0x44,
                };
                Some((id, 2))
            } else {
                Some((0x45 + (rng.rand() * 2.0) as u32 % 2, 3))
            };
            // (an id the class's table has no anim for, e.g. the Q112's 0x3f..0x42 -> q112_anim_error: the reaction is over
            // at once; it used to hold him for good, its anim never finishing)
            let react = react.filter(|&(id, _)| lib.anim(g.anim(id, 0.0)).is_some());
            if let Some((id, kind)) = react {
                g.hit = Some((g.anim(id, rng.rand()).to_owned(), kind));
                g.hit_id = id;
                g.reacting = kind;
                g.turn = None;
                // stopMove: leave a cover held on the way (vt+0x18c), not the slot (+0x484 0x20000): the reaction's
                // end ducks there
                if g.cover.is_some() && !g.at_barricade {
                    leave_cover(&mut g, ge, &mut spots.trenches, false);
                }
            }
            // the Q112: its Hurt (0x0043a100: vo_delay, kept as its line; no HurtShort) and, below a quarter of its
            // health, its eye loop (0x00435170)
            if g.kind == Kind::Q112 {
                let max = g.max_health;
                let r = rng.rand();
                if let Some(q) = g.q112.as_mut() {
                    if let Some(name) = q.eye_after_hit(h.0, max, r) {
                        let id = crate::audio::SoundId::new();
                        q.eye = Some(id);
                        sounds.write(crate::audio::PlaySound::on(name, ge).looping(true).with_id(id));
                        info!("EVENT ai: q112 {ge:?} eye loop {name}");
                    }
                }
                if barks.take(Delay::Vo, time.elapsed_secs(), &mut rng) {
                    let line = q112::pick(&q112::HURT_LINES, rng.rand());
                    let req = g.speech.line(&voices, crate::audio::PlaySound::at(line, tr.translation));
                    sounds.write(req);
                }
            } else if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) {
                // Hurt (0x00439e00, shared pool, death_delay) first; when refused HurtShort (0x00439e30) at most every
                // 4 s per grunt; the shared one restarts the 4 s too (+0xb48)
                g.hurt_voice_timer = 4.0;
                // (kept as his line, 0x00439e00 -> +0xb50)
                let set = g.voice_set;
                let req = g.speech.line(&voices, bark_request("Hurt:{}:last4", set, tr.translation).following(ge));
                sounds.write(req);
            } else if g.hurt_voice_timer <= 0.0 {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt:{}:butlast4", g.voice_set, tr.translation, ge);
            }
            let source = g.attacker.take();
            if busy {
                // a waiting grunt that looks (state 0x8c with trigger_los, +0x488 0x40) wakes on a hit with a source,
                // which becomes his target (vt+0x23c(source, 1), then 0x0042f5c0(source)); without trigger_los he goes
                // on waiting
                if let Some(a) = source.filter(|_| g.mind == Mind::WaitTrigger && g.trigger_los) {
                    g.target = Some(a);
                    activate(&mut g, false, Some(a));
                    alerts.push((g.alert_group, a, ge));
                }
            } else if let Some(a) = source {
                // retaliation: the end of GenericAIObject_Slot52 0x00430340 offers the damage source (when it is one of
                // the grunt's enemies, list +0x604: Roman or a companion) as a target (vt+0x1b4(source, 1, a2): a2 set
                // when no reaction started), and a source taken alerts his group (a1 = 1)
                if let Ok(ap) = targets.get(a).map(|t| t.translation()) {
                    let current = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
                    let had = g.target;
                    if offer_target(&mut g, (a, ap), current, tr.translation, time.elapsed_secs(), react.is_none()) {
                        if std::env::var("AIDBG").is_ok() {
                            info!("AIDBG retaliate {:?} -> {:?} (had {:?})", body.id, a, had);
                        }
                        alerts.push((g.alert_group, a, ge));
                    }
                }
            }
        }
        // the killing hit's damage for Character_Die's ragdoll test (record +0x14 >= hit_ragdoll, game_02.cpp:1370):
        // what a weapon set itself (`last_hit`) or else the health lost this frame; a survivor's is spent here
        if h.0 > 0.0 {
            g.last_hit = 0.0;
        } else if h.0 < g.last_health {
            g.last_hit = g.last_hit.max(g.last_health - h.0);
        }
        g.last_health = h.0;
        // an IC move carries him (state 0x64: ic.rs and the mover); held by a companion who comes to kill him (state
        // 0xf: close_kill.rs); a fall is fall.rs's (state 0x82)
        if matches!(g.mind, Mind::Scripted | Mind::Held | Mind::Falling) {
            continue;
        }
        // the Q112's trail is of one target (0x004246a0 when another is taken)
        let current = g.target;
        if let Some(q) = g.q112.as_mut() {
            q.follow_target(current);
        }
        // the all-fours breathing (state 0x12d) lasts its timer (+0x490, 3..7 s)
        if g.reacting == 6 {
            g.anim_left -= dt;
            if g.anim_left <= 0.0 {
                g.hit = None;
            }
        }
        if !stats.enabled {
            continue;
        }
        let pos = tr.translation;
        let flat = |v: Vec3| Vec3::new(v.x, 0.0, v.z);
        let limping = g.limp_pct > 0.0 && h.0 < g.max_health * g.limp_pct / 100.0;
        let run_speed = if limping { g.limp_speed } else { g.move_speed };
        let target = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
        g.timer -= dt;
        g.ray_timer -= dt;
        // a target farther than 2 x scan_distance is dropped (states 0x3c, 0xa: vt+0x180 0x0043ecf0, which keeps at
        // any range an army target, any target on a base-defence level (0x0044e600) and any after a charge (+0x958);
        // a priority target's scan_distance is its distance when found, AI_FindTarget; the guard state 0x79 too,
        // through 0x0042ec20 every frame). (Its +0x488 flag 4 is not known.)
        if let Some(p) = target {
            let kept = g.charged || g.base_defence || g.target.is_some_and(|e| army.contains(e));
            if flat(p - pos).length() > 2.0 * g.scan_distance && !kept {
                g.target = None;
            }
        }
        let target = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
        let in_range = target.is_some_and(|p| flat(p - pos).length() <= g.range);
        // (the infinite accuracy ranges are a guard's for good once an offer found his target beyond his range:
        // `offer_target`, 0x0042c790)
        // the target's ref_hit_00 (0x00428280) and the grunt's own: the sight rays' ends
        let target_hit = target.map(|p| {
            g.target
                .and_then(|e| anims.get(e).ok())
                .and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok()))
                .map(|j| j.translation())
                .unwrap_or(p + Vec3::Y * 7.0)
        });
        let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(pos + Vec3::Y * 6.0);
        let tgt = target.zip(target_hit);
        // the reaction's end (states 0x12f .. 0x132, 0x12c .. 0x12e: their anim done, charanim.rs cleared `hit`)
        if g.hit.is_none() && g.reacting != 0 {
            let sight = || tgt.is_some_and(|(_, thit)| sight_ray(&physics, own_hit, thit, &[body.id]));
            if let Some(m) = reaction_end(&mut g, ge, h.0, target.is_some(), in_range, sight, &mut spots, (pos, tgt, &physics, body.id), &mut rng, &mut lib) {
                g.mind = m;
            }
        }
        // a troop first walks out of the door (state 0x5a 0x00442407: following its path, nothing else)
        if let Some(x) = g.exit {
            if flat(x - pos).length() < 4.0 {
                g.exit = None;
                match g.marker_tags.take() {
                    // marker tags (+0x558): vt+0x18c and the spline (0x0042d1b0); no charge then
                    Some(tags) => {
                        g.charge = false;
                        follow_spline(&mut g, &markers, tags, rng.rand());
                    }
                    None => {
                        // no marker tag: a point 20..50 ahead, within +-45 degrees of its heading (state 0x5b); there
                        // the home point and, with +0x488 0x200 (an alarm troop), the charge (0x0042ce70), else stand
                        let a = g.yaw + rng.range(-std::f32::consts::FRAC_PI_4, std::f32::consts::FRAC_PI_4);
                        let d = rng.range(20.0, 50.0);
                        wander_to(&mut g, pos, pos + Vec3::new(a.sin(), 0.0, a.cos()) * d, run_speed);
                    }
                }
            } else {
                g.mind = Mind::ToMarker;
            }
        }
        // a trigger_marker grunt, once awake (its trigger, a target found, or hurt), follows his spline (the activation
        // 0x0042f5c0 -> AI_FollowMarkerSpline 0x0042d1b0, ai_01.cpp:2498-2531)
        if g.exit.is_none() && g.spline.is_none() && g.mind != Mind::WaitTrigger && g.mind != Mind::Dead {
            if let Some(tags) = g.marker_tags.take() {
                follow_spline(&mut g, &markers, tags, rng.rand());
            }
        }
        // the spline states 0x96 (to its head) / 0x97 (along it): unless the marker the grunt comes from is strict, a
        // target found ends the run (the spline dropped: +0x488 0x18000 clear on lev_1_m1's grunts) and is engaged
        if let Some(run) = g.spline {
            let from = if run.started && run.index > 0 { run.index - 1 } else { 0 };
            let strict = markers.spline_marker(run.tag, if run.started { from } else { 0 }).is_some_and(|m| m.strict);
            if target.is_some() && !strict {
                g.spline = None;
                g.marker_face = None;
                g.mind = Mind::Roam;
            } else if let Some(mk) = markers.spline_marker(run.tag, run.index) {
                g.mind = Mind::ToMarker;
                // the route's last node reached (AIRoute_CheckReached 0x0046f900: within 2.5 units in the plane, and
                // facing its heading when it has one, `marker_face`)
                let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
                let faced = g.marker_face.is_none_or(|f| f == Vec3::ZERO || fwd.dot(f) >= MARKER_FACING_DOT);
                if flat(mk.pos - pos).length() < MARKER_REACHED && faced {
                    g.marker_face = None;
                    // Marker_ProcessArrival 0x0044ba30 (0x96's arrival does it for the head, then state 0x97)
                    g.spline = Some(SplineRun { started: true, ..run });
                    let idx = markers.spline_index(run.tag, run.index).unwrap_or(0);
                    stats.markers += 1;
                    if let Some(m) = marker_arrival(&mut g, ge, idx, &markers, (pos, tgt, &physics, body.id), &mut spots, &mut rng, (&mut arrive, &mut triggers, &mut close.2), player_e) {
                        g.mind = m;
                    }
                }
            } else {
                g.spline = None;
            }
        }
        // the reload's end rolls the fire counters (0x00427600: flag 4; flag 1 too after a spent cycle)
        if g.mind == Mind::Reload && g.pause <= 0.0 {
            if !g.fire_ready {
                g.throw_armed = true;
            }
            g.fire_ready = true;
        }
        // debug: the search as at a reload's end (playtest `cover` step)
        if g.force_hide {
            g.force_hide = false;
            g.fire_ready = true;
            let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
            g.mind = match h {
                Hide::Reload => Mind::Reload,
                h => hide_to_mind(&mut g, h),
            };
            info!("EVENT ai: cover search -> {:?}", g.mind);
        }
        // the one-shot fire and reload anims' lengths of the classes whose cycle follows its anims (the sniper, the
        // rocket captain)
        if g.fire_len <= 0.0 {
            // a shot per "fire" note: the anim's length over its notes (a looped chaingun set has several)
            let n = g.anim(0x23, 0.0);
            let notes = lib.anim(n).map(|a| a.notes.iter().filter(|x| x.1.trim().eq_ignore_ascii_case("fire")).count()).unwrap_or(0).max(1);
            g.fire_len = anim_len(&mut lib, n) / notes as f32;
        }
        if g.reload_len <= 0.0 {
            g.reload_len = anim_len(&mut lib, g.anim(0x29, 0.0));
        }
        g.shot_age += dt;
        // the sniper's SetTarget 0x00434930: a target taken raises the gun to the eye (Captain_sniper_attach), one lost
        // lowers it (Captain_sniper_detach); state 0x104 holds him still while the anim plays, then engages. It runs at
        // the SetTarget itself, so a target lost while the gun goes up lowers it at once (the pose cut short)
        if g.kind == Kind::Sniper && g.target.is_some() != g.aiming {
            g.aiming = g.target.is_some();
            let n = if g.aiming { "captain_sniper_attach" } else { "captain_sniper_detach" };
            g.pose = Some((n, anim_len(&mut lib, n)));
            g.pause = 0.0;
            info!("EVENT ai: sniper {ge:?} {}", if g.aiming { "aims" } else { "lowers his gun" });
        }
        if let Some((n, left)) = g.pose {
            g.pose = (left > dt).then_some((n, left - dt));
        }
        // a squad member (+0x9bc): follows his bone (0xdc) or idles (0xde) unless the squad is halted and he has a
        // target: then he fights where he stands (0xdd, ai_06.cpp:2644-2698). A squad gone lets him go.
        let in_squad = g.squad.and_then(|l| squads.get(l.squad).ok().map(|s| (l, s)));
        if g.squad.is_some() && in_squad.is_none() {
            g.squad = None;
            g.alert_group = 0;
            if g.mind == Mind::InSquad {
                g.mind = Mind::Roam;
            }
        }
        let squad_fight = in_squad.is_some_and(|(_, s)| s.members_engage() && g.target.is_some());
        if in_squad.is_some() && !squad_fight && !matches!(g.mind, Mind::Dead) {
            g.mind = Mind::InSquad;
        }
        let was = g.mind;
        // the pursuit's ray (0x004137e0 / 0x00414110): the grunt's ref_hit_00 to the target's, a placed target's own
        // collision counting as a sight of it
        let chase_sees = g.mind == Mind::Chase && !g.guard && in_range && g.ray_timer <= 0.0 && {
            let target_body = g.target.and_then(|t| placed.get(t).ok()).and_then(|p| p.collider);
            let skip: Vec<BodyId> = std::iter::once(body.id).chain(target_body).collect();
            target_hit.is_some_and(|thit| sight_ray(&physics, own_hit, thit, &skip))
        };
        g.mind = match (g.mind, target) {
            // a charge's pursuit (0x0042ce70 -> vt+0x184) comes before any engaging
            (Mind::Roam, Some(_)) if std::mem::take(&mut g.pursue) => Mind::Chase,
            (Mind::InSquad, _) if !squad_fight => Mind::InSquad,
            (Mind::Jump, _) if g.jumper.is_some_and(|j| j.flight.is_some()) => Mind::Jump,
            (Mind::Teleport, _) => Mind::Teleport,
            (Mind::Cast, _) => Mind::Cast,
            (Mind::ToMarker, _) => Mind::ToMarker,
            // state 0xae has no exit
            (Mind::Retreat, _) => Mind::Retreat,
            // state 0xc8 (ai_06.cpp:2493-2532): the run ends at its point or, without the bomb on him, when the timer
            // runs out
            (Mind::Flee, _) if if g.flee_bomb { flat(g.goal - pos).length() >= MARKER_REACHED } else { g.timer > 0.0 } => Mind::Flee,
            // the scare's end charges his target, else the player (0x0042ce70)
            (Mind::Flee, _) => {
                g.flee_bomb = false;
                charge(&mut g, player_e, &mut rng)
            }
            (Mind::Cower, _) => Mind::Cower,
            // the wander (0x3d / 0x5b) and the one-shot states run their own course (below)
            (Mind::Wander, _) => Mind::Wander,
            (Mind::ToSlot, _) if g.slot.is_some() => Mind::ToSlot,
            (Mind::Taunt, _) => Mind::Taunt,
            (Mind::CloseAttack, Some(_)) => Mind::CloseAttack,
            (Mind::Throw, _) => Mind::Throw,
            // waiting for a turn under the shooter limit (state 0x11): only a hand-over engages again (max_fire.rs)
            (Mind::MaxFireWait, Some(_)) if !g.retry_engage => Mind::MaxFireWait,
            // the trench's states run their own course (below)
            (Mind::Cover, _) if g.cover.is_some() => Mind::Cover,
            (Mind::Gunner, _) if g.gun.is_some() => Mind::Gunner,
            // in the slot the fire state 0xa keeps firing while the target is held (its exits: the clip's end, a lost
            // line of sight); no target: duck (guess: 0x0042bc60's path is not traced for a barricade grunt)
            (Mind::Fire, Some(_)) if g.at_barricade => Mind::Fire,
            (Mind::Fire, None) if g.at_barricade => {
                duck(&mut g);
                Mind::Cover
            }
            (Mind::Reload, _) if g.pause > 0.0 => Mind::Reload,
            // the reload's end (state 0x32, ai_06.cpp:1223): in the slot engage (0x00427910: in range fire, else duck);
            // otherwise in range (2D, +0x5d8) with a line of sight fire again (vt+0x230), else leave any cover and 50 %
            // search for a hiding spot (vt+0x240(1, 0, 0) with flag 4 set: nothing found -> pursue), 50 % pursue
            (Mind::Reload, Some(_)) if g.at_barricade => {
                if in_range {
                    Mind::Fire
                } else {
                    duck(&mut g);
                    Mind::Cover
                }
            }
            (Mind::Reload, Some(_)) if !g.guard => {
                let (_, thit) = tgt.unwrap_or((pos, pos));
                if in_range && sight_ray(&physics, own_hit, thit, &[body.id]) {
                    Mind::Fire
                } else if rng.rand() < 0.5 {
                    let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                    hide_to_mind(&mut g, h)
                } else {
                    leave_cover(&mut g, ge, &mut spots.trenches, true);
                    Mind::Chase
                }
            }
            (Mind::WaitTrigger, None) => Mind::WaitTrigger,
            (Mind::Bully, None) => Mind::Bully,
            (Mind::Home, None) => Mind::Home,
            // the chase engages on its ray (state 0x3c); the archers (vt+0x1cc false) only within 0.9 x +0x5d8
            // (K 0x005bcc38) first
            (Mind::Chase, Some(p)) if !g.guard && !g.kind.chase_ray_only() && flat(p - pos).length() > 0.9 * g.range => Mind::Chase,
            // and only on its ray (every 0.2..0.4 s, ai_06.cpp:1434-1470): in range with a clear sight of the target
            (Mind::Chase, Some(_)) if !g.guard && in_range && !chase_sees => Mind::Chase,
            (_, Some(_)) if in_range || g.guard => {
                // engage 0x00427910 on coming into it: in range with flag 4, the close attack when the class has one
                // (vt+0x1e0: the captain), allow_close_attack, flag 1 and rand * 200 >> 15 < 30; else the fire state 0xa
                // a monk's roll first (vt+0x220 0x00437130): the basket rain (0xf1) or the twiglet summon (0xf2 /
                // 0xf3), else the fire state
                // (a hand-over engages afresh, vt+0x230(0): no close attack; the limit is asked again by max_fire::upkeep)
                // in range a trigger_charge grunt's +0x488 0x100 and his charge's point list go (`charged`)
                if in_range && std::mem::take(&mut g.trigger_charge) {
                    g.charged = false;
                }
                let retry = std::mem::take(&mut g.retry_engage);
                let engaged = !retry && (was == Mind::Fire || (was == Mind::Guard && g.guard_firing));
                // (the twiglet summon needs +0x484 bit 14 clear (0x00437130): not still turning to the target, more than
                // 45 degrees off his facing (0x0042df70); else its n < 50 falls to the baskets' n < 200)
                let forward = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
                let turning = target.is_some_and(|p| !within_arc(p, pos, forward, 45.0));
                let act = match g.monk.as_mut() {
                    Some(m) if !engaged => {
                        let forced = m.forced.is_some();
                        match m.roll(&mut rng) {
                            monk::MonkAct::Twiglets if turning && !forced => monk::MonkAct::Baskets,
                            a => a,
                        }
                    }
                    _ => monk::MonkAct::Fire,
                };
                if act == monk::MonkAct::Baskets || act == monk::MonkAct::Twiglets {
                    let (spell, anim) = if act == monk::MonkAct::Baskets { (monk::Spell::Baskets, 0x26) } else { (monk::Spell::Twiglets, 0x25) };
                    g.anim_id = anim;
                    g.anim_left = anim_len(&mut lib, g.anim(anim, 0.0));
                    let forward = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
                    if let Some(m) = g.monk.as_mut() {
                        m.cast = false;
                        m.spell = spell;
                        m.summon_at = monk::summon_point(pos, forward, &mut rng);
                    }
                    Mind::Cast
                } else if !engaged && !retry && g.engage_throws(in_range, || force.take("throw") || rng.rand() * 200.0 < 30.0) {
                    g.start_throw(&mut lib);
                    Mind::Throw
                } else if g.guard {
                    // a guard fires from his guard state (0x79, ai_06.cpp:2664-2673: the fire cycle vt+0x198 with
                    // may_move clear, its clip's end the reload set 0x2a.. in place, the counters rolled at its end)
                    g.guard_firing = true;
                    Mind::Guard
                } else {
                    Mind::Fire
                }
            }
            // a squad member fights in place (0xdd): no pursuit
            (_, Some(_)) if squad_fight => Mind::Fire,
            (_, Some(_)) => Mind::Chase,
            (Mind::Guard, None) => Mind::Guard,
            // the target gone from the states that need one (0x0042bc60)
            (Mind::Fire | Mind::Chase | Mind::Reload | Mind::MaxFireWait | Mind::CloseAttack, None) => lost_target(&mut g, ge, &mut spots.trenches),
            _ => Mind::Roam,
        };
        // a monk's pursuit is a teleport (vt+0x184 0x00436b90: +0xb58 = 0, then the bamf)
        // (and the bamf a knock-over hang ends with: topsy.rs, MonkObject_Bamf via vt+0x240)
        let bamf = g.monk.as_mut().is_some_and(|m| std::mem::take(&mut m.bamf));
        if (g.mind == Mind::Chase || bamf) && g.monk.is_some() {
            if let Some(m) = g.monk.as_mut() {
                m.special = 0;
            }
            let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
            g.mind = hide_to_mind(&mut g, h);
            if g.mind == Mind::Teleport {
                stats.teleports += 1;
                info!("EVENT ai: monk {ge:?} teleports");
            }
        }
        // a jumper captain's pursuit (vt+0x184 0x004295d0): +0x934 = a point within 20 of the target; on the ground
        // (0x00454c20: vt+0xb8's contact, guess) state 0xbc, whose end (its walk of 0.2..0.4 s, +0x490) launches a
        // glide there (0x0042a2c0: within 50 a hop, else the ballistic launch at turbo_glide_xyvel under the glide's
        // gravity, the wings out, state 0xbd); in the air he flies to a jump marker (0x0042b480), none: the hiding-spot
        // search
        if g.mind == Mind::Chase && g.jumper.is_some_and(|j| j.flight.is_none()) {
            if let Some(p) = target {
                let ground = g.jumper.is_some_and(|j| j.stepped) && ground_below(&physics, pos, 3.0, body.id).is_some();
                let to = p + Vec3::new(rng.range(-20.0, 20.0), 0.0, rng.range(-20.0, 20.0));
                let (range, ms) = (g.range, g.move_speed);
                let j = g.jumper.as_mut().expect("a jumper");
                if ground {
                    // the walk (0xbc) first, then the launch
                    let left = j.walk.unwrap_or_else(|| rng.range(0.2, 0.4)) - dt;
                    if left > 0.0 {
                        j.walk = Some(left);
                    } else {
                        j.walk = None;
                        // (the marker he holds stays his: 0x0042a2c0 has no vt+0x188)
                        j.glide(pos, to);
                        // the landing reads only the contacts of the steps after the take-off (Slot44 notes a contact in
                        // the step it happens)
                        physics.contacts.forget(body.id);
                        stats.jumps += 1;
                        g.mind = Mind::Jump;
                        info!("EVENT ai: jumper {ge:?} takes off {:.0} from the target", (to - pos).with_y(0.0).length());
                    }
                } else {
                    // the marker he holds is not a candidate (held: not free); it is left (vt+0x188, his history then
                    // skips it) only once the next one is found (0x0042b480, ai_03.cpp:688-691)
                    match jumper::pick_marker(j, pos, target, range, &jviews) {
                        Some((i, closer)) => {
                            if let Some(old) = j.leave() {
                                joccupy.push((old, None));
                                if let Some(v) = jviews.iter_mut().find(|v| v.e == old) {
                                    v.free = true;
                                }
                            }
                            let (m, at) = (jviews[i].e, jviews[i].pos);
                            jviews[i].free = false;
                            joccupy.push((m, Some(ge)));
                            j.start(pos, m, at, closer, ms);
                            physics.contacts.forget(body.id);
                            g.mind = Mind::Jump;
                            stats.jumps += 1;
                            info!("EVENT ai: jumper {ge:?} jumps to a marker {:.0} away", (at - pos).with_y(0.0).length());
                        }
                        None => {
                            let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                            g.mind = hide_to_mind(&mut g, h);
                        }
                    }
                }
            }
        }
        // this update's physics step gives the jumper his contacts from now on
        if let Some(j) = g.jumper.as_mut().filter(|_| dt > 0.0) {
            j.stepped = true;
        }
        // (the playtest log and the anims see the stance: at rest after a fight the stand set)
        if matches!(g.mind, Mind::Fire | Mind::Chase | Mind::Reload | Mind::Cover | Mind::Gunner | Mind::Throw | Mind::CloseAttack) {
            g.attention = false;
        }
        let mut goal = pos;
        let mut speed = 0.0;
        // a heading a marker asks to be faced in (`marker_face`)
        let mut marker_turn: Option<Vec3> = None;
        // a jumper in the air: his own velocity (jumper.rs), not the walk's
        let mut fly: Option<Vec3> = None;
        // a hit reaction holds the grunt (states 0x12f..0x131)
        let reacting = g.hit.is_some() || g.pose.is_some();
        match if reacting { Mind::Dead } else { g.mind } {
            Mind::InSquad => {
                // 0xdc: to the bone; 0xde (the squad waiting): stand
                if let Some((l, s)) = in_squad {
                    if s.phase != squad::SquadPhase::Waiting {
                        (goal, speed) = squad::member_goal(s, l, pos, g.move_speed);
                    }
                }
            }
            Mind::Roam | Mind::Bully => {
                // idle / roaming (state 1, grunt-ai.md): with no target the grunt stands at attention where it is and
                // fidgets (charanim.rs); it only moves along a patrol spline, which no lev_1_m1 grunt has. (The
                // engine's earlier stand-in wandered round home, marching with footsteps the original never plays.)
                goal = pos;
                speed = 0.0;
            }
            Mind::Chase => {
                // pursue 0x004295d0: a point within +-20 of the target; every 0.2-0.4 s a ray decides to engage. Its
                // entry clears the pause +0x5c8 (ai_02.cpp:2293): the engage after it fires without a leftover wait
                g.pause = 0.0;
                if let Some(p) = target {
                    if g.ray_timer <= 0.0 {
                        pursuit_point(&mut g, p, limping, &mut rng);
                    }
                    goal = g.goal;
                    speed = run_speed;
                    // the Q112 keeps a trail of its target's way (0x00424630)
                    if let Some(q) = g.q112.as_mut() {
                        q.keep(p);
                    }
                }
            }
            Mind::Flee => {
                goal = g.goal;
                speed = run_speed;
            }
            Mind::Retreat => {
                // state 0xae: along the route to the point 2000 off; at its end he stands
                goal = g.goal;
                speed = if flat(g.goal - pos).length() < MARKER_REACHED { 0.0 } else { run_speed };
            }
            Mind::ToMarker => {
                let mk = g.spline.and_then(|r| markers.spline_marker(r.tag, r.index)).map(|m| m.pos);
                goal = g.exit.or(mk).unwrap_or(pos);
                // the speeds on a spline (vt+0x80 0x004297a0 in states 0x96 / 0x97): the leg's marker speed
                // (0x0044b4f0), else patrol_speed with marker_patrol, else the walk (move_speed, limp_speed limping)
                let leg = g.spline.filter(|_| g.exit.is_none()).map_or(0.0, |r| markers.leg_speed(r.tag, r.index));
                speed = if leg != 0.0 {
                    leg
                } else if g.patrol && g.exit.is_none() {
                    g.patrol_speed
                } else {
                    run_speed
                };
                if g.route_wait > 0.0 && g.exit.is_none() {
                    g.route_wait -= dt;
                    speed = 0.0;
                }
                // at the marker with a heading to face: still, turning to it (0x00454c90)
                if let (Some(f), Some(at), None) = (g.marker_face, mk, g.exit) {
                    if flat(at - pos).length() < MARKER_REACHED {
                        speed = 0.0;
                        marker_turn = Some(f);
                    }
                }
                if g.exit.is_none() && mk.is_none() {
                    g.mind = Mind::Roam;
                }
            }
            Mind::Home => {
                // state 0x50: back to +0x4c0 (anim 0xa); there the stand set, vt+0x18c and state 1
                goal = g.home;
                speed = run_speed;
                if flat(g.home - pos).length() < 4.0 || g.timer <= 0.0 {
                    leave_cover(&mut g, ge, &mut spots.trenches, false);
                    g.mind = Mind::Roam;
                }
            }
            Mind::ToSlot => {
                // states 0x1e / 0x1f (ai_06.cpp:1070-1180): the run to the slot's point; every 0.75..1.25 s (+0x490) a
                // look: the target within the engagement range (3D) and in sight -> with rounds (flag 4) the slot let
                // go (vt+0x18c) and the engage (vt+0x230); without, once its 5 s (+0x494) are up within 50 of the
                // point, the slot let go and the reload where he stands (anims 0x2a.., state 0x32). At the point: with
                // rounds the fight there (state 0x34), else the reload facing the slot's way (0x33), the slot kept
                let Some(mut hs) = g.slot else {
                    g.mind = Mind::Roam;
                    continue;
                };
                goal = hs.point;
                speed = run_speed;
                hs.look -= dt;
                hs.give_up -= dt;
                let mut next: Option<Mind> = None;
                if hs.look <= 0.0 {
                    hs.look = rng.range(0.75, 1.25);
                    if let (Some(p), Some(thit)) = (target, target_hit) {
                        if (p - pos).length() < g.range && sight_ray(&physics, own_hit, thit, &[body.id]) {
                            if g.fire_ready {
                                next = Some(Mind::Fire);
                            } else if hs.give_up < 0.0 && (hs.point - pos).length_squared() < 2500.0 {
                                g.pause = g.reload_len;
                                next = Some(Mind::Reload);
                            }
                        }
                    }
                }
                if let Some(m) = next {
                    g.slot = Some(hs);
                    coverslot::release(&mut g.slot, ge, &mut spots.slots);
                    g.mind = m;
                } else if flat(hs.point - pos).length() < 4.0 {
                    hs.arrived = true;
                    g.slot = Some(hs);
                    g.yaw = hs.dir.x.atan2(hs.dir.z);
                    g.mind = if g.fire_ready && target.is_some() {
                        Mind::Fire
                    } else {
                        g.pause = g.reload_len;
                        Mind::Reload
                    };
                } else {
                    g.slot = Some(hs);
                }
            }
            Mind::Wander => {
                // states 0x3d / 0x5b: to the point; there home = here and the stand set (state 1), or with +0x488 0x200
                // the charge (0x0042ce70); a point it cannot reach ends the walk when its time is up (the original's
                // route request fails at once then: guess at the equivalent)
                goal = g.goal;
                speed = run_speed;
                if flat(g.goal - pos).length() < 4.0 || g.timer <= 0.0 {
                    g.home = pos;
                    g.attention = false;
                    if std::mem::take(&mut g.charge) {
                        g.mind = charge(&mut g, player_e, &mut rng);
                    } else {
                        g.mind = Mind::Roam;
                    }
                }
            }
            Mind::Taunt => {
                // state 0x6e: the taunt anim facing the target; at its end a guard back to 0x7a (here its reload, as the
                // engine's guard cycle has it after the last clip: guess), else the clip's end (0x0042b8e0)
                g.anim_left -= dt;
                if g.anim_left <= 0.0 {
                    g.mind = if g.guard {
                        g.pause = g.reload_len;
                        Mind::Reload
                    } else if target.is_some() {
                        let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                        hide_to_mind(&mut g, h)
                    } else {
                        lost_target(&mut g, ge, &mut spots.trenches)
                    };
                }
            }
            Mind::CloseAttack => {
                // state 0xb (ai_06.cpp:889-925): facing the target (below), the swing looped; each "hit" note hurts it
                // within m_h2hMax across (0x0043eae0); a loop's end with the target m_h2hMax away or more ends it
                let (_, reach) = melee::h2h(g.kind).unwrap_or((20.0, 10.0));
                let len = anim_len(&mut lib, g.anim(g.anim_id, 0.0)).max(0.1);
                let t0 = g.anim_left;
                g.anim_left += dt;
                if let (Some(te), Some(p)) = (g.target, target) {
                    for n in melee::hit_times(&mut lib, g.anim(g.anim_id, 0.0), g.anim_id) {
                        if t0 < n && g.anim_left >= n && flat(p - pos).length() < reach {
                            let (damage, impact) = melee::hit_of(g.kind);
                            let dir = melee::knock_dir(pos, p, rng.rand());
                            close.0.write(melee::CloseHit { by: ge, target: te, damage, impact, dir });
                        }
                    }
                    if g.anim_left >= len {
                        g.anim_left -= len;
                        if p.distance(pos) >= reach {
                            g.mind = Mind::Fire;
                        }
                    }
                }
            }
            Mind::MaxFireWait => {
                // state 0x11 (ai_06.cpp:849-867): facing the target (below), the next idle base as each one ends
                g.anim_left -= dt;
                if g.anim_left <= 0.0 {
                    g.anim_id = max_fire::next_wait_anim(g.anim_id, &mut rng);
                    g.anim_left = anim_len(&mut lib, g.anim(g.anim_id, 0.0));
                }
            }
            Mind::Throw => {
                // state 0x10: the throw anim facing the target; its "throw" note (captain_grenade_throw 1.333 s) sends
                // the grenade (0x0043d180 "throw" -> 0x00428340: the captain_grenade from the hand at the target's
                // ref_hit_00; flag 1 cleared); the end: a guard 0x7a, flag 4 clear the clip's end (0x0042b8e0), else
                // 50 % that and 50 % engage again (ai_06.cpp:813-836); the note does nothing while a weapon effect holds
                // the grenades (`throw_held`; the extra shooter's +0x488 0x100000 hold is not modelled: max_fire.rs
                // keeps no such flag)
                let total = anim_len(&mut lib, g.anim(0x27, 0.0));
                let before = total - g.anim_left;
                g.anim_left -= dt;
                if !g.thrown && before + dt >= 1.333 {
                    g.thrown = true;
                    stats.throws += 1;
                    g.throw_armed = false;
                    if let (Some(thit), false) = (target_hit, throw_held) {
                        let hand = ga
                            .and_then(|a| a.skeleton.index.get("ref_lhand").and_then(|&i| targets.get(a.joints[i]).ok()))
                            .map(|j| j.translation())
                            .unwrap_or(pos + Vec3::Y * 9.0);
                        grenade::throw(&mut commands, grenade_assets.as_deref(), &data, &mut rng, hand, thit, ge);
                    }
                }
                if g.anim_left <= 0.0 {
                    g.mind = if g.guard {
                        Mind::Guard
                    } else if g.at_barricade {
                        // thrown from a trench's slot: 0x0042b8e0's slot rule, or the engage (in range fire, else duck)
                        let research = !g.fire_ready || rng.rand() < 0.5;
                        g.mind = Mind::Fire;
                        match target {
                            Some(p) if research => barricade_clip_end(&mut g, ge, p, pos, tgt, &mut spots, &physics, body.id, &mut rng),
                            Some(_) if in_range => {}
                            _ => duck(&mut g),
                        }
                        g.mind
                    } else if !g.fire_ready || rng.rand() < 0.5 {
                        if target.is_some() {
                            let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                            hide_to_mind(&mut g, h)
                        } else {
                            lost_target(&mut g, ge, &mut spots.trenches)
                        }
                    } else {
                        Mind::Fire
                    };
                }
            }
            Mind::Fire | Mind::Guard => {
                // every way into the fire state passes the engage (0x00427910 / 0x00427c80: a reload's end, a hit's
                // end, a guard's engage), which asks the mission's shooter limit first: wait there (max_fire.rs)
                if target.is_some() && (g.mind == Mind::Fire || g.guard_firing) && !limit.may_fire(ge, &mut g.shooter, &mut rng) {
                    g.mind = Mind::MaxFireWait;
                    g.guard_firing = false;
                    // (0x004271c0 with no idle base playing draws the first; the wait state below plays it)
                    g.anim_id = max_fire::next_wait_anim(0, &mut rng);
                    g.anim_left = anim_len(&mut lib, g.anim(g.anim_id, 0.0));
                }
                if let Some(p) = target.filter(|_| g.mind == Mind::Fire || g.guard_firing) {
                    // the target's ref_hit_00 (0x00428280): the aim point of 0x004284e0 and the sight ray's end
                    let hit = g
                        .target
                        .and_then(|e| anims.get(e).ok())
                        .and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok()))
                        .map(|j| j.translation())
                        .unwrap_or(p + Vec3::Y * 7.0);
                    let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(pos + Vec3::Y * 6.0);
                    // the turn toward the target first (no fire while it plays; the yaw keeps turning below)
                    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
                    let to = flat(p - pos).normalize_or_zero();
                    if let Some((_, left)) = g.turn.as_mut() {
                        *left -= dt;
                        if *left <= 0.0 {
                            g.turn = None;
                        }
                    } else if to.dot(fwd) < 0.7 && g.kind.turns() && g.shot_age >= g.fire_len && !g.reloading {
                        // (0x0042e050 starts it only from the stand / aim anims, +0x498 2, 0xf, 0x12..0x20: not while a
                        // shot's fire anim 0x23 or the reload set 0x28.. plays)
                        let right = fwd.cross(to).y < 0.0;
                        // after a turn a guard's first fire cycle is dry (measured: the original's courtyard guards,
                        // most of which turn, fire anim then reload and first shots ~80 frames later, two that faced
                        // the player shooting at once; the first fight's archer, facing, shoots at once). Guess at the
                        // path: engage -> state 0xa -> back to the guard cycle with empty counters (note #7)
                        g.dry_first = g.guard;
                        // 0x0042e050 (vt+0x218: the grunts and archers; the captain turns without one): dot <
                        // K(0x005bcd4c) -0.7 anim 5, else 6 / 7 by the cross product's sign (the archers' table gives
                        // the grunts' turn anims for these ids)
                        let id = if to.dot(fwd) < -0.7 {
                            5
                        } else if right {
                            7
                        } else {
                            6
                        };
                        let n = g.anim(id, 0.0);
                        g.turn = Some((n, anim_len(&mut lib, n)));
                    }
                    let before = stats.shots;
                    let mut end = FireEnd::None;
                    if g.turn.is_none() {
                        // the weapon in its right hand (ref_rhand, grunt init 0x00434fb0): where the shot leaves
                        let hand = ga.and_then(|a| a.skeleton.index.get("ref_rhand").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation());
                        // the sight ray reaching the target's own collision is a sight of it (0x00426fd0: a placed
                        // object as target, the houses a village attacker shoots)
                        let target_body = g.target.and_then(|t| placed.get(t).ok()).and_then(|p| p.collider);
                        let skip: Vec<BodyId> = std::iter::once(body.id).chain(target_body).collect();
                        end = fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, hit, own_hit, hand, &skip, dt, &mut stats, &mut sounds, &mut fx, (&mut barks, time.elapsed_secs(), &voices, &data));
                    }
                    if stats.shots > before && std::env::var("AIDBG").is_ok() {
                        info!("AIDBG gruntshot t {:.3}", rtime.elapsed_secs());
                    }
                    // the close attack at a burst's or the clip's end (0x00428980 -> 0x00428160: the target within
                    // m_h2hMin), before the next burst, the reload or the search (ai/melee.rs)
                    if matches!(end, FireEnd::Burst | FireEnd::Clip { close_attack: true }) && !g.at_barricade {
                        if let Some(id) = melee::close_attack(g.kind, pos, p, || rng.rand()) {
                            g.mind = Mind::CloseAttack;
                            g.anim_id = id;
                            g.anim_left = 0.0;
                            g.pause = 0.0;
                            end = FireEnd::None;
                            close.1.swings += 1;
                            info!("EVENT ai: grunt {ge:?} swings at his target");
                        }
                    }
                    // a counted shooter stops at its clip's end (0x00428980 -> 0x0042bb10): its place is handed over
                    // (a lost line of sight keeps the engine's fire state, which keeps the place: max_fire.rs)
                    if matches!(end, FireEnd::Clip { .. } | FireEnd::Taunt) && g.shooter != max_fire::Shooter::None {
                        let mut s = g.shooter;
                        limit.release(ge, &mut s);
                        g.shooter = s;
                    }
                    match end {
                        // in the slot a lost line of sight ducks (0x00428980: anim 0x66, state 0xa2)
                        FireEnd::Sight if g.at_barricade => duck(&mut g),
                        // elsewhere (ai_02.cpp:1886-1912) the pursuit (vt+0x184 0x004295d0) until its ray sees the
                        // target again; a guard keeps his guard state (0x7a and back), a squad member his place
                        FireEnd::Sight if !g.guard && !squad_fight => {
                            pursue(&mut g, p, limping, &mut rng);
                            stats.sight_pursuits += 1;
                        }
                        // the clip's end (0x0042b8e0): in the slot, the target within 80 degrees of its direction ->
                        // duck, else leave it (vt+0x188) and search again (vt+0x240(1, 1, 0)); elsewhere a non-guard
                        // searches (vt+0x240(1, 0, 0)): 90 % the reload in place first
                        FireEnd::Clip { .. } if g.at_barricade => barricade_clip_end(&mut g, ge, p, pos, tgt, &mut spots, &physics, body.id, &mut rng),
                        FireEnd::Clip { .. } if !g.guard => {
                            if let Some(m) = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng).mind() {
                                g.mind = m;
                            }
                        }
                        // the taunt after the last clip (state 0x6e): its anim's length
                        FireEnd::Taunt => {
                            stats.taunts += 1;
                            let n = g.anim(g.anim_id, 0.0);
                            g.anim_left = anim_len(&mut lib, n);
                        }
                        _ => {}
                    }
                    if g.mind == Mind::Cover {
                        info!("EVENT ai: grunt {ge:?} cover {:?}", g.cover.map(|c| c.phase));
                    }
                }
            }
            Mind::Cover => {
                // the barricade states 0xa0..0xa5 (ai_06.cpp:2055-2218)
                let mut c = g.cover.expect("cover state without a cover");
                c.timer -= dt;
                match c.phase {
                    CoverPhase::ToExit => {
                        goal = c.exit;
                        speed = run_speed;
                        if flat(c.exit - pos).length() < 3.0 {
                            c.phase = CoverPhase::ToSpot;
                        }
                    }
                    CoverPhase::ToSpot => {
                        goal = c.spot;
                        speed = run_speed;
                        if flat(c.spot - pos).length() < 1.5 {
                            // in the slot: +0x484 0x20000, anim 0x66, state 0xa2 (with no target one is searched for,
                            // AI_FindTarget: the search system's next tick, all round now)
                            g.at_barricade = true;
                            c.phase = CoverPhase::Duck;
                            c.timer = DUCK_TIME;
                            info!("EVENT ai: grunt {ge:?} in trench at {:.0} {:.0}", c.spot.x, -c.spot.z);
                        }
                    }
                    CoverPhase::Duck => {
                        if c.timer <= 0.0 {
                            // the fire counters rolled afresh (0x00427600), 1..6 s of breathing (0xa3); the rounds +0x5bc
                            // now (the spray counts them down; the fire cycle rolls its own from left_clips 0)
                            // and the bursts +0x5b8, clamped to at most the rounds
                            g.left_clips = 0;
                            g.left_rounds = rng.range(g.rounds.0 as f32, g.rounds.1 as f32 + 0.999) as i64;
                            g.left_burst = roll_count(&mut rng, g.bursts).min(g.left_rounds);
                            if !g.fire_ready {
                                g.throw_armed = true;
                            }
                            g.fire_ready = true;
                            c.phase = CoverPhase::Breathe;
                            c.timer = rng.range(1.0, 6.0);
                        }
                    }
                    CoverPhase::Breathe => {
                        // the target outside the slot's 80 degrees (or none): the timer is cut to 0 (not with hold)
                        let out = !g.hold && target.is_none_or(|p| !within_arc(p, c.spot, c.dir, 80.0));
                        if out {
                            c.timer = 0.0;
                        }
                        if c.timer <= 0.0 {
                            // rand(1000) >= 100 and (a target, or out of the arc): stand up (0xa4); else rand(100) >= 50
                            // with a target and vt+0x1fc (the captain) the fire spray (0x68, 0xa6), else peek (0x67, 0xa5)
                            if rng.rand() >= 0.1 && (target.is_some() || out) {
                                c.phase = CoverPhase::Stand;
                                c.timer = STAND_TIME;
                            } else if rng.rand() >= 0.5 && target.is_some() && g.kind.spray() {
                                c.phase = CoverPhase::Spray;
                                c.timer = anim_len(&mut lib, g.anim(0x68, 0.0));
                            } else {
                                c.phase = CoverPhase::Peek;
                                c.timer = PEEK_TIME;
                            }
                        }
                    }
                    CoverPhase::Peek => {
                        // its end: rounds left (+0x5bc > 0 after the fresh roll) -> breathe again
                        if c.timer <= 0.0 {
                            c.phase = CoverPhase::Breathe;
                            c.timer = rng.range(1.0, 6.0);
                        }
                    }
                    CoverPhase::Spray => {
                        // each "fire" note of the spray anim (GenericAIObject_HandleEvent 0x0043d180, state 0x68,
                        // ai_05.cpp:1237-1256): nothing unless both the rounds +0x5bc and the bursts +0x5b8 are left;
                        // else a shot (vt+0x194; here at the target, when he still has one) and both counted down,
                        // shot or not; its end: flag 4 with rounds left -> breathe (the counters rolled), else duck
                        let name = g.anim(0x68, 0.0);
                        let notes: Vec<f32> = lib.anim(name).map(|a| a.notes.iter().filter(|n| n.1.trim().eq_ignore_ascii_case("fire")).map(|n| n.0).collect()).unwrap_or_default();
                        let len = anim_len(&mut lib, name);
                        let (t0, t1) = (len - (c.timer + dt), len - c.timer);
                        for n in notes {
                            if n > t0 && n <= t1 && g.left_rounds > 0 && g.left_burst > 0 {
                                if let Some((_, thit)) = tgt {
                                    shoot(&mut commands, &mut fx, &g, &mut rng, pos, thit, &data);
                                    stats.shots += 1;
                                }
                                g.left_rounds -= 1;
                                g.left_burst -= 1;
                            }
                        }
                        if c.timer <= 0.0 {
                            if g.fire_ready && g.left_rounds > 0 {
                                c.phase = CoverPhase::Breathe;
                                c.timer = rng.range(1.0, 6.0);
                            } else {
                                g.fire_ready = false;
                                c.phase = CoverPhase::Duck;
                                c.timer = DUCK_TIME;
                            }
                        }
                    }
                    CoverPhase::Stand => {
                        if c.timer <= 0.0 {
                            // engage (0x00427910 with allow_close_attack, state 0xa4's end): in range the captain's
                            // grenade at the engage's roll, else the fire state 0xa, keeping the trench either way (in
                            // the slot, +0x484 0x20000, vt+0x188 is skipped); else (or no target, L_4459e4) duck
                            if target.is_some() && in_range {
                                if g.engage_throws(in_range, || force.take("throw") || rng.rand() * 200.0 < 30.0) {
                                    g.start_throw(&mut lib);
                                } else {
                                    g.mind = Mind::Fire;
                                    g.pause = 0.0;
                                }
                            } else {
                                c.phase = CoverPhase::Duck;
                                c.timer = DUCK_TIME;
                            }
                        }
                    }
                }
                if g.cover.is_some() {
                    g.cover = Some(c);
                }
            }
            Mind::Gunner => {
                // the mounted-gun states 0x7b / 0x7d / 0x7c (ai_06.cpp:583-797)
                let mut gn = g.gun.expect("gunner state without a gun");
                let vi = spots.guns.iter().position(|v| v.e == gn.turret);
                match (gn.phase, vi) {
                    (_, None) => {
                        g.gun = None;
                        g.mind = if target.is_some() { Mind::Chase } else { Mind::Roam };
                    }
                    (GunPhase::ToGun, Some(i)) => {
                        if mounted.0 == Some(gn.turret) {
                            // the player got on first (vt+0x14c, flag 8): 0x004266b0, vt+0x18c, 0x0042b8e0 (guess: pursue
                            // or idle; the search's re-run is not done here)
                            leave_gun(&mut g, ge, &mut body, &mut spots.guns);
                            g.mind = if target.is_some() { Mind::Chase } else { Mind::Roam };
                        } else if flat(gn.exit - pos).length() < 4.0 {
                            // attached at ref_grunt_08 (the gun's origin): the body held at the seat the anims show (their
                            // centre 0.3, 7.5 behind the origin), +0x484 0x8000, the gun's flag 4 (vt+0x160), the
                            // counters from the stig tables (0x00427600 with flag 4 cleared), +0x494 = stig_fire_delay,
                            // +0x490 = 0, state 0x7d
                            spots.guns[i].ai = Some(ge);
                            spots.guns[i].reserved = None;
                            gn.lift = body.offset;
                            body.offset = gn.lift + Quat::from_rotation_y(gn.yaw) * crate::level::game_to_engine() * Vec3::new(0.3, 7.5, 0.0);
                            g.fixed = Some(gn.origin);
                            tr.translation = gn.origin;
                            g.yaw = gn.yaw;
                            g.fire_ready = false;
                            gun_roll(&mut g, &mut gn, &mut rng);
                            gn.timer = rng.range(g.stig.delay.0, g.stig.delay.1);
                            g.search_timer = 0.0;
                            gn.phase = GunPhase::On;
                            g.gun = Some(gn);
                            info!("EVENT ai: grunt {ge:?} on the mounted gun");
                        } else {
                            goal = gn.exit;
                            speed = run_speed;
                        }
                    }
                    (GunPhase::On, Some(i)) => {
                        g.yaw = gn.yaw;
                        // with flag 1 (the counters ran out together) the aim and the shots go at the target led for a
                        // bullet at 360 from the gun's muzzle (its vt+0x148, 0x00497f60), the lead starting from the
                        // target's ref_hit_00 (0x004580a0 with its bone flag), the height mostly the target's position's
                        // (ai/aim.rs), without the aim error; the aim measured from the muzzle too
                        let muzzle = guns.get(gn.turret).ok().map(|(_, tu, tgt, ta, _)| tu.bone_world(tgt, ta, tu.emitter()).w_axis.truncate());
                        let aim_from = if gn.snap { muzzle.unwrap_or(pos) } else { pos };
                        let led = if gn.snap {
                            target.zip(g.target).map(|(p, te)| {
                                let hit = target_hit.unwrap_or(p);
                                let l = aim::lead_point(hit, motion.velocity(te), aim_from, aim::GUN_LEAD_SPEED);
                                l.with_y(aim::aim_height(p.y, hit.y))
                            })
                        } else {
                            None
                        };
                        let aim_at = if gn.snap { led } else { target };
                        // the aim (vt+0x190 0x00428fc0 with 65 degrees: state 0x7d calls it twice a frame)
                        gun_aim(&mut gn, aim_from, aim_at, 65.0, dt);
                        let mut cl = gun_aim(&mut gn, aim_from, aim_at, 65.0, dt);
                        if gn.snap && (gn.clips != 0 || gn.rounds != 0 || gn.burst != 0) {
                            cl = true;
                        }
                        let never_leave = guns.get(gn.turret).map(|(_, t, ..)| t.never_leave).unwrap_or(false);
                        let mut off = false;
                        if !(cl || g.hold) {
                            if !never_leave {
                                off = true;
                            }
                        } else if gn.timer <= 0.0 && target.is_some() && cl {
                            g.hold = false;
                            gun_cycle(&mut g, &mut gn, &mut rng, dt);
                            // the clips spent: the delay to the next sequence from the gun's table (ai_06.cpp:728-733;
                            // the first, on getting on, is always stig_fire_delay)
                            if gn.clips <= 0 {
                                gn.timer = rng.range(gn.table.delay.0, gn.table.delay.1);
                            }
                        } else {
                            gn.firing = false;
                            gn.timer -= dt;
                            if gn.timer <= 0.0 {
                                if gn.clips == 0 && gn.rounds == 0 && gn.burst == 0 {
                                    gn.snap = true;
                                }
                                gun_roll(&mut g, &mut gn, &mut rng);
                            }
                        }
                        if off {
                            // off (0x004266b0) at ref_grunt_exit, walking there (0x7c)
                            g.gun = Some(gn);
                            if let Some(t) = leave_gun(&mut g, ge, &mut body, &mut spots.guns) {
                                if let Ok((_, mut tu, ..)) = guns.get_mut(t) {
                                    tu.ai_aim(0.5, 0.5);
                                }
                            }
                            gn.phase = GunPhase::Off;
                            g.gun = Some(gn);
                            info!("EVENT ai: grunt {ge:?} off the mounted gun (target out of its arc)");
                        } else {
                            spots.guns[i].ai = Some(ge);
                            if let Ok((_, mut tu, gt, ta, _)) = guns.get_mut(gn.turret) {
                                tu.ai_aim(gn.aim.h, gn.aim.v);
                                // the fire set's "fire" notes (0.067, 0.233 of its 0.3 s loop): with rounds and burst left a
                                // shot (0x0043d180 -> vt+0x194 0x004284e0: the aim error -> the gun's vt+0x13c: ai_projectile
                                // from the emitter, the muzzle flash), both counters down
                                if gn.firing {
                                    let t0 = gn.anim_t;
                                    gn.anim_t += dt;
                                    for note in [0.0667f32, 0.2333] {
                                        let crossed = ((t0 - note) / 0.3).floor() != ((gn.anim_t - note) / 0.3).floor();
                                        if !(crossed && gn.rounds > 0 && gn.burst > 0) {
                                            continue;
                                        }
                                        // a forge cannon's note fires only at a target in the gunner's 180-degree view
                                        // and at least 100 away in the plane (0x0043ec60, the gun's kind 2: vt+0xf0 with
                                        // 180, K 0x005bcc10 = 100^2)
                                        if tu.two_pivot() {
                                            let to = target.map_or(Vec3::ZERO, |p| (p - pos).with_y(0.0));
                                            if to.length() < 100.0 || to.dot(gn.fwd.with_y(0.0)) < 0.0 {
                                                gn.held += 1;
                                                continue;
                                            }
                                        }
                                        gn.rounds -= 1;
                                        gn.burst -= 1;
                                        let (Some(p), Some(hit)) = (target, target_hit) else { continue };
                                        let to = (p - pos).with_y(0.0);
                                        let d = to.length();
                                        let s = g.aim_error_scale(d);
                                        let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
                                        let aim = match led {
                                            // flag 1: straight at the led point (0x004284e0's turret branch)
                                            Some(l) => l,
                                            None => hit + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s,
                                        };
                                        if led.is_some() {
                                            stats.led_shots += 1;
                                        }
                                        let from = tu.bone_world(gt, ta, tu.emitter()).w_axis.truncate();
                                        // (a forge cannon's shell flies as vehicles/rockets.rs lobs it; a bullet as before)
                                        let to = from + (aim - from).normalize_or(gn.fwd) * from.distance(aim).max(1.0);
                                        crate::vehicles::rockets::shoot(&mut commands, &mut fx, &data, &tu.ai_projectile, from, to, Some(ge), crate::fx::Src::Badguy);
                                        tu.flash(&mut commands, &mut fx, ta, &targets);
                                        stats.shots += 1;
                                        gn.shots += 1;
                                    }
                                }
                            }
                            g.gun = Some(gn);
                        }
                    }
                    (GunPhase::Off, Some(_)) => {
                        goal = gn.exit;
                        speed = run_speed;
                        if flat(gn.exit - pos).length() < 4.0 {
                            // 0x7c's end: flag 4 cleared, the hiding-spot search (vt+0x240(1, 0, 0))
                            g.gun = None;
                            g.fire_ready = false;
                            g.mind = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng).mind().unwrap_or(Mind::Reload);
                            if g.mind == Mind::Reload {
                                g.pause = 0.467 + 1.133;
                            }
                        } else {
                            g.gun = Some(gn);
                        }
                    }
                }
            }
            Mind::Reload => g.pause -= dt,
            Mind::Teleport => {
                // state 0xf0: the flight (no gravity: physics flag 0x100, guess) for at most 5 s; within 8 he lands on the
                // ground there and engages (vt+0x230) or rests
                let mut m = g.monk.expect("a monk");
                if m.flight_t == 0.0 {
                    // the bamf's start (MonkObject_Bamf 0x00436bb0, ai_05.cpp:420-425): fol_monk_bamf_1 or _2 on him,
                    // (rand() * 100 >> 15) < 50
                    let name = if rng.rand() * 100.0 < 50.0 { "fol_monk_bamf_1" } else { "fol_monk_bamf_2" };
                    sounds.write(crate::audio::PlaySound::on(name, ge));
                }
                m.flight_t += dt;
                let near = (g.goal - pos).length() < 8.0;
                if near || m.flight_t > 5.0 {
                    if let Some((_, hit, _)) = physics.raycast(pos + Vec3::Y * 20.0, -Vec3::Y, 200.0) {
                        tr.translation = hit;
                        if let Ok(mut b) = physics.world.body_mut(body.id) {
                            let _ = b.set_position(crate::physics::jr((hit + body.offset) * k), Activation::Activate);
                        }
                    }
                    g.home = pos;
                    g.mind = if target.is_some() { Mind::Fire } else { Mind::Roam };
                    stats.landings += 1;
                    info!("EVENT ai: monk {ge:?} arrives");
                } else {
                    fly = Some(monk::bamf_velocity(pos, g.goal, m.flight_t, 2.0 * g.move_speed));
                }
                g.monk = Some(m);
            }
            Mind::Cast => {
                // state 0xf1: cast_spell_1; its event (here halfway) drops the baskets round the target, the end runs the
                // bamf (which, the counter re-rolled, re-engages). States 0xf2 / 0xf3: cast_spell_2 turned to the summon
                // point; at its end five idle twiglets stand round him (ai_06.cpp:2928-2972), then the same end (vt+0x240)
                let total = anim_len(&mut lib, g.anim(g.anim_id, 0.0));
                g.anim_left -= dt;
                if let Some(mut m) = g.monk.filter(|m| m.spell == monk::Spell::Twiglets) {
                    if !m.cast && g.anim_left <= 0.0 {
                        m.cast = true;
                        for at in monk::twiglet_spots(pos, &physics, &mut rng) {
                            let game = crate::level::game_to_engine().inverse() * at;
                            commands.write_message(crate::twiglet::SpawnTwiglet { at: Mat4::from_translation(game), mover: None });
                        }
                        stats.throws += 1;
                        info!("EVENT ai: monk {ge:?} summons twiglets");
                    }
                    g.monk = Some(m);
                } else if let Some(mut m) = g.monk {
                    if !m.cast && g.anim_left <= total * 0.5 {
                        m.cast = true;
                        if let Some(p) = target {
                            monk::baskets(&mut commands, &mut fx, &data, p, ge, &mut rng);
                            stats.throws += 1;
                            info!("EVENT ai: monk {ge:?} casts the baskets");
                        }
                    }
                    g.monk = Some(m);
                }
                if g.anim_left <= 0.0 {
                    let h = find_hiding_spot(&mut g, ge, (true, false, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                    g.mind = hide_to_mind(&mut g, h);
                }
            }
            Mind::Jump => {
                // the flight (0xc4 / 0xc5 / 0xbd): a contact with vz <= 0 lands (0x0042e1c0)
                // any contact of the last step: the contact note (0x004320b0) the landing reads
                let grounded = body::touched(&physics, body.id);
                let mut j = g.jumper.expect("a jumper in flight");
                match j.step(pos, dt, grounded) {
                    jumper::FlightStep::WingsOut => info!("EVENT ai: jumper {ge:?} glides"),
                    jumper::FlightStep::Landed => {
                        // (the marker stays occupied by him: freed when he leaves it or dies, jumper.rs)
                        stats.landings += 1;
                        info!("EVENT ai: jumper {ge:?} lands");
                        // anim 0x6c (state 0xbf), then +0x4ac = 0xc3: with flag 4 and a target engage, else reload
                        let n = g.anim(0x6c, 0.0);
                        g.pose = Some((n, anim_len(&mut lib, n)));
                        g.mind = if target.is_some() && g.fire_ready {
                            Mind::Fire
                        } else {
                            g.pause = g.reload_len;
                            Mind::Reload
                        };
                    }
                    jumper::FlightStep::Flying => {}
                }
                // gliding or hopping (0xbd) he fires with flag 4 (vt+0x198); a lost line of sight does not end it
                // (ai_02.cpp:1894)
                if j.flight.is_some_and(jumper::Flight::fires) {
                    if let (Some(p), Some(thit), true) = (target, target_hit, g.fire_ready) {
                        g.jumper = Some(j);
                        fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, thit, own_hit, None, &[body.id], dt, &mut stats, &mut sounds, &mut fx, (&mut barks, time.elapsed_secs(), &voices, &data));
                        g.mind = Mind::Jump;
                        j = g.jumper.expect("a jumper");
                    }
                }
                if j.flight.is_some() {
                    fly = Some(j.vel);
                }
                g.jumper = Some(j);
            }
            _ => {}
        }
        // a window / tower grunt stays at its place
        if let Some(f) = g.fixed {
            goal = pos;
            speed = 0.0;
            let k = physics.meters_per_unit;
            if let Ok(mut b) = physics.world.body_mut(body.id) {
                if (pos - f).length() > 0.5 {
                    let _ = b.set_position(crate::physics::jr((f + body.offset) * k), Activation::Activate);
                }
                let _ = b.set_linear_velocity(jv(Vec3::ZERO));
            }
        }
        let mut goal = goal;
        g.replan -= dt;
        // the Q112 on its way to a trail point: no new plan until that route is walked (q112.rs)
        let crumb_walk = g.mind == Mind::Chase && g.q112.as_ref().is_some_and(|q| q.on_crumb) && !g.route.is_empty();
        if speed > 0.0 && (crumb_walk || flat(goal - pos).length() > 15.0 && !line_of_sight(&physics, pos, goal)) {
            if !crumb_walk && (g.replan <= 0.0 || g.route.is_empty()) {
                g.replan = 1.0;
                let mut route = crate::nav::path(&physics, pos, goal, &[]);
                // the Q112 with no way to its target: a route to the oldest point of its trail (ai_06.cpp:1501-1520)
                let chasing = g.mind == Mind::Chase;
                if let Some(q) = g.q112.as_mut().filter(|_| chasing) {
                    q.on_crumb = false;
                    if !q112::route_reaches(route.as_deref(), goal) {
                        if let Some(r) = q.next_point().and_then(|c| crate::nav::path(&physics, pos, c, &[])) {
                            route = Some(r);
                            q.on_crumb = true;
                        }
                    }
                }
                g.route = route.unwrap_or_default();
            }
            while g.route.first().is_some_and(|w| flat(*w - pos).length() < 4.0) {
                g.route.remove(0);
            }
            if let Some(w) = g.route.first() {
                goal = *w;
            }
        } else {
            g.route.clear();
        }
        // a guard's clip ends in the reload set where he stands (state 0x79 keeps running: the anim 0x2a with flag 4
        // clear, then 0x00427600); the fire cycle's own Reload is his guard state with the reload under way
        if g.guard && g.mind == Mind::Reload {
            g.mind = Mind::Guard;
            g.guard_firing = true;
            g.reloading = true;
        }
        if g.reloading && g.pause <= 2.0 / 60.0 {
            g.reloading = false;
            if !g.fire_ready {
                g.throw_armed = true;
            }
            g.fire_ready = true;
        }
        if g.target.is_none() {
            g.guard_firing = false;
        }
        // fighting in a squad: he holds his place in the formation
        if squad_fight {
            speed = 0.0;
        }
        g.walking = speed > 1.0 && flat(goal - pos).length() > 1.0;
        let dir = flat(goal - pos).normalize_or_zero();
        // the turns to the target of GenericAI_TurnToTarget 0x0042df70 (the fire cycle's, a guard's, the slot's stand,
        // and the reload in place: state 0x32 calls it every frame with 45 degrees before its PlayAnim4 of the reload
        // set at the aim weights, ai_06.cpp:1224-1227)
        let to_target = matches!((g.mind, target, g.cover.map(|c| c.phase)), (Mind::Fire | Mind::Reload, Some(_), _) | (Mind::Cover, Some(_), Some(CoverPhase::Stand)))
            || (g.mind == Mind::Guard && target.is_some() && g.guard_firing);
        let face = match (g.mind, target, g.cover.map(|c| c.phase)) {
            (Mind::ToMarker, _, _) if marker_turn.is_some() => marker_turn.unwrap_or(dir),
            // the summon turns him to its point (state 0xf3: 0x00454e60 toward +0x934)
            (Mind::Cast, _, _) if g.monk.is_some_and(|m| m.spell == monk::Spell::Twiglets) => {
                g.monk.map(|m| flat(m.summon_at - pos).normalize_or_zero()).unwrap_or(dir)
            }
            (Mind::Fire | Mind::Reload | Mind::Chase | Mind::Taunt | Mind::Throw | Mind::Cast | Mind::MaxFireWait | Mind::CloseAttack, Some(p), _) => flat(p - pos).normalize_or_zero(),
            // a guard firing turns to his target (0x0042df70, 45 degrees)
            (Mind::Guard, Some(p), _) if g.guard_firing => flat(p - pos).normalize_or_zero(),
            // in the slot: its direction +0x908 (0x00454e60), standing up: the target (0x0042df70)
            (Mind::Cover, Some(p), Some(CoverPhase::Stand)) => flat(p - pos).normalize_or_zero(),
            (Mind::Cover, _, Some(CoverPhase::Duck | CoverPhase::Breathe | CoverPhase::Peek | CoverPhase::Stand | CoverPhase::Spray)) => {
                g.cover.map(|c| flat(c.dir).normalize_or_zero()).unwrap_or(dir)
            }
            _ => dir,
        };
        // 0x0042df70: the aim weights step toward the target (vt+0x190, 45 degrees); off target he turns (bit 14) at
        // 0x00454e60's rate until he faces it, on target his body stays and the four-pose set aims (charanim.rs);
        // elsewhere the fire cycle's weights are back at the centre (0x00428980)
        if to_target {
            let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
            if !g.aim.track(fwd, pos, target, TURN_TO_TARGET_WINDOW, false, dt) {
                g.turning = true;
            }
        } else {
            g.aim = aim::AimBlend::default();
            g.turning = false;
        }
        if face != Vec3::ZERO {
            let want = face.x.atan2(face.z);
            let d = (want - g.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            if !to_target {
                g.yaw += d * (dt * 8.0).min(1.0);
            } else if g.turning {
                let (step, faced) = body::turn_toward(d, TURN_TO_TARGET_RATE, dt);
                g.yaw += step;
                g.turning = !faced;
            }
        }
        if let Some(v) = fly {
            // in the air: facing the target while gliding with one, else the velocity (0xc5 / rising 0xc4)
            let gliding = g.jumper.is_some_and(|j| j.flight.is_some_and(jumper::Flight::fires));
            let f = match target {
                Some(p) if gliding => flat(p - pos),
                _ => flat(v),
            }
            .normalize_or_zero();
            if f != Vec3::ZERO {
                g.yaw = f.x.atan2(f.z);
            }
        }
        tr.rotation = Quat::from_rotation_y(g.yaw) * g.base;
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            if let Some(v) = fly {
                let _ = b.set_linear_velocity(jv(v * k));
                b.activate();
            } else {
                let v = bv(b.linear_velocity());
                let want = dir * speed * k;
                let _ = b.set_linear_velocity(jv(Vec3::new(want.x, v.y, want.z)));
                if speed > 0.0 {
                    b.activate();
                }
            }
        }
    }
    // the group alerts of the targets taken above (SetTarget 0x0042cc70 with a1)
    if !alerts.is_empty() {
        for (ge, mut g, ..) in &mut grunts {
            if let Some(&(_, tgt, _)) = alerts.iter().find(|&&a| alerted(&g, ge, a, traitors.contains(ge))) {
                g.pending = Some((tgt, rng.range(0.25, 1.0)));
            }
        }
    }
    for (m, who) in joccupy {
        if let Ok((_, mut jm, _)) = jmarks.get_mut(m) {
            jm.occupant = who;
        }
    }
    for v in &spots.trenches {
        if let Ok((_, mut b)) = trenches.get_mut(v.e) {
            if b.occupant != v.occupant {
                b.occupant = v.occupant;
            }
        }
    }
    for v in &spots.guns {
        if let Ok((_, mut t, ..)) = guns.get_mut(v.e) {
            t.reserved = v.reserved;
            t.ai = v.ai;
        }
    }
    coverslot::write_back(&spots.slots, &mut covers);
    stats.alive = alive;
    stats.dead = dead;
    stats.states = states;
    stats.ms = t0.elapsed().as_secs_f32() * 1000.0;
}

/// One shot of a grunt's weapon at a target point (vt+0x13c -> 0x004284e0 -> the weapon): the aim error 0x004284e0
/// (sideways L in [-fire_inaccuracy_angle, +angle], up V in [0, fire_inaccuracy_height], scaled by s from the accuracy
/// ranges: d <= min 0.1, beyond max 1, between max(0.1, (d^2 - min^2) / (max^2 - min^2))), from the weapon's emitter
/// (0x004a2010: measured in the original, hook log_hits' and-shots.txt, second encounter, eight grunts facing their
/// target: the muzzle minus the grunt's position ~6.9 ahead, 1.3..1.5 to its right (the rifle at the right shoulder)
/// and 8.4 up; the bullets then pass where the original's do: a grunt standing in front of a shooter is hit), the
/// projectile and the muzzle fx (which carries the shot's sound).
fn shoot(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, g: &Grunt, rng: &mut FxStats, pos: Vec3, hit: Vec3, data: &crate::gamedata::GameData) {
    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
    let to = (hit - pos).with_y(0.0);
    let d = to.length();
    let s = g.aim_error_scale(d);
    let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
    let aim = hit + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s;
    let right = Vec3::new(-fwd.z, 0.0, fwd.x);
    let emitter = pos + fwd * 6.9 + right * 1.4 + Vec3::Y * 8.4;
    let muzzle = emitter - (aim - emitter).normalize_or(Vec3::Z) * 4.0;
    let dir = (aim - muzzle).normalize_or(Vec3::Z);
    if std::env::var("AIDBG").is_ok() {
        let o = crate::level::game_to_engine().inverse() * (muzzle + dir * 4.0 - pos);
        let gp = crate::level::game_to_engine().inverse() * pos;
        info!("AIDBG muzzle {:.1} {:.1} off {:.2} {:.2} {:.2}", gp.x, gp.y, o.x, o.y, o.z);
    }
    // the rocket captain's launcher fires the captain_rocket (CaptainRocket: ai/rocket.rs), the others a bullet
    match (g.me, crate::vehicles::rockets::is_rocket(&g.bullet)) {
        (Some(me), true) => rocket::fire(commands, fx, data, rng, muzzle + dir * 4.0, aim, me),
        _ => {
            let shot = crate::player::spawn_projectile_to(commands, fx, &g.bullet, muzzle + dir * 4.0, aim, false, g.me);
            // an arrow flies with its model (arrows.rs)
            if let Some(m) = &g.arrow_model {
                commands.entity(shot).insert(crate::arrows::ArrowShaft { model: m.clone() });
            }
        }
    }
    if let Some(m) = g.weapon.s("muzzle_fx_name") {
        fx.write(crate::fx::StartFx { name: m.to_owned(), at: muzzle + dir * 4.0, rotation: Transform::IDENTITY.looking_to(dir, Vec3::Y).rotation, follow: None, position_only: false });
    }
}

/// Fire cycle 0x00428980 with the counters of 0x00427600 and the aim error of 0x004284e0.
#[allow(clippy::too_many_arguments)]
fn fire_cycle(
    commands: &mut Commands,
    g: &mut Grunt,
    physics: &Physics,
    rng: &mut FxStats,
    pos: Vec3,
    p: Vec3,
    hit: Vec3,
    own_hit: Vec3,
    hand: Option<Vec3>,
    skip: &[BodyId],
    dt: f32,
    stats: &mut AiStats,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    fx: &mut MessageWriter<crate::fx::StartFx>,
    (barks, now, voices, data): (&mut Barks, f32, &crate::audio::PlayingVoices, &crate::gamedata::GameData),
) -> FireEnd {
    g.pause -= dt;
    if g.pause > 0.0 {
        return FireEnd::None;
    }
    // the counters (0x00427600): the first time (flag 4 clear) the clips too, then per clip the rounds and the first
    // burst, the burst at most the rounds
    if g.left_clips <= 0 {
        g.left_clips = roll_count(rng, g.clips);
        g.left_rounds = 0;
    }
    if g.left_rounds <= 0 {
        g.left_rounds = roll_count(rng, g.rounds);
        g.left_burst = roll_count(rng, g.bursts).min(g.left_rounds);
    }
    // fires only inside the 180-degree cone with line of sight
    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
    let to = (p - pos).with_y(0.0);
    if to.normalize_or_zero().dot(fwd) < 0.0 {
        return FireEnd::None;
    }
    if !sight_ray(physics, own_hit, hit, skip) {
        // no shot this frame (a guard: state 0x7a and back, the target dropped at the next search tick; in a trench:
        // duck, the caller)
        return FireEnd::Sight;
    }
    let _ = hand;
    if g.dry_first {
        // the dry first cycle: the fire anim without a shot, then the reload as after an empty clip
        g.dry_first = false;
        g.left_clips = 0;
        g.mind = Mind::Reload;
        g.pause = 0.467 + g.reload_len + 2.0 / 60.0;
        return FireEnd::None;
    }
    shoot(commands, fx, g, rng, pos, hit, data);
    g.shot_age = 0.0;
    if g.kind == Kind::Sniper {
        stats.sniper_shots += 1;
    }
    if g.rocket {
        stats.rocket_shots += 1;
    }
    if g.jumper.is_some_and(|j| j.flight.is_some_and(jumper::Flight::fires)) {
        stats.glide_shots += 1;
    }
    // (the shot sound comes from the weapon's muzzle fx: the original's sound log has one per shot)
    // the Fight cue as the fire anim starts with a target (0x00428980 -> vt+0x13c), under vo_delay
    if barks.take(Delay::Vo, now, rng) {
        if g.kind == Kind::Q112 {
            // its own cue (0x0043a200: Q1250006_Fight, kept as its line)
            let req = g.speech.line(voices, crate::audio::PlaySound::at(q112::FIGHT_LINE, pos));
            sounds.write(req);
        } else {
            let line = barks.cue_line(CaptainCue::Fight, g.kind, g.voice_set, rng);
            sounds.write(crate::audio::PlaySound::at(line, pos).looping(false));
        }
    }
    stats.shots += 1;
    // the fire cycle (0x00428980, counters 0x00427600, as the companions'): a shot on the fire anim's note (0.033 s
    // into its 0.467 s loop; here at its start), fire_num_rounds per clip. An empty clip plays the reload anim
    // (grunt_bow_reload_* 1.3 s, grunt_reload_br 1.133 s); a guard (state 0x79, ai_06.cpp:2656) then re-rolls the
    // counters on the first-call path, without fire_clip_delay, and fires the next frame: anim + 1 frame + reload +
    // 1 frame = 108 frames for the archer (the original's 837 -> 945); a moving grunt (state 0x32) waits the clip delay
    // the fire anim's length per shot: grunt_fire_* / grunt_bow_fire_* 0.467 s (one "fire" note each), the captain's
    // captain_fire_* 0.633 s looped (+0x484 0x1000: 0x00428250(1) at CaptainObject_Slot6) with one note per loop
    //
    // Every shot counts down the clip's rounds and the burst (the "fire" note, ai_05.cpp:1255-1256). A burst spent with
    // rounds left (ai_02.cpp:1928-1937) waits fire_burst_delay and rolls the next burst (at most the rounds left). A clip
    // spent counts the clips (+0x5b4); with clips left a moving grunt reloads in place (anims 0x2a.., state 0x32, its end
    // rolling the next clip after fire_clip_delay); a guard (may_move clear) does not stop there and spends the rest at
    // once. After the last clip (flag 4 cleared) the taunt, the close attack, then the search (0x0042b8e0).
    let fire_len = g.fire_len;
    g.left_rounds -= 1;
    g.left_burst -= 1;
    g.pause = fire_len;
    if g.left_rounds > 0 {
        if g.left_burst > 0 {
            return FireEnd::None;
        }
        g.pause = fire_len + rng.range(g.burst_delay.0, g.burst_delay.1);
        g.left_burst = roll_count(rng, g.bursts).min(g.left_rounds);
        stats.bursts += 1;
        return FireEnd::Burst;
    }
    g.left_clips = if g.guard { 0 } else { g.left_clips - 1 };
    if g.left_clips > 0 {
        g.mind = Mind::Reload;
        g.pause = fire_len + g.reload_len + rng.range(g.clip_delay.0, g.clip_delay.1);
        return FireEnd::Reload;
    }
    g.fire_ready = false;
    if g.guard {
        // a guard's last clip (may_move clear: no taunt, ai_02.cpp:1957): the reload set where he stands
        g.mind = Mind::Reload;
        g.pause = fire_len + g.reload_len + 2.0 / 60.0;
        return FireEnd::Clip { close_attack: true };
    }
    // the taunt after the last clip (0x00428980, ai_02.cpp:1955-1981): a target, vt+0x1d8 (all three classes),
    // taunt_delay passed (Game+0xb84 / +0xb90, 300..360 s game-wide), not in a trench's slot (+0x484 0x20000);
    // the heading test (0x004e81a0 of the two facings < 180) passes but for opposite facings, and the grunt's own
    // line +0xb50 silent: the Taunt bark (vt+0x15c, kept as his line), a taunt anim 0x32 + (rand * 3 >> 15),
    // state 0x6e; the timer restarts only then. A taunt due but blocked by his own line skips the close attack
    // (tryFallback cleared, ai_02.cpp:1962).
    let taunt_due = !g.at_barricade && g.kind.taunts() && barks.ready(Delay::Taunt, now, rng);
    if taunt_due && !g.speech.saying(voices) {
        barks.take(Delay::Taunt, now, rng);
        let set = g.voice_set;
        let req = g.speech.line(voices, bark_request("Taunt", set, pos));
        sounds.write(req);
        g.anim_id = 0x32 + ((rng.rand() * 3.0) as u32).min(2);
        g.mind = Mind::Taunt;
        return FireEnd::Taunt;
    }
    g.mind = Mind::Reload;
    g.pause = fire_len + g.reload_len + rng.range(g.clip_delay.0, g.clip_delay.1);
    FireEnd::Clip { close_attack: !taunt_due }
}

/// One of 0x00427600's integer rolls (RandRange: min..=max).
fn roll_count(rng: &mut FxStats, (lo, hi): (i64, i64)) -> i64 {
    rng.range(lo as f32, hi as f32 + 0.999) as i64
}

/// Character_Die: Dead bark once; ragdoll (a tumbling rigid body here) when the killing hit was at least
/// hit_ragdoll and fewer ragdolls than the limit are down (GoRagdoll 0x00431310), impulse = direction x amount x 0.15;
/// else a death anim. Either way the body gets its corpse timer (corpses.rs).
/// A jumper captain's turbo pack (+0xb08): the model on his "chest" (`jumper_packs`).
#[derive(Component)]
struct TurboPack {
    model: Entity,
    /// its own "chest" put on the captain's (once its rig exists)
    aligned: bool,
    /// the flight the last frame saw (a ride's start and end, the wings opening)
    ride: Option<jumper::Flight>,
    /// the glide loop's voice (+0xb0c)
    wings: crate::jetpack::WingSounds,
}

/// The jumper captains' pack model (0x00425d30 -> PlayerObject_SetTurboPack 0x0047b9f0 with the troop as owner:
/// "turbo" attached by its own "chest" at his "chest", Object_Attach 0x004539f0, turbo_idle), riding with him until
/// his body is removed (the teardown GenericAIObject_Slot55 0x00432730 takes it off); while he glides it plays
/// turbo_forward_cycle (0x0042a2c0, state 0xbc's launch), and turbo_idle again when the ride ends (ai_03.cpp:2692).
/// Every launch plays FX_turbo_Launch_captain on the pack's chest (PlayerObject_PlayChestFx 0x0047bba0: the glide launch
/// and its hop 0x0042a2c0, the marker launch 0x0042b480 ai_03.cpp:712); the wings opening (the glide launch, state
/// 0xbc, ai_02.cpp:2975; the re-aim into the glide, ai_06.cpp:2417) play the wing sounds and the glide loop, and the
/// ride's end (the landing's turbo reset 0x0042e1c0, a hit or death in flight ai_04.cpp:332 / 437) wings_in with the
/// loop stopped (Jumper_PlayWingSounds 0x0047bb30, `jetpack::WingSounds`).
#[allow(clippy::too_many_arguments)]
fn jumper_packs(
    mut commands: Commands,
    mut grunts: Query<(Entity, &Grunt, &crate::skin::Animator, Option<&mut TurboPack>)>,
    mut packs: Query<&mut crate::skin::Animator, Without<Grunt>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    (mut sounds, mut stop): (MessageWriter<crate::audio::PlaySound>, MessageWriter<crate::audio::StopVoice>),
) {
    for (e, g, ga, pack) in &mut grunts {
        let Some(j) = g.jumper.as_ref() else { continue };
        let Some(mut pack) = pack else {
            let chest = crate::jetpack::chest(ga);
            let Some(model) = data.templates.get("turbo").and_then(|t| t.model.clone()) else { continue };
            if let Some(m) = chest.and_then(|c| kit.spawn(&mut commands, &model, Transform::IDENTITY, true).map(|m| (c, m))) {
                commands.entity(m.0).add_child(m.1);
                commands.entity(e).insert(TurboPack { model: m.1, aligned: false, ride: None, wings: default() });
            }
            continue;
        };
        // the ride: a dead captain's is over (the hit in flight, ai_04.cpp:332)
        let ride = j.flight.filter(|_| g.mind != Mind::Dead);
        match (pack.ride, ride) {
            (None, Some(_)) => {
                fx.write(crate::jetpack::chest_fx("fx_turbo_launch_captain", crate::jetpack::chest(ga), e, &joints));
            }
            (Some(_), None) => pack.wings.close(e, &mut sounds, &mut stop),
            _ => {}
        }
        if ride == Some(jumper::Flight::Glide) && pack.ride != Some(jumper::Flight::Glide) {
            pack.wings.open(e, &mut sounds);
        }
        pack.ride = ride;
        let Ok(mut a) = packs.get_mut(pack.model) else { continue };
        if !pack.aligned {
            // Object_Attach's child bone: the pack's "chest" (rest pose) on the captain's chest joint
            if let Some(&i) = a.skeleton.index.get("chest") {
                commands.entity(pack.model).insert(Transform::from_matrix(crate::jetpack::model_space(&a.skeleton, i).inverse()));
            }
            pack.aligned = true;
        }
        let wings = if j.flight == Some(jumper::Flight::Glide) { "turbo_forward_cycle" } else { "turbo_idle" };
        if a.layers.first().is_none_or(|l| l.anim != wings) {
            // (0x00408e30 with blend 0.25)
            a.blend = 0.25;
            a.play(0, wings, true, crate::skin::Mask::All);
        }
    }
}

/// A character's max_health as its property set gives it, before the difficulty's scale (`difficulty_health`).
#[derive(Component)]
struct UnscaledHealth(f32);

/// Ai_InitGeneric 0x004247f0 (ai_01.cpp:2323-2333): the bad guys' health (vt+0xe8: grunts, captains, the sniper, the
/// monk, the Q112) x Game+0x558 and the player's side's (vt+0xe4: Q and Jonesy) x Game+0x554, the chosen difficulty's
/// factors (gamedata.rs DIFFICULTIES: bad guys easy 0.5, hard and very hard 2; friends easy 2, very hard 0.5), into
/// max_health (+0x458) and health (+0x454) both. Scaled in proportion, so a character made hurt or dead stays so; a
/// difficulty set while the level runs (a saved game's, applied after the level's objects: savegame.rs) scales them
/// again from the unscaled value. Roman's max_health takes the player's side's factor as well (PlayerObject_Load
/// 0x00473da0, game_05.cpp:561: +0x458 = Game+0x554 x +0x458, health +0x454 the same).
fn difficulty_health(
    mut commands: Commands,
    difficulty: Res<crate::gamedata::Difficulty>,
    mut grunts: Query<(Entity, &mut Grunt, &mut Health, Option<&UnscaledHealth>)>,
    mut companions: Query<(Entity, &mut Companion, &mut Health, Option<&UnscaledHealth>), Without<Grunt>>,
    mut roman: Query<(Entity, &mut crate::player::Roman, &mut Health, Option<&UnscaledHealth>), (Without<Grunt>, Without<Companion>)>,
) {
    let f = difficulty.factors();
    let changed = difficulty.is_changed();
    // (max, health, last health seen) to the new max, all in proportion
    let scale = |max: &mut f32, health: &mut f32, last: &mut f32, to: f32| {
        let k = if *max > 0.0 { to / *max } else { 1.0 };
        *max = to;
        *health *= k;
        *last *= k;
    };
    for (e, mut g, mut h, base) in &mut grunts {
        let unscaled = match base {
            Some(b) if changed => b.0,
            Some(_) => continue,
            None => {
                commands.entity(e).insert(UnscaledHealth(g.max_health));
                g.max_health
            }
        };
        let g = &mut *g;
        scale(&mut g.max_health, &mut h.0, &mut g.last_health, unscaled * f.badguy_health);
    }
    for (e, mut c, mut h, base) in &mut companions {
        let unscaled = match base {
            Some(b) if changed => b.0,
            Some(_) => continue,
            None => {
                commands.entity(e).insert(UnscaledHealth(c.max_health));
                c.max_health
            }
        };
        let c = &mut *c;
        scale(&mut c.max_health, &mut h.0, &mut c.last_health, unscaled * f.friendly_health);
    }
    // (a saved game sets his health as it was saved, absolute, with its difficulty: then only the maximum changes)
    for (e, mut r, mut h, base) in &mut roman {
        match base {
            Some(b) if changed => {
                r.max_health = b.0 * f.friendly_health;
                h.0 = h.0.min(r.max_health);
            }
            Some(_) => {}
            None => {
                commands.entity(e).insert(UnscaledHealth(r.max_health));
                let to = r.max_health * f.friendly_health;
                scale(&mut r.max_health, &mut h.0, &mut 0.0, to);
            }
        }
    }
}

fn die(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut grunts: Query<(Entity, &mut Grunt, &Body, &mut Transform, &Health, Option<&mut crate::skin::Animator>, (Option<&DeathAnim>, Option<&ic::Knocked>))>,
    joints: Query<&GlobalTransform>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut km: ResMut<KillMeter>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    alive: Query<&Health>,
    (mut barks, time, mut guns): (ResMut<Barks>, Res<Time>, Query<&mut crate::turret::Turret>),
    (voices, mut stop): (crate::audio::PlayingVoices, MessageWriter<crate::audio::StopVoice>),
    (corpses, settings, mut ai_stats, mut clock): (Query<&crate::corpses::Corpse>, Res<crate::graphics::GraphicsSettings>, ResMut<AiStats>, ResMut<launch::LaunchClock>),
    roman: Query<&GlobalTransform, With<Player>>,
) {
    let roman_at = roman.single().ok().map(|t| t.translation());
    let mut alerts: Vec<(i64, Entity, Entity)> = Vec::new();
    // the ragdolls already down (GoRagdoll 0x00431310 counts them over the AI list: 0x00431000)
    let rules = crate::corpses::CorpseRules::of(&settings);
    let mut down = crate::corpses::Corpses::ragdolls(&corpses);
    for (e, mut g, body, mut tr, h, animator, (forced, knocked)) in &mut grunts {
        // (a monk under trigger_no_damage takes no damage: think gives it back, `trigger_kills` clears it first)
        if g.mind == Mind::Dead || h.0 > 0.0 || g.no_damage {
            continue;
        }
        // a gunner's death lets the gun go (0x00431720 -> 0x004266b0: flag 4 cleared, the gun's corner set back to the
        // centre); he dies where his body is, at the seat behind the gun
        if let Some(gn) = g.gun.take() {
            if let Ok(mut t) = guns.get_mut(gn.turret) {
                if t.ai == Some(e) {
                    t.ai = None;
                    t.ai_aim(0.5, 0.5);
                }
                if t.reserved == Some(e) {
                    t.reserved = None;
                }
            }
            if gn.phase == GunPhase::On {
                tr.translation += body.offset - gn.lift;
                g.fixed = None;
            }
        }
        g.mind = Mind::Dead;
        km.meter += 1.0;
        // his alert group hears of it (0x00431720): with a group id (+0x574) and a living killer, every other living
        // member takes the killer as a target after 0.25..1 s (0x0042c700 -> the +0x578 hand-over)
        if let Some(k) = g.attacker.take().filter(|&k| alive.get(k).is_ok_and(|h| h.0 > 0.0)) {
            if g.alert_group != 0 {
                alerts.push((g.alert_group, k, e));
            }
        }
        // an enemy killed, any cause (0x00431720), and his score_value
        ms.enemies_killed += 1;
        ms.killed(g.score);
        if g.kind == Kind::Q112 {
            // the Q112's death (Q112Object_Slot53 0x00435220): its eye loop stopped; its kept Hurt / Fight line stopped
            // as every character's at death (0x00431720 -> renderer vt+0xd8 on +0xb50); its Dead line (0x0043a2b0)
            // always (no Q112 is trigger-killed in the data, the record kind 0x8000 that would skip it)
            if let Some(id) = g.q112.as_mut().and_then(|q| q.eye.take()) {
                stop.write(crate::audio::StopVoice { id, immediate: false });
            }
            g.dead_said = true;
            let req = g.speech.death_line(&voices, &mut stop, crate::audio::PlaySound::at(q112::pick(&q112::DEAD_LINES, rng.rand()), tr.translation));
            sounds.write(req);
        }
        if !g.dead_said {
            g.dead_said = true;
            // Dead (0x00439e60, pool 26..28) under death_delay and with no scream playing, else DeadAtPosition
            // (0x00439ea0, pool 21..25): once; either stops his line first and is kept as the line
            let dead = !g.speech.screaming(&voices) && barks.take(Delay::Death, time.elapsed_secs(), &mut rng);
            let cat = if dead { "Dead:{}:last3" } else { "Dead:{}:butlast3" };
            let set = g.voice_set;
            // Dead says it on him (0x004396a0); DeadAtPosition only for the +0x488 bit 29 set (set 2), the others at the
            // place he died (0x00439890: audio +0xcc with virtual +0x54's position)
            let req = bark_request(cat, set, tr.translation);
            let req = if dead || set == 2 { req.following(e) } else { req };
            let req = g.speech.death_line(&voices, &mut stop, req);
            sounds.write(req);
        }
        // a killing blast's fling (GoRagdoll 0x00431310's impact branch), else the body's own motion
        let fling = g.blast_fling.take();
        let mut push = fling.unwrap_or_else(|| physics.world.body(body.id).map(|b| bv(b.linear_velocity())).unwrap_or(Vec3::ZERO) / physics.meters_per_unit);
        if let Some(f) = fling {
            ai_stats.blast_flings += 1;
            ai_stats.max_fling = ai_stats.max_fling.max(f.length());
        }
        physics.remove(body.id);
        // the death (Character_Die 0x004319e0, in its order): a window / tower grunt killed by a record with flag
        // 0x80 (the sticky bomb) is blown out of the window (anim 0x3e); a hit of at least hit_ragdoll ragdolls; a
        // ledge_death grunt falls (0x39 + rand*4: grunt_hit_fall_1 / _2 / _1 / _2); a window / tower grunt
        // grunt_hit_window (0x3d); else 0x00427100: grunt_barricade_death (0x56) in a trench's slot, else grunt_die_1..6.
        // Not reached here: the 0x57 flag variant (in back: no lev_1_m1 grunt sets it) and record flag 2's launch.
        // a knock-away's GoRagdoll (ai/ic.rs `knocks`: vt+0x17c straight from GenericAI_Update or the wake-up, not
        // Character_Die): its own launch / impact / ragdoll choice (ic::go_ragdoll); no window blow-out, no death anim
        let knock = knocked.map(|k| (k, ic::go_ragdoll(k, clock.allows(), clock.launched_now(), rng.rand(), rng.rand())));
        let blown_out = knock.is_none() && g.window && g.cartoon;
        // dying on all fours (Character_Die 0x004319e0, game_02.cpp:1293 / 1347-1357): falling to them (state 300) the
        // fall's anim plays on, nothing more; on them (0x12d) anim 0x37 (captain_allfours_death), state 0x191; neither
        // ragdolls
        let fours_death = (knock.is_none() && forced.is_none() && g.hit.is_some()).then_some(g.reacting).and_then(|r| match r {
            5 => Some(0x35),
            6 => Some(0x37),
            _ => None,
        });
        // a blast's fling is a launch (Character_Die 0x004319e0 on an area record: state 0x195) whatever its speed
        // (only the killing hit decides it, never the body's own speed: game_02.cpp:1370-1378)
        let wants_ragdoll = knock.is_some() || (forced.is_none() && fours_death.is_none() && !blown_out && (g.last_hit >= g.hit_ragdoll || fling.is_some()));
        let mut thrown = fling.is_some();
        let mut limited = true;
        let mut knock_launch = None;
        match knock {
            Some((k, ic::RagdollGo::Launch { spread })) => knock_launch = k.attacker.map(|a| (a, spread)),
            Some((_, ic::RagdollGo::Thrown(v))) => {
                push = v;
                thrown = true;
            }
            Some((_, ic::RagdollGo::Ragdoll { impulse, limited: l })) => {
                push = impulse;
                limited = l;
            }
            None => {}
        }
        // GoRagdoll 0x00431310's launch: a killing record with flag 0x80 (the sticky bomb's, from the bomb) at least 10 s
        // after the last launch (Game+0xb34) throws him through the air (Character_Launch 0x00431020: toward a point
        // 40..60 from the bomb along its line to him, forward or back past it, at rand 2.5..3 x his move_speed), before
        // the ragdoll count is looked at. The original flails him through that arc (anims 0x49 / 0x4a) and he goes
        // limp on landing; here the ragdoll is launched with the arc's velocity (as the glove's punch, glove.rs)
        // a blast's fling at the very time of a launch (GoRagdoll's impact branch with Game+0xb34 == now,
        // physics_01.cpp:3597-3612, as ic::go_ragdoll for a knock): 25 % (rand * 100 >> 15 < 25) a launch from the
        // record's attacker (+0x2c: the sticky's thrower, Roman, whose launch stamped the clock) turned within 45
        // degrees, else the fling x rand 2..5
        let blast_launch = match fling {
            Some(f) if knock.is_none() && fours_death.is_none() && clock.launched_now() && !(g.cartoon && clock.allows()) => match ic::go_ragdoll(
                &ic::Knocked { impulse: f, amount: f32::INFINITY, quiet: false, attacker: roman_at },
                false,
                true,
                rng.rand(),
                rng.rand(),
            ) {
                ic::RagdollGo::Launch { spread } => roman_at.map(|a| (a, spread)),
                ic::RagdollGo::Thrown(v) => {
                    push = v;
                    None
                }
                ic::RagdollGo::Ragdoll { .. } => None,
            },
            _ => None,
        };
        let launch_from = if knock.is_some() {
            knock_launch
        } else {
            g.cartoon_from.filter(|_| wants_ragdoll && g.cartoon && clock.allows()).map(|f| (f, 0.0)).or(blast_launch)
        };
        let launched = launch_from.is_some();
        if let Some((from, spread)) = launch_from {
            clock.stamp();
            let rolls = launch::LaunchRolls { angle: rng.rand(), side: rng.rand(), forward: rng.rand(), anim: rng.rand() };
            push = launch::launch(from, tr.translation, rng.range(2.5, 3.0), g.move_speed, spread, false, rolls).velocity;
            info!("EVENT ai: {e:?} launched ({:.0} / s)", push.length());
        }
        // GoRagdoll with more ragdolls down than the limit allows: the animated death of 0x00427100 instead
        // a launch or a blast's fling comes before GoRagdoll's count of the bodies down (the original throws the whole
        // body, state 0x195, whatever the count): flung bodies always fly; a quiet knock skips the count
        g.ragdoll = launched || (wants_ragdoll && (thrown || !limited || crate::corpses::Corpses::may_ragdoll(down, &rules)));
        down += g.ragdoll as u32;
        g.death_name = if let Some(d) = forced {
            Some(d.0)
        } else if let Some(id) = fours_death {
            Some(g.anim(id, 0.0))
        } else if blown_out {
            Some("grunt_window_blown_out")
        } else if g.ragdoll {
            None
        } else if wants_ragdoll {
            // 0x00427100: the trench death (0x56) in a trench's slot, else grunt_die_1..6 (death_anim)
            g.at_barricade.then_some("grunt_barricade_death")
        } else if g.ledge_death {
            Some(if rng.rand() < 0.5 { "grunt_hit_fall_1" } else { "grunt_hit_fall_2" })
        } else if g.window {
            Some("grunt_hit_window")
        } else if g.at_barricade {
            // 0x00427100: +0x484 0x20000 -> anim 0x56 (GruntObject 0x0043bd70: grunt_barricade_death)
            Some("grunt_barricade_death")
        } else {
            None
        };
        if std::env::var("AIDBG").is_ok() {
            info!("AIDBG death {e:?}: {:?} ragdoll {} (hit {:.0}, cartoon {})", g.death_name, g.ragdoll, g.last_hit, g.cartoon);
        }
        if !g.ragdoll {
            // the death anim on slot 0 (charanim.rs), then the corpse timer
            commands.entity(e).remove::<Body>().insert(crate::corpses::Corpse::animated(0));
            continue;
        }
        commands.entity(e).insert(crate::corpses::Corpse::ragdoll());
        // thrown through the air (a launch or a blast's fling, state 0x195, or a body moving at launch speed: the body
        // fall on landing, bodyfall.rs)
        if launched || thrown || push.length() > crate::bodyfall::LAUNCH_SPEED {
            commands.entity(e).insert(crate::bodyfall::Launched::default());
        }
        // an articulated ragdoll from the pose at death (ragdoll.rs); the single box below when that fails
        if let Some(mut a) = animator {
            if let Some(rd) = crate::ragdoll::start(&mut physics, &a, &joints, push) {
                a.layers.clear();
                commands.entity(e).remove::<Body>().insert(rd);
                continue;
            }
        }
        let k = physics.meters_per_unit;
        let half = Vec3::new(2.6, 1.6, 4.8) * tr.scale * k;
        let Ok(shape) = Shape::new_box(jv(half)) else { continue };
        let center_local = Vec3::new(0.0, 0.0, 4.8);
        let origin = tr.translation + tr.rotation * (center_local * tr.scale);
        let settings = BodySettings::new_dynamic().mass(80.0).friction(0.8).angular_damping(0.4);
        if let Some(id) = physics.add_dynamic(e, &shape, origin, tr.rotation, push, settings) {
            commands.entity(e).insert(Body { id, offset: center_local, sync_rotation: true });
        }
    }
    for (group, killer, who) in alerts {
        for (ge, mut g, _, _, _, _, _) in &mut grunts {
            // (0x0042c700 also passes over a traitor of the group: not tested here, a traitor's group mates rarely outlive him)
            if alerted(&g, ge, (group, killer, who), false) {
                if std::env::var("AIDBG").is_ok() {
                    info!("AIDBG group {group} alerted by a death: {ge:?} -> {killer:?}");
                }
                g.pending = Some((killer, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// Roman's companions, Q (q111, Q111Object 0x005a6df8) and Jonesy (molmer, MolmerObject 0x005ae5f8): their shared
/// update MolmerObject_Slot47 0x00485910 (ai_08.cpp).
/// - Formation: the slot is Roman's bone ref_q_00 / ref_molmer_00 (0x00483d60); idle (mode 1) until the 2D distance
///   to Roman exceeds formation_distance + 2.5 (0x0048631c), then run to the slot (mode 2), idle again there. The
///   slot must stand on walkable ground and a route must lead there round Roman and the partner, else he stays
///   (`formation_route`). The run's speed is fitted (`formation_speed`; 0x00486cb0's (d / delay) x move_speed ramp is
///   not the measured speed). Which of the pair takes which slot: the leader pick (fn leader_pick).
/// - Targets (0x00485490 -> AI_FindTarget 0x00426920): the nearest live grunt within scan_distance, in the 190 degree
///   facing cone and in line of sight, then refused when farther than scan_distance from the anchor (0x004853a0);
///   nothing found, the partner's target within reach (Slot139). The fight's end (mode 3, `FightEnd`): home at once,
///   or the partner's target, or a 3..7 s linger; the close kill (ai/close_kill.rs); stunned or carried (modes 5 / 10,
///   ai/carried.rs).
/// - Roman's Fire held (0x00485580, mode 1 under Defend Me / Return To Me, no target): facing within 45 degrees of
///   Roman's heading he reloads in place (the reload set 0x28..0x2b: no shots), and he turns toward Roman's heading
///   (0x00486525, flag 0x800).
/// - Fire cycle (0x00428980, counters 0x00427600): bursts of fire_num_bursts shots on the fire anims' notes (Jonesy:
///   the 4 blended 0.3 s anims each fire at 0: 4 at once; Q: 6 notes per 0.633 s loop), fire_burst_delay between
///   bursts, fire_num_rounds per clip, then the reload anim and fire_clip_delay. Shots: the weapon's projectile at the
///   aim point (+ scatter up to fire_inaccuracy_height); the muzzle fx carries the sound (Q's is one fx per burst, its
///   machine-gun loop).
/// - Team orders (fn team_orders): Defend Me / Return To Me / Defend Area / Attack, their lines and HUD texts; the
///   Fight cue at the fire anim's start (docs/subsystems/companions.md).
/// Not ported: object types 0x51 / 0x53 and the rest of the enemy list (World+0xe7c) as attack targets (the companions
/// fight grunts only).
#[derive(Component)]
pub struct Companion {
    pub walker: crate::physics::Walker,
    pub q: bool,
    /// max_health (+0x458, 0x004555c0; the HUD meter's ratio)
    pub max_health: f32,
    /// the run anim's playback rate by his speed (p_q / p_molmer anim_run_*: skin.rs RunRates, charanim.rs)
    pub run_rates: crate::skin::RunRates,
    pub distance: f32,
    pub delay: f32,
    pub scan: f32,
    pub speed: f32,
    pub base: Quat,
    pub yaw: f32,
    pub vy: f32,
    pub moving: bool,
    /// the fire anim playing (anims), reloading (seconds left of the reload anim)
    pub firing: bool,
    pub reloading: f32,
    /// Jonesy's TNT throw (anim 39 molmer_throw_tnt, 2.3 s): the time into it
    pub throwing: Option<f32>,
    /// +0x484 flag 1: a throw is allowed; set when the fire counters are rolled after a full clip cycle (0x00427600,
    /// at spawn and at the reload's end), cleared by the throw (0x00428340)
    throw_armed: bool,
    /// the engagement range +0x5d8, rolled once in [fire_range_min, fire_range_max] (0x00425d30); negative: not yet
    range: f32,
    fire_range: (f32, f32),
    /// in range of the target (engaged, state 0xa); before that it walks toward it (state 0x3c)
    engaged: bool,
    /// aim elevation weight 0 (down) .. 1 (up), for the anim blend
    pub aim_v: f32,
    target: Option<Entity>,
    /// how the fight ended (mode 3's tests 0x004265f0 / 0x004265c0): None while fighting or not in one
    fight_end: Option<FightEnd>,
    /// AI_FindTarget's timer +0x490: a search every 0.75..1.25 s
    search: f32,
    rounds: u32,
    burst: u32,
    loop_t: f32,
    delay_t: f32,
    muzzle: Option<Entity>,
    p: CompanionFire,
    /// the route to the slot (0x0046f6d0, re-planned every 0.25 s while moving: nav.rs A*)
    route: Vec<Vec3>,
    replan: f32,
    /// Q's healthy tea (0x004857e0, mode 9): the drop anim's time left, and given once (+0xbcc 0x20000)
    pub tea: f32,
    tea_given: bool,
    /// placed on its formation point beside Roman at the start (0x004836b0 from the player's reset 0x00475650)
    spawned: bool,
    /// whether the start places it there: not when it waits for a trigger (receive_trigger_activate +0x52c, kept only
    /// with a tower_marker_tag, 0x00483250): it starts where the level placed it (lev_4_m2's Jonesy, ai/tower.rs)
    starts_in_formation: bool,
    /// send_trigger_on_death (+0x528, sent by the AI's death 0x00431720): lev_4_m2's Jonesy 5001 (failure 2),
    /// lev_bonus_m3's pair 5002 / 5001
    death_trigger: i64,
    /// stig_damage_multiplier (+0x8fc) and forge_damage_multiplier (+0x900), default 1 (ai_01.cpp:2348): a mounted
    /// gun's bullets do 0.3 and the forge cannon's mortar 0.1 of their damage to Q and Jonesy (p_q / p_molmer; no other
    /// AI property set of the 24 levels changes them)
    damage_mult: (f32, f32),
    /// hits and death (GenericAIObject_Slot52 0x00430340 and Character_Die 0x004319e0, no companion overrides):
    /// health last frame, seconds since the last Hurt bark (+0xb48, 10 s), the reaction anim playing (name, time
    /// left), a player / teammate shot that hit (FriendlyFire bark, no damage: `friendly_fire`), the attacker to
    /// offer as a target, dead (permanent: *_die, then the body is removed)
    pub last_health: f32,
    pub hurt_t: f32,
    pub hit: Option<(&'static str, f32)>,
    pub friendly: bool,
    /// the damage of a friendly shot this frame: no health lost, but the hit reaction plays (`friendly_fire`)
    friendly_damage: Option<f32>,
    pub attacker: Option<Entity>,
    pub dead: Option<f32>,
    /// the standing order (flags +0xbcc 1 / 4 / 8; docs/subsystems/companions.md)
    pub order: Order,
    /// formation_angle (+0xbc0, default 20): the spread of the formation points round an order's anchor (0x00483c30)
    angle: f32,
    /// +0xb68 after an attack order: seconds idle before the pair returns silently (3..7 s, 0x00484100)
    linger: f32,
    /// +0xbcc flag 0x10, the leader of the pair (0x00483810): it takes Roman's ref_q_00 (else ref_molmer_00) and under
    /// an area / attack order the formation point turned by +formation_angle (0x00483d60 / 0x00483c30); Q starts with
    /// it (Q111Object_Slot6 0x00486eb0: flags |= 0x10 | 0x200)
    pub lead: bool,
    /// +0xb90: when the leader was last picked (the partner's newer pick is copied, not redone)
    swap_t: f32,
    /// +0xb7c: Roman's direction at the last pick (a turn past 90 degrees forces the next one)
    roman_dir: Vec3,
    /// mode 2's 0.25 s re-pick timer (+0xb68 while moving)
    swap_timer: f32,
    /// the formation point the route leads to: taken at the regroup's start and again with each pick on the move
    /// (0x00483d60 after 0x00483810, every 0.25 s while the partner moves too), not followed live in between, so a
    /// companion trails Roman by the slot plus up to 0.25 s of his walk (the original's walk: about 30 units behind,
    /// stopping where the last pick put the slot when Roman starts off)
    goal: Option<Vec3>,
    /// an order asks for a forced pick (the area order 0x00484ca0: 0x00483810(1))
    force_swap: bool,
    /// +0xbcc flag 0x80: the partner started a regroup (0x004837b0), so this one regroups too at its next idle frame
    regroup_req: bool,
    /// his current line (+0xb50: the hurt lines, the banter's; dialogue.rs asks whether it still plays)
    pub speech: crate::audio::Speech,
    /// when he last fired (game time): Game+0xafc (Q, Q111Object_Slot101 0x004878f0) / +0xb0c (Jonesy, 0x0044f160),
    /// stamped before each shot's aim error; with his moves it is what the idle banter's 30 s are counted from
    pub last_shot: Option<f32>,
    /// an IC anim carries him (state 0x64: the tower task's, a close kill's): physics off (0x004577b0(0): bullets pass
    /// him), no hit reactions (0x00430340 while 0x00426630), and his anims are the owner's
    pub ic: bool,
    /// a full-body anim his state asks for instead of the formation's (the stun's hang: carried.rs), looped or not
    pub pose: Option<(&'static str, bool)>,
    /// how his last fight ended (the playtest log): "home", "linger" (then back), "takeover"
    last_end: (&'static str, u32),
    /// the fights he has begun (mode 1 / 2 -> 3), for the playtest log
    fights: u32,
    /// the reload under way is the one Roman's Fire asks for (0x00485580: the reload set, flag 0x20 of +0xbcc)
    support_reload: bool,
    /// flag 0x800: turning toward Roman's heading since his Fire was held, until facing it (0x00486525)
    face_heading: bool,
    /// the return order sends him back to formation (0x00484290 when he does not stay: the pick, the route, mode 2)
    return_req: bool,
    /// the attack order named a new target (0x00484100): the close kill is tried at once, else +0xb68 is rolled
    order_engage: bool,
    /// +0x514: when he last took an offered target (GenericAI_OfferTarget 0x0042c820); Roman's attacker is taken only
    /// within 10 s of it (0x00486df0)
    offer_t: Option<f32>,
    /// the one who hit Roman this frame (0x0047afa0 -> 0x00486df0), offered by `companion_damage`
    roman_attacker: Option<Entity>,
    /// seconds before a failed route to the formation point is tried again (`set_out`)
    route_retry: f32,
    /// in a fight (mode 3) at the end of his last update: a fight starting rolls +0xb68 (0x00486200 / 0x004866da)
    was_fighting: bool,
}

/// How a companion's fight ended (MolmerObject_Slot47 0x00485910 mode 3).
#[derive(Clone, Copy, PartialEq, Debug)]
enum FightEnd {
    /// the generic reaction to a lost target with its fire counters left (0x0042bc60 with +0x484 flag 4: the home route,
    /// state 0x50): mode 3 ends at once (0x004265f0) and he runs back to formation (mode 2)
    Home,
    /// with the clip spent (flag 4 clear: the reload in place, state 0x32, then idle): once idle (0x004265c0) he takes
    /// over the partner's live target, else counts down +0xb68 (rolled 3..7 s when the fight began) and then goes back
    Linger,
}

impl FightEnd {
    /// The target lost with nothing new in sight (0x0042bc60): with the clip spent (reloading, or no rounds left: +0x484
    /// flag 4 clear) the reload in place and the linger, else the home route.
    fn on_loss(clip_spent: bool) -> Self {
        if clip_spent {
            FightEnd::Linger
        } else {
            FightEnd::Home
        }
    }
}

/// The pair's standing order (+0xbcc flags; 0x004844c0, 0x00484290, 0x00485120).
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Order {
    /// flags 1 | 8, the start (0x004836b0): "Defend Me". Formation on Roman's ref bones; targets within scan_distance
    /// of Roman (0x004853a0); regroup when away from the slot
    Guard,
    /// no flag 1 (a second "return" toggles to it): "Return To Me". Generic targeting (0x0043ee90, no anchor limit);
    /// regroup only when Roman is farther than delay_distance (mode 1, 0x0048631c)
    Loose,
    /// flag 1 without 8: "Defend Area" at the aimed point, facing Roman's direction at the order (0x00484ca0)
    Area { at: Vec3, dir: Vec3 },
    /// flags 1 | 4: "Attack" the grunt under the crosshair, anchored where it stood (0x00484100); `target` is the held
    /// attack target +0xbb0
    Attack { at: Vec3, dir: Vec3, target: Entity },
}

#[derive(Clone)]
struct CompanionFire {
    rounds: (u32, u32),
    bursts: (u32, u32),
    burst_delay: (f32, f32),
    clip_delay: (f32, f32),
    height: f32,
    /// fire_inaccuracy_angle (the exe's key, default 15), fire_accuracy_min / max (50 / 150)
    angle: f32,
    accuracy: (f32, f32),
    bullet: String,
    muzzle_fx: String,
}

impl Companion {
    pub fn new(walker: crate::physics::Walker, p: &crate::gamedata::Props, data: &crate::gamedata::GameData, q: bool, yaw: f32, base: Quat) -> Self {
        let weapon = data.props(if q { "p_q_chaingun" } else { "p_molmer_pistol" });
        let fire = CompanionFire {
            rounds: (p.i("fire_num_rounds_min", 30) as u32, p.i("fire_num_rounds_max", 30) as u32),
            bursts: (p.i("fire_num_bursts_min", 4) as u32, p.i("fire_num_bursts_max", 4) as u32),
            burst_delay: (p.f("fire_burst_delay_min", 0.2), p.f("fire_burst_delay_max", 0.5)),
            clip_delay: (p.f("fire_clip_delay_min", 0.5), p.f("fire_clip_delay_max", 1.0)),
            height: p.f("fire_inaccuracy_height", 1.0),
            angle: p.f("fire_inaccuracy_angle", 15.0),
            accuracy: (p.f("fire_accuracy_min", 50.0), p.f("fire_accuracy_max", 150.0)),
            bullet: format!("p_{}", weapon.s("projectile_name").unwrap_or("molmer_bullet")),
            muzzle_fx: weapon.s("muzzle_fx_name").unwrap_or("").to_owned(),
        };
        Companion {
            walker,
            q,
            max_health: p.f("max_health", 500.0),
            run_rates: crate::skin::RunRates::from_props(p),
            distance: p.f("formation_distance", 30.0),
            delay: p.f("delay_distance", 50.0),
            scan: p.f("scan_distance", 300.0),
            speed: p.f("move_speed", 46.0),
            base,
            yaw,
            vy: -5.0,
            moving: false,
            firing: false,
            reloading: 0.0,
            throwing: None,
            throw_armed: true,
            range: -1.0,
            engaged: false,
            fire_range: (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0)),
            aim_v: 0.5,
            target: None,
            fight_end: None,
            search: 0.0,
            rounds: fire.rounds.0,
            burst: fire.bursts.0,
            loop_t: 0.0,
            delay_t: 0.0,
            muzzle: None,
            p: fire,
            route: Vec::new(),
            replan: 0.0,
            tea: 0.0,
            tea_given: false,
            spawned: false,
            // 0x00483250 drops a trigger without a marker tag ("%s setup to receive trigger, with no marker tag set")
            starts_in_formation: p.i("receive_trigger_activate", 0) == 0 || p.i("tower_marker_tag", 0) == 0,
            death_trigger: p.i("send_trigger_on_death", 0),
            damage_mult: (p.f("stig_damage_multiplier", 1.0), p.f("forge_damage_multiplier", 1.0)),
        last_health: f32::NAN,
        hurt_t: 99.0,
        hit: None,
        friendly: false,
        friendly_damage: None,
        attacker: None,
        dead: None,
        order: Order::Guard,
        angle: p.f("formation_angle", 20.0),
        linger: 0.0,
        lead: q,
        swap_t: 0.0,
        roman_dir: Vec3::ZERO,
        swap_timer: 0.25,
        goal: None,
        force_swap: false,
        regroup_req: false,
        speech: default(),
        last_shot: None,
        ic: false,
        pose: None,
        last_end: ("none", 0),
        fights: 0,
        support_reload: false,
        face_heading: false,
        return_req: false,
        order_engage: false,
        offer_t: None,
        roman_attacker: None,
        route_retry: 0.0,
        was_fighting: false,
        }
    }
}

impl Companion {
    /// Roman was hit by `by` (0x0047afa0 -> 0x00486df0): offered as his target by `companion_damage` (`retaliate`).
    pub fn roman_attacked(&mut self, by: Entity) {
        if self.dead.is_none() {
            self.roman_attacker = Some(by);
        }
    }

    /// The update's mode +0xb5c as the original numbers it (MolmerObject_Slot47 0x00485910): 1 idle in formation, 2
    /// moving to the formation point, 3 engaging (the tower task's 6 / 7 are ai/tower.rs's). Return To Me fights
    /// through the generic update in mode 1 (flags & 5 clear: 0x0043ee90). For the oracle trace and the partner tests.
    pub fn mode(&self) -> u8 {
        let fighting = self.target.is_some() || self.fight_end.is_some();
        match self.order {
            Order::Loose if self.moving && self.target.is_none() => 2,
            Order::Loose => 1,
            _ if fighting => 3,
            _ if self.moving => 2,
            _ => 1,
        }
    }

    /// The anchor +0xb70 of an anchored order (flag 1): Roman's position under Defend Me (flag 8 copies it each frame,
    /// 0x00485910), the order's point under Defend Area / Attack; none under Return To Me.
    fn anchor(&self, roman: Vec3) -> Option<Vec3> {
        match self.order {
            Order::Guard => Some(roman),
            Order::Loose => None,
            Order::Area { at, .. } | Order::Attack { at, .. } => Some(at),
        }
    }

    /// Whether he may engage a target at `p` within `range` of himself (Q111Object_Slot96 0x004853a0): refused farther
    /// (2D) than scan_distance from the anchor, then 0x0043ecf0: farther (2D) than `range` from `me`. (The attack
    /// order's own target always passes while it lives: callers skip it.)
    fn accepts(&self, p: Vec3, me: Vec3, roman: Vec3, range: f32) -> bool {
        self.anchor(roman).is_none_or(|a| (p - a).with_y(0.0).length() < self.scan) && (p - me).with_y(0.0).length() <= range
    }

    /// The attack order's held target +0xbb0, if the order stands.
    fn attack_target(&self) -> Option<Entity> {
        match self.order {
            Order::Attack { target, .. } => Some(target),
            _ => None,
        }
    }

    /// Q111Object_Slot94 0x00486da0: he declines `p` as a target beyond 2 x scan_distance (vt+0x180, Slot96) and on
    /// the tower task (modes 6 / 7). (The generic busy test 0x0042f8d0 has nothing to refuse in his states here.)
    fn declines(&self, p: Vec3, me: Vec3, roman: Vec3, on_task: bool) -> bool {
        on_task || !self.accepts(p, me, roman, 2.0 * self.scan)
    }

    /// A target offered at `p` (Q111Object_Slot109 0x00485360 -> GenericAI_OfferTarget 0x0042c820): ignored while the
    /// attack order's target lives (`attack_alive`); with a live target (at `held`) ignored within 10 s of the last
    /// offer taken (+0x514) or when that one is nearer (3D); else taken, the time stamped. `now` is the game time.
    fn offer(&mut self, cand: Entity, p: Vec3, me: Vec3, held: Option<Vec3>, attack_alive: bool, now: f32) {
        if attack_alive {
            return;
        }
        if let Some(h) = held {
            if self.offer_t.is_some_and(|t| now - t < 10.0) || (h - me).length_squared() < (p - me).length_squared() {
                return;
            }
        }
        self.target = Some(cand);
        self.fight_end = None;
        self.offer_t = Some(now);
    }

    /// The share of a projectile's damage it takes by the damage's class (GenericAIObject_Slot52 0x00430340: record
    /// flag 0x80000 x +0x8fc, 0x100000 x +0x900).
    pub fn damage_scale(&self, kind: crate::player::DamageKind) -> f32 {
        match kind {
            crate::player::DamageKind::Plain | crate::player::DamageKind::BaseTurret => 1.0,
            crate::player::DamageKind::Stig => self.damage_mult.0,
            crate::player::DamageKind::Forge => self.damage_mult.1,
            crate::player::DamageKind::Traitor => 0.0,
        }
    }

    /// A shot from its own side (the player's or the other companion's: GenericAIObject_Slot52 0x00430340, victim and
    /// source both vt+0xe4, the source not dead by vt+0xe0, ai_04.cpp:366-381): the friendly-fire time (+0xb48, the
    /// FriendlyFire bark) and the damage given back to its health, so no loss; the rest of the hit runs as for any
    /// damage, its reaction by the message's `damage` (no offer: the player is not one of its enemies, list +0x604).
    pub fn friendly_fire(&mut self, damage: f32) {
        self.friendly = true;
        self.friendly_damage = Some(self.friendly_damage.map_or(damage, |d| d.max(damage)));
    }

    /// Lets go of its target (the held attack target +0xbb0 and the current one).
    pub fn drop_target(&mut self) {
        self.target = None;
        self.engaged = false;
    }

    /// 0x00484290 on this companion (one half of the return order; also the tower task's failure): the held attack
    /// target dropped, Defend Me <-> Return To Me (an area / attack order -> Defend Me). Idle in formation (mode 1)
    /// the Defend Me / Return To Me toggle just stays; a new Defend Me keeps a target its range test passes (vt+0x180:
    /// the next search tick's anchor and 2 x scan_distance tests), Return To Me drops it. Otherwise he goes back to
    /// formation: the target dropped, the leader pick unless the partner idles (0x00483810 / 0x004837d0), the route to
    /// the formation point (0x00483d60(1, 0, 0)) and mode 2 with +0xb68 = 0.25 (`return_req`, run by `companions`).
    pub fn order_return(&mut self) {
        let idle = self.mode() == 1;
        let (order, stay) = match self.order {
            Order::Guard => (Order::Loose, idle),
            Order::Loose => (Order::Guard, idle),
            Order::Area { .. } | Order::Attack { .. } => (Order::Guard, false),
        };
        self.order = order;
        if order == Order::Guard && stay {
            // (the range test of the target kept, at once)
            self.search = 0.0;
        }
        if order == Order::Loose || !stay {
            self.drop_target();
            self.fight_end = None;
        }
        if !stay {
            self.route.clear();
            self.return_req = true;
        }
    }

    /// The target this companion fights (+0x588), if any.
    pub fn target(&self) -> Option<Entity> {
        self.target
    }

    /// Takes `t` as his target (vt+0x23c: a close kill's victim); the fight's +0xb68 rolled as at an engagement's start.
    pub fn engage(&mut self, t: Entity, rng: &mut FxStats) {
        if self.target.is_none() {
            self.linger = rng.range(3.0, 7.0);
        }
        self.target = Some(t);
        self.fight_end = None;
    }

    /// A stun or a carry is over (mode 5 / 10's end, 0x00485910 at 0x00486025): back to the mode it left, its target
    /// lost (0x0042bc60), standing.
    pub fn recover(&mut self) {
        self.target = None;
        self.engaged = false;
        self.firing = false;
        self.moving = false;
        self.route.clear();
        self.fight_end = None;
        self.pose = None;
    }

    /// The run speed to the formation point: fitted, the original's companions cover 37.3 units/s at move_speed 40 in
    /// formation (mode 2, its drive, move_power 100, not ported).
    pub fn formation_speed(&self) -> f32 {
        self.speed * 0.933
    }

    /// Sets out for the formation point `goal` (0x00483d60 with "move", `formation_route`): on success the route, the
    /// goal and mode 2 (moving, the 0.25 s re-pick timer +0xb68); false when there is no way (he keeps his mode). A
    /// failed try waits for the re-plan timer before the next (the engine's A* costs more than the original's grid).
    fn set_out(&mut self, physics: &Physics, me: Vec3, goal: Vec3, avoid: &[Vec3]) -> bool {
        if self.route_retry > 0.0 {
            return false;
        }
        match formation_route(physics, me, goal, avoid) {
            Some(route) => {
                self.route = route;
                self.replan = 0.25;
                self.goal = Some(goal);
                self.moving = true;
                self.swap_timer = 0.25;
                true
            }
            None => {
                self.route_retry = 0.25;
                if std::env::var("AIDBG").is_ok() {
                    let g = |v: Vec3| crate::level::game_to_engine().inverse() * v;
                    info!("AIDBG companion q={}: no way from {:?} to the formation point {:?}", self.q, g(me).round(), g(goal).round());
                }
                false
            }
        }
    }

    /// The reload anim's length (molmer_reload_br 3.47 s, q111_reload_br 1.97 s).
    fn reload_anim(&self) -> f32 {
        if self.q {
            1.967
        } else {
            3.467
        }
    }

    /// The run speed elsewhere: move_speed (the generic speeds 0x004297a0; the oracle lev_4_m2_tower: Jonesy's walk to
    /// the tower, mode 6, covers 2450 units in 3700 frames, 39.7 units/s at move_speed 40).
    pub fn run_speed(&self) -> f32 {
        self.speed
    }

    /// One frame of walking along the flat direction `dir` at `speed` (standing when `dir` is zero), with gravity: the
    /// walker body moves and the transform follows it.
    pub fn walk(&mut self, physics: &mut Physics, t: &mut Transform, dir: Vec3, speed: f32, dt: f32) {
        let speed = if dir != Vec3::ZERO { speed } else { 0.0 };
        let vel = dir * speed + Vec3::Y * self.vy;
        if let Some((feet, on)) = physics.move_walker(&self.walker, vel, dt) {
            t.translation = feet;
            self.vy = if on { -5.0 } else { (self.vy - 160.0 * dt).max(-200.0) };
        }
    }
}

/// The route to a formation point (0x00483d60 with "move"): the point must stand on walkable ground (its nav cell
/// 0x0046f320, a lookup in the plane: here a floor anywhere in its column below his height) and a route must lead there
/// with Roman's and the partner's places blocked for the search (0x0046ece0(.., 2, 1) / (.., 2, 0) round 0x0046f6d0:
/// `avoid`); None when either fails. A point within reach in the open needs no route search. The route is never empty:
/// its end is where the walk ends (0x00470c30). (Roman's ref bones need not lie on the ground: only their x / z
/// count.)
fn formation_route(physics: &Physics, me: Vec3, goal: Vec3, avoid: &[Vec3]) -> Option<Vec<Vec3>> {
    // down from just above his own height (as nav.rs samples), through the undersides of what overhangs
    let mut top = me.y + 20.0;
    let mut floor = false;
    for _ in 0..4 {
        match physics.raycast(goal.with_y(top), -Vec3::Y, 600.0) {
            Some(h) if h.2.y > 0.55 => {
                floor = true;
                break;
            }
            Some(h) => top = h.1.y - 0.5,
            None => break,
        }
    }
    if !floor {
        return None;
    }
    // a straight walk at knee height from `from` to the point (only x / z of the point count)
    let clear = |from: Vec3| {
        let (a, b) = (from + Vec3::Y * 5.0, goal.with_y(from.y + 5.0));
        physics.raycast(a, (b - a).normalize_or(Vec3::X), (b - a).length()).is_none()
    };
    // a point within reach in the open needs no route search: the point itself
    if (goal - me).with_y(0.0).length() < 9.0 && clear(me) {
        return Some(vec![goal]);
    }
    // else the grid's route, which ends at the nearest cell it reaches; the point itself last when open from there
    let mut route = crate::nav::path(physics, me, goal, avoid)?;
    if route.last().is_some_and(|&l| (goal - l).with_y(0.0).length() < 9.0 && clear(l)) {
        route.push(goal);
    }
    Some(route)
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        app.add_plugins((close_kill::CloseKillPlugin, carried::CarriedPlugin))
            .init_resource::<melee::CloseStats>()
            .add_message::<melee::CloseHit>()
            .add_systems(Update, (companion_damage, companions).chain().run_if(crate::playing));
    }
}

/// The companions' hits and death (no overrides of the generic handlers: GenericAIObject_Slot52 0x00430340,
/// Character_Die 0x004319e0; barks audio_02.cpp:197-267 (Q), audio_01.cpp:1545-1610 (Jonesy)):
/// - a player or teammate shot does no damage (same team) but draws the FriendlyFire line (shared delay);
/// - damage: the Damaged line under the shared delay Game+0xb44, else Hurt at most every 10 s (+0xb48); a reaction
///   anim by the damage (<= hit_level_1 20 *_hit_mgun, <= hit_level_2 51 *_hit_small, else *_hit_med) only while idle
///   or engaging (Q111Object_Slot129 0x00486e90: modes 1 / 3, so not on the move, GenericAIObject_Slot52 0x00430cf8)
///   and not during the tower task's IC anims (+0xa18 bit 0, 0x00426630); the attacker offered as a target unless
///   declined (Slot94 0x00486da0: on the tower task, modes 6 / 7, or out of reach, `declines`; then `offer`);
/// - Roman hit by a grunt (0x00486df0): not under the attack order (flag 4), only within 10 s of his last offered
///   target (+0x514), unless declined (Slot94): the target dropped and the grunt offered;
/// - death (permanent): the Dead line, *_die, score_value -500 to Accidents, its send_trigger_on_death (the AI's death
///   0x00431720 sends +0x528: lev_4_m2's Jonesy fails the mission), the body removed after the anim and the corpse
///   timer (0.5 s, all of it fading: corpses.rs).
/// The shared delay is vo_delay Game+0xb44 (range Game+0xb48 / +0xb4c = 30..45 s from level load, game_02.cpp:2057),
/// the one the grunts' Spotted / Fight cues use (Barks, Delay::Vo).
#[allow(clippy::too_many_arguments)]
fn companion_damage(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Companion, &Health, &Transform, Has<tower::OnTask>)>,
    grunts: Query<(&Grunt, &GlobalTransform)>,
    roman: Query<&GlobalTransform, With<crate::player::Roman>>,
    mut triggers: MessageWriter<crate::mission::SendTrigger>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    mut barks: ResMut<Barks>,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let roman_at = roman.single().map(|t| t.translation()).unwrap_or_default();
    // a live grunt's position (the enemy list +0x604: the companions fight grunts)
    let live = |e: Entity| grunts.get(e).ok().filter(|(g, _)| g.mind != Mind::Dead).map(|(_, t)| t.translation());
    let mut say = |sounds: &mut MessageWriter<crate::audio::PlaySound>, rng: &mut FxStats, list: &[&str], at: Vec3| {
        let n = list[((rng.rand() * list.len() as f32) as usize).min(list.len() - 1)];
        sounds.write(crate::audio::PlaySound::at(n, at));
    };
    for (e, mut c, h, t, on_task) in &mut q {
        let at = t.translation;
        if let Some(d) = c.dead.as_mut() {
            *d += dt;
            continue;
        }
        c.hurt_t += dt;
        if let Some((n, left)) = c.hit {
            c.hit = (left - dt > 0.0).then_some((n, left - dt));
        }
        if std::mem::take(&mut c.friendly) && barks.take(Delay::Vo, now, &mut rng) {
            let l: &[&str] = if c.q { &["q1150068_friendlyfire", "q1150069_friendlyfire", "q1150070_friendlyfire", "q1150071_friendlyfire", "q1150072_friendlyfire", "q1150073_friendlyfire"] } else { &["jon50076_friendlyfire", "jon50077_friendlyfire", "jon50078_friendlyfire", "jon50079_friendlyfire", "jon50080_friendlyfire"] };
            say(&mut sounds, &mut rng, l, at);
        }
        if c.last_health.is_nan() {
            c.last_health = h.0;
        }
        if h.0 <= 0.0 {
            c.dead = Some(0.0);
            c.hit = None;
            // the body: no more walking; its die anim (slot 0, charanim.rs), then the corpse timer removes it
            physics.remove_walker(&c.walker);
            commands.entity(e).insert(crate::corpses::Corpse::animated(0));
            let l: &[&str] = if c.q { &["q1150051_dead", "q1150052_dead", "q1150055_dead", "q1150056_dead", "q1150057_dead", "q1150058_dead", "q1150059_dead"] } else { &["jon50062_dead", "jon50063_dead", "jon50064_dead", "jon50065_dead", "jon50066_dead"] };
            say(&mut sounds, &mut rng, l, at);
            // not a kill (vt+0xe8 false); score_value -500 to Accidents (0x00431720), which also sends +0x528
            ms.killed(-500.0);
            if c.death_trigger != 0 {
                triggers.write(crate::mission::SendTrigger(c.death_trigger));
            }
            info!("EVENT {} died", if c.q { "Q" } else { "Jonesy" });
            continue;
        }
        let friendly_damage = c.friendly_damage.take();
        let hurt = (h.0 < c.last_health).then(|| c.last_health - h.0);
        if let Some(d) = hurt.or(friendly_damage) {
            if hurt.is_some() && barks.take(Delay::Vo, now, &mut rng) {
                let l: &[&str] = if c.q { &["q1150050_help", "q1150053_help", "q1150054_help", "q1150064_hurt", "q1150065_hurt", "q1150066_hurt", "q1150067_hurt"] } else { &["jon50060_help", "jon50061_help", "jon50070_hurt", "jon50071_hurt", "jon50072_hurt", "jon50073_hurt"] };
                say(&mut sounds, &mut rng, l, at);
            } else if hurt.is_some() && c.hurt_t >= 10.0 {
                c.hurt_t = 0.0;
                let l: &[&str] = if c.q { &["q1150060_hurt", "q1150061_hurt", "q1150062_hurt"] } else { &["jon50067_hurt", "jon50068_hurt"] };
                say(&mut sounds, &mut rng, l, at);
            }
            let (n, len) = if d <= 20.0 {
                ("hit_mgun", 0.3)
            } else if d <= 51.0 {
                ("hit_small", if c.q { 2.133 } else { 0.633 })
            } else {
                ("hit_med", if c.q { 2.8 } else { 1.967 })
            };
            // a small hit does not restart one already playing (states 0x12f / 0x132); none on the move or on the
            // tower task (Slot129 / 0x00426630 above)
            let reacts = c.mode() != 2 && !on_task && !c.ic;
            if reacts && !(n == "hit_mgun" && c.hit.is_some()) {
                c.hit = Some((n, len));
            }
            if let Some((a, p)) = c.attacker.take().and_then(|a| live(a).map(|p| (a, p))) {
                if !c.declines(p, at, roman_at, on_task) {
                    let attack_alive = c.attack_target().and_then(live).is_some();
                    let held = c.target.and_then(live);
                    c.offer(a, p, at, held, attack_alive, now);
                }
            }
        }
        c.attacker = None;
        // Roman's attacker (0x00486df0)
        if let Some((a, p)) = c.roman_attacker.take().and_then(|a| live(a).map(|p| (a, p))) {
            let recent = c.offer_t.is_some_and(|t| now - t < 10.0);
            if c.attack_target().is_none() && recent && !c.declines(p, at, roman_at, on_task) {
                c.drop_target();
                c.offer(a, p, at, None, false, now);
            }
        }
        c.last_health = h.0;
    }
}

/// The fire notes of one loop of the blended fire anims (seconds into the loop) and the loop's length: Jonesy's
/// molmer_fire_tl/tr/bl/br (0.3 s) each fire at 0; Q's q111_fire_bl (0.033, 0.467) and _br (0, 0.133, 0.3, 0.467).
const JONESY_NOTES: (&[f32], f32) = (&[0.0, 0.0, 0.0, 0.0], 0.3);
const Q_NOTES: (&[f32], f32) = (&[0.0, 0.033, 0.133, 0.3, 0.467, 0.467], 0.633);

#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn companions(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut rng: ResMut<FxStats>,
    data: Res<crate::gamedata::GameData>,
    (ctl, bot): (Res<crate::input::Controls>, Res<crate::bot::Bot>),
    roman: Query<(&Transform, &crate::player::Roman, &crate::skin::Animator, &Health), Without<Companion>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut q: Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), FreeCompanion>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Companion>>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: ResMut<crate::playtest::PlayState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut pickups, mut dynamite, (mut barks, voices), mut hud, mut pending, mut grabs, taken, mut force): (
        MessageWriter<crate::pickup::SpawnPickup>,
        MessageWriter<crate::sticky::ThrowDynamite>,
        (ResMut<Barks>, crate::audio::PlayingVoices),
        Option<ResMut<crate::hud::HudState>>,
        crate::levelreset::PerLevel<(bool, bool)>,
        MessageWriter<close_kill::Grab>,
        Query<(), (With<Companion>, Or<(With<carried::Knocked>, With<close_kill::Killing>)>)>,
        ResMut<AiForce>,
    ),
    (path, topsy, mounted, held_q): (
        Res<crate::campath::CameraPath>,
        Res<crate::topsy::Topsy>,
        Res<crate::turret::Mounted>,
        Query<&Companion, Or<(With<tower::OnTask>, With<carried::Knocked>, With<close_kill::Killing>)>>,
    ),
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let Ok((rt, r, ra, rh)) = roman.single() else { return };
    let bone = |a: &crate::skin::Animator, n: &str| a.skeleton.index.get(n).and_then(|&i| joints.get(a.joints[i]).ok()).map(|t| t.translation());
    // Roman's aim point (Game+0xa6c): where the crosshair ray meets the world, else far along it
    let aim_point = cam.single().ok().map(|c| {
        let (o, d) = (c.translation(), c.forward().as_vec3());
        physics.raycast(o + d * 20.0, d, 500.0).map(|h| h.1).unwrap_or(o + d * 520.0)
    });
    // the camera's own gate (0x00410150: a cinematic camera path playing, camera +0x17c == 1) and Fire held (button 8,
    // 0x00414810 at 0.5)
    let fire_held = (ctl.down(crate::input::Act::Fire) || ctl.pressed(crate::input::Act::Fire) || bot.fire_ok) && !r.dead && !path.active();
    // Roman's direction (0x00453150), flat
    let roman_dir = (Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * Vec3::Z).with_y(0.0).normalize_or(Vec3::Z);
    // the heading the companions turn to under Fire (0x00485580 / 0x00486525): Roman's, or on a mounted gun the gun's
    // (Game+0xacc while Game+0xac8 is set; here the camera's, which the gun's swivel carries)
    let roman_heading = match (mounted.0, cam.single()) {
        (Some(_), Ok(c)) => {
            let f = c.forward().as_vec3();
            f.x.atan2(f.z)
        }
        _ => r.yaw + std::f32::consts::PI,
    };
    // PlayerCanAct 0x0047a950: alive, no camera path (0x00410150), not in the topsy states (+0xa34 & 0x7f800000)
    // (its camera mode 2 test, 0x0040fab0: no setter of mode 2 found in the recovered code)
    let may_act = !r.dead && !path.active() && !topsy.active() && !hud.as_ref().is_some_and(|h| h.cinematic);
    // Q alive (the player's +0xad4, 0x00475d40, dropped only once dead: on the tower task or carried too)
    let q_alive = q.iter().any(|(_, c, ..)| c.q && c.dead.is_none()) || held_q.iter().any(|c| c.q && c.dead.is_none());
    team_orders(
        &mut q,
        !taken.is_empty(),
        pending.get(),
        &mut barks,
        hud.as_deref_mut(),
        (&mut sounds, &mut fx),
        &mut rng,
        (&physics, &data),
        &grunts,
        (ctl.pressed(crate::input::Act::TeamSend), ctl.pressed(crate::input::Act::TeamRecall)),
        may_act,
        cam.single().ok().map(|c| (c.translation(), c.forward().as_vec3())),
        aim_point,
        roman_dir,
        (now, q_alive),
    );
    // each one's target, for the partner's take-over (0x00485910 mode 3, Q111Object_Slot139 0x00485490)
    let pair_targets: Vec<(Entity, Option<Entity>)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(e, c, ..)| (e, c.target)).collect();
    // the pair as the leader pick sees it (entity, position, mode +0xb5c, its pick time); a pick updates its own entry
    let mut pair: Vec<(Entity, Vec3, u8, f32)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(e, c, t, ..)| (e, t.translation, c.mode(), c.swap_t)).collect();
    let mut lead_changes: Vec<(Entity, bool)> = Vec::new();
    let mut regroup_reqs: Vec<Entity> = Vec::new();
    let mut silent_return = false;
    for (e, mut c, mut t, ca, ch) in &mut q {
        // what the partner set on him earlier this frame (0x004837e0's lead flag, 0x004837b0's flag 0x80: set at once
        // in the original)
        if let Some(&(_, lead)) = lead_changes.iter().rev().find(|x| x.0 == e) {
            c.lead = lead;
        }
        lead_changes.retain(|x| x.0 != e);
        if regroup_reqs.contains(&e) && matches!(c.order, Order::Guard | Order::Loose) {
            c.regroup_req = true;
        }
        regroup_reqs.retain(|&x| x != e);
        // a dead companion does nothing; a hit reaction holds him (states 0x12f..0x131: movement stopped)
        if c.dead.is_some() || c.hit.is_some() {
            c.moving = false;
            c.firing = false;
            continue;
        }
        if c.range < 0.0 {
            c.range = rng.range(c.fire_range.0, c.fire_range.1);
        }
        let c = &mut *c;
        // at the start: on the formation point (Roman's ref_q_00 / ref_molmer_00), grounded (0x00455860); one that
        // waits for a tower trigger stays where the level placed it (0x004836b0: not while +0x52c is set)
        if !c.spawned && !c.starts_in_formation {
            c.spawned = true;
        }
        if !c.spawned {
            if let Some(slot) = bone(ra, if c.q { "ref_q_00" } else { "ref_molmer_00" }) {
                let feet = physics.raycast_landscape(slot + Vec3::Y * 1000.0, -Vec3::Y, 10000.0).unwrap_or(slot);
                physics.place_walker(&c.walker, feet);
                t.translation = feet;
                c.yaw = r.yaw + std::f32::consts::PI;
                // (and Roman's direction at that moment, +0xb7c: the first regroup's forced-pick test reads it)
                c.roman_dir = roman_dir;
                c.spawned = true;
            }
        }
        let me = t.translation;
        // Q's healthy tea (Q11Voice_PlayAmbient 0x004857e0, the first step of mode 1: idle in formation, not in a
        // flag-0x20 anim such as Fire's reload): once, when Roman's health is at most a quarter of its maximum (a dead
        // Roman's too: no other test) and he is within 50 units: a line (50 %: q1150174_Ambient_Spec, else
        // q1150026_Ambient at Q), the drop_tea anim (id 0x5a, 7.63 s), then (mode 9) "pickup_healthytea" thrown from
        // Q + 4 up toward Roman at normalise(2D direction, 0.7 up) x 40
        if c.q && c.tea > 0.0 {
            c.tea -= dt;
            if c.tea <= 0.0 {
                let to = (rt.translation - me).with_y(0.0).normalize_or_zero();
                let v = Vec3::new(to.x, 0.7, to.z).normalize() * 40.0;
                pickups.write(crate::pickup::SpawnPickup { name: "pickup_healthytea".into(), at: me + Vec3::Y * 4.0, vel: v, hover: false });
            }
            continue;
        }
        if c.q && !c.tea_given && c.mode() == 1 && !c.support_reload && rh.0 <= r.max_health * 0.25 && rt.translation.distance(me) < 50.0 {
            c.tea_given = true;
            c.tea = 7.633;
            c.moving = false;
            c.firing = false;
            if rng.rand() < 0.5 {
                sounds.write(crate::audio::PlaySound::ui("q1150174_Ambient_Spec"));
            } else {
                sounds.write(crate::audio::PlaySound::at("q1150026_Ambient", me));
            }
            continue;
        }
        // targets. In a fight (mode 3) while he has a target or its end is pending. A dead target is lost (the generic
        // update's 0x0042bc60: a new search at once, +0x490 = 0); nothing found, its fire counters decide how the fight
        // ends (`FightEnd`)
        let in_fight = c.was_fighting;
        if let Some(tg) = c.target {
            if grunts.get(tg).map_or(true, |(_, g, _, _)| g.mind == Mind::Dead) {
                c.target = None;
                c.search = 0.0;
                c.fight_end = Some(FightEnd::on_loss(c.reloading > 0.0 || c.rounds == 0));
            }
        }
        c.search -= dt;
        c.replan -= dt;
        c.route_retry -= dt;
        let live_at = |t: Entity| grunts.get(t).ok().filter(|(_, g, _, _)| g.mind != Mind::Dead).map(|(_, _, gt, _)| gt.translation());
        // at each search tick a held target is tested as Slot96 0x004853a0 does at 2 x scan_distance (vt+0x180 of the
        // generic fight): refused farther (2D) than scan_distance from the anchor or than 2 x scan_distance from him,
        // and dropped: the original's companions stand and fight until Roman has moved on (fightT: Jonesy lets go at
        // ~1350, Q at ~1440, then they run after him). The attack order's own target is kept while it lives (+0xbb0)
        if c.search <= 0.0 {
            if let Some(p) = c.target.filter(|&t| c.attack_target() != Some(t)).and_then(live_at) {
                if !c.accepts(p, me, rt.translation, 2.0 * c.scan) {
                    // (a live target he lets go of: the home route, back to formation as the original's do)
                    c.target = None;
                    c.fight_end = Some(FightEnd::Home);
                }
            }
            if c.target.is_some() {
                c.search = rng.range(0.75, 1.25);
            }
        }
        // (a playtest's ai_force "companions_hold": no target)
        if force.0.iter().any(|w| w == "companions_hold") {
            c.target = None;
        } else if c.target.is_none() && c.search <= 0.0 {
            c.search = rng.range(0.75, 1.25);
            // AI_FindTarget 0x00426920: the nearest live grunt within scan_distance, in the cone, in sight; then
            // refused by Slot96 when farther than scan_distance from the anchor (nothing taken that tick)
            let face = Quat::from_rotation_y(c.yaw) * Vec3::Z;
            c.target = grunts
                .iter()
                .filter(|(_, g, _, _)| g.mind != Mind::Dead)
                .map(|(ge, _, gt, _)| (ge, gt.translation()))
                .filter(|(_, p)| (*p - me).with_y(0.0).length() < c.scan)
                .filter(|(_, p)| (*p - me).with_y(0.0).normalize_or_zero().dot(face) > (95.0f32).to_radians().cos())
                .filter(|(ge, p)| {
                    let eye = me + Vec3::Y * 8.0;
                    let to = *p + Vec3::Y * 6.0 - eye;
                    physics.raycast(eye, to.normalize_or(Vec3::Y), to.length()).is_none_or(|(b, _, _)| physics.bodies.get(&b) == Some(ge))
                })
                .min_by(|a, b| a.1.distance(me).total_cmp(&b.1.distance(me)))
                .filter(|(_, p)| c.accepts(*p, me, rt.translation, f32::INFINITY))
                .map(|(ge, _)| ge);
            if std::env::var("AIDBG").is_ok() {
                if let Some((_, _, gt, _)) = c.target.and_then(|t| grunts.get(t).ok()) {
                    info!("AIDBG companion q={} targets grunt at {:?} d {:.0} me {:?} roman {:?}", c.q, gt.translation(), gt.translation().distance(me), me, rt.translation);
                }
            }
        }
        // nothing found in Defend Me / Defend Area (Q111Object_Slot139 0x00485490, run with the anchored flags out of
        // a fight): the partner's live target, when Slot96 accepts it at 2 x scan_distance (vt+0x23c)
        if c.target.is_none() && c.fight_end.is_none() && matches!(c.order, Order::Guard | Order::Area { .. }) && !force.0.iter().any(|w| w == "companions_hold") {
            let partners = pair_targets.iter().find(|(pe, _)| *pe != e).and_then(|(_, t)| *t);
            if let Some((t, _)) = partners.and_then(|t| live_at(t).map(|p| (t, p))).filter(|(_, p)| c.accepts(*p, me, rt.translation, 2.0 * c.scan)) {
                c.target = Some(t);
                info!("EVENT companions: {} joins the partner's fight", if c.q { "Q" } else { "Jonesy" });
            }
        }
        if c.target.is_some() {
            // a fight begins (mode 1 / 2 -> 3, 0x00486200 / 0x004866da): +0xb68 = 3..7 s (the attack order rolls its
            // own, `order_engage`); a new target in the same fight keeps what is left of it
            if !in_fight && !c.order_engage {
                c.linger = rng.range(3.0, 7.0);
            }
            if !in_fight {
                c.fights += 1;
            }
            c.fight_end = None;
        }
        // the attack order's new target (0x00484100): the close kill tried at once (0x00427e80), else +0xb68 rolled
        if std::mem::take(&mut c.order_engage) {
            if let Some(grab) = close_kill::attempt(c, e, me, &grunts, &joints, &physics, &mut rng, &mut force) {
                grabs.write(grab);
            } else {
                c.linger = rng.range(3.0, 7.0);
            }
        }
        // the fight's end (mode 3, 0x00486908): Home at once; Linger once idle (the reload over): the partner's live
        // target taken over (0x004265c0 true, vt+0x23c), else +0xb68 counted down; then back to formation (mode 2,
        // 0x00483d60(1, 0, 0))
        let mut back_to_formation = false;
        match c.fight_end {
            Some(FightEnd::Home) => {
                back_to_formation = true;
                c.last_end = ("home", c.last_end.1 + 1);
            }
            Some(FightEnd::Linger) if c.reloading <= 0.0 && c.throwing.is_none() => {
                let partners = pair_targets.iter().find(|(pe, _)| *pe != e).and_then(|(_, t)| *t);
                if let Some(t) = partners.filter(|t| grunts.get(*t).is_ok_and(|(_, g, _, _)| g.mind != Mind::Dead)) {
                    c.target = Some(t);
                    c.fight_end = None;
                    c.last_end = ("takeover", c.last_end.1 + 1);
                    info!("EVENT companions: {} takes over the partner's target", if c.q { "Q" } else { "Jonesy" });
                } else {
                    c.linger -= dt;
                    back_to_formation = c.linger <= 0.0;
                    if back_to_formation {
                        c.last_end = ("linger", c.last_end.1 + 1);
                    }
                }
            }
            _ => {}
        }
        if back_to_formation {
            c.fight_end = None;
        }
        // engage 0x00427910: a target beyond the rolled range +0x5d8 is approached (state 0x3c: every 0.2..0.4 s, in
        // range when within 0.9 x +0x5d8 with a clear ray); in range, the engage roll with allow 1: a class that can
        // throw (Molmer's vt+0x1e0; not Q) with flag 1 set throws TNT when rand 0..89 < 30, else fires (state 0xa).
        // The original's dump (fightT): Jonesy walks from 181 to 130 units and Q to 153 before firing
        let tdist = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0).length());
        let in_range = |c: &Companion| tdist.is_some_and(|d| d < c.range);
        // a target in an IC move (+0xa18 bit 0, 0x00426630) or held for a close kill (state 0xf) gets no TNT: the
        // engage's roll is overridden to 1000 (ai_02.cpp:1189-1197)
        let target_busy = |t: Option<Entity>| t.and_then(|tg| grunts.get(tg).ok()).is_some_and(|(_, g, _, _)| matches!(g.mind, Mind::Scripted | Mind::Held));
        match tdist {
            None => c.engaged = false,
            Some(d) if !c.engaged && d <= 0.9 * c.range => {
                c.engaged = true;
                // the engage (0x00427910) first tries the close kill (0x00427e80, close_kill.rs)
                if let Some(grab) = close_kill::attempt(c, e, me, &grunts, &joints, &physics, &mut rng, &mut force) {
                    grabs.write(grab);
                } else if !c.q && c.throw_armed && c.throwing.is_none() && rng.rand() * 90.0 < 30.0 && !target_busy(c.target) {
                    c.throwing = Some(0.0);
                }
            }
            _ => {}
        }
        let approach = c.target.filter(|_| !c.engaged).and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0));
        // formation: Roman's bone for this companion; under an area / attack order the point beside the anchor
        // (0x00483c30: the reversed direction turned by +-formation_angle, x formation_distance; the side is the one
        // of the companion's bone, guess for the leader flag 0x10 of 0x00483810)
        // the formation points (0x00483d60): the leader's and the other's
        let formation = |order: Order, angle: f32, distance: f32, lead: bool| match order {
            // 0x00483c30: the anchor's reversed direction turned by +-formation_angle (the leader's side +: the
            // rotation's sense is a guess), x formation_distance
            Order::Area { at, dir } | Order::Attack { at, dir, .. } => {
                let a = angle.to_radians() * if lead { 1.0 } else { -1.0 };
                at + Quat::from_rotation_y(a) * -dir * distance
            }
            _ => bone(ra, if lead { "ref_q_00" } else { "ref_molmer_00" }).unwrap_or(rt.translation),
        };
        let points = (formation(c.order, c.angle, c.distance, true), formation(c.order, c.angle, c.distance, false));
        // the leader pick 0x00483810: forced by an order (0x00484ca0), at the regroup's start, every 0.25 s on the
        // move and on arrival
        if std::mem::take(&mut c.force_swap) {
            leader_pick(c, e, me, true, &mut pair, &mut lead_changes, points, rt.translation, roman_dir, now);
        }
        let slot = if c.lead { points.0 } else { points.1 };
        let from_roman = (rt.translation - me).with_y(0.0).length();
        // playtest log: how far each companion is from Roman
        log.0.insert(if c.q { "q_dist".into() } else { "jonesy_dist".into() }, (from_roman.round() as i64).into());
        log.0.insert(if c.q { "q_pos".into() } else { "jonesy_pos".into() }, serde_json::json!([(me.x * 10.0).round() / 10.0, (-me.z * 10.0).round() / 10.0]));
        log.0.insert(if c.q { "q_health".into() } else { "jonesy_health".into() }, ch.map(|h| h.0).unwrap_or(0.0).into());
        let order = match c.order {
            Order::Guard => "defend_me",
            Order::Loose => "return",
            Order::Area { .. } => "defend_area",
            Order::Attack { .. } => "attack",
        };
        log.0.insert(if c.q { "q_order".into() } else { "jonesy_order".into() }, order.into());
        log.0.insert(if c.q { "q_lead".into() } else { "jonesy_lead".into() }, c.lead.into());
        // the fight (mode 3): "target" while he has one, "linger" while its end is pending, else "none"; and the way the
        // last one ended
        let fight = if c.target.is_some() { "target" } else if c.fight_end.is_some() { "linger" } else { "none" };
        log.0.insert(if c.q { "q_fight".into() } else { "jonesy_fight".into() }, fight.into());
        // seconds since his last shot (the banter's clock, Game+0xafc / +0xb0c), -1 before any
        log.0.insert(if c.q { "q_since_shot".into() } else { "jonesy_since_shot".into() }, c.last_shot.map_or(-1.0, |s| ((now - s) * 10.0).round() / 10.0).into());
        log.0.insert(if c.q { "q_fight_end".into() } else { "jonesy_fight_end".into() }, c.last_end.0.into());
        log.0.insert(if c.q { "q_fight_ends".into() } else { "jonesy_fight_ends".into() }, c.last_end.1.into());
        log.0.insert(if c.q { "q_fights".into() } else { "jonesy_fights".into() }, c.fights.into());
        // the regroup test (mode 1, 0x0048631c): the 2D distance from the anchor beyond formation_distance + 2.5
        // (Defend Me: Roman, whose position the anchor follows, flag 8; Defend Area / Attack: the order's point), from
        // Roman beyond delay_distance (Return To Me), or the partner's request (flag 0x80). (The original's intro, hook
        // dumps of World+0xe94: both set off within 0.5 s of Roman, Jonesy on Q's request, and run ~32 units behind him)
        let regroup = match c.order {
            Order::Guard => from_roman > c.distance + 2.5,
            Order::Loose => from_roman > c.delay,
            Order::Area { at, .. } | Order::Attack { at, .. } => (at - me).with_y(0.0).length() > c.distance + 2.5,
        };
        let partner = pair.iter().find(|p| p.0 != e).copied();
        // the partner in mode 1 (0x004837d0) holds this one's picks back on the move, on arrival and at a return
        let partner_idle = partner.is_some_and(|p| p.2 == 1);
        // Roman's and the partner's places, kept out of the route search (0x0046ece0 round 0x0046f6d0)
        let avoid: Vec<Vec3> = std::iter::once(rt.translation).chain(partner.map(|p| p.1)).collect();
        let point = |c: &Companion| if c.lead { points.0 } else { points.1 };
        // (mode 3 holds him while the fight's end is pending)
        let idle = c.target.is_none() && c.fight_end.is_none() && !c.moving;
        if idle && (regroup || c.regroup_req) {
            // the regroup's start (ai_08.cpp:1948-1961): the partner is asked to follow unless this regroup answers its
            // request (0x004837b0: flag 0x80, not taken under an area / attack anchor), then the pick, forced when Roman
            // faces more than 90 degrees away from his direction at the last one (dot < 0; not under an area / attack
            // order, flag 1 without 8), then the route to the formation point (0x00483d60(1, 0, 0))
            if !std::mem::take(&mut c.regroup_req) {
                if let Some(p) = partner {
                    regroup_reqs.push(p.0);
                }
            }
            if matches!(c.order, Order::Guard | Order::Loose) {
                let force = roman_dir.dot(c.roman_dir) < 0.0;
                leader_pick(c, e, me, force, &mut pair, &mut lead_changes, points, rt.translation, roman_dir, now);
            }
            c.set_out(&physics, me, point(c), &avoid);
        } else if c.target.is_none() && std::mem::take(&mut c.return_req) {
            // the return order (0x00484290): the pick unless the partner idles, the route
            if !partner_idle {
                leader_pick(c, e, me, false, &mut pair, &mut lead_changes, points, rt.translation, roman_dir, now);
            }
            c.set_out(&physics, me, point(c), &avoid);
        } else if back_to_formation {
            // the fight's end (mode 3 -> 2: 0x00483d60(1, 0, 0))
            c.set_out(&physics, me, point(c), &avoid);
        }
        // the attack order's target gone and the fight over: the first update with the held target +0xbb0 out of the
        // fight (Q111Object_Slot139 0x00485490, mode 1 / 2) drops it and the pair returns to Defend Me without a line
        // or a HUD text (0x004845b0(0)). (The countdown of +0xb68 at 0x004862d2 is reached only in a flag-0x20 anim,
        // which the attack order never plays.)
        if matches!(c.order, Order::Attack { .. }) && c.target.is_none() && c.fight_end.is_none() {
            silent_return = true;
        }
        // with a target they stand and fight (the original's dump, fightT: Q fires from (-232, 884) and Jonesy from
        // (-221, 865) without moving until they let go of the target)
        if c.target.is_some() && c.moving {
            c.moving = false;
            c.route.clear();
        }
        // on the move: the formation point of the last pick (`goal`)
        let goal = c.goal.filter(|_| c.moving).unwrap_or(slot);
        let to_slot = (goal - me).with_y(0.0);
        if c.moving && matches!(c.order, Order::Guard | Order::Loose) {
            // every 0.25 s on the move (+0xb68, ai_08.cpp:2033-2052)
            c.swap_timer -= dt;
            if c.swap_timer < 0.0 {
                c.swap_timer = 0.25;
                if !partner_idle {
                    leader_pick(c, e, me, false, &mut pair, &mut lead_changes, points, rt.translation, roman_dir, now);
                    // and the route to the new formation point (0x00483d60(1, 0, 1))
                    c.goal = Some(point(c));
                }
            }
        }
        if c.moving {
            // the route followed (0x00470aa0), re-planned every 0.25 s; none to be had: the walk ends where he stands
            // (0x00470c30 false -> mode 1)
            if c.replan <= 0.0 {
                c.replan = 0.25;
                match formation_route(&physics, me, goal, &avoid) {
                    Some(route) => c.route = route,
                    None => {
                        c.moving = false;
                        c.route.clear();
                    }
                }
            }
            while c.route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < 4.0) {
                c.route.remove(0);
            }
            // arrival (mode 2 -> 1, ai_08.cpp:2056-2081): the route used up (AIRoute_HasRoute 0x00470c30 false after
            // AIRoute_Update popped its last node), wherever that left him: a pick unless the partner idles, and +0xb68
            // = 3..7 s (an attack order's idle time). (A formation point he cannot reach, inside a building beside
            // Roman, ends the walk at the route's nearest reachable point instead of walking into the wall for good.)
            if c.moving && c.route.is_empty() {
                if !partner_idle {
                    leader_pick(c, e, me, false, &mut pair, &mut lead_changes, points, rt.translation, roman_dir, now);
                }
                c.linger = rng.range(3.0, 7.0);
                c.moving = false;
            }
        }
        // the run anim while approaching (moving is cleared again next frame while it has a target)
        if approach.is_some() {
            c.moving = true;
        }
        let to = approach.unwrap_or_else(|| c.route.first().map(|w| (*w - me).with_y(0.0)).unwrap_or(to_slot));
        // full move_speed while moving (the original's intro: 38 of 40 units/s at 32 units from Roman)
        let dir = to.normalize_or_zero();
        let speed = c.formation_speed();
        c.walk(&mut physics, &mut t, if c.moving || approach.is_some() { dir } else { Vec3::ZERO }, speed, dt);
        // what to shoot at: the target's ref_hit_00
        let target_pos = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, a)| a.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(gt.translation() + Vec3::Y * 6.0));
        // Roman's Fire held, in mode 1 with no target: 0x00485580 (run under Defend Me / Return To Me: flags 0x27 skip
        // the anchored orders) with his heading within 45 degrees of Roman's (0x005bcd04 / 0x005bcd00) plays the reload
        // set in place: no shots (flag 0x20; the fire counters rolled at its end, 0x00427600); and in any order
        // 0x00486525 turns him toward Roman's heading (flag 0x800, 0x00454e60 at rate 8) until he faces it
        let heading_off = (roman_heading - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
        let free = c.target.is_none() && c.fight_end.is_none() && !c.moving;
        if free && fire_held {
            c.face_heading = true;
            if heading_off.abs() <= 45f32.to_radians() && matches!(c.order, Order::Guard | Order::Loose) && c.reloading <= 0.0 && c.throwing.is_none() {
                c.reloading = c.reload_anim();
                c.support_reload = true;
            }
        }
        let shoot_at = if approach.is_some() { None } else { target_pos };
        // facing: the target, else the way it runs, else (flag 0x800) Roman's heading
        let face = target_pos.map(|p| (p - me).with_y(0.0)).filter(|v| v.length() > 0.1);
        if let Some(f) = face {
            c.yaw = f.x.atan2(f.z);
        } else if c.moving && dir != Vec3::ZERO {
            c.yaw = dir.x.atan2(dir.z);
        } else if c.face_heading && free {
            c.yaw += heading_off * (8.0 * dt).min(1.0);
            c.face_heading = heading_off.abs() > 1f32.to_radians();
        }
        t.rotation = Quat::from_rotation_y(c.yaw) * c.base;
        // the fire cycle
        let (notes, loop_len) = if c.q { Q_NOTES } else { JONESY_NOTES };
        if let Some(tt) = c.throwing {
            // molmer_throw_tnt (2.3 s): make_tnt 0.467 (the pistol hidden: not shown), FX fx_wpn_grenade_throw at 1.667
            // (its sound wpn_grenade_throw; started by the anim note, gameaudio.rs), Fire_tnt at 1.733: the dynamite from the hand (0x00428340); then the fire
            // cycle again (state 0x10's end: 50% fire, 50% close in first, not ported)
            c.firing = false;
            let now = tt + dt;
            let crossed = |n: f32| tt < n && now >= n;
            let hand = ca.and_then(|a| bone(a, "ref_hand_00")).unwrap_or(me + Vec3::Y * 8.0);
            if crossed(1.733) {
                c.throw_armed = false;
                if let Some((ge, _, gt, ga)) = c.target.and_then(|tg| grunts.get(tg).ok()) {
                    let point = ga.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(gt.translation() + Vec3::Y * 6.0);
                    let n = (rng.rand() * 7.0) as usize;
                    let joint = ga.and_then(|a| a.skeleton.index.get(&format!("ref_hit_0{n}")).map(|&i| a.joints[i]));
                    dynamite.write(crate::sticky::ThrowDynamite { from: hand, target: ge, joint, point });
                }
            }
            c.throwing = if now >= 2.3 || (c.target.is_none() && tt < 1.733) { None } else { Some(now) };
        } else if c.reloading > 0.0 {
            c.reloading -= dt;
            c.firing = false;
            if c.reloading <= 0.0 {
                c.support_reload = false;
                c.rounds = rng.range(c.p.rounds.0 as f32, c.p.rounds.1 as f32 + 0.999) as u32;
                c.burst = rng.range(c.p.bursts.0 as f32, c.p.bursts.1 as f32 + 0.999) as u32;
                // the counters rolled after the clip cycle set flag 1 again; then the engage roll (state 0x32's end)
                c.throw_armed = true;
                if !c.q && in_range(&*c) && rng.rand() * 90.0 < 30.0 {
                    c.throwing = Some(0.0);
                }
            }
        } else if let Some(at) = shoot_at.filter(|_| std::env::var("NOCOMPFIRE").is_err()) {
            let muzzle = me + Vec3::Y * 7.0 + Quat::from_rotation_y(c.yaw) * Vec3::Z * 3.0;
            let dz = at - muzzle;
            let deg = (dz.y / dz.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
            c.aim_v = (deg.clamp(-26.5, 26.5) + 26.5) / 53.0;
            if c.delay_t > 0.0 {
                c.delay_t -= dt;
                c.firing = false;
            } else {
                // the fire anim starting with a target (0x00428980 -> vt+0x13c, ai_02.cpp:1860): the Fight cue under
                // vo_delay (Q11Voice 0x004875d0: Q1150038 / Q1150040_Fight; AIVoice 0x0044eab0: JON50026_Fight,
                // JON50028 / JON50029_Taunt; equal odds), kept as his line (+0xb50) so the next speech test hears him
                if !c.firing && c.target.is_some() && barks.take(Delay::Vo, now, &mut rng) {
                    let l: &[&str] = if c.q { &["Q1150038_Fight", "Q1150040_Fight"] } else { &["JON50026_Fight", "JON50028_Taunt", "JON50029_Taunt"] };
                    let n = l[((rng.rand() * l.len() as f32) as usize).min(l.len() - 1)];
                    let req = c.speech.line(&voices, crate::audio::PlaySound::at(n, me));
                    sounds.write(req);
                }
                c.firing = true;
                let prev = c.loop_t;
                c.loop_t += dt;
                for &n in notes {
                    let hit = (prev == 0.0 && n == 0.0) || (n > prev && n <= c.loop_t);
                    if hit && c.burst > 0 && c.rounds > 0 {
                        c.burst -= 1;
                        // Jonesy: each molmer_fire_* anim has "fire" at 0 and "fire_alt" at 0.067; the four fire notes
                        // use up the burst of 4, then each fire_alt finds the burst empty and only wastes a round
                        // (0x0043d180, ai_05.cpp:1264): a clip of 24 rounds is 3 bursts of 4 shots, as the original's
                        // sound log and hook AI dump show (fightT)
                        c.rounds = c.rounds.saturating_sub(if c.q { 1 } else { 2 });
                        // his last-shot stamp (Game+0xafc / +0xb0c, before the aim error: Q111Object_Slot101 0x004878f0,
                        // 0x0044f160), read by the idle banter (player.rs)
                        c.last_shot = Some(now);
                        // aim error 0x004284e0 (as the grunts'): sideways in [-angle, angle], up in [0, height], scaled
                        // by the distance (from his own position, vt+0x54) between fire_accuracy_min and max
                        let to = (at - me).with_y(0.0);
                        let d = to.length();
                        let (lo, hi) = c.p.accuracy;
                        let sc = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
                        let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
                        let off = side * rng.range(-c.p.angle, c.p.angle) * sc + Vec3::Y * rng.range(0.0, c.p.height) * sc;
                        let aim = (at + off - muzzle).normalize_or(Vec3::Z);
                        let bullet = data.props(&c.p.bullet);
                        crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, muzzle, aim, true, Some(e));
                        if c.q {
                            // Q's muzzle fx runs for the burst (its machine-gun loop): one per burst
                            if c.muzzle.is_none() {
                                let m = commands.spawn((Transform::from_translation(muzzle), Visibility::default())).id();
                                c.muzzle = Some(m);
                                fx.write(crate::fx::StartFx { name: c.p.muzzle_fx.clone(), at: muzzle, rotation: Quat::IDENTITY, follow: Some(m), position_only: true });
                            }
                        } else if !c.p.muzzle_fx.is_empty() {
                            fx.write(crate::fx::StartFx { name: c.p.muzzle_fx.clone(), at: muzzle, rotation: Transform::IDENTITY.looking_to(aim, Vec3::Y).rotation, follow: None, position_only: false });
                        }
                    }
                }
                if c.loop_t >= loop_len {
                    c.loop_t = 0.0;
                    if c.burst == 0 || c.rounds == 0 {
                        c.burst = rng.range(c.p.bursts.0 as f32, c.p.bursts.1 as f32 + 0.999) as u32;
                        c.delay_t = rng.range(c.p.burst_delay.0, c.p.burst_delay.1);
                        if let Some(m) = c.muzzle.take() {
                            commands.entity(m).despawn();
                        }
                        // a burst's or the clip's end tries the close kill (0x00428980 -> 0x00427e80: ai_02.cpp:1942 / 1986)
                        if let Some(grab) = close_kill::attempt(c, e, me, &grunts, &joints, &physics, &mut rng, &mut force) {
                            grabs.write(grab);
                        }
                    }
                    if c.rounds == 0 {
                        // the reload anim (molmer_reload_br 3.47 s, q111_reload_br 1.97 s) and the clip delay
                        c.reloading = c.reload_anim() + rng.range(c.p.clip_delay.0, c.p.clip_delay.1);
                    }
                }
            }
            if let (Some(m), true) = (c.muzzle, c.q) {
                commands.entity(m).insert(Transform::from_translation(muzzle));
            }
        } else {
            c.firing = false;
            c.loop_t = 0.0;
            if let Some(m) = c.muzzle.take() {
                commands.entity(m).despawn();
            }
        }
        c.was_fighting = c.target.is_some() || c.fight_end.is_some();
    }
    // (the requests and lead flags set on one updated earlier this frame)
    for pe in regroup_reqs {
        if let Ok((_, mut c, ..)) = q.get_mut(pe) {
            if matches!(c.order, Order::Guard | Order::Loose) {
                c.regroup_req = true;
            }
        }
    }
    for (pe, lead) in lead_changes {
        if let Ok((_, mut c, ..)) = q.get_mut(pe) {
            c.lead = lead;
        }
    }
    // 0x004845b0(0): 0x00484290 on both (the order's target dropped, Defend Me, back to formation), no line, no text
    if silent_return {
        for (_, mut c, ..) in &mut q {
            if c.dead.is_none() {
                c.order_return();
            }
        }
        info!("EVENT companions: the attack is over, back to Defend Me");
    }
}

/// The leader pick 0x00483810 for companion `e` at `me`: the partner's newer pick (+0xb90) is taken over as it is;
/// else the one of the pair nearer the anchor (Roman, or an area / attack order's point; 3D) is `ebp`, and unless
/// forced nothing changes while the anchor is within delay_distance (+0xbb8) of it. Otherwise the leader's point `a`
/// (ref_q_00, or the formation point on the leader's side) and the other's `b` are compared from `ebp`: moving (mode 2)
/// by the absolute cosine of each direction with Roman's (the one less aligned goes ... as the original's compare:
/// `less` = |cos b| < |cos a|), idle by squared distance (`less` = a nearer); `ebp` leads when `less`, the other one
/// otherwise, the partner taking the opposite flag (0x004837e0). The pick time and Roman's direction are stored.
#[allow(clippy::too_many_arguments)]
fn leader_pick(
    c: &mut Companion,
    e: Entity,
    me: Vec3,
    force: bool,
    pair: &mut [(Entity, Vec3, u8, f32)],
    changes: &mut Vec<(Entity, bool)>,
    (a, b): (Vec3, Vec3),
    roman: Vec3,
    roman_dir: Vec3,
    now: f32,
) {
    c.roman_dir = roman_dir;
    let partner = pair.iter().find(|p| p.0 != e).copied();
    // (the pick time as the partner, updated later this frame, reads it: live in the original)
    let stamp = |pair: &mut [(Entity, Vec3, u8, f32)], t: f32| {
        if let Some(own) = pair.iter_mut().find(|p| p.0 == e) {
            own.3 = t;
        }
    };
    if let Some(p) = partner {
        if c.swap_t < p.3 {
            c.swap_t = p.3;
            stamp(pair, c.swap_t);
            return;
        }
    }
    let anchor = match c.order {
        Order::Area { at, .. } | Order::Attack { at, .. } => at,
        _ => roman,
    };
    let (mut ebp_e, mut ebp) = (e, me);
    if let Some(p) = partner {
        if (anchor - me).length_squared() > (anchor - p.1).length_squared() {
            (ebp_e, ebp) = (p.0, p.1);
        }
    }
    if force || (anchor - ebp).length_squared() > c.delay * c.delay {
        // an area / attack order's two points (0x00483c30, flag 1 without 8) are always compared by distance; Roman's
        // bones on the move (mode 2) by how far each is off his direction, idle by distance
        let anchored = matches!(c.order, Order::Area { .. } | Order::Attack { .. });
        let less = if c.moving && !anchored {
            let ca = (a - ebp).normalize_or_zero().dot(roman_dir).abs();
            let cb = (b - ebp).normalize_or_zero().dot(roman_dir).abs();
            cb < ca
        } else {
            (a - ebp).length_squared() < (b - ebp).length_squared()
        };
        c.lead = if less { ebp_e == e } else { ebp_e != e };
    }
    c.swap_t = now;
    stamp(pair, now);
    // the partner always takes the opposite flag, picked or not (0x00483be0 -> 0x004837e0)
    if let Some(p) = partner {
        changes.retain(|x| x.0 != p.0);
        changes.push((p.0, !c.lead));
    }
}

/// Picks one of `n` entries the way the voice code does: (rand() * range) >> 15, here range x a uniform [0, 1).
fn roll(rng: &mut FxStats, range: f32) -> u32 {
    (rng.rand() * range) as u32
}

/// The companions the formation update drives: not on the tower task (ai/tower.rs), not stunned or carried
/// (carried.rs, modes 5 / 10), not in a close kill (close_kill.rs).
type FreeCompanion = (Without<crate::player::Roman>, Without<tower::OnTask>, Without<carried::Knocked>, Without<close_kill::Killing>);

/// Team orders (0x0047a9a0 -> 0x004852b0 -> 0x00485120; docs/subsystems/companions.md). Key 1 / D-pad up
/// (TeammatesSend 0x12) and key 3 / D-pad down (TeammatesReturn 0x13) set the pending flags 0x2000 / 0x4000 of both
/// companions while Roman may act (0x0047a950: alive, not in a cinematic). The first of the pair to update handles
/// them for both, then clears both flags ("return" wins when both are set), unless one of them is busy with an
/// animation (modes 5 / 10: here Q's tea); a dead companion is dropped from the pair.
/// - Return (0x004845b0(1), 0x00484290 on each): Defend Me <-> Return To Me (an area / attack order -> Defend Me);
///   targets dropped, back to formation; a line under vo_delay; the HUD text "ui_teammate_defend_player" /
///   "ui_teammate_return".
/// - Send: a live grunt under the crosshair (0x00408020 of the aim) -> attack it (0x00484830 / 0x00484100: anchored
///   at it, its target, +0xb68 = 3..7 s), "ui_teammate_attack", the attack lines; else defend the aimed point
///   (0x00484ca0), "ui_teammate_defend_loc", the defend lines.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn team_orders(
    q: &mut Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), FreeCompanion>,
    busy: bool,
    pending: &mut (bool, bool),
    barks: &mut Barks,
    hud: Option<&mut crate::hud::HudState>,
    (sounds, fx): (&mut MessageWriter<crate::audio::PlaySound>, &mut MessageWriter<crate::fx::StartFx>),
    rng: &mut FxStats,
    (physics, data): (&Physics, &crate::gamedata::GameData),
    grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    (send, ret): (bool, bool),
    may_act: bool,
    ray: Option<(Vec3, Vec3)>,
    aim_point: Option<Vec3>,
    roman_dir: Vec3,
    (now, q_alive): (f32, bool),
) {
    if may_act {
        pending.0 |= send;
        pending.1 |= ret;
    }
    if !pending.0 && !pending.1 {
        return;
    }
    // (entity, is Q, position) of the live pair; the first is "self" (+0xbcc flag 0x200: Q)
    let live: Vec<(Entity, bool, Vec3)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(e, c, t, ..)| (e, c.q, t.translation)).collect();
    if live.is_empty() {
        *pending = (false, false);
        return;
    }
    // (either busy with an animation: modes 5 / 10, Q's tea, a close kill: the orders wait)
    if busy || q.iter().any(|(_, c, ..)| c.dead.is_none() && c.tea > 0.0) {
        return;
    }
    let (send, ret) = std::mem::take(pending);
    let pair = live.len() == 2;
    let self_q = live[0].1;
    let at_of = |is_q: bool| live.iter().find(|l| l.1 == is_q).map(|l| l.2);
    let say = |sounds: &mut MessageWriter<crate::audio::PlaySound>, n: &str, who: Option<bool>| {
        sounds.write(match who.and_then(at_of) {
            Some(p) => crate::audio::PlaySound::at(n, p),
            // Roman's lines play without a speaker (audio +0xd0)
            None => crate::audio::PlaySound::ui(n),
        });
    };
    let text: &'static str;
    if ret {
        let mut guard = false;
        for &(e, ..) in &live {
            if let Ok((_, mut c, ..)) = q.get_mut(e) {
                c.order_return();
                guard = c.order == Order::Guard;
            }
        }
        if barks.take(Delay::Vo, now, rng) {
            let generic = if pair {
                let v = roll(rng, 1000.0);
                if v < 800 {
                    true
                } else {
                    // 0x004845b0: Jonesy's or Q's line, each said by its own speaker
                    if v < 900 {
                        say(sounds, "jon25002_B_Return", Some(false))
                    } else {
                        say(sounds, "q1111007_B_Return", Some(true))
                    }
                    false
                }
            } else if roll(rng, 500.0) < 400 {
                true
            } else {
                say(sounds, if self_q { "q1111007_B_Return" } else { "jon25002_B_Return" }, Some(self_q));
                false
            };
            if generic {
                let l = ["rom50030_Return", "rom50031_Return", "rom50057_Return", "rom50058_Return"];
                say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], None);
            }
        }
        text = if guard { "ui_teammate_defend_player" } else { "ui_teammate_return" };
    } else {
        debug_assert!(send);
        // the object under the crosshair: a live grunt (the enemy list +0x604; object types 0x51 / 0x53 not ported)
        let target = ray
            .and_then(|(o, d)| physics.raycast_bullet(o + d * 20.0, d, 1000.0))
            .and_then(|(b, _, _)| physics.bodies.get(&b).copied())
            .and_then(|ge| grunts.get(ge).ok())
            .filter(|(_, g, _, _)| g.mind != Mind::Dead)
            .map(|(ge, _, gt, _)| (ge, gt.translation()));
        let (order, attack) = match (target, aim_point) {
            (Some((target, at)), _) => (Order::Attack { at, dir: roman_dir, target }, true),
            (None, Some(at)) => (Order::Area { at, dir: roman_dir }, false),
            (None, None) => return,
        };
        for (k, &(e, ..)) in live.iter().enumerate() {
            if let Ok((_, mut c, ..)) = q.get_mut(e) {
                c.order = order;
                c.route.clear();
                c.fight_end = None;
                // Defend Area re-picks the leader, forced, for the first of the pair (0x00484ca0 -> 0x00483810(1))
                c.force_swap = !attack && k == 0;
                // 0x00484100: a target not already his (+0x588) becomes his (vt+0x23c); the close kill is tried at
                // once, else +0xb68 is rolled (`order_engage`, run by `companions`); the one he fights is left as it is
                if let Some((ge, _)) = target.filter(|(ge, _)| c.target != Some(*ge)) {
                    c.engaged = false;
                    c.target = Some(ge);
                    c.order_engage = true;
                }
            }
        }
        // the order's marker at the defended point (0x00484ca0: Fx_Create of Game+0xd34, p_globalsettings
        // teammate_command_fx "fx_team_mates", at the position, flags 8)
        if let (false, Some(at)) = (attack, aim_point) {
            if let Some(name) = data.props("p_globalsettings").s("teammate_command_fx").filter(|n| !n.is_empty()) {
                fx.write(crate::fx::StartFx { name: name.to_owned(), at, rotation: Quat::IDENTITY, follow: None, position_only: false });
            }
        }
        if barks.take(Delay::Vo, now, rng) {
            // pair: half generic, a quarter Jonesy's, a quarter Q's; alone 70 % generic, else his own
            let who = if pair {
                match roll(rng, 400.0) {
                    v if v < 200 => None,
                    v if v < 300 => Some(false),
                    _ => Some(true),
                }
            } else if roll(rng, 1000.0) < 700 {
                None
            } else {
                Some(self_q)
            };
            match who {
                None if attack => {
                    // ROM50117..119 (with Q alive: the player's +0xad4, 0x00475d40, on the tower task or carried too)
                    // are not in lev_1_m1's bank: silent there, as in the original
                    let m = if q_alive { 600.0 } else { 299.0 };
                    let l = ["rom50027_Attack", "rom50032_Attack", "rom50034_Attack", "ROM50117_Attack", "ROM50118_Attack", "ROM50119_Attack"];
                    say(sounds, l[(roll(rng, m) / 100).min(5) as usize], None);
                }
                None => {
                    let l = ["rom50028_Defend", "rom50029_Defend", "rom50114_Defend", "rom50116_Defend"];
                    say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], None);
                }
                // the same lines for both orders (0x004849b5 / 0x00484c5a, 0x00484a5a / 0x00484be8)
                Some(false) => {
                    let l = ["jon50025_Send", "jon50131_Attack", "JON50033_Defend", "JON50130_Defend"];
                    say(sounds, l[(roll(rng, 400.0) / 100).min(3) as usize], Some(false));
                }
                Some(true) => {
                    let v = roll(rng, 400.0);
                    say(sounds, if v < 100 { "q1150039_Send" } else if v < 200 { "Q1150116_Defend" } else { "Q1150118_Defend" }, Some(true));
                }
            }
        }
        text = if attack { "ui_teammate_attack" } else { "ui_teammate_defend_loc" };
    }
    // 0x00463890 + flag 4: the order's text for 3.0 s, hiding the others (0x00467890); its 3 s are set only on its
    // first visible frame, so the same order again while it shows keeps counting down
    if let Some(h) = hud {
        let left = h.team.filter(|t| t.0 == text && t.1 > 0.0).map_or(3.0, |t| t.1);
        h.team = Some((text, left));
    }
    info!("EVENT team order {text}");
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 0x0042bc60 for a companion whose target died: the fire counters left (+0x484 flag 4) send him home at once,
    /// a spent clip (the reload in place, then idle) lets the partner's target or the +0xb68 linger decide.
    #[test]
    fn a_lost_target_ends_the_fight_by_the_clip() {
        assert_eq!(FightEnd::on_loss(false), FightEnd::Home);
        assert_eq!(FightEnd::on_loss(true), FightEnd::Linger);
    }

    fn grunt(props: serde_json::Value, kind: Kind) -> Grunt {
        let p = crate::gamedata::Props(props.as_object().cloned().unwrap_or_default());
        let data = crate::gamedata::GameData::default();
        make_grunt(&p, &data, kind, Vec3::ZERO, 0.0, &mut crate::fxvm::CrtRand::default())
    }

    /// Ai_InitGeneric 0x004247f0: a bad guy's max_health and health x Game+0x558 (very hard 2, easy 0.5), a hurt one
    /// in proportion; a difficulty chosen later (a saved game's) scales again from the property value.
    #[test]
    fn difficulty_scales_bad_guys_health() {
        use bevy::ecs::system::RunSystemOnce;
        let mut app = App::new();
        app.insert_resource(crate::gamedata::Difficulty(3));
        let fresh = app.world_mut().spawn((grunt(serde_json::json!({"max_health": 100.0}), Kind::Grunt), Health(100.0))).id();
        let hurt = app.world_mut().spawn((grunt(serde_json::json!({"max_health": 100.0}), Kind::Captain), Health(40.0))).id();
        app.world_mut().run_system_once(difficulty_health).unwrap();
        let health = |app: &App, e: Entity| (app.world().get::<Grunt>(e).unwrap().max_health, app.world().get::<Health>(e).unwrap().0);
        assert_eq!(health(&app, fresh), (200.0, 200.0));
        assert_eq!(health(&app, hurt), (200.0, 80.0));
        // not again on the next frame
        app.world_mut().run_system_once(difficulty_health).unwrap();
        assert_eq!(health(&app, fresh), (200.0, 200.0));
        // easy, set after the level's objects
        *app.world_mut().resource_mut::<crate::gamedata::Difficulty>() = crate::gamedata::Difficulty(0);
        app.world_mut().run_system_once(difficulty_health).unwrap();
        assert_eq!(health(&app, fresh), (50.0, 50.0));
        assert_eq!(health(&app, hurt), (50.0, 20.0));
    }

    /// fn_0042f870: receive_trigger_kill kills the AI whatever his health; trigger_no_damage (trigger_charge only)
    /// gives back what a hit took, but not that death.

    /// pickup_drop_specific: a weapon table pickup or a health pickup, matched without case; else the need-based pick.
    #[test]
    fn specific_drops() {
        assert_eq!(drop_specific("PICKUP_HEALTH_2"), Some("pickup_health_2"));
        assert_eq!(drop_specific("pickup_health_1"), Some("pickup_health_1"));
        assert_eq!(drop_specific("pickup_sticky_bombs"), Some("pickup_sticky_bombs"));
        assert_eq!(drop_specific("pickup_nothing"), None);
        assert_eq!(grunt(serde_json::json!({"pickup_drop_specific": "pickup_health_2"}), Kind::Q112).drop_specific, Some("pickup_health_2"));
        assert_eq!(grunt(serde_json::json!({"pickup_drop_specific": ""}), Kind::Grunt).drop_specific, None);
    }

    /// The voice set: captains keep Ai_InitGeneric's coin (two sets); grunts roll three ways.
    #[test]
    fn voice_sets_by_class() {
        let mut rng = crate::fxvm::CrtRand::default();
        let data = crate::gamedata::GameData::default();
        let p = crate::gamedata::Props::default();
        let mut seen = [[false; 3]; 2];
        for _ in 0..200 {
            seen[0][make_grunt(&p, &data, Kind::Captain, Vec3::ZERO, 0.0, &mut rng).voice_set as usize] = true;
            seen[1][make_grunt(&p, &data, Kind::Grunt, Vec3::ZERO, 0.0, &mut rng).voice_set as usize] = true;
        }
        assert_eq!(seen, [[true, true, false], [true, true, true]]);
    }

    /// 0x0043bd70 / 0x0043abc0: the forge cannon, mover death and tower climb ids; ids 8 / 9 on a base defence.
    #[test]
    fn the_grunt_tables_name_the_mover_ids() {
        let fl = AnimFlags::default();
        for k in [Kind::Grunt, Kind::Bow] {
            assert_eq!(anim_name_with(k, 105, 0.0, fl), "grunt_fcannon_sit");
            assert_eq!(anim_name_with(k, 106, 0.0, fl), "grunt_fcannon_fire");
            assert_eq!(anim_name_with(k, 128, 0.0, fl), "mover_grunt_mdie_2");
            assert_eq!(anim_name_with(k, 130, 0.0, fl), "mover_grunt_qdie_2");
            assert_eq!(anim_name_with(k, 139, 0.0, fl), "mover_grunt_tower_climb");
        }
        assert_eq!(anim_name_with(Kind::Grunt, 8, 0.0, fl), "grunt_run");
        assert_eq!(anim_name_with(Kind::Grunt, 9, 0.0, AnimFlags { base_defence: true, ..fl }), "grunt_run_no_sound");
        assert_eq!(anim_name_with(Kind::Bow, 8, 0.0, AnimFlags { base_defence: true, ..fl }), "grunt_bow_run");
    }

    /// 0x00437690 / 0x00437860: a captain's cue comes from his set's table, each line once before any repeats.
    #[test]
    fn captain_cues_deal_their_tables() {
        let mut barks = Barks::default();
        let mut rng = FxStats::default();
        let mut fight: Vec<&str> = (0..6).map(|_| barks.captain_line(CaptainCue::Fight, 0, &mut rng)).collect();
        fight.sort();
        assert_eq!(fight, ["CA150004_Fight", "CA150005_Fight", "CA150006_Fight", "CA150007_Fight", "CA150008_Fight", "CA150009_Fight"]);
        let mut spotted: Vec<&str> = (0..3).map(|_| barks.captain_line(CaptainCue::Spotted, 1, &mut rng)).collect();
        spotted.sort();
        assert_eq!(spotted, ["CA250014_Spotted", "CA250015_Spotted", "CA250016_Spotted"]);
        assert_eq!(barks.cue_line(CaptainCue::Spotted, Kind::Grunt, 0, &mut rng), "@grunt:Spotted:0");
    }
}
