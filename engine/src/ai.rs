//! Grunt AI ported from the recovered state machine (GenericAI_Update 0x0043ee90 and its helpers, documented in
//! docs/subsystems/grunt-ai.md), on the game's own per-instance parameters, for the classes lev_1_m1 makes (`Kind`:
//! GruntObject, BowGruntObject, CaptainObject) and the companions Q / Jonesy (`Companion`).
//! - The state +0x460 is `Mind` (GenericAI_SetState 0x004262c0); the anims by id through the class tables (`anim_name`),
//!   played by charanim.rs as PlayAnim 0x004262d0 (one) / PlayAnim4 0x00426480 (the four-pose sets) do.
//! - States: idle / roaming (1), guard (0x79, become guard 0x0042d0f0, its flag cleared by 0x0042cff0), wait for a
//!   trigger (0x8c; the trigger handler 0x0042f870 runs in mission.rs's dispatch), chase (0x3c), firing (0xa) with the
//!   fire cycle 0x00428980, reload (0x32), flee (0xc8), the marker splines (0x96 / 0x97, `SplineRun`), the wander
//!   (0x3d / 0x5b), home (0x50, after 0x0042bc60), the taunt (0x6e), the captain's grenade (0x10), the hit reactions
//!   (0x12f..0x132, 300..0x12e), the trenches (0xa0..0xa6), the mounted gun (0x7b / 0x7d / 0x7c), death.
//! - Targets: AI_FindTarget 0x00426920 (`search`: the noises of the player's shots, `Interests`, then the cone
//!   Object_IsInViewCone 0x00455d90 and the sight ray), the alert groups, retaliation 0x0042c820.
//! Functions this port does not cite are explained in docs/port/ai.csv (replaced by the engine, or not needed).

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
    /// state 0xc8
    Flee,
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
}

impl Kind {
    pub fn of(template: &str) -> Kind {
        match template {
            "grunt" => Kind::Bow,
            "captain" => Kind::Captain,
            _ => Kind::Grunt,
        }
    }
    /// vt+0x1f4: may take a trench (0x00429f00); the archers may not
    pub fn uses_trenches(self) -> bool {
        self != Kind::Bow
    }
    /// vt+0x1f0: may man a mounted gun (0x0042a100); the captains may not
    pub fn uses_guns(self) -> bool {
        self != Kind::Captain
    }
    /// vt+0x1e0: the close attack of the engage roll (0x00427910: anim 0x27, state 0x10), the captain's grenade
    pub fn close_attack(self) -> bool {
        self == Kind::Captain
    }
    /// vt+0x1fc: the fire spray from a trench (anim 0x68, state 0xa6)
    pub fn spray(self) -> bool {
        self == Kind::Captain
    }
    /// vt+0x200: the all-fours reaction when the health falls past allfours_pct (anim 0x35, state 300)
    pub fn allfours(self) -> bool {
        self == Kind::Captain
    }
    /// vt+0x1cc: the chase engages on the ray alone (state 0x3c); without it the target must first be within
    /// +0x5d8 x K(0x005bcc38) (the archers)
    pub fn chase_ray_only(self) -> bool {
        self != Kind::Bow
    }
    /// The weapon template held (0x004a0ef0's index -> the weapon table, game_01.cpp:600)
    pub fn weapon(self) -> &'static str {
        match self {
            Kind::Grunt => "grunt_rifle",
            Kind::Bow => "grunt_bow",
            Kind::Captain => "captain_mg",
        }
    }
}

/// The weapon template a grunt template holds (Kind::weapon): level.rs attaches its model to the hand.
pub fn weapon_template(template: &str) -> Option<&'static str> {
    matches!(template, "grunt" | "grunt_regular" | "captain").then(|| Kind::of(template).weapon())
}

/// The anim name for an anim id (the class's vt+0x1a4: GruntObject 0x0043bd70, BowGruntObject 0x0043abc0,
/// CaptainObject 0x0043b610; anim_01.cpp). `r` is a uniform 0..1 draw for the ids that pick by rand (bow run 10,
/// deaths 80..85, the idle 18 off a cold level); `cold` is Game+0x2708 == 1 (world 1: lev_1_m1). Ids the class has no
/// anim for give its error anim (the original's "grunt_anim_error"); ids 8 / 9 give grunt_run (grunt_run_no_sound only
/// in a cutscene, 0x0044e600).
pub fn anim_name(kind: Kind, id: u32, r: f32, cold: bool) -> &'static str {
    let idle18 = |k: Kind| -> &'static str {
        if cold {
            return if k == Kind::Captain { "captain_idle_shiver" } else { "grunt_idle_shiver" };
        }
        // this virtual again with (rand << 3 >> 15) + 0x13: 19..26
        anim_name(k, 19 + (r * 8.0) as u32 % 8, 0.0, cold)
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
            113 => "grunt_hang_into",
            114 => "grunt_hanging",
            115 => "grunt_blown_up_into",
            116 => "grunt_idle_base_1",
            117 => "grunt_idle_base_2",
            118 => "grunt_idle_base_3",
            125 => "grunt_scare",
            131 => "grunt_stig_mgun_fire_tl",
            132 => "grunt_stig_mgun_fire_tr",
            133 => "grunt_stig_mgun_fire_bl",
            134 => "grunt_stig_mgun_fire_br",
            135 => "grunt_stig_mgun_idle_tl",
            136 => "grunt_stig_mgun_idle_tr",
            137 => "grunt_stig_mgun_idle_bl",
            138 => "grunt_stig_mgun_idle_br",
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
            113 => "grunt_hang_into",
            114 => "grunt_hanging",
            115 => "grunt_blown_up_into",
            116 => "grunt_idle_base_1",
            117 => "grunt_idle_base_2",
            118 => "grunt_idle_base_3",
            125 => "grunt_scare",
            131 => "grunt_stig_mgun_fire_tl",
            132 => "grunt_stig_mgun_fire_tr",
            133 => "grunt_stig_mgun_fire_bl",
            134 => "grunt_stig_mgun_fire_br",
            135 => "grunt_stig_mgun_idle_tl",
            136 => "grunt_stig_mgun_idle_tr",
            137 => "grunt_stig_mgun_idle_bl",
            138 => "grunt_stig_mgun_idle_br",
            _ => "grunt_bow_anim_error",
        },
        // the rocket (+0x484 0x8000000) and jumper glide (state 0xbd) variants of ids 0..3 / 33..36 are not reached:
        // lev_1_m1's captains are neither (rocket_captain 0; generator 215 sets no num_jumper_captain)
        Kind::Captain => match id {
            0 => "captain_idle_tl",
            1 => "captain_idle_tr",
            2 => "captain_idle_bl",
            3 => "captain_idle_br",
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
            33 => "captain_fire_tl",
            34 => "captain_fire_tr",
            35 => "captain_fire_bl",
            36 => "captain_fire_br",
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
            // rand * 200 >> 15 < 100: the first (0x005b3be0), else the second (guess at which string is which)
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
            97 => "captain_fall",
            98 => "captain_parachute",
            101 => "captain_barricade_hide",
            102 => "captain_barricade_reload",
            103 => "captain_barricade_peek",
            104 => "captain_barricade_fire_spray",
            113 => "captain_hang_into",
            114 => "captain_hanging",
            115 => "captain_blown_up_into",
            116 => "captain_idle_base_1",
            117 => "captain_idle_base_2",
            118 => "captain_idle_base_3",
            _ => "captain_error_anim",
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
    /// aim weights +0x580 (0 left .. 1 right) / +0x584 (0 bottom .. 1 top)
    pub h: f32,
    pub v: f32,
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
    /// engagement range +0x5d8, drawn once from fire_range_min..max
    pub range: f32,
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
    /// his own entity: his bullets' owner (the damage record's source +0x2c)
    pub me: Option<Entity>,
    /// score_value: to the Kills money when he dies (Game+0x2724)
    pub score: f32,
    /// the source of the last damage taken (the hit message's +0x2c), offered as a target (0x0042c820)
    pub attacker: Option<Entity>,
    /// game time a target was last adopted by an offer (+0x514)
    pub offer_t: f32,
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
    pub left_rounds: i64,
    pub left_bursts: i64,
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
    pub last_hit: f32,
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
    pub ray_timer: f32,
    /// died by a hit of at least hit_ragdoll (blown away as a body) rather than a death anim
    pub ragdoll: bool,
    /// which death anim (grunt_die_1..6) when not ragdolled
    pub death_anim: u8,
    /// the death anim Character_Die picked instead of grunt_die_N (window, ledge)
    pub death_name: Option<&'static str>,
    /// ledge_death (+0x484 bit 31): falls off his ledge when he dies
    pub ledge_death: bool,
    /// a window or tower grunt (+0x488 & 0x200080: modes *_window / *_tower)
    pub window: bool,
    /// the last damage record had flag 0x80 (allow_cartoon_death: the sticky bomb's)
    pub cartoon: bool,
    /// chance in % of a pickup on death (pickup_drop_percent, default 100)
    pub drop_percent: f32,
    /// the trench held (+0x904) and its phase (states 0xa0..0xa5)
    pub cover: Option<Cover>,
    /// +0x484 0x20000: in the trench's slot (set on arrival, 0xa1; cleared on leaving, 0x004273a0 / 0x004274e0): the
    /// target search sees all round (AI_FindTarget ai_02.cpp:771), the clip's end and a lost line of sight duck
    /// (0xa2) instead of reloading or pursuing, and the death anim is 0x56 grunt_barricade_death (0x00427100)
    pub at_barricade: bool,
    /// +0x918 / +0x91c / +0x920: the covers left last (0x004273a0); the search skips the first two
    pub cover_history: [Option<Entity>; 3],
    /// debug (playtest `cover` step): run the hiding-spot search now, as at a reload's end (flag 4 set)
    pub force_hide: bool,
    /// +0x484 flag 4: the fire counters are rolled (0x00427600: at init, a reload's end, the trench's duck); cleared at a
    /// clip's end (0x00428980)
    pub fire_ready: bool,
    /// +0x484 0x200 (FindHidingSpot's a2, a hide marker's search): in the trench the breathing is not cut short by a
    /// target outside the slot's arc, and without a target the grunt peeks instead of standing up
    pub hold: bool,
    /// the mounted gun held (+0x898) and its state
    pub gun: Option<Gun>,
    pub stig: StigTable,
    /// the class (vtable): anim table, weapon, the yes / no slots
    pub kind: Kind,
    /// the rest stance: attention (anim 0xf, with the fidgets 0x00427330: attention_* modes at start) or ready (the
    /// stand set 2, 3, 0, 1 by PlayAnim4: ready_* modes, and every return to rest after a fight, which plays the set:
    /// 0x0042bc60, 0x7a, the reload's end, L_442860)
    pub attention: bool,
    /// the marker spline followed (+0x570 its head, +0x450 the index, +0x558..+0x55c the tag range): `None` while not
    /// on one; `started` once the walk to the head (state 0x96) arrived and the markers are followed (0x97)
    pub spline: Option<SplineRun>,
    /// +0x488 bit 1 marker_looping (wrap at the spline's end), bit 0 marker_patrol: read by Ai_InitGeneric
    pub looping: bool,
    /// +0x488 0x200: a troop of an alarm generator (0x0042dd50) charges the player when it has no marker tags
    pub charge: bool,
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

#[derive(Resource, Default)]
pub struct AiStats {
    pub ms: f32,
    pub alive: usize,
    pub dead: usize,
    pub shots: usize,
    pub enabled: bool,
    pub states: [usize; 16],
    /// grunts holding a target: the player, a companion (playtest log, for comparisons with the hook's AI dump)
    pub targets: [usize; 2],
    /// marker arrivals so far, taunts, grenades thrown (playtest log)
    pub markers: usize,
    pub taunts: usize,
    pub throws: usize,
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
}

const DISMAY: [&str; 16] = [
    "rom50039_dismay", "rom50040_dismay", "rom50062_dismay", "rom50174_dismay", "rom50202_dismay", "rom50210_dismay",
    "rom50045_ambient", "jon50023_dismay", "jon50048_dismay", "jon50049_dismay", "jon50117_dismay", "jon50118_dismay",
    "jon50119_dismay", "jon50174_dismay", "q1150168_dismay", "q1150115_dismay",
];

fn dismay(
    time: Res<Time>,
    mut km: ResMut<KillMeter>,
    mut rng: ResMut<FxStats>,
    companions: Query<(&Companion, &Transform, Option<&Health>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let dt = time.delta_secs();
    km.cooldown -= dt;
    if km.cooldown < 0.0 && km.meter >= 3.0 {
        km.cooldown = rng.range(30.0, 45.0);
        km.meter = 0.0;
        // a line whose speaker is there (Roman always; Jonesy / Q while alive): guess at 0x00493b70's choice
        let alive = |q: bool| companions.iter().find(|(c, _, h)| c.q == q && h.is_none_or(|h| h.0 > 0.0)).map(|(_, t, _)| t.translation);
        let pool: Vec<(&str, Option<Vec3>, bool)> = DISMAY
            .iter()
            .filter_map(|n| match &n[..3] {
                "rom" => Some((*n, None, true)),
                "jon" => alive(false).map(|p| (*n, Some(p), true)),
                _ => alive(true).map(|p| (*n, Some(p), true)),
            })
            .collect();
        if !pool.is_empty() {
            let (name, at, _) = pool[((rng.rand() * pool.len() as f32) as usize).min(pool.len() - 1)];
            sounds.write(match at {
                Some(p) => crate::audio::PlaySound::at(name, p),
                None => crate::audio::PlaySound::ui(name),
            });
        }
    }
    km.meter = (km.meter - dt * 0.5).max(0.0);
}

impl Plugin for AiPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(AiStats { enabled: true, ..default() })
            .init_resource::<KillMeter>()
            .init_resource::<Barks>()
            .add_message::<StickyStuck>()
            .init_resource::<GrenadeAssets>()
            .init_resource::<Interests>()
            .add_systems(Update, (interests, search, scare, think, die, dismay, log_cover, log_ai, grenades).chain().after(crate::fx::FxSet));
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
            Some(gn) => serde_json::json!({"phase": format!("{:?}", gn.phase), "shots": gn.shots, "h": gn.h, "v": gn.v, "firing": gn.firing}),
            None => serde_json::json!({"phase": "", "shots": 0}),
        },
    );
    state.0.insert("cover".into(), serde_json::json!({"held": held, "in_slot": in_slot, "going": going, "phase": phase, "firing": firing, "deaths": deaths, "slots": slots}));
}

/// The playtest state's `ai`: the live grunts by state, the reactions, spline runs, captains, grenades, noises, and the
/// grunt nearest Roman (its state, class, stance, reaction, whether it targets him).
fn log_ai(
    grunts: Query<(Entity, &Grunt, &GlobalTransform)>,
    roman: Query<(Entity, &GlobalTransform), With<Player>>,
    grenades: Query<(), With<Grenade>>,
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
        })
    });
    state.0.insert(
        "ai".into(),
        serde_json::json!({
            "live": live.len(),
            "minds": minds,
            "reacting": count(&|g| g.hit.is_some()),
            "long_hits": count(&|g| g.reacting == 4),
            "spline": count(&|g| g.spline.is_some()),
            "captains": count(&|g| g.kind == Kind::Captain),
            "archers": count(&|g| g.kind == Kind::Bow),
            "attention": count(&|g| g.attention),
            "targets_player": live.iter().filter(|(_, g, _)| g.target.is_some() && g.target == re).count(),
            "grenades": grenades.iter().count(),
            "noise": noise.list.len(),
            "markers": stats.markers,
            "taunts": stats.taunts,
            "throws": stats.throws,
            "near": near.unwrap_or_default(),
        }),
    );
}

/// Capsule of a character with the game's `collision_radius`, as tall as a grunt (9.8 units).
pub fn capsule(physics: &Physics, radius: f32) -> Shape {
    let k = physics.meters_per_unit;
    let r = radius.min(4.5) * 0.55;
    Shape::new_capsule((4.9 - r).max(0.5) * k, r * k).expect("capsule")
}

/// Height of the capsule's centre above the feet.
pub fn capsule_lift(_radius: f32) -> f32 {
    4.9 + 0.3
}

/// Ai_InitGeneric 0x004247f0 + the class init (GruntObject_Slot6 0x00434fb0, BowGruntObject_Slot6 0x004347b0,
/// CaptainObject_Slot6 0x00434cd0 with its slot 8 0x00434da0: the range rolled again, AI_ResolveStartObjects 0x00425e20):
/// parameters with the code's defaults, the weapon (the bow at ref_lhand 0xe, the rifle 0xf / the captain's machine gun
/// 0x10 at ref_rhand; a captain's +0x484 0x1000: the fire set loops, 0x00428250), the voice set, the mode's start state.
pub fn make_grunt(p: &crate::gamedata::Props, data: &crate::gamedata::GameData, kind: Kind, home: Vec3, yaw: f32, rng: &mut crate::fxvm::CrtRand) -> Grunt {
    let mode = p.s("mode").unwrap_or("attention_roaming").to_owned();
    let (rmin, rmax) = (p.f("fire_range_min", 200.0), p.f("fire_range_max", 350.0));
    // the weapon by class (0x004a0ef0's index into the weapon table); projectile speed x3 (Projectile_Load)
    let bow = kind == Kind::Bow;
    let weapon = data.props(&format!("p_{}", kind.weapon()));
    let proj = data.props(&format!("p_{}", weapon.s("projectile_name").unwrap_or("grunt_rifle_bullet")));
    let move_speed = p.f("move_speed", 46.0);
    let guard = mode.contains("guard") || mode.contains("window") || mode.contains("tower");
    let mind = if mode.starts_with("trigger") { Mind::WaitTrigger } else if guard { Mind::Guard } else { Mind::Roam };
    let max_health = p.f("max_health", 100.0);
    let pick = rng.rand() * 300 >> 15;
    Grunt {
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
        attacker: None,
        offer_t: f32::NEG_INFINITY,
        clips: (p.i("fire_num_clips_min", 1), p.i("fire_num_clips_max", 1)),
        rounds: (p.i("fire_num_rounds_min", 1), p.i("fire_num_rounds_max", 1)),
        bursts: (p.i("fire_num_bursts_min", 1), p.i("fire_num_bursts_max", 1)),
        burst_delay: (p.f("fire_burst_delay_min", 0.0), p.f("fire_burst_delay_max", 0.0)),
        clip_delay: (p.f("fire_clip_delay_min", 0.0), p.f("fire_clip_delay_max", 0.0)),
        // the exe reads fire_inaccuracy_angle (default 15), not the data's fire_inaccuracy_xy
        inaccuracy_angle: p.f("fire_inaccuracy_angle", 15.0),
        inaccuracy_height: p.f("fire_inaccuracy_height", 10.0),
        accuracy: (p.f("fire_accuracy_min", 50.0), p.f("fire_accuracy_max", 150.0)),
        infinite_accuracy: (p.f("fire_infinite_accuracy_min", 400.0), p.f("fire_infinite_accuracy_max", 800.0)),
        infinite: false,
        left_rounds: 0,
        left_bursts: 0,
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
        last_hit: 0.0,
        hit_level_1: p.f("hit_level_1", 10.0),
        hit_level_2: p.f("hit_level_2", 25.0),
        hit: None,
        fixed: None,
        hurt_voice_timer: 0.0,
        dead_said: false,
        // GruntObject_Slot6 (ai_04.cpp:2340): < 100 GR1 (+0x484 bit 21), < 200 GR3 (+0x488 bit 29), else GR2
        voice_set: if pick < 100 { 0 } else if pick < 200 { 2 } else { 1 },
        ray_timer: 0.0,
        ragdoll: false,
        death_anim: (rng.rand() % 6) as u8 + 1,
        death_name: None,
        ledge_death: p.i("ledge_death", 0) != 0,
        window: mode.contains("window") || mode.contains("tower"),
        cartoon: false,
        drop_percent: p.f("pickup_drop_percent", 100.0),
        cover: None,
        at_barricade: false,
        cover_history: [None; 3],
        force_hide: false,
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
        kind,
        // attention_* modes stand at attention (0x0042d0f0 with a0 = 1: anim 0xf), ready_* with the stand set; the
        // roaming default plays anim 0xf (vt+0x19c(0xf, 1), ai_01.cpp:2444)
        attention: !mode.starts_with("ready"),
        spline: None,
        looping: p.i("marker_looping", 0) != 0,
        charge: false,
        anim_id: 0,
        anim_left: 0.0,
        thrown: false,
        reacting: 0,
        hit_id: 0,
        throw_armed: true,
        long_hit_pct: p.f("long_hit_pct", 60.0),
        allfours_pct: p.f("allfours_pct", 20.0),
        priority_tags: (0, 0),
    }
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

fn bark(sounds: &mut MessageWriter<crate::audio::PlaySound>, category: &str, set: u8, at: Vec3) {
    sounds.write(crate::audio::PlaySound { name: voice(category, set), at: Some(at), follow: None, looped: Some(false), volume: 1.0 });
}

/// The "ai_interest" noises of the player (AIInterestObject, vtable 0x005a7820, in the list World+0xee0): posted by
/// 0x00404810 only for Roman's own projectiles (Projectile_GetOwner == Game+0x748): one following each projectile with
/// an fx_start (0x0047ee30: radius 200, timer 0, alive while the projectile is: the update 0x00404680 drops it when its
/// target is gone and the timer is 0) and one at its end point (0x0047ed40, at every end: radius 200, 2 s); a repeat of
/// the same owner and target only refreshes the entry (0x00404750 / 0x00404790). AI_FindTarget's step 2 takes the
/// owner (the player) when a grunt's ref_hit_00 lies within the radius (3D) and the player within 2 x scan_distance
/// (2D, vt+0x180 0x0043ecf0), with no cone or line-of-sight test: shots flying or landing near a grunt alert him.
#[derive(Resource, Default)]
pub struct Interests {
    list: Vec<Interest>,
    /// the projectiles in flight (tracked by player.rs's update), their last position (where their end interest goes)
    /// and owner
    flying: std::collections::HashMap<Entity, (Vec3, Option<Entity>, bool)>,
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
        let seen = self.flying.get(&e).is_some_and(|f| f.2);
        self.flying.insert(e, (pos, owner, seen));
    }
}

/// AIInterestObject_Update 0x00404680 and the posting of 0x00404810 for Roman's projectiles.
fn interests(time: Res<Time>, mut it: ResMut<Interests>, shots: Query<(Entity, &Transform, &crate::player::Projectile)>, roman: Query<Entity, With<crate::player::Roman>>) {
    let dt = time.delta_secs();
    let Ok(re) = roman.single() else { return };
    let it = &mut *it;
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
            match shots.get(f) {
                Ok((_, t, _)) => i.pos = t.translation,
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

/// AI_FindTarget 0x00426920 and the alert-group hand-over: every 0.75..1.25 s; the player inside hear_distance is
/// taken at once; otherwise within scan_distance, inside the 190-degree cone, with line of sight.
fn search(
    time: Res<Time>,
    mut rng: ResMut<FxStats>,
    physics: NonSend<Physics>,
    player: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), With<Player>>,
    companions: Query<(Entity, &GlobalTransform, Option<&crate::skin::Animator>), (With<Companion>, Without<Player>)>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform, &Body, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Grunt>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stats: ResMut<AiStats>,
    mut barks: ResMut<Barks>,
    noise: Res<Interests>,
) {
    let dt = time.delta_secs();
    let Ok((pe, _, _)) = player.single() else { return };
    stats.targets = [0, 0];
    for (_, g, _, _, _) in &grunts {
        if let Some(t) = g.target.filter(|_| g.mind != Mind::Dead) {
            stats.targets[if t == pe { 0 } else { 1 }] += 1;
        }
    }
    // the candidates (the AI's list +0x604: the player and his companions; the original's grunts take Jonesy and Q
    // too, hook AI dump fightT): each with its position and its ref_hit_00 (the point the sight ray aims at)
    let hit_point = |t: &GlobalTransform, a: Option<&crate::skin::Animator>| {
        a.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(t.translation() + Vec3::Y * 7.0)
    };
    let cands: Vec<(Entity, Vec3, Vec3)> =
        player.iter().chain(companions.iter()).map(|(e, t, a)| (e, t.translation(), hit_point(t, a))).collect();
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
                g.pending = None;
                if g.target.is_none() {
                    g.target = Some(tgt);
                    if g.mind == Mind::WaitTrigger {
                        g.mind = Mind::Roam;
                    }
                }
            } else {
                g.pending = Some((tgt, left - dt));
            }
        }
        g.search_timer -= dt;
        if g.search_timer > 0.0 {
            continue;
        }
        // a guard (0x0042ec20, every frame of state 0x79) releases its target each time the search timer runs out and
        // searches again: one that loses the cone or the line of sight loses its target within 0.75..1.25 s
        if g.guard && g.target.is_some() {
            g.target = None;
        }
        if g.target.is_some() {
            continue;
        }
        // waiting for a trigger: only trigger_los grunts look (state 0x8c)
        if g.mind == Mind::WaitTrigger && !g.trigger_los {
            continue;
        }
        g.search_timer = rng.range(0.75, 1.25);
        let me = t.translation();
        // AI_FindTarget's walk of +0x604: within scan_distance (2D), the closest that passes; the player inside
        // hear_distance is taken at once; otherwise inside the 190-degree cone (or within hear_distance) with line of
        // sight
        let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
        // its own ref_hit_00 (0x00428280 with "ref_hit_00"): where the sight ray starts (raised by 5)
        let own_hit = ga.and_then(|a| a.skeleton.index.get("ref_hit_00").and_then(|&i| joints.get(a.joints[i]).ok())).map(|j| j.translation()).unwrap_or(me + Vec3::Y * 6.0);
        let mut best: Option<(f32, Entity, bool)> = None;
        // step 2: a noise of the player's within its radius of the grunt's ref_hit_00 (3D), the player within 2 x
        // scan_distance (2D): taken first (no cone, no line of sight); the list below only replaces it when nearer
        if noise.list.iter().any(|i| i.pos.distance(own_hit) <= i.radius) {
            if let Some(&(ce, cpos, _)) = cands.iter().find(|c| c.0 == pe) {
                let d2 = (cpos - me).with_y(0.0).length();
                if d2 <= 2.0 * g.scan_distance {
                    best = Some((d2, ce, true));
                }
            }
        }
        for &(ce, cpos, chit) in &cands {
            let to = (cpos - me).with_y(0.0);
            let d2 = to.length();
            if d2 > g.scan_distance || best.is_some_and(|b| d2 > b.0) {
                continue;
            }
            if ce == pe && d2 < g.hear_distance {
                best = Some((d2, ce, true));
                break;
            }
            // at a barricade (+0x484 0x20000) the cone test is skipped (ai_02.cpp:771)
            let cone = to.normalize_or_zero().dot(fwd) >= 95f32.to_radians().cos() || d2 < g.hear_distance || g.at_barricade;
            if cone && sight_ray(&physics, own_hit, chit, &[gbody.id]) {
                best = Some((d2, ce, false));
            }
        }
        if let Some((d2, tgt, heard)) = best {
            // the Spotted cue: a new target when there was none (SetTarget 0x0042cc70 -> vt+0x144), under vo_delay
            // (30..45 s, from level load: the first fight has no line)
            if g.target.is_none() && barks.take(Delay::Vo, time.elapsed_secs(), &mut rng) {
                bark(&mut sounds, "Spotted", g.voice_set, me);
            }
            g.target = Some(tgt);
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam; // activate 0x0042f5c0 (charge/marker flags: not ported)
            }
            if std::env::var("AIDBG").is_ok() { info!("AIDBG spotted {:?} by {:?} at {:?} d {:.0} heard {} mode {:?}", tgt, ge, me, d2, heard, g.mind); }
            if g.alert_group != 0 {
                spotted.push((g.alert_group, tgt, ge));
            }
        }
    }
    for (group, tgt, who) in spotted {
        for (ge, mut g, _, _, _) in &mut grunts {
            if ge != who && g.alert_group == group && g.target.is_none() && g.pending.is_none() && g.mind != Mind::Dead {
                g.pending = Some((tgt, rng.range(0.25, 1.0)));
            }
        }
    }
}

/// Line of sight from a grunt's eye to the target's ref_hit_00 (+5 z, 0x00426fd0): nothing static in between.
/// Line of sight as 0x00426fd0: from the source raised by 5 (0x005bcbdc) to the target point (its ref_hit_00), true
/// when nothing is hit on the way; bodies in `skip` (the looker's own) are passed through.
pub fn sight_ray(physics: &Physics, from_hit: Vec3, target: Vec3, skip: &[BodyId]) -> bool {
    let mut from = from_hit + Vec3::Y * 5.0;
    let dir = (target - from).normalize_or(Vec3::Y);
    let mut left = (target - from).length();
    while let Some((b, at, _)) = physics.raycast(from, dir, left) {
        if !skip.contains(&b) {
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

pub fn line_of_sight(physics: &Physics, from_feet: Vec3, to_feet: Vec3) -> bool {
    let eye = from_feet + Vec3::Y * 8.0;
    let aim = to_feet + Vec3::Y * 7.0;
    let v = aim - eye;
    physics.raycast(eye + v.normalize_or(Vec3::Y) * 4.0, v.normalize_or(Vec3::Y), (v.length() - 8.0).max(0.1)).is_none()
}

/// Scare 0x0042fb60: a sticky bomb stuck within 25 units makes a grunt flee 100 units from the player for 3-5 s
/// (run_scared), then charge him; a guard plays grenade_stuck and the Sticky_Scream bark instead of fleeing.
fn scare(
    mut stuck: MessageReader<StickyStuck>,
    mut rng: ResMut<FxStats>,
    player: Query<&GlobalTransform, With<Player>>,
    mut grunts: Query<(Entity, &mut Grunt, &GlobalTransform)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut barks: ResMut<Barks>,
    time: Res<Time>,
) {
    let ppos = player.single().map(|p| p.translation()).unwrap_or_default();
    for s in stuck.read() {
        for (ge, mut g, t) in &mut grunts {
            if g.mind == Mind::Dead || t.translation().distance(s.0) >= 25.0 {
                continue;
            }
            // Scare 0x0042fb60: the grunt the bomb is stuck to says Sticky (death_delay; else General_Scream), the others
            // General_Scream, a guard without the bomb on him nothing (ai_03.cpp:2930-2943)
            if s.1 == Some(ge) {
                let cat = if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) { "Sticky_Scream" } else { "General_Scream" };
                bark(&mut sounds, cat, g.voice_set, t.translation());
            } else if !g.guard {
                bark(&mut sounds, "General_Scream", g.voice_set, t.translation());
            }
            if g.guard {
                continue;
            }
            let away = (t.translation() - ppos).with_y(0.0).normalize_or(Vec3::X);
            g.goal = t.translation() + away * 100.0;
            g.timer = rng.range(3.0, 5.0);
            g.mind = Mind::Flee;
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
    /// 0x004987a0: ai_can_use, no kickout time left (kickout not ported), no player on it (flag 8)
    usable: bool,
    reserved: Option<Entity>,
    ai: Option<Entity>,
    collider: Option<BodyId>,
}

/// The frame's cover objects.
struct Spots {
    trenches: Vec<TrenchView>,
    guns: Vec<GunView>,
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

/// The gunner's counters (0x00427600 with a kind-1 gun): the first time (flag 4 clear) clips 3..5, rounds, burst and no
/// pause; later rounds, burst and the clip pause; the burst delay cleared, the burst at most the rounds.
fn gun_roll(g: &mut Grunt, gn: &mut Gun, rng: &mut FxStats) {
    let int = |rng: &mut FxStats, (lo, hi): (i64, i64)| rng.range(lo as f32, hi as f32 + 0.999) as i64;
    let st = g.stig;
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

/// The gun's firing cycle 0x004292f0 (kind 1): the clip pause (idle), the burst delay, the fire set while rounds and
/// burst last; a spent burst re-rolls the burst and its delay; a spent clip counts the clips down (a pause between, flag
/// 4 cleared after the last). Returns firing.
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
        let st = g.stig;
        gn.burst = rng.range(st.bursts.0 as f32, st.bursts.1 as f32 + 0.999) as i64;
        gn.delay = rng.range(st.burst_delay.0, st.burst_delay.1);
        gn.burst = gn.burst.min(gn.rounds);
    } else {
        gn.clips -= 1;
        if gn.clips > 0 {
            gn.pause = rng.range(g.stig.clip_delay.0, g.stig.clip_delay.1);
        } else {
            g.fire_ready = false;
        }
    }
    gn.firing = false;
    false
}

/// The gunner's aim 0x00428fc0 (vt+0x190, window in degrees): the horizontal weight from the angle between the gun's
/// heading and the target (inside +-window: 1 - (angle + window) / (2 window), on target; beyond: 1 right or 0 left),
/// the vertical from the elevation acos(dz / len) - 90 (inside +-26.5: 1 - (e + 26.5) / 53; above: 1, below: 0, off
/// target); with flag 1 and on target set at once, else stepped by dt / (2 window * 0.0037037); no target: (0.5, 0.5),
/// flag 1 cleared, on target. (The flagged case's predicted target point, 0x004580a0, is not ported.)
fn gun_aim(gn: &mut Gun, from: Vec3, target: Option<Vec3>, window: f32, dt: f32) -> bool {
    let (th, tv, ok) = match target {
        None => {
            gn.snap = false;
            (0.5, 0.5, true)
        }
        Some(p) => {
            let d = p - from;
            let flat = d.with_y(0.0).normalize_or_zero();
            let ang = gn.fwd.cross(flat).y.atan2(gn.fwd.dot(flat)).to_degrees();
            let (th, mut ok) = if ang < -window {
                (1.0, false)
            } else if ang > window {
                (0.0, false)
            } else {
                (1.0 - (ang + window) / (2.0 * window), true)
            };
            let e = (d.y / d.length().max(1e-3)).clamp(-1.0, 1.0).acos().to_degrees() - 90.0;
            let tv = if e > 26.5 {
                ok = false;
                0.0
            } else if e < -26.5 {
                ok = false;
                1.0
            } else {
                1.0 - (e + 26.5) / 53.0
            };
            (th, tv, ok)
        }
    };
    if gn.snap && ok {
        gn.h = th;
        gn.v = tv;
        return ok;
    }
    let r = dt / (2.0 * window * 0.003_703_7);
    let step = |c: f32, t: f32| if t < c { (c - r).max(t) } else { (c + r).min(t) };
    gn.h = step(gn.h, th);
    gn.v = step(gn.v, tv);
    ok
}

/// Off a gun (0x004266b0): the gun's flags 4 / 0x10 cleared, the body back on its feet, +0x8000 / flag 1 cleared.
/// Returns the gun when the grunt was on it (its aim back to the centre: the corner set at 0.5, 0.5).
fn leave_gun(g: &mut Grunt, me: Entity, body: &mut Body, guns: &mut [GunView]) -> Option<Entity> {
    let gn = g.gun.take()?;
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
    } else if !history {
        g.cover_history = [None; 3];
    }
    g.at_barricade = false;
}

/// GenericAI_FindHidingSpot 0x0042a7b0 (vt+0x240) as lev_1_m1's grunts reach it: (a0 need_target, a1, a2 hold) = (1, 0,
/// 0) at the clip's end through 0x0042b8e0 and at a reload's end (state 0x32), (1, 1, 0) leaving a trench, (0, 0, 1) on
/// arriving at a marker whose action is hide (Marker_ProcessArrival 0x0044ba30; nothing found: AI_Wander 60..100):
/// - a clip's end (+0x484 flag 4 clear) with a target: 90 % (0x2d0000) the reload in place, the search skipped
///   (0x004531e0(target), true for an airborne target, taken false: guess);
/// - pickup_grab_pct (+0x86c, default 1) % 0x0042a500, a grab of a pickup: not ported;
/// - a held cover is left (vt+0x188); need_target without a target: nothing found;
/// - the candidates: the nearest free mounted gun (0x0042a100, `pick_gun`; docs/subsystems/turret.md) and the
///   nearest trench (0x00429f00, `pick_trench`); a trench nearer than the gun wins; then the generic cover slots of the
///   list World+0xed4 (kinds 1 ground / 6 enter of objects other than type 3), which lev_1_m1 has none of (its
///   barracks have only ref_exit / ref_spawn nodes; the trenches are type 3, skipped there);
/// - a trench: the path to its ref_exit must exist (0x0046f6d0 == 1: a straight line or a nav route here), then the slot
///   is taken (vt+0x158) and state 0xa0 starts; +0x484 0x200 = hold (a2);
/// - nothing: flag 4 set -> pursue (vt+0x184), else the reload in place (state 0x32).
#[allow(clippy::too_many_arguments)]
fn find_hiding_spot(g: &mut Grunt, me: Entity, (need_target, hold): (bool, bool), pos: Vec3, target: Option<(Vec3, Vec3)>, spots: &mut Spots, physics: &Physics, own: BodyId, rng: &mut FxStats) -> Hide {
    let flag4 = g.fire_ready;
    if !flag4 && target.is_some() && rng.rand() < 0.9 {
        return Hide::Reload;
    }
    leave_cover(g, me, &mut spots.trenches, true);
    g.hold = false;
    let fail = if flag4 { Hide::Pursue } else { Hide::Reload };
    // the two candidates by 2D squared distance from the grunt: a trench nearer than the gun wins, else the gun
    let gi = pick_gun(g, pos, target, need_target, &spots.guns, physics, own);
    let ti = pick_trench(g, me, pos, target, need_target, &spots.trenches, physics, own);
    let d2 = |p: Vec3| (p - pos).with_y(0.0).length_squared();
    let ti = match (gi, ti) {
        (Some(gi), Some(ti)) => (d2(spots.trenches[ti].origin) < d2(spots.guns[gi].origin)).then_some(ti),
        (_, t) => t,
    };
    if let (Some(i), None) = (gi, ti) {
        // the gun: the path to it (0x0046f6b0 with the gun's collision off; here to ref_grunt_exit: guess), reserved
        // (vt+0x15c(1)), state 0x7b
        let gv = &spots.guns[i];
        if !line_of_sight(physics, pos, gv.exit) && crate::nav::path(physics, pos, gv.exit, &[]).is_none() {
            return fail;
        }
        spots.guns[i].reserved = Some(me);
        let gv = &spots.guns[i];
        g.gun = Some(Gun {
            turret: gv.e,
            phase: GunPhase::ToGun,
            origin: gv.origin,
            exit: gv.exit,
            yaw: gv.yaw,
            fwd: gv.fwd,
            lift: Vec3::ZERO,
            timer: 0.0,
            h: 0.5,
            v: 0.5,
            clips: 0,
            rounds: 0,
            burst: 0,
            pause: 0.0,
            delay: 0.0,
            firing: false,
            anim_t: 0.0,
            snap: false,
            shots: 0,
        });
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

/// The reload set's length (anims 0x2a, 0x2b, 0x28, 0x29 by PlayAnim4: the engine times the one it plays):
/// grunt_reload_br 1.133 s, grunt_bow_reload_* 1.3 s, captain_reload_* 2.3 s.
fn reload_len(kind: Kind) -> f32 {
    match kind {
        Kind::Grunt => 1.133,
        Kind::Bow => 1.3,
        Kind::Captain => 2.3,
    }
}

/// The state the hiding-spot search's answer starts (0x0042a7b0): a trench (0xa0), a gun (0x7b), the reload in place
/// (anims 0x2a.., state 0x32), or the pursuit (vt+0x184 0x004295d0, state 0x3c; without a target it ends in 0x0042bc60
/// at once: home).
fn hide_to_mind(g: &mut Grunt, h: Hide) -> Mind {
    match h {
        Hide::Cover => Mind::Cover,
        Hide::Gunner => Mind::Gunner,
        Hide::Reload => {
            g.pause = reload_len(g.kind);
            Mind::Reload
        }
        Hide::Pursue => Mind::Chase,
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
        g.pause = reload_len(g.kind);
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
fn wander_to(g: &mut Grunt, from: Vec3, to: Vec3, speed: f32) {
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
fn follow_spline(g: &mut Grunt, markers: &crate::alarm::Markers, tags: (i64, i64), r: f32) {
    match markers.pick_spline(tags, r) {
        Some(tag) => {
            g.spline = Some(SplineRun { tag, index: 0, started: false });
            g.mind = Mind::ToMarker;
        }
        None => {
            g.spline = None;
        }
    }
}

/// Marker_ProcessArrival 0x0044ba30 at the grunt's current marker (index `marker` into `Markers::markers`): its trigger
/// (Object_SendTrigger 0x00461870), its alarm (alarm.rs: fx_siren and the runner's Help bark, once), then by its flags:
/// hide (vt+0x240(0, 0, 1), else AI_Wander 60..100), ready_guard / attention_guard (0x0042d0f0: a guard there, stand set
/// or attention), trigger_charge (0x0042ce70(0): the player), trigger_charge_tag (lev_1_m1 has none: taken as the plain
/// charge), ic_* (none on lev_1_m1: stands), a positive scatter_min (AI_Wander scatter_min..max); else the next marker:
/// the spline switches when the marker names marker_next_min..max (AIMarker_NextTag 0x0044b830), else index + 1, at the
/// end wrapping with marker_looping (+0x488 bit 1) or stopping: the stand set and state 1 with home = here
/// (ai_06.cpp:1797-1817). (The marker flags kill / deactivate: none on lev_1_m1.) Returns the state it starts, or None
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
    (arrive, triggers): (&mut MessageWriter<crate::alarm::MarkerArrival>, &mut MessageWriter<crate::mission::SendTrigger>),
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
    let speed = g.move_speed;
    let stand = |g: &mut Grunt| {
        g.spline = None;
        g.home = pos;
        g.attention = false;
        Some(Mind::Roam)
    };
    if mk.flags & crate::alarm::Marker::HIDE != 0 {
        g.spline = None;
        let h = find_hiding_spot(g, me, (false, true), pos, tgt, spots, physics, own, rng);
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
        // 0x0042ce70(0): the player as target (unless one is held), the pursuit, +0x490 = 1.5..2.0 s
        g.spline = None;
        g.target = g.target.or(player);
        g.search_timer = rng.range(1.5, 2.0);
        g.attention = false;
        return Some(Mind::Chase);
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
        return None;
    }
    // another spline (0x0044c030 + 0x00426640): its first marker; a missing one is logged ("Marker : Switching to
    // spline that does not exists") and the run stays where it is
    if markers.spline_len(next) > 0 {
        g.spline = Some(SplineRun { tag: next, index: 0, started: true });
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
        let h = find_hiding_spot(g, me, (true, false), pos, tgt, spots, physics, own, rng);
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
            g.hit = Some((anim_name(g.kind, id, rng.rand(), true).to_owned(), 4));
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
            g.hit = Some((anim_name(g.kind, 0x36, 0.0, true).to_owned(), 6));
            g.reacting = 6;
            g.anim_left = rng.range(3.0, 7.0);
            None
        }
        6 => {
            let n = anim_name(g.kind, 0x38, 0.0, true);
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

/// The captain's grenade model (template captain_grenade: grunt_grenade_1), loaded with the level (level.rs).
#[derive(Resource, Default)]
pub struct GrenadeAssets(pub Vec<(Handle<Mesh>, Handle<StandardMaterial>)>);

/// A thrown captain_grenade (GrenadeProjectile, vtable 0x005a5010): launched by 0x00482870 at the target point pulled
/// back 20 units toward the thrower, with the projectile's speed (p_captain_grenade speed x 3, Projectile_Load) under
/// g = 64.348 (0x00471a00: the lower of the two arcs, guess); the generic projectile update (0x0047e580) ray-casts its
/// path and the first hit (0x0047eae0) ends it: area damage (AreaDamage_Apply 0x0047f020: area_damage 30, radius
/// 10..30, impact_force 15000), the direct damage to the object hit (50), the end fx (fx_end fx_imp_sticky), gone.
/// Its lifetime (15 s) ends it unexploded (guess: 0x004820b0 then still applies the area damage).
#[derive(Component)]
pub struct Grenade {
    vel: Vec3,
    life: f32,
    owner: Entity,
}

fn throw_grenade(commands: &mut Commands, assets: Option<&GrenadeAssets>, data: &crate::gamedata::GameData, from: Vec3, target: Vec3, owner: Entity) {
    let p = data.props("p_captain_grenade");
    let speed = p.f("speed", 35.0) * 3.0;
    // the target point pulled back toward the thrower by 20 when farther than 20 (0x00482870)
    let d = target - from;
    let to = if d.length() > 20.0 { from + d.normalize() * (d.length() - 20.0) } else { target };
    let d = to - from;
    let h = Vec2::new(d.x, d.z).length();
    let g = 64.348f32;
    let (v2, y) = (speed * speed, d.y);
    let disc = v2 * v2 - g * (g * h * h + 2.0 * y * v2);
    let flat_dir = Vec3::new(d.x, 0.0, d.z).normalize_or(Vec3::Z);
    let vel = if h > 1e-3 && disc >= 0.0 {
        let a = ((v2 - disc.sqrt()) / (g * h)).atan();
        flat_dir * speed * a.cos() + Vec3::Y * speed * a.sin()
    } else {
        // out of reach: thrown at 45 degrees
        (flat_dir + Vec3::Y).normalize() * speed
    };
    let e = commands
        .spawn((Transform::from_translation(from).with_rotation(crate::level::game_to_engine()), Visibility::default(), Grenade { vel, life: p.f("lifetime", 15.0), owner }))
        .id();
    if let Some(a) = assets {
        for (mesh, mat) in &a.0 {
            let c = commands.spawn((Mesh3d(mesh.clone()), MeshMaterial3d(mat.clone()))).id();
            commands.entity(e).add_child(c);
        }
    }
    info!("EVENT ai: captain threw a grenade");
}

/// The grenades' flight and end (see Grenade).
#[allow(clippy::too_many_arguments)]
fn grenades(
    mut commands: Commands,
    time: Res<Time>,
    physics: NonSend<Physics>,
    data: Res<crate::gamedata::GameData>,
    mut q: Query<(Entity, &mut Grenade, &mut Transform)>,
    mut health: Query<&mut Health>,
    mut blasts: MessageWriter<crate::fx::Explode>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    roman: Query<(Entity, &Transform, &crate::player::Roman), Without<Grenade>>,
) {
    let dt = time.delta_secs();
    let p = data.props("p_captain_grenade");
    // Roman is no physics body (a walker): the path passing within 4.5 of his centre hits him (as the bullets'
    // fallback without his collision parts, player.rs)
    let roman = roman.single().ok().map(|(e, t, r)| (e, t.translation + Vec3::Y * r.walker.lift));
    for (e, mut gr, mut t) in &mut q {
        gr.life -= dt;
        gr.vel.y -= 64.348 * dt;
        let step = gr.vel * dt;
        let from = t.translation;
        let mut hit = None;
        let mut start = from;
        let mut rest = step.length();
        let dir = step.normalize_or(Vec3::NEG_Y);
        while rest > 0.0 {
            let Some((b, at, _)) = physics.raycast(start, dir, rest) else { break };
            let obj = physics.bodies.get(&b).copied();
            // the thrower's own body is in the projectile's ignore chain (+0x574)
            if obj == Some(gr.owner) {
                rest -= at.distance(start) + 0.05;
                start = at + dir * 0.05;
                continue;
            }
            hit = Some((at, obj));
            break;
        }
        if let Some((re, c)) = roman {
            let u = ((c - from).dot(step) / step.length_squared().max(1e-6)).clamp(0.0, 1.0);
            let at = from + step * u;
            if at.distance(c) < 4.5 && hit.is_none_or(|(h, _)| h.distance(from) > at.distance(from)) {
                hit = Some((at, Some(re)));
            }
        }
        if hit.is_none() && gr.life > 0.0 {
            t.translation = from + step;
            continue;
        }
        let (at, obj) = hit.unwrap_or((from, None));
        blasts.write(crate::fx::Explode {
            at,
            area_damage: p.f("area_damage", 30.0),
            radius_inner: p.f("damage_radius_inner", 10.0),
            radius_outer: p.f("damage_radius_outer", 30.0),
            impact_force: p.f("impact_force", 15000.0),
            source: obj,
            delay: 0.0,
            by: crate::fx::Src::Badguy,
            cartoon: false,
        });
        if let Some(mut h) = obj.and_then(|o| health.get_mut(o).ok()) {
            h.0 -= p.f("damage", 50.0);
        }
        if let Some(n) = p.s("fx_end") {
            fx.write(crate::fx::StartFx { name: n.to_owned(), at, rotation: crate::level::game_to_engine(), follow: None, position_only: false });
        }
        commands.entity(e).despawn();
    }
}

/// What ended the fire cycle's frame.
#[derive(PartialEq)]
enum FireEnd {
    None,
    /// the last burst of the clip (0x00428980 -> 0x0042b8e0)
    Clip,
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
    mut grunts: Query<(Entity, &mut Grunt, &mut Body, &mut Transform, &Health, Option<&crate::skin::Animator>)>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    rtime: Res<Time<Real>>,
    mut barks: ResMut<Barks>,
    (markers, mut arrive, mut trenches, mut guns, mounted, data): (
        Res<crate::alarm::Markers>,
        MessageWriter<crate::alarm::MarkerArrival>,
        Query<(Entity, &mut Barricade)>,
        Query<(Entity, &mut crate::turret::Turret, &GlobalTransform, &crate::skin::Animator, Option<&crate::level::Placed>), Without<Grunt>>,
        Res<crate::turret::Mounted>,
        Res<crate::gamedata::GameData>,
    ),
    (mut lib, player_q, mut triggers, grenade_assets): (
        ResMut<crate::skin::AnimLibrary>,
        Query<Entity, With<Player>>,
        MessageWriter<crate::mission::SendTrigger>,
        Option<Res<GrenadeAssets>>,
    ),
) {
    let t0 = Instant::now();
    let player_e = player_q.single().ok();
    let dt = time.delta_secs();
    let k = physics.meters_per_unit;
    let (mut alive, mut dead) = (0, 0);
    let mut states = [0usize; 16];
    let views: Vec<TrenchView> = trenches
        .iter()
        .map(|(e, b)| TrenchView { e, origin: b.origin, exit: b.exit, spot: b.spot, dir: b.dir, occupant: b.occupant, collider: b.collider })
        .collect();
    let gun_views: Vec<GunView> = guns
        .iter()
        .map(|(e, t, gt, a, placed)| {
            let (rot, origin) = (gt.rotation(), gt.translation());
            let (yaw, _, _) = (rot * crate::level::game_to_engine().inverse()).to_euler(EulerRot::YXZ);
            GunView {
                e,
                origin,
                exit: t.grunt_exit.map(|i| t.bone_world(gt, a, i).w_axis.truncate()).unwrap_or(origin),
                yaw,
                fwd: t.forward(gt, a).with_y(0.0).normalize_or_zero(),
                usable: t.ai_can_use && mounted.0 != Some(e),
                reserved: t.reserved,
                ai: t.ai,
                collider: placed.and_then(|p| p.collider),
            }
        })
        .collect();
    let mut spots = Spots { trenches: views, guns: gun_views };
    for (ge, mut g, mut body, mut tr, h, ga) in &mut grunts {
        g.me = Some(ge);
        if g.mind == Mind::Dead {
            dead += 1;
            continue;
        }
        alive += 1;
        states[g.mind as usize] += 1;
        // a trench held by a grunt another system took elsewhere (a scare, a throw): let it go (guess: as vt+0x18c)
        if g.cover.is_some() && !matches!(g.mind, Mind::Cover | Mind::Fire | Mind::Reload) {
            leave_cover(&mut g, ge, &mut spots.trenches, false);
        }
        // likewise a gun (0x004266b0)
        if g.gun.is_some() && g.mind != Mind::Gunner {
            if let Some(t) = leave_gun(&mut g, ge, &mut body, &mut spots.guns) {
                if let Ok((_, mut tu, ..)) = guns.get_mut(t) {
                    tu.ai_aim(0.5, 0.5);
                }
            }
        }
        // hurt: bark at most every 4 s (0x004394c0); a hurt grunt waiting for a trigger activates (ai_04.cpp:420)
        g.hurt_voice_timer -= dt;
        if h.0 < g.last_health && h.0 > 0.0 {
            g.last_hit = g.last_health - h.0;
            // the hit reaction (GenericAIObject_Slot52 0x00430340, ai_04.cpp:444-535; anim names by the class's table,
            // `anim_name`), in its order: on a gun (+0x484 0x8000) none; the all-fours fall (vt+0x200: the captain)
            // when the health crosses allfours_pct (+0x9e0) %: anim 0x35, state 300; then (vt+0x204: all three) damage
            // <= hit_level_1 a small hit (not while one or a long hit plays, states 0x12f / 0x132): one of ids
            // 0x3f..0x42 by rand * 400 >> 15, state 0x12f; <= hit_level_2 a medium hit, 0x43 / 0x44 alternating on the
            // anim playing (the first by rand * 200 >> 15 < 100), state 0x130; else a big hit 0x45 + (rand * 2 >> 15),
            // state 0x131. (The record flags 0x100 / 4, anims 0x77.. / 0x47.., are not set by any lev_1_m1 damage.)
            let d = g.last_hit;
            let on_gun = g.gun.is_some_and(|x| x.phase == GunPhase::On);
            let (before, after) = (g.last_health / g.max_health, h.0 / g.max_health);
            let fours = g.allfours_pct / 100.0;
            let cold = true; // Game+0x2708 == 1 (world 1)
            let react: Option<(u32, u8)> = if on_gun {
                None
            } else if g.kind.allfours() && before > fours && after <= fours {
                Some((0x35, 5))
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
            if let Some((id, kind)) = react {
                g.hit = Some((anim_name(g.kind, id, rng.rand(), cold).to_owned(), kind));
                g.hit_id = id;
                g.reacting = kind;
                g.turn = None;
                // stopMove: leave a cover held on the way (vt+0x18c), not the slot (+0x484 0x20000): the reaction's
                // end ducks there
                if g.cover.is_some() && !g.at_barricade {
                    leave_cover(&mut g, ge, &mut spots.trenches, false);
                }
            }
            // Hurt (0x00439e00, shared pool, death_delay) first; when refused HurtShort (0x00439e30) at most every 4 s
            // per grunt; the shared one restarts the 4 s too (+0xb48)
            if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt:{}:last4", g.voice_set, tr.translation);
            } else if g.hurt_voice_timer <= 0.0 {
                g.hurt_voice_timer = 4.0;
                bark(&mut sounds, "Hurt:{}:butlast4", g.voice_set, tr.translation);
            }
            if g.mind == Mind::WaitTrigger {
                g.mind = Mind::Roam;
            }
            // retaliation: the end of GenericAIObject_Slot52 0x00430340 offers the damage source (when it is one of
            // the grunt's enemies, list +0x604: Roman or a companion) as a target, GenericAI_OfferTarget 0x0042c820:
            // ignored while a target adopted this way is less than 10 s old (K 0x005bcb68 against +0x514); taken when
            // there is no target or the source is nearer (3D) than the current one
            if let Some(a) = g.attacker.take() {
                let now = time.elapsed_secs();
                let ap = targets.get(a).ok().map(|t| t.translation());
                let cur = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
                // (ignored too on the way to a cover: +0x904 held without +0x484 0x20000, 0x0042c820)
                let recent = (g.target.is_some() && now - g.offer_t < 10.0) || (g.cover.is_some() && !g.at_barricade);
                if let (Some(ap), false) = (ap, recent) {
                    let me = tr.translation;
                    if cur.is_none_or(|c| (ap - me).length_squared() <= (c - me).length_squared()) {
                        if std::env::var("AIDBG").is_ok() {
                            info!("AIDBG retaliate {:?} -> {:?} (had {:?})", body.id, a, g.target);
                        }
                        g.target = Some(a);
                        g.offer_t = now;
                    }
                }
            }
        }
        g.last_health = h.0;
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
        // a target farther than 2 x scan_distance is dropped (states 0x3c, 0xa)
        if let Some(p) = target {
            if flat(p - pos).length() > 2.0 * g.scan_distance && !g.guard {
                g.target = None;
            }
        }
        let target = g.target.and_then(|e| targets.get(e).ok()).map(|t| t.translation());
        let in_range = target.is_some_and(|p| flat(p - pos).length() <= g.range);
        // a guard whose target is beyond its range fires with the infinite accuracy ranges (ai_03.cpp:1383)
        g.infinite = g.guard && target.is_some() && !in_range;
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
                g.mind = Mind::Roam;
            } else if let Some(mk) = markers.spline_marker(run.tag, run.index) {
                g.mind = Mind::ToMarker;
                if flat(mk.pos - pos).length() < 8.0 {
                    // Marker_ProcessArrival 0x0044ba30 (0x96's arrival does it for the head, then state 0x97)
                    g.spline = Some(SplineRun { started: true, ..run });
                    let idx = markers.spline_index(run.tag, run.index).unwrap_or(0);
                    stats.markers += 1;
                    if let Some(m) = marker_arrival(&mut g, ge, idx, &markers, (pos, tgt, &physics, body.id), &mut spots, &mut rng, (&mut arrive, &mut triggers), player_e) {
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
            g.mind = match find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng) {
                Hide::Cover => Mind::Cover,
                Hide::Gunner => Mind::Gunner,
                Hide::Pursue => Mind::Chase,
                Hide::Reload => Mind::Reload,
            };
            info!("EVENT ai: cover search -> {:?}", g.mind);
        }
        let was = g.mind;
        g.mind = match (g.mind, target) {
            (Mind::ToMarker, _) => Mind::ToMarker,
            (Mind::Flee, _) if g.timer > 0.0 => Mind::Flee,
            // the scare's end charges the player (0x0042ce70)
            (Mind::Flee, _) => {
                g.target = g.target.or(player_e);
                Mind::Chase
            }
            // the wander (0x3d / 0x5b) and the one-shot states run their own course (below)
            (Mind::Wander, _) => Mind::Wander,
            (Mind::Taunt, _) => Mind::Taunt,
            (Mind::Throw, _) => Mind::Throw,
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
                    let h = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                    hide_to_mind(&mut g, h)
                } else {
                    leave_cover(&mut g, ge, &mut spots.trenches, true);
                    Mind::Chase
                }
            }
            (Mind::WaitTrigger, None) => Mind::WaitTrigger,
            (Mind::Home, None) => Mind::Home,
            // the chase engages on its ray (state 0x3c); the archers (vt+0x1cc false) only within 0.9 x +0x5d8
            // (K 0x005bcc38) first
            (Mind::Chase, Some(p)) if !g.guard && !g.kind.chase_ray_only() && flat(p - pos).length() > 0.9 * g.range => Mind::Chase,
            (_, Some(_)) if in_range || g.guard => {
                // engage 0x00427910 on coming into it: in range with flag 4, the close attack when the class has one
                // (vt+0x1e0: the captain), allow_close_attack, flag 1 and rand * 200 >> 15 < 30; else the fire state 0xa
                if was != Mind::Fire && g.kind.close_attack() && in_range && g.fire_ready && g.throw_armed && rng.rand() * 200.0 < 30.0 {
                    g.anim_id = 0x27;
                    g.anim_left = anim_len(&mut lib, anim_name(g.kind, 0x27, 0.0, true));
                    g.thrown = false;
                    Mind::Throw
                } else {
                    Mind::Fire
                }
            }
            (_, Some(_)) => Mind::Chase,
            (Mind::Guard, None) => Mind::Guard,
            // the target gone from the states that need one (0x0042bc60)
            (Mind::Fire | Mind::Chase | Mind::Reload, None) => lost_target(&mut g, ge, &mut spots.trenches),
            _ => Mind::Roam,
        };
        // (the playtest log and the anims see the stance: at rest after a fight the stand set)
        if matches!(g.mind, Mind::Fire | Mind::Chase | Mind::Reload | Mind::Cover | Mind::Gunner | Mind::Throw) {
            g.attention = false;
        }
        let mut goal = pos;
        let mut speed = 0.0;
        // a hit reaction holds the grunt (states 0x12f..0x131)
        let reacting = g.hit.is_some();
        match if reacting { Mind::Dead } else { g.mind } {
            Mind::Roam => {
                // idle / roaming (state 1, grunt-ai.md): with no target the grunt stands at attention where it is and
                // fidgets (charanim.rs); it only moves along a patrol spline, which no lev_1_m1 grunt has. (The
                // engine's earlier stand-in wandered round home, marching with footsteps the original never plays.)
                goal = pos;
                speed = 0.0;
            }
            Mind::Chase => {
                // pursue 0x004295d0: a point within +-20 of the target; every 0.2-0.4 s a ray decides to engage
                if let Some(p) = target {
                    if g.ray_timer <= 0.0 {
                        g.ray_timer = if limping { rng.range(0.27, 0.54) } else { rng.range(0.2, 0.4) };
                        g.goal = p + Vec3::new(rng.range(-20.0, 20.0), 0.0, rng.range(-20.0, 20.0));
                    }
                    goal = g.goal;
                    speed = run_speed;
                }
            }
            Mind::Flee => {
                goal = g.goal;
                speed = run_speed;
            }
            Mind::ToMarker => {
                let mk = g.spline.and_then(|r| markers.spline_marker(r.tag, r.index)).map(|m| m.pos);
                goal = g.exit.or(mk).unwrap_or(pos);
                speed = run_speed;
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
                        g.target = g.target.or(player_e);
                        g.search_timer = rng.range(1.5, 2.0);
                        g.mind = Mind::Chase;
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
                        g.pause = reload_len(g.kind);
                        Mind::Reload
                    } else if target.is_some() {
                        let h = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                        hide_to_mind(&mut g, h)
                    } else {
                        lost_target(&mut g, ge, &mut spots.trenches)
                    };
                }
            }
            Mind::Throw => {
                // state 0x10: the throw anim facing the target; its "throw" note (captain_grenade_throw 1.333 s) sends
                // the grenade (0x0043d180 "throw" -> 0x00428340: the captain_grenade from the hand at the target's
                // ref_hit_00; flag 1 cleared); the end: a guard 0x7a, flag 4 clear the clip's end (0x0042b8e0), else
                // 50 % that and 50 % engage again (ai_06.cpp:813-836)
                let total = anim_len(&mut lib, anim_name(g.kind, 0x27, 0.0, true));
                let before = total - g.anim_left;
                g.anim_left -= dt;
                if !g.thrown && before + dt >= 1.333 {
                    g.thrown = true;
                    stats.throws += 1;
                    g.throw_armed = false;
                    if let Some(thit) = target_hit {
                        let hand = ga
                            .and_then(|a| a.skeleton.index.get("ref_lhand").and_then(|&i| targets.get(a.joints[i]).ok()))
                            .map(|j| j.translation())
                            .unwrap_or(pos + Vec3::Y * 9.0);
                        throw_grenade(&mut commands, grenade_assets.as_deref(), &data, hand, thit, ge);
                    }
                }
                if g.anim_left <= 0.0 {
                    g.mind = if g.guard {
                        Mind::Guard
                    } else if !g.fire_ready || rng.rand() < 0.5 {
                        if target.is_some() {
                            let h = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng);
                            hide_to_mind(&mut g, h)
                        } else {
                            lost_target(&mut g, ge, &mut spots.trenches)
                        }
                    } else {
                        Mind::Fire
                    };
                }
            }
            Mind::Fire => {
                if let Some(p) = target {
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
                    } else if to.dot(fwd) < 0.7 && g.kind != Kind::Captain {
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
                        let n = anim_name(g.kind, id, 0.0, true);
                        g.turn = Some((n, anim_len(&mut lib, n)));
                    }
                    let before = stats.shots;
                    let mut end = FireEnd::None;
                    if g.turn.is_none() {
                        // the weapon in its right hand (ref_rhand, grunt init 0x00434fb0): where the shot leaves
                        let hand = ga.and_then(|a| a.skeleton.index.get("ref_rhand").and_then(|&i| targets.get(a.joints[i]).ok())).map(|j| j.translation());
                        end = fire_cycle(&mut commands, &mut g, &physics, &mut rng, pos, p, hit, own_hit, hand, body.id, dt, &mut stats, &mut sounds, &mut fx, (&mut barks, time.elapsed_secs()));
                    }
                    if stats.shots > before && std::env::var("AIDBG").is_ok() {
                        info!("AIDBG gruntshot t {:.3}", rtime.elapsed_secs());
                    }
                    match end {
                        // in the slot a lost line of sight ducks (0x00428980: anim 0x66, state 0xa2)
                        FireEnd::Sight if g.at_barricade => duck(&mut g),
                        // the clip's end (0x0042b8e0): in the slot, the target within 80 degrees of its direction ->
                        // duck, else leave it (vt+0x188) and search again (vt+0x240(1, 1, 0)); elsewhere a non-guard
                        // searches (vt+0x240(1, 0, 0)): 90 % the reload in place first
                        FireEnd::Clip if g.at_barricade => {
                            let c = g.cover.expect("at a barricade without a cover");
                            if within_arc(p, c.spot, c.dir, 80.0) {
                                duck(&mut g);
                            } else {
                                leave_cover(&mut g, ge, &mut spots.trenches, true);
                                if let Some(m) = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng).mind() {
                                    g.mind = m;
                                }
                            }
                        }
                        FireEnd::Clip if !g.guard => {
                            if let Some(m) = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng).mind() {
                                g.mind = m;
                            }
                        }
                        // the taunt after the last clip (state 0x6e): its anim's length
                        FireEnd::Taunt => {
                            stats.taunts += 1;
                            let n = anim_name(g.kind, g.anim_id, 0.0, true);
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
                            g.left_clips = 0;
                            g.left_rounds = rng.range(g.rounds.0 as f32, g.rounds.1 as f32 + 0.999) as i64;
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
                                c.timer = anim_len(&mut lib, anim_name(g.kind, 0x68, 0.0, true));
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
                        // each "fire" note of the spray anim a shot at the target (the fire event 0x0043d180 -> vt+0x13c),
                        // rounds counted down; its end: flag 4 with rounds left -> breathe (the counters rolled), else duck
                        let name = anim_name(g.kind, 0x68, 0.0, true);
                        let notes: Vec<f32> = lib.anim(name).map(|a| a.notes.iter().filter(|n| n.1.trim().eq_ignore_ascii_case("fire")).map(|n| n.0).collect()).unwrap_or_default();
                        let len = anim_len(&mut lib, name);
                        let (t0, t1) = (len - (c.timer + dt), len - c.timer);
                        for n in notes {
                            if n > t0 && n <= t1 && g.left_rounds > 0 {
                                if let Some((_, thit)) = tgt {
                                    g.left_rounds -= 1;
                                    shoot(&mut commands, &mut fx, &g, &mut rng, pos, thit);
                                    stats.shots += 1;
                                }
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
                            // engage (0x00427910): in range the fire state 0xa, keeping the trench; else (or no
                            // target, L_4459e4) duck
                            if target.is_some() && in_range {
                                g.mind = Mind::Fire;
                                g.pause = 0.0;
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
                        // the aim (vt+0x190 0x00428fc0 with 65 degrees: state 0x7d calls it twice a frame)
                        gun_aim(&mut gn, pos, target, 65.0, dt);
                        let mut cl = gun_aim(&mut gn, pos, target, 65.0, dt);
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
                            if gn.clips <= 0 {
                                gn.timer = rng.range(g.stig.delay.0, g.stig.delay.1);
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
                                tu.ai_aim(gn.h, gn.v);
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
                                        gn.rounds -= 1;
                                        gn.burst -= 1;
                                        let (Some(p), Some(hit)) = (target, target_hit) else { continue };
                                        let to = (p - pos).with_y(0.0);
                                        let d = to.length();
                                        let (lo, hi) = g.accuracy;
                                        let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
                                        let side = Vec3::new(-to.z, 0.0, to.x).normalize_or_zero();
                                        let aim = hit + side * rng.range(-g.inaccuracy_angle, g.inaccuracy_angle) * s + Vec3::Y * rng.range(0.0, g.inaccuracy_height) * s;
                                        let from = tu.bone_world(gt, ta, tu.emitter()).w_axis.truncate();
                                        let dir = (aim - from).normalize_or(gn.fwd);
                                        let bullet = data.props(&tu.ai_projectile);
                                        crate::player::spawn_projectile(&mut commands, &mut fx, &bullet, from, dir, false, Some(ge));
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
                            g.mind = find_hiding_spot(&mut g, ge, (true, false), pos, tgt, &mut spots, &physics, body.id, &mut rng).mind().unwrap_or(Mind::Reload);
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
        if speed > 0.0 && flat(goal - pos).length() > 15.0 && !line_of_sight(&physics, pos, goal) {
            if g.replan <= 0.0 || g.route.is_empty() {
                g.replan = 1.0;
                g.route = crate::nav::path(&physics, pos, goal, &[]).unwrap_or_default();
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
        let dir = flat(goal - pos).normalize_or_zero();
        let face = match (g.mind, target, g.cover.map(|c| c.phase)) {
            (Mind::Fire | Mind::Chase | Mind::Taunt | Mind::Throw, Some(p), _) => flat(p - pos).normalize_or_zero(),
            // in the slot: its direction +0x908 (0x00454e60), standing up: the target (0x0042df70)
            (Mind::Cover, Some(p), Some(CoverPhase::Stand)) => flat(p - pos).normalize_or_zero(),
            (Mind::Cover, _, Some(CoverPhase::Duck | CoverPhase::Breathe | CoverPhase::Peek | CoverPhase::Stand | CoverPhase::Spray)) => {
                g.cover.map(|c| flat(c.dir).normalize_or_zero()).unwrap_or(dir)
            }
            _ => dir,
        };
        if face != Vec3::ZERO {
            let want = face.x.atan2(face.z);
            let d = (want - g.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            g.yaw += d * (dt * 8.0).min(1.0);
        }
        tr.rotation = Quat::from_rotation_y(g.yaw) * g.base;
        if let Ok(mut b) = physics.world.body_mut(body.id) {
            let v = bv(b.linear_velocity());
            let want = dir * speed * k;
            let _ = b.set_linear_velocity(jv(Vec3::new(want.x, v.y, want.z)));
            if speed > 0.0 {
                b.activate();
            }
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
fn shoot(commands: &mut Commands, fx: &mut MessageWriter<crate::fx::StartFx>, g: &Grunt, rng: &mut FxStats, pos: Vec3, hit: Vec3) {
    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
    let to = (hit - pos).with_y(0.0);
    let d = to.length();
    let (lo, hi) = if g.infinite { g.infinite_accuracy } else { g.accuracy };
    let s = if d <= lo { 0.1 } else if d >= hi { 1.0 } else { ((d * d - lo * lo) / (hi * hi - lo * lo)).max(0.1) };
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
    crate::player::spawn_projectile(commands, fx, &g.bullet, muzzle + dir * 4.0, dir, false, g.me);
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
    own: BodyId,
    dt: f32,
    stats: &mut AiStats,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    fx: &mut MessageWriter<crate::fx::StartFx>,
    (barks, now): (&mut Barks, f32),
) -> FireEnd {
    g.pause -= dt;
    if g.pause > 0.0 {
        return FireEnd::None;
    }
    if g.left_clips <= 0 {
        g.left_clips = rng.range(g.clips.0 as f32, g.clips.1 as f32 + 0.999) as i64;
        g.left_bursts = 0;
    }
    if g.left_bursts <= 0 {
        g.left_bursts = rng.range(g.bursts.0 as f32, g.bursts.1 as f32 + 0.999) as i64;
        g.left_rounds = 0;
    }
    if g.left_rounds <= 0 {
        g.left_rounds = rng.range(g.rounds.0 as f32, g.rounds.1 as f32 + 0.999) as i64;
    }
    // fires only inside the 180-degree cone with line of sight
    let fwd = Vec3::new(g.yaw.sin(), 0.0, g.yaw.cos());
    let to = (p - pos).with_y(0.0);
    if to.normalize_or_zero().dot(fwd) < 0.0 {
        return FireEnd::None;
    }
    if !sight_ray(physics, own_hit, hit, &[own]) {
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
        g.pause = 0.467 + reload_len(g.kind) + 2.0 / 60.0;
        return FireEnd::None;
    }
    shoot(commands, fx, g, rng, pos, hit);
    // (the shot sound comes from the weapon's muzzle fx: the original's sound log has one per shot)
    // the Fight cue as the fire anim starts with a target (0x00428980 -> vt+0x13c), under vo_delay
    if barks.take(Delay::Vo, now, rng) {
        bark(sounds, "Fight", g.voice_set, pos);
    }
    stats.shots += 1;
    // the fire cycle (0x00428980, counters 0x00427600, as the companions'): a shot on the fire anim's note (0.033 s
    // into its 0.467 s loop; here at its start), fire_num_rounds per clip. An empty clip plays the reload anim
    // (grunt_bow_reload_* 1.3 s, grunt_reload_br 1.133 s); a guard (state 0x79, ai_06.cpp:2656) then re-rolls the
    // counters on the first-call path, without fire_clip_delay, and fires the next frame: anim + 1 frame + reload +
    // 1 frame = 108 frames for the archer (the original's 837 -> 945); a moving grunt (state 0x32) waits the clip delay
    // the fire anim's length per shot: grunt_fire_* / grunt_bow_fire_* 0.467 s (one "fire" note each), the captain's
    // captain_fire_* 0.633 s looped (+0x484 0x1000: 0x00428250(1) at CaptainObject_Slot6) with one note per loop
    let fire_len = if g.kind == Kind::Captain { 0.633 } else { 0.467 };
    g.left_rounds -= 1;
    g.pause = fire_len;
    if g.left_rounds <= 0 {
        g.left_bursts -= 1;
        g.pause = fire_len + rng.range(g.burst_delay.0, g.burst_delay.1);
        if g.left_bursts <= 0 {
            g.left_clips -= 1;
            g.fire_ready = false;
            // the taunt after the last burst (0x00428980, ai_02.cpp:1955-1981): a target, vt+0x1d8 (all three classes),
            // taunt_delay passed (Game+0xb84 / +0xb90, 300..360 s game-wide), not in a trench's slot (+0x484 0x20000);
            // the heading test (0x004e81a0 of the two facings < 180) passes but for opposite facings, and the grunt's own
            // speech handle +0xb50 is taken as silent (the engine keeps none per grunt): the Taunt bark (vt+0x15c), a
            // taunt anim 0x32 + (rand * 3 >> 15), state 0x6e; the timer restarts only then
            if !g.at_barricade && barks.ready(Delay::Taunt, now, rng) {
                barks.take(Delay::Taunt, now, rng);
                bark(sounds, "Taunt", g.voice_set, pos);
                g.anim_id = 0x32 + ((rng.rand() * 3.0) as u32).min(2);
                g.mind = Mind::Taunt;
                return FireEnd::Taunt;
            }
            g.mind = Mind::Reload;
            g.pause = fire_len + reload_len(g.kind) + if g.guard { 2.0 / 60.0 } else { rng.range(g.clip_delay.0, g.clip_delay.1) };
            return FireEnd::Clip;
        }
    }
    FireEnd::None
}

/// Character_Die: Dead bark once; ragdoll (a tumbling rigid body here) when the killing hit was at least
/// hit_ragdoll, GoRagdoll impulse = direction x amount x 0.15. Death anims (grunt_die_*) come with skinning.
fn die(
    mut commands: Commands,
    mut physics: NonSendMut<Physics>,
    mut grunts: Query<(Entity, &mut Grunt, &Body, &mut Transform, &Health, Option<&mut crate::skin::Animator>)>,
    joints: Query<&GlobalTransform>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut km: ResMut<KillMeter>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    alive: Query<&Health>,
    (mut barks, time, mut guns): (ResMut<Barks>, Res<Time>, Query<&mut crate::turret::Turret>),
) {
    let mut alerts: Vec<(i64, Entity, Entity)> = Vec::new();
    for (e, mut g, body, mut tr, h, animator) in &mut grunts {
        if g.mind == Mind::Dead || h.0 > 0.0 {
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
        if !g.dead_said {
            g.dead_said = true;
            // Dead (0x00439e60, pool 26..28) under death_delay, else DeadAtPosition (0x00439ea0, pool 21..25): once
            let cat = if barks.take(Delay::Death, time.elapsed_secs(), &mut rng) { "Dead:{}:last3" } else { "Dead:{}:butlast3" };
            bark(&mut sounds, cat, g.voice_set, tr.translation);
        }
        let push = physics.world.body(body.id).map(|b| bv(b.linear_velocity())).unwrap_or(Vec3::ZERO) / physics.meters_per_unit;
        physics.remove(body.id);
        // the death (Character_Die 0x004319e0, in its order): a window / tower grunt killed by a record with flag
        // 0x80 (the sticky bomb) is blown out of the window (anim 0x3e); a hit of at least hit_ragdoll ragdolls; a
        // ledge_death grunt falls (0x39 + rand*4: grunt_hit_fall_1 / _2 / _1 / _2); a window / tower grunt
        // grunt_hit_window (0x3d); else 0x00427100: grunt_barricade_death (0x56) in a trench's slot, else grunt_die_1..6.
        // Not reached here: the 0x57 flag variant (in back: no lev_1_m1 grunt sets it) and record flag 2's launch.
        let blown_out = g.window && g.cartoon;
        g.ragdoll = !blown_out && (g.last_hit >= g.hit_ragdoll || push.length() > 30.0);
        g.death_name = if blown_out {
            Some("grunt_window_blown_out")
        } else if g.ragdoll {
            None
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
            commands.entity(e).remove::<Body>();
            continue;
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
        for (ge, mut g, _, _, _, _) in &mut grunts {
            if ge != who && g.alert_group == group && g.mind != Mind::Dead && g.pending.is_none() {
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
///   to Roman exceeds formation_distance + 2.5 (0x004860e2), then run to the slot (mode 2) at speed (d / delay) x
///   move_speed below 2 x delay_distance, else move_speed (0x00486cb0), d the distance to Roman; idle again there.
///   (The leader swap of the two slots, 0x00483810, is not ported.)
/// - Targets (0x00485490 -> AI_FindTarget 0x00426920): the nearest live grunt within scan_distance, in the 190 degree
///   facing cone and in line of sight; refused when farther than scan_distance from Roman (0x004853a0). Lost or dead:
///   back to formation after 3..7 s.
/// - Support fire (0x00485580): following, no target, Roman's Fire held and his heading within 45 degrees of the
///   companion's: the fire cycle at Roman's aim point (Game+0xa6c), turning toward Roman's heading.
/// - Fire cycle (0x00428980, counters 0x00427600): bursts of fire_num_bursts shots on the fire anims' notes (Jonesy:
///   the 4 blended 0.3 s anims each fire at 0: 4 at once; Q: 6 notes per 0.633 s loop), fire_burst_delay between
///   bursts, fire_num_rounds per clip, then the reload anim and fire_clip_delay. Shots: the weapon's projectile at the
///   aim point (+ scatter up to fire_inaccuracy_height); the muzzle fx carries the sound (Q's is one fx per burst, its
///   machine-gun loop).
/// - Team orders (fn team_orders): Defend Me / Return To Me / Defend Area / Attack, their lines and HUD texts; the
///   Fight cue at the fire anim's start (docs/subsystems/companions.md).
/// Not ported: the slot swap (leader flag 0x10, 0x00483810), the order marker fx (Game+0xd34), object types 0x51 /
/// 0x53 as attack targets.
#[derive(Component)]
pub struct Companion {
    pub walker: crate::physics::Walker,
    pub q: bool,
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
    lost: f32,
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
    /// hits and death (GenericAIObject_Slot52 0x00430340 and Character_Die 0x004319e0, no companion overrides):
    /// health last frame, seconds since the last Hurt bark (+0xb48, 10 s), the reaction anim playing (name, time
    /// left), a player / teammate shot that hit (FriendlyFire bark, no damage), the attacker to offer as a target,
    /// dead (permanent: *_die, then the body is removed)
    pub last_health: f32,
    pub hurt_t: f32,
    pub hit: Option<(&'static str, f32)>,
    pub friendly: bool,
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
    /// an order asks for a forced pick (the area order 0x00484ca0: 0x00483810(1))
    force_swap: bool,
    /// +0xbcc flag 0x80: the partner started a regroup (0x004837b0), so this one regroups too at its next idle frame
    regroup_req: bool,
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
    /// flags 1 | 4: "Attack" the grunt under the crosshair, anchored where it stood (0x00484100)
    Attack { at: Vec3, dir: Vec3 },
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
            lost: 0.0,
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
        last_health: f32::NAN,
        hurt_t: 99.0,
        hit: None,
        friendly: false,
        attacker: None,
        dead: None,
        order: Order::Guard,
        angle: p.f("formation_angle", 20.0),
        linger: 0.0,
        lead: q,
        swap_t: 0.0,
        roman_dir: Vec3::ZERO,
        swap_timer: 0.25,
        force_swap: false,
        regroup_req: false,
        }
    }
}

impl Companion {
    /// Roman was hit (0x0047afa0 -> 0x00486df0): unless engaged, the attacker becomes the target.
    pub fn roman_attacked(&mut self, by: Entity) {
        if self.dead.is_none() && !self.engaged {
            self.target = Some(by);
        }
    }
}

pub struct CompanionPlugin;

impl Plugin for CompanionPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (companion_damage, companions).chain().run_if(crate::playing));
    }
}

/// The companions' hits and death (no overrides of the generic handlers: GenericAIObject_Slot52 0x00430340,
/// Character_Die 0x004319e0; barks audio_02.cpp:197-267 (Q), audio_01.cpp:1545-1610 (Jonesy)):
/// - a player or teammate shot does no damage (same team) but draws the FriendlyFire line (shared delay);
/// - damage: the Damaged line under the shared delay Game+0xb44, else Hurt at most every 10 s (+0xb48); a reaction
///   anim by the damage (<= hit_level_1 20 *_hit_mgun, <= hit_level_2 51 *_hit_small, else *_hit_med); the attacker
///   offered as a target (Slot109 0x00485360: ignored while a live target is held);
/// - death (permanent): the Dead line, *_die, score_value -500 to Accidents, the body removed after the anim and the
///   corpse timer (0.5 s + 0.5 s fade: guess on the fade's look).
/// The shared delay is vo_delay Game+0xb44 (range Game+0xb48 / +0xb4c = 30..45 s from level load, game_02.cpp:2057),
/// the one the grunts' Spotted / Fight cues use (Barks, Delay::Vo).
#[allow(clippy::too_many_arguments)]
fn companion_damage(
    mut commands: Commands,
    time: Res<Time>,
    mut physics: NonSendMut<Physics>,
    mut q: Query<(Entity, &mut Companion, &Health, &Transform, &mut Visibility)>,
    alive: Query<&Health, Without<Companion>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut ms: ResMut<crate::stats::MissionStats>,
    mut rng: ResMut<FxStats>,
    mut barks: ResMut<Barks>,
) {
    let dt = time.delta_secs();
    let now = time.elapsed_secs();
    let mut say = |sounds: &mut MessageWriter<crate::audio::PlaySound>, rng: &mut FxStats, list: &[&str], at: Vec3| {
        let n = list[((rng.rand() * list.len() as f32) as usize).min(list.len() - 1)];
        sounds.write(crate::audio::PlaySound::at(n, at));
    };
    for (e, mut c, h, t, mut vis) in &mut q {
        let at = t.translation;
        let len = if c.q { 2.533 } else { 2.0 };
        if let Some(d) = c.dead.as_mut() {
            *d += dt;
            if *d >= len + 1.0 && *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
                physics.remove_walker(&c.walker);
                commands.entity(e).remove::<Companion>();
            }
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
            let l: &[&str] = if c.q { &["q1150051_dead", "q1150052_dead", "q1150055_dead", "q1150056_dead", "q1150057_dead", "q1150058_dead", "q1150059_dead"] } else { &["jon50062_dead", "jon50063_dead", "jon50064_dead", "jon50065_dead", "jon50066_dead"] };
            say(&mut sounds, &mut rng, l, at);
            // not a kill (vt+0xe8 false); score_value -500 to Accidents (0x00431720)
            ms.killed(-500.0);
            info!("EVENT {} died", if c.q { "Q" } else { "Jonesy" });
            continue;
        }
        if h.0 < c.last_health {
            let d = c.last_health - h.0;
            if barks.take(Delay::Vo, now, &mut rng) {
                let l: &[&str] = if c.q { &["q1150050_help", "q1150053_help", "q1150054_help", "q1150064_hurt", "q1150065_hurt", "q1150066_hurt", "q1150067_hurt"] } else { &["jon50060_help", "jon50061_help", "jon50070_hurt", "jon50071_hurt", "jon50072_hurt", "jon50073_hurt"] };
                say(&mut sounds, &mut rng, l, at);
            } else if c.hurt_t >= 10.0 {
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
            // a small hit does not restart one already playing (states 0x12f / 0x132)
            if !(n == "hit_mgun" && c.hit.is_some()) {
                c.hit = Some((n, len));
            }
            if let Some(a) = c.attacker.take().filter(|&a| alive.get(a).is_ok_and(|h| h.0 > 0.0)) {
                if c.target.is_none_or(|t| alive.get(t).is_err()) {
                    c.target = Some(a);
                }
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
    ctl: Res<crate::input::Controls>,
    bot: Res<crate::playtest::Bot>,
    roman: Query<(&Transform, &crate::player::Roman, &crate::skin::Animator, &Health), Without<Companion>>,
    cam: Query<&GlobalTransform, With<Camera3d>>,
    mut q: Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), Without<crate::player::Roman>>,
    grunts: Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    joints: Query<&GlobalTransform, Without<Companion>>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut log: ResMut<crate::playtest::PlayState>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (mut pickups, mut dynamite, mut barks, mut hud, mut pending): (
        MessageWriter<crate::pickup::SpawnPickup>,
        MessageWriter<crate::sticky::ThrowDynamite>,
        ResMut<Barks>,
        Option<ResMut<crate::hud::HudState>>,
        Local<(bool, bool)>,
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
    let fire_held = (ctl.down(crate::input::Act::Fire) || ctl.pressed(crate::input::Act::Fire) || bot.fire_ok) && !r.dead;
    // Roman's direction (0x00453150), flat
    let roman_dir = (Quat::from_rotation_y(r.yaw + std::f32::consts::PI) * Vec3::Z).with_y(0.0).normalize_or(Vec3::Z);
    let may_act = !r.dead && !hud.as_ref().is_some_and(|h| h.cinematic);
    team_orders(
        &mut q,
        &mut pending,
        &mut barks,
        hud.as_deref_mut(),
        &mut sounds,
        &mut rng,
        &physics,
        &grunts,
        (ctl.pressed(crate::input::Act::TeamSend), ctl.pressed(crate::input::Act::TeamRecall)),
        may_act,
        cam.single().ok().map(|c| (c.translation(), c.forward().as_vec3())),
        aim_point,
        roman_dir,
        now,
    );
    // who is fighting (the attack order's linger waits for the partner: 0x0042c6f0, guess "has a target")
    let fighting: Vec<(bool, bool)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(_, c, ..)| (c.q, c.target.is_some())).collect();
    // the pair as the leader pick sees it (entity, position, moving = mode 2, its pick time)
    let pair: Vec<(Entity, Vec3, bool, f32)> = q.iter().filter(|(_, c, ..)| c.dead.is_none()).map(|(e, c, t, ..)| (e, t.translation, c.moving, c.swap_t)).collect();
    let mut lead_changes: Vec<(Entity, bool)> = Vec::new();
    let mut regroup_reqs: Vec<Entity> = Vec::new();
    let mut silent_return = false;
    for (e, mut c, mut t, ca, ch) in &mut q {
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
        // at the start: on the formation point (Roman's ref_q_00 / ref_molmer_00), grounded (0x00455860)
        if !c.spawned {
            if let Some(slot) = bone(ra, if c.q { "ref_q_00" } else { "ref_molmer_00" }) {
                let feet = physics.raycast(slot + Vec3::Y * 1000.0, -Vec3::Y, 10000.0).map(|h| h.1).unwrap_or(slot);
                physics.place_walker(&c.walker, feet);
                t.translation = feet;
                c.yaw = r.yaw + std::f32::consts::PI;
                c.spawned = true;
            }
        }
        let me = t.translation;
        // Q's healthy tea (Q11Voice_PlayAmbient 0x004857e0): once, when Roman's health is at most a quarter of its
        // maximum and he is within 50 units: a line (50 %: q1150174_Ambient_Spec, else q1150026_Ambient at Q), the
        // drop_tea anim (id 0x5a, 7.63 s), then (mode 9) "pickup_healthytea" thrown from Q + 4 up toward Roman at
        // normalise(2D direction, 0.7 up) x 40
        if c.q && c.tea > 0.0 {
            c.tea -= dt;
            if c.tea <= 0.0 {
                let to = (rt.translation - me).with_y(0.0).normalize_or_zero();
                let v = Vec3::new(to.x, 0.7, to.z).normalize() * 40.0;
                pickups.write(crate::pickup::SpawnPickup { name: "pickup_healthytea".into(), at: me + Vec3::Y * 4.0, vel: v, hover: false });
            }
            continue;
        }
        if c.q && !c.tea_given && !r.dead && rh.0 <= r.max_health * 0.25 && rt.translation.distance(me) < 50.0 {
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
        // targets
        if let Some(tg) = c.target {
            if grunts.get(tg).map_or(true, |(_, g, _, _)| g.mind == Mind::Dead) {
                c.target = None;
                c.lost = rng.range(3.0, 7.0);
            }
        }
        c.search -= dt;
        // the anchor targets must stay near (0x004853a0 / 0x004852e0, flag 1): Roman (Defend Me: the anchor follows
        // him, flag 8), the order's point (Defend Area / Attack); none in Return To Me (flag 1 clear)
        let anchor = match c.order {
            Order::Guard => Some(rt.translation),
            Order::Loose => None,
            Order::Area { at, .. } | Order::Attack { at, .. } => Some(at),
        };
        // at each search tick a target farther (2D) than scan_distance from the anchor is refused (0x004853a0 in
        // formation) and dropped: the original's companions stand and fight until Roman has moved on (fightT: Jonesy
        // lets go at ~1350, Q at ~1440, then they run after him). The attack order's own target is kept (+0xbb0)
        if c.search <= 0.0 {
            if let (Some((_, _, gt, _)), Some(a)) = (c.target.and_then(|t| grunts.get(t).ok()), anchor.filter(|_| !matches!(c.order, Order::Attack { .. }))) {
                if (gt.translation() - a).with_y(0.0).length() > c.scan {
                    c.target = None;
                }
            }
            if c.target.is_some() {
                c.search = rng.range(0.75, 1.25);
            }
        }
        if c.target.is_none() && c.search <= 0.0 {
            c.search = rng.range(0.75, 1.25);
            let face = Quat::from_rotation_y(c.yaw) * Vec3::Z;
            c.target = grunts
                .iter()
                .filter(|(_, g, _, _)| g.mind != Mind::Dead)
                .map(|(ge, _, gt, _)| (ge, gt.translation()))
                .filter(|(_, p)| (*p - me).with_y(0.0).length() < c.scan && anchor.is_none_or(|a| (*p - a).with_y(0.0).length() < c.scan))
                .filter(|(_, p)| (*p - me).with_y(0.0).normalize_or_zero().dot(face) > (95.0f32).to_radians().cos())
                .filter(|(ge, p)| {
                    let eye = me + Vec3::Y * 8.0;
                    let to = *p + Vec3::Y * 6.0 - eye;
                    physics.raycast(eye, to.normalize_or(Vec3::Y), to.length()).is_none_or(|(b, _, _)| physics.bodies.get(&b) == Some(ge))
                })
                .min_by(|a, b| a.1.distance(me).total_cmp(&b.1.distance(me)))
                .map(|(ge, _)| ge);
            if std::env::var("AIDBG").is_ok() {
                if let Some((_, _, gt, _)) = c.target.and_then(|t| grunts.get(t).ok()) {
                    info!("AIDBG companion q={} targets grunt at {:?} d {:.0} me {:?} roman {:?}", c.q, gt.translation(), gt.translation().distance(me), me, rt.translation);
                }
            }
        }
        // engage 0x00427910: a target beyond the rolled range +0x5d8 is approached (state 0x3c: every 0.2..0.4 s, in
        // range when within 0.9 x +0x5d8 with a clear ray); in range, the engage roll with allow 1: a class that can
        // throw (Molmer's vt+0x1e0; not Q) with flag 1 set throws TNT when rand 0..89 < 30, else fires (state 0xa).
        // The original's dump (fightT): Jonesy walks from 181 to 130 units and Q to 153 before firing
        let tdist = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0).length());
        let in_range = |c: &Companion| tdist.is_some_and(|d| d < c.range);
        match tdist {
            None => c.engaged = false,
            Some(d) if !c.engaged && d <= 0.9 * c.range => {
                c.engaged = true;
                if !c.q && c.throw_armed && c.throwing.is_none() && rng.rand() * 90.0 < 30.0 {
                    c.throwing = Some(0.0);
                }
            }
            _ => {}
        }
        let approach = c.target.filter(|_| !c.engaged).and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, _)| (gt.translation() - me).with_y(0.0));
        if c.lost > 0.0 {
            c.lost -= dt;
        }
        // formation: Roman's bone for this companion; under an area / attack order the point beside the anchor
        // (0x00483c30: the reversed direction turned by +-formation_angle, x formation_distance; the side is the one
        // of the companion's bone, guess for the leader flag 0x10 of 0x00483810)
        // the formation points (0x00483d60): the leader's and the other's
        let formation = |order: Order, angle: f32, distance: f32, lead: bool| match order {
            // 0x00483c30: the anchor's reversed direction turned by +-formation_angle (the leader's side +: the
            // rotation's sense is a guess), x formation_distance
            Order::Area { at, dir } | Order::Attack { at, dir } => {
                let a = angle.to_radians() * if lead { 1.0 } else { -1.0 };
                at + Quat::from_rotation_y(a) * -dir * distance
            }
            _ => bone(ra, if lead { "ref_q_00" } else { "ref_molmer_00" }).unwrap_or(rt.translation),
        };
        let points = (formation(c.order, c.angle, c.distance, true), formation(c.order, c.angle, c.distance, false));
        // the leader pick 0x00483810: forced by an order (0x00484ca0), at the regroup's start, every 0.25 s on the
        // move and on arrival
        if std::mem::take(&mut c.force_swap) {
            leader_pick(c, e, me, true, &pair, &mut lead_changes, points, rt.translation, roman_dir, now);
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
        // measured in the original's intro (hook dumps of World+0xe94): both set off within 0.5 s of Roman and run at
        // ~38 units/s about 32 units behind him; so they regroup on the distance to their slot, not to Roman
        // regroup (mode 1, 0x0048631c): Defend Me on the distance to the slot (as measured); Return To Me only when
        // Roman is farther than delay_distance; an anchored order beyond formation_distance + 2.5 of the anchor
        let regroup = match c.order {
            Order::Guard => (slot - me).with_y(0.0).length() > 15.0,
            Order::Loose => from_roman > c.delay,
            Order::Area { at, .. } | Order::Attack { at, .. } => (at - me).with_y(0.0).length() > c.distance + 2.5,
        };
        if c.target.is_none() && c.lost <= 0.0 && (regroup || (std::mem::take(&mut c.regroup_req) && !c.moving)) {
            // the regroup's start (mode 1, ai_08.cpp:1948-1961): the partner is asked to follow (0x004837b0: flag 0x80
            // unless it holds an area / attack anchor), then the pick, forced when Roman faces more than 90 degrees
            // away from his direction at the last one (dot < 0); not under an area / attack order (flag 1 without 8)
            if !c.moving {
                if let Some(p) = pair.iter().find(|p| p.0 != e && !p.2) {
                    regroup_reqs.push(p.0);
                }
            }
            if !c.moving && matches!(c.order, Order::Guard | Order::Loose) {
                let force = roman_dir.dot(c.roman_dir) < 0.0;
                leader_pick(c, e, me, force, &pair, &mut lead_changes, points, rt.translation, roman_dir, now);
            }
            c.moving = true;
        }
        // the attack order (flag 4) idle in formation with the partner not fighting: +0xb68 runs down, then the pair
        // returns to Defend Me without a line or a HUD text (0x004845b0(0), mode 1 at 0x004862d2)
        if matches!(c.order, Order::Attack { .. }) && c.target.is_none() && !c.moving && !fighting.iter().any(|&(q, f)| q != c.q && f) {
            c.linger -= dt;
            if c.linger <= 0.0 {
                silent_return = true;
            }
        }
        // with a target they stand and fight (the original's dump, fightT: Q fires from (-232, 884) and Jonesy from
        // (-221, 865) without moving until they let go of the target)
        if c.target.is_some() && c.moving {
            c.moving = false;
            c.route.clear();
        }
        let to_slot = (slot - me).with_y(0.0);
        // the partner's mode 1 (idle: 0x004837d0) holds the picks of a moving companion back
        let partner_idle = pair.iter().any(|p| p.0 != e && !p.2);
        if to_slot.length() < 4.0 {
            // arrival (mode 2 -> 1, ai_08.cpp:2060-2064): a pick unless the partner idles
            if c.moving && !partner_idle {
                leader_pick(c, e, me, false, &pair, &mut lead_changes, points, rt.translation, roman_dir, now);
            }
            c.moving = false;
            c.route.clear();
        }
        if c.moving && matches!(c.order, Order::Guard | Order::Loose) {
            // every 0.25 s on the move (+0xb68, ai_08.cpp:2033-2052)
            c.swap_timer -= dt;
            if c.swap_timer < 0.0 {
                c.swap_timer = 0.25;
                if !partner_idle {
                    leader_pick(c, e, me, false, &pair, &mut lead_changes, points, rt.translation, roman_dir, now);
                }
            }
        }
        if c.moving {
            c.replan -= dt;
            if c.replan <= 0.0 {
                c.replan = 0.25;
                c.route = crate::nav::path(&physics, me, slot, &[]).unwrap_or_default();
            }
            while c.route.first().is_some_and(|w| (*w - me).with_y(0.0).length() < 4.0) {
                c.route.remove(0);
            }
        }
        // the run anim while approaching (moving is cleared again next frame while it has a target)
        if approach.is_some() {
            c.moving = true;
        }
        let to = approach.unwrap_or_else(|| c.route.first().map(|w| (*w - me).with_y(0.0)).unwrap_or(to_slot));
        // full move_speed while moving (the original's intro: 38 of 40 units/s at 32 units from Roman)
        // fitted: the original's companions cover 37.3 units/s at move_speed 40 (its drive, move_power 100, not ported)
        let speed = if c.moving || approach.is_some() { c.speed * 0.933 } else { 0.0 };
        let dir = to.normalize_or_zero();
        let vel = dir * speed + Vec3::Y * c.vy;
        if let Some((feet, on)) = physics.move_walker(&c.walker, vel, dt) {
            t.translation = feet;
            c.vy = if on { -5.0 } else { (c.vy - 160.0 * dt).max(-200.0) };
        }
        // what to shoot at: the target's ref_hit_00, or (support fire) Roman's aim point
        let target_pos = c.target.and_then(|tg| grunts.get(tg).ok()).map(|(_, _, gt, a)| a.and_then(|a| bone(a, "ref_hit_00")).unwrap_or(gt.translation() + Vec3::Y * 6.0));
        let roman_heading = r.yaw + std::f32::consts::PI;
        let heading_ok = {
            let d = (roman_heading - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            d.abs() <= 45f32.to_radians()
        };
        let support = c.target.is_none() && !c.moving && fire_held && heading_ok && matches!(c.order, Order::Guard | Order::Loose);
        let shoot_at = if approach.is_some() { None } else { target_pos.or(if support { aim_point } else { None }) };
        // facing: the target, else (support fire / following) Roman's heading, else the way it runs
        let face = target_pos.map(|p| (p - me).with_y(0.0)).filter(|v| v.length() > 0.1);
        if let Some(f) = face {
            c.yaw = f.x.atan2(f.z);
        } else if c.moving && dir != Vec3::ZERO {
            c.yaw = dir.x.atan2(dir.z);
        } else if fire_held {
            let d = (roman_heading - c.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            c.yaw += d * (8.0 * dt).min(1.0);
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
                // JON50028 / JON50029_Taunt; equal odds)
                if !c.firing && c.target.is_some() && barks.take(Delay::Vo, now, &mut rng) {
                    let l: &[&str] = if c.q { &["Q1150038_Fight", "Q1150040_Fight"] } else { &["JON50026_Fight", "JON50028_Taunt", "JON50029_Taunt"] };
                    let n = l[((rng.rand() * l.len() as f32) as usize).min(l.len() - 1)];
                    sounds.write(crate::audio::PlaySound::at(n, me));
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
                        // aim error 0x004284e0 (as the grunts'): sideways in [-angle, angle], up in [0, height], scaled
                        // by the distance between fire_accuracy_min and max
                        let to = (at - muzzle).with_y(0.0);
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
                    }
                    if c.rounds == 0 {
                        // the reload anim (molmer_reload_br 3.47 s, q111_reload_br 1.97 s) and the clip delay
                        c.reloading = if c.q { 1.967 } else { 3.467 } + rng.range(c.p.clip_delay.0, c.p.clip_delay.1);
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
    }
    for pe in regroup_reqs {
        if let Ok((_, mut c, ..)) = q.get_mut(pe) {
            c.regroup_req = matches!(c.order, Order::Guard | Order::Loose);
        }
    }
    for (pe, lead) in lead_changes {
        if let Ok((_, mut c, ..)) = q.get_mut(pe) {
            c.lead = lead;
        }
    }
    if silent_return {
        for (_, mut c, ..) in &mut q {
            if c.dead.is_none() {
                c.order = Order::Guard;
                c.route.clear();
            }
        }
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
    pair: &[(Entity, Vec3, bool, f32)],
    changes: &mut Vec<(Entity, bool)>,
    (a, b): (Vec3, Vec3),
    roman: Vec3,
    roman_dir: Vec3,
    now: f32,
) {
    c.roman_dir = roman_dir;
    let partner = pair.iter().find(|p| p.0 != e);
    if let Some(p) = partner {
        if c.swap_t < p.3 {
            c.swap_t = p.3;
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
        let less = if c.moving {
            let ca = (a - ebp).normalize_or_zero().dot(roman_dir).abs();
            let cb = (b - ebp).normalize_or_zero().dot(roman_dir).abs();
            cb < ca
        } else {
            (a - ebp).length_squared() < (b - ebp).length_squared()
        };
        let set = if less { ebp_e == e } else { ebp_e != e };
        c.lead = set;
        if let Some(p) = partner {
            changes.retain(|x| x.0 != p.0);
            changes.push((p.0, !set));
        }
    }
    c.swap_t = now;
}

/// Picks one of `n` entries the way the voice code does: (rand() * range) >> 15, here range x a uniform [0, 1).
fn roll(rng: &mut FxStats, range: f32) -> u32 {
    (rng.rand() * range) as u32
}

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
    q: &mut Query<(Entity, &mut Companion, &mut Transform, Option<&crate::skin::Animator>, Option<&Health>), Without<crate::player::Roman>>,
    pending: &mut (bool, bool),
    barks: &mut Barks,
    hud: Option<&mut crate::hud::HudState>,
    sounds: &mut MessageWriter<crate::audio::PlaySound>,
    rng: &mut FxStats,
    physics: &Physics,
    grunts: &Query<(Entity, &Grunt, &GlobalTransform, Option<&crate::skin::Animator>)>,
    (send, ret): (bool, bool),
    may_act: bool,
    ray: Option<(Vec3, Vec3)>,
    aim_point: Option<Vec3>,
    roman_dir: Vec3,
    now: f32,
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
    if q.iter().any(|(_, c, ..)| c.dead.is_none() && c.tea > 0.0) {
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
                c.order = if c.order == Order::Guard { Order::Loose } else { Order::Guard };
                guard = c.order == Order::Guard;
                c.target = None;
                c.engaged = false;
                c.lost = 0.0;
                c.route.clear();
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
            (Some((_, at)), _) => (Order::Attack { at, dir: roman_dir }, true),
            (None, Some(at)) => (Order::Area { at, dir: roman_dir }, false),
            (None, None) => return,
        };
        for (k, &(e, ..)) in live.iter().enumerate() {
            if let Ok((_, mut c, ..)) = q.get_mut(e) {
                c.order = order;
                c.route.clear();
                c.lost = 0.0;
                // Defend Area re-picks the leader, forced, for the first of the pair (0x00484ca0 -> 0x00483810(1))
                c.force_swap = !attack && k == 0;
                if let Some((ge, _)) = target {
                    if c.target != Some(ge) {
                        c.engaged = false;
                    }
                    c.target = Some(ge);
                    c.linger = rng.range(3.0, 7.0);
                }
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
                    // ROM50117..119 (with Q alive) are not in lev_1_m1's bank: silent there, as in the original
                    let m = if at_of(true).is_some() { 600.0 } else { 299.0 };
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
    // 0x00463890 + flag 4: the order's text for 3.0 s, hiding the others (0x00467890)
    if let Some(h) = hud {
        h.team = Some((text, 3.0));
    }
    info!("EVENT team order {text}");
}
