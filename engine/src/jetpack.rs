//! The jumper pack: Roman's turbo (jet) pack (the player's +0xabc, template "turbo"; game_06.cpp 0x0047aaf0 ..
//! 0x0047bba0, physics_03.cpp 0x00478200, anim_01.cpp 0x00475d50). Given by the mission (player_has_jetpack: Game byte
//! +0xd92, applied by the player's reset 0x00475650 through 0x0047b8c0 -> PlayerObject_SetTurboPack 0x0047b9f0) or a
//! pickup_jumper_pack (0x00472980); lev_3_m1 / lev_3_m2 start with it, lev_4_m3's objective is to find it.
//!
//! What it does (the oracle's traces, work/oracle-blockers1/jet_probe*, and tools/oracle_scenarios/jetpack_l3m1.json):
//! - On the ground (the grounded grace +0x570 > 0) Jump launches instead of jumping (0x0047aaf0): fx_turbo_launch on
//!   the pack's chest (veh_jumper_blastoff), Fx_fs_roman_jump at the feet, and an upward kick of turbo_force x
//!   turbo_time (1000 x 0.2 = 200 units/s on lev_3_m1: the original rises at 196.8 in its first frame whether Jump is
//!   tapped or held; the force action 0x00455a30 is not decoded, its whole impulse lands in one step, the frame after
//!   the press: the pool applies it at the next physics step, `take_kick`). A turbo boost
//!   on the pack (pickup_turbo_boost, 0x0047b900 at its ref_turbo) makes it turbo_boost_force and is used up.
//! - In the air the turbo state (flag 2) has its own air control: the stick's wish (turbo_fwd / side_max_speed) pushed
//!   at turbo_movement_power, from no velocity (the drive's current velocity is zero in the jump state: game_06.cpp
//!   0x004798c5), and its own gravity (turbo_anti_gravity x 0.5).
//! - Jump up while falling (vz < 0, flag 0x10 set by the launch or by falling with the pack) spreads the wings (flag
//!   4, veh_jumper_wings_out and the veh_jumper_glide_lp loop): a tap launch glides from the top of its arc by itself.
//!   Jump held folds them (veh_jumper_wings_in). Gliding, the drive pulls toward the glide's velocity: forward
//!   turbo_glide_xyvel plus the stick's turbo_glide_fwd / bkd / side_max_speed, and down at most turbo_glide_zvel,
//!   from the body's own velocity at turbo_glide_power, under turbo_glide_anti_gravity (game_06.cpp:2556-2668):
//!   measured on lev_3_m1, the glide settles at 40 units/s forward and -31.8 down (4.7 x (25 - 31.8) = -32.2 gravity).
//! - Landing folds the wings; a turbo landing plays trotter_turbo_land, or trotter_turbo_skid faster than
//!   land_speed_turbo (game_05.cpp:1867-1876), and takes the turbo landing damage; with a pack the fall rules are the
//!   turbo ones (fall_speed_turbo, turbo_death_falling_distance / time: physics_03.cpp:1604-1650).
//! - The pack model rides Roman's "chest" (Object_Attach 0x004539f0 at the pack's own "chest"); gliding in the air its
//!   wings play turbo_up / forward / backward / left / right_cycle by the glide's wish speeds and, in the air from a
//!   launch, the turn, else turbo_idle (0x00475d50); a turn's start while gliding plays veh_jumper_turn_1 / 2
//!   (0x00476a10, timer +0x988 0.2 s).
//! - The pack's sounds and launch effect are shared with the jumper captains, whose packs are the same object (ai.rs
//!   `jumper_packs`): [`WingSounds`] (Jumper_PlayWingSounds 0x0047bb30) and [`chest_fx`] (PlayerObject_PlayChestFx
//!   0x0047bba0).

use bevy::prelude::*;

/// The pack's numbers (PlayerObject_Load 0x00473da0, game_05.cpp:623-724: p_trotter's turbo_* keys and the code's
/// defaults; the anti-gravities already x 0.5, 0x005bcb4c).
#[derive(Clone, Debug, PartialEq)]
pub struct Tuning {
    pub fwd_speed: f32,
    pub side_speed: f32,
    pub force: f32,
    pub boost_force: f32,
    pub time: f32,
    pub gravity_scale: f32,
    pub power: f32,
    pub glide_zvel: f32,
    pub glide_xyvel: f32,
    pub glide_fwd_speed: f32,
    pub glide_bkd_speed: f32,
    pub glide_side_speed: f32,
    pub glide_power: (f32, f32),
    pub glide_gravity_scale: f32,
    /// the speed floor for any stick (fwd / side_max_speed_min +0x998 / +0x994, 4)
    pub speed_min: f32,
    /// fall_speed_turbo (+0xa58): falling below it is the fall state with a pack
    pub fall_speed: f32,
    /// land_speed_turbo (+0xa70): a faster landing skids
    pub land_speed: f32,
    /// landing_damage_*_turbo (+0xbd4 .. +0xbec)
    pub landing_damage: crate::player::LandingDamage,
    /// turbo_death_falling_time / distance (+0xbf8 / +0xbfc)
    pub death_time: f32,
    pub death_distance: f32,
}

impl Tuning {
    pub fn from_props(p: &crate::gamedata::Props) -> Tuning {
        let anti = p.f("turbo_anti_gravity", 0.25) * 0.5;
        Tuning {
            fwd_speed: p.f("turbo_fwd_max_speed", 2.0),
            side_speed: p.f("turbo_side_max_speed", 2.0),
            force: p.f("turbo_force", 175.0),
            boost_force: p.f("turbo_boost_force", 300.0),
            time: p.f("turbo_time", 3.0),
            gravity_scale: anti,
            power: p.f("turbo_movement_power", 10.0),
            glide_zvel: p.f("turbo_glide_zvel", 10.0),
            glide_xyvel: p.f("turbo_glide_xyvel", 30.0),
            glide_fwd_speed: p.f("turbo_glide_fwd_max_speed", 50.0),
            glide_bkd_speed: p.f("turbo_glide_bkd_max_speed", 40.0),
            glide_side_speed: p.f("turbo_glide_side_max_speed", 4.0),
            glide_power: (p.f("turbo_glide_power_min", 10.0), p.f("turbo_glide_power_max", 10.0)),
            // (its default: turbo_anti_gravity x 0.25, game_05.cpp:639)
            glide_gravity_scale: p.f("turbo_glide_anti_gravity", p.f("turbo_anti_gravity", 0.25) * 0.25) * 0.5,
            speed_min: p.f("fwd_max_speed_min", 4.0),
            fall_speed: p.f("fall_speed_turbo", -50.0),
            land_speed: p.f("land_speed_turbo", 35.0),
            landing_damage: crate::player::LandingDamage {
                min_hurt: p.f("landing_damage_min_hurt_turbo", 70.0),
                max_hurt: p.f("landing_damage_max_hurt_turbo", 200.0),
                min_speed: p.f("landing_damage_min_speed_turbo", 40.0),
                max_speed: p.f("landing_damage_max_speed_turbo", 100.0),
            },
            death_time: p.f("turbo_death_falling_time", 3.0),
            death_distance: p.f("turbo_death_falling_distance", 200.0),
        }
    }
}

/// Where the pack has Roman (the flags +0xa34: 2 turbo, 4 gliding).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Flight {
    #[default]
    None,
    /// launched (flag 2): the turbo air control and gravity, wings folded
    Turbo,
    /// wings out (flags 2 | 4)
    Glide,
}

/// What the pack did this frame, for its sounds and effects.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum JetEvent {
    Launch,
    WingsOut,
    WingsIn,
    Turn,
}

/// Roman's pack: whether he has one, its boost, the flight and what the frame's update did.
#[derive(Component, Debug)]
pub struct JetPack {
    pub tuning: Tuning,
    /// the pack object +0xabc exists (PlayerObject_HasTurboPack 0x0047b8b0)
    pub pack: bool,
    /// the boost object +0xac0 on it (0x0047b8e0: both)
    pub boost: bool,
    pub flight: Flight,
    /// flag 0x10: the wings may open (set by a launch and by falling with the pack; never cleared)
    armed: bool,
    /// flag 0x80: in the air from a launch (0x0047aaf0's flags 0x92; the turbo landing clears it, game_05.cpp:1877)
    launched: bool,
    /// the stick (x right, y forward, after the move curve) and the turn this frame (the pack's wing anims, the turn
    /// sound: 0x00476a10's arguments)
    pub stick: Vec2,
    pub turn: f32,
    /// the turn sound's timer (+0x988)
    turn_timer: f32,
    pub events: Vec<JetEvent>,
    /// a launch's kick waiting for the next physics step (the force action pool 0x00455a30)
    kick: Option<f32>,
    /// the glide loop's voice (`effects`)
    wings: WingSounds,
}

impl JetPack {
    pub fn new(tuning: Tuning, pack: bool) -> JetPack {
        JetPack { tuning, pack, boost: false, flight: Flight::None, armed: false, launched: false, stick: Vec2::ZERO, turn: 0.0, turn_timer: 0.0, events: Vec::new(), kick: None, wings: WingSounds::default() }
    }

    /// The Jump button's part of 0x0047aaf0 with a pack: true when it launched (its kick comes with the next frame's
    /// `take_kick`). `pressed` this frame, `down` held; `grounded` the grace +0x570 > 0; `falling` the fall state
    /// (0x600); `landing` the landing anim playing (0x100).
    pub fn jump_input(&mut self, pressed: bool, down: bool, grounded: bool, falling: bool, landing: bool, vz: f32) -> bool {
        if !self.pack || self.kick.is_some() {
            return false;
        }
        match self.flight {
            // flag 4: Jump held folds the wings (Jumper_PlayWingSounds 0x0047bb30 with 0)
            Flight::Glide => {
                if down {
                    self.flight = Flight::Turbo;
                    self.events.push(JetEvent::WingsIn);
                }
                false
            }
            // flag 2, or falling with the pack, not landing: Jump up while dropping spreads them
            _ if (self.flight == Flight::Turbo || falling) && !landing => {
                if self.armed && !down && vz < 0.0 {
                    self.flight = Flight::Glide;
                    self.events.push(JetEvent::WingsOut);
                }
                false
            }
            _ if pressed && grounded => {
                // flags |= 0x92 (turbo, armed, in the air); the boost is used up (0x0047b900 off)
                let force = if self.boost { self.tuning.boost_force } else { self.tuning.force };
                self.boost = false;
                self.flight = Flight::Turbo;
                self.armed = true;
                self.launched = true;
                self.events.push(JetEvent::Launch);
                self.kick = Some(force * self.tuning.time);
                true
            }
            _ => false,
        }
    }

    /// The last frame's launch kick, now applied (upward units/s).
    pub fn take_kick(&mut self) -> Option<f32> {
        self.kick.take()
    }

    /// Falling with the pack and nothing under him arms the wings (0x00478200: flag 0x10).
    pub fn fell(&mut self) {
        if self.pack {
            self.armed = true;
        }
    }

    /// Down on the ground: the wings fold (0x00478200 with flag 4 and contacts), the flight ends with the landing.
    /// True when it was a turbo landing (flag 2: the turbo landing anim and damage).
    pub fn landed(&mut self) -> bool {
        // (a launch waiting for its kick is still on the ground)
        if self.kick.is_some() {
            return false;
        }
        if self.flight == Flight::Glide {
            self.events.push(JetEvent::WingsIn);
        }
        self.launched = false;
        std::mem::take(&mut self.flight) != Flight::None
    }

    /// The gravity scale of the flight (0x004788a0's last force: turbo_anti_gravity / turbo_glide_anti_gravity x 0.5).
    pub fn gravity_scale(&self) -> Option<f32> {
        match self.flight {
            Flight::None => None,
            Flight::Turbo => Some(self.tuning.gravity_scale),
            Flight::Glide => Some(self.tuning.glide_gravity_scale),
        }
    }

    /// The flight's drive for one frame (0x004799d5 -> 0x00471810 / 0x00455a30): the velocity after it, or None out of a
    /// flight. The drive's force is the wanted minus the current velocity times the power (scaled down by max speed / |d|
    /// beyond the max speed), applied over the frame: in the turbo state the current velocity counts as zero, so the
    /// stick accelerates without limit; gliding it is the body's velocity, and the wanted one goes forward at
    /// glide_xyvel plus the stick and down at most glide_zvel (up never).
    pub fn air_velocity(&self, vel: Vec3, fwd: Vec3, right: Vec3, dt: f32) -> Option<Vec3> {
        let t = &self.tuning;
        let s = self.stick;
        let stick = s.length().min(1.0);
        let sp = |max: f32, a: f32| self.ramp(max, a);
        let (wanted, current, radius, power) = match self.flight {
            Flight::None => return None,
            Flight::Turbo => (fwd * sp(t.fwd_speed, s.y) + right * sp(t.side_speed, s.x), Vec3::ZERO, t.fwd_speed.max(t.side_speed), t.power),
            Flight::Glide => {
                let back = s.y < 0.0;
                let wish = self.glide_wish();
                let ahead = t.glide_xyvel + wish.y;
                let down = vel.y.clamp(-t.glide_zvel, 0.0);
                let wanted = fwd * ahead + right * wish.x + Vec3::Y * down;
                let fwd_max = if back { t.glide_bkd_speed } else { t.glide_fwd_speed };
                (wanted, vel, t.glide_side_speed.max(fwd_max), (1.0 - stick) * t.glide_power.0 + stick * t.glide_power.1)
            }
        };
        let d = wanted - current;
        let dist = d.length();
        let strength = if radius > 0.0 && dist > radius { power * radius / dist } else { power };
        let mut v = vel + d * strength * dt;
        if self.flight == Flight::Turbo {
            // (the turbo state's drive is horizontal: the wanted velocity has no vertical part and the current counts
            // as zero, so the vertical speed is gravity's alone)
            v.y = vel.y;
        }
        Some(v)
    }

    /// The speed for a stick deflection `a` of a direction whose top speed is `max`: max x a + (1 - a) x min
    /// (game_06.cpp:2586, the ramps +0x998 / +0x994).
    fn ramp(&self, max: f32, a: f32) -> f32 {
        let min = self.tuning.speed_min.min(max);
        if a > 0.0 {
            max * a + (1.0 - a) * min
        } else if a < 0.0 {
            -(max * -a + (1.0 + a) * min)
        } else {
            0.0
        }
    }

    /// The glide's wish speeds from the stick (x right, y forward), before the glide's own forward speed: the side
    /// and forward / back speeds of game_06.cpp:2560-2610 (flag 4: +0xaa0, +0xa98 / +0xa9c).
    fn glide_wish(&self) -> Vec2 {
        let t = &self.tuning;
        let s = self.stick;
        let fwd_max = if s.y < 0.0 { t.glide_bkd_speed } else { t.glide_fwd_speed };
        Vec2::new(self.ramp(t.glide_side_speed, s.x), self.ramp(fwd_max, s.y))
    }

    /// The turn sound (0x00476a10): a turn's start while gliding, then not again until 0.2 s without turning.
    pub fn turn_sound(&mut self, dt: f32) {
        if self.turn != 0.0 {
            if self.turn_timer == 0.0 && self.pack && self.flight == Flight::Glide {
                self.events.push(JetEvent::Turn);
            }
            self.turn_timer = 0.2;
        } else {
            self.turn_timer = (self.turn_timer - dt).max(0.0);
        }
    }

    /// The pack's own anim (0x00475d50): gliding in the air a wing cycle by the stick (or the turn), else turbo_idle.
    pub fn wing_anim(&self, airborne: bool) -> &'static str {
        if self.flight != Flight::Glide || !airborne {
            return "turbo_idle";
        }
        // the direction (0x00476a10's arguments, game_06.cpp:3075): the drive's wish speeds (x to the left, y
        // forward; its z is always 0, so the up cycle is for no speed at all), or (-turn, 0) while turning in the air
        // from a launch (flag 0x80)
        let wish = self.glide_wish();
        let (x, y) = if self.launched && self.turn != 0.0 { (-self.turn, 0.0) } else { (-wish.x, wish.y) };
        if x == 0.0 && y == 0.0 {
            "turbo_up_cycle"
        } else if y.abs() >= x.abs() {
            if y > 0.0 { "turbo_forward_cycle" } else { "turbo_backward_cycle" }
        } else if x > 0.0 {
            "turbo_left_cycle"
        } else {
            "turbo_right_cycle"
        }
    }
}

/// The wing sounds of a pack's wearer (Jumper_PlayWingSounds 0x0047bb30, Roman's and the jumper captains'): opening
/// plays veh_jumper_wings_out and starts the veh_jumper_glide_lp loop on the wearer, keeping its voice (the captain's
/// +0xb0c); closing plays veh_jumper_wings_in and stops that loop (audio +0xd8 with the handle: only this wearer's).
#[derive(Debug, Default)]
pub struct WingSounds {
    glide_loop: Option<crate::audio::SoundId>,
}

impl WingSounds {
    pub fn open(&mut self, wearer: Entity, sounds: &mut MessageWriter<crate::audio::PlaySound>) {
        let id = crate::audio::SoundId::new();
        sounds.write(crate::audio::PlaySound::on("veh_jumper_wings_out", wearer));
        sounds.write(crate::audio::PlaySound::on("veh_jumper_glide_lp", wearer).looping(true).with_id(id));
        self.glide_loop = Some(id);
    }

    pub fn close(&mut self, wearer: Entity, sounds: &mut MessageWriter<crate::audio::PlaySound>, stop: &mut MessageWriter<crate::audio::StopVoice>) {
        sounds.write(crate::audio::PlaySound::on("veh_jumper_wings_in", wearer));
        if let Some(id) = self.glide_loop.take() {
            stop.write(crate::audio::StopVoice { id, immediate: false });
        }
    }
}

/// An effect on a pack's "chest", following it (PlayerObject_PlayChestFx 0x0047bba0: Fx_Create flag 4 at the pack's
/// "chest", which sits on the wearer's): Roman's launch (fx_turbo_launch) and the jumper captains'
/// (fx_turbo_launch_captain). `chest` is the wearer's chest joint, `wearer` the fallback.
pub fn chest_fx(effect: &str, chest: Option<Entity>, wearer: Entity, joints: &Query<&GlobalTransform>) -> crate::fx::StartFx {
    let at = chest.and_then(|c| joints.get(c).ok()).or_else(|| joints.get(wearer).ok()).map(|g| g.translation()).unwrap_or_default();
    crate::fx::StartFx { name: effect.into(), at, rotation: crate::level::game_to_engine(), follow: chest.or(Some(wearer)), position_only: true }
}

/// Gives Roman's pack (the pickup, a playtest): false when he has one (0x00472980 refuses).
pub fn give_pack(jet: &mut JetPack) -> bool {
    if jet.pack {
        return false;
    }
    jet.pack = true;
    true
}

/// Puts a boost on the pack (pickup_turbo_boost, 0x004729c0: refused when pack and boost are both there).
pub fn give_boost(jet: &mut JetPack) -> bool {
    if jet.pack && jet.boost {
        return false;
    }
    jet.boost = true;
    true
}

pub struct JetPackPlugin;

impl Plugin for JetPackPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, equip)
            .add_systems(Update, (effects, models).chain().after(crate::player::control).run_if(crate::playing))
            .add_systems(Update, log);
    }
}

/// A new Roman gets his pack component: the pack when the mission gives it (player_has_jetpack).
fn equip(mut commands: Commands, q: Query<Entity, Added<crate::player::Roman>>, data: Option<Res<crate::gamedata::GameData>>, mission: Option<Res<crate::mission::Mission>>) {
    let Some(data) = data else { return };
    for e in &q {
        let pack = mission.as_ref().is_some_and(|m| m.loadout.jetpack);
        commands.entity(e).insert(JetPack::new(Tuning::from_props(&data.template_props("trotter")), pack));
        if pack {
            info!("EVENT jetpack: Roman starts with the jumper pack");
        }
    }
}

/// The "chest" joint of a rig (Roman's, a jumper captain's), which a pack rides.
pub(crate) fn chest(a: &crate::skin::Animator) -> Option<Entity> {
    a.skeleton.index.get("chest").and_then(|&i| a.joints.get(i).copied())
}

/// The frame's pack events: the launch fx on the chest (PlayerObject_PlayChestFx 0x0047bba0: Fx_Create flag 4 at the
/// pack's "chest"), the wing sounds (Jumper_PlayWingSounds 0x0047bb30: wings_out then the glide loop; wings_in and the
/// loop stopped), the turn sound (one of veh_jumper_turn_1 / 2, table 0x005fb9d4 dealt by 0x00493b70).
fn effects(
    mut q: Query<(Entity, &mut JetPack, &crate::skin::Animator)>,
    joints: Query<&GlobalTransform>,
    mut fx: MessageWriter<crate::fx::StartFx>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stop: MessageWriter<crate::audio::StopVoice>,
    mut deck: crate::levelreset::PerLevel<(crate::audio::RandomDeck, crate::fxvm::CrtRand)>,
) {
    let (deck, rng) = deck.get();
    for (e, mut jp, a) in &mut q {
        for ev in std::mem::take(&mut jp.events) {
            match ev {
                JetEvent::Launch => {
                    fx.write(chest_fx("fx_turbo_launch", chest(a), e, &joints));
                    info!("EVENT jetpack: launch");
                }
                JetEvent::WingsOut => {
                    jp.wings.open(e, &mut sounds);
                    info!("EVENT jetpack: wings out");
                }
                JetEvent::WingsIn => {
                    jp.wings.close(e, &mut sounds, &mut stop);
                    info!("EVENT jetpack: wings in");
                }
                JetEvent::Turn => {
                    const TURN: [&str; 2] = ["veh_jumper_turn_1", "veh_jumper_turn_2"];
                    let k = deck.deal(TURN.len(), || rng.rand() as f32 / 32768.0);
                    sounds.write(crate::audio::PlaySound::on(TURN[k], e));
                }
            }
        }
    }
}

/// The pack's model on Roman (PlayerObject_SetTurboPack 0x0047b9f0: "turbo" by its own "chest" at his "chest",
/// turbo_idle) with the boost's on the pack's ref_turbo (PlayerObject_SetTurboBoost 0x0047b900:
/// "pickup_turbo_boost"), and the wing anims (0x00475d50).
#[derive(Component)]
struct PackModel {
    boost: Option<Entity>,
    /// its own "chest" put on Roman's (once its rig exists)
    aligned: bool,
}

/// A bone's rest transform in its model's space (the rig's joints chain their rest transforms from the model's root).
pub(crate) fn model_space(s: &crate::skin::Skeleton, mut i: usize) -> Mat4 {
    let mut m = s.rest[i].to_matrix();
    while let Ok(p) = usize::try_from(s.parents[i]) {
        m = s.rest[p].to_matrix() * m;
        i = p;
    }
    m
}

#[allow(clippy::type_complexity)]
fn models(
    mut commands: Commands,
    roman: Query<(&JetPack, &crate::skin::Animator, &crate::player::Roman), Without<PackModel>>,
    mut packs: Query<(Entity, &mut PackModel, &mut crate::skin::Animator), Without<crate::player::Roman>>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
) {
    let Ok((jp, ra, r)) = roman.single() else { return };
    let Some((pe, mut pm, mut a)) = packs.iter_mut().next() else {
        if !jp.pack {
            return;
        }
        let (Some(chest), Some(model)) = (chest(ra), data.templates.get("turbo").and_then(|t| t.model.clone())) else { return };
        if let Some(p) = kit.spawn(&mut commands, &model, Transform::IDENTITY, true) {
            commands.entity(p).insert(PackModel { boost: None, aligned: false });
            commands.entity(chest).add_child(p);
        }
        return;
    };
    if !jp.pack {
        commands.entity(pe).despawn();
        return;
    }
    if !pm.aligned {
        // Object_Attach's child bone: the pack's "chest" (rest pose) on Roman's chest joint
        if let Some(&i) = a.skeleton.index.get("chest") {
            commands.entity(pe).insert(Transform::from_matrix(model_space(&a.skeleton, i).inverse()));
        }
        pm.aligned = true;
    }
    let wings = jp.wing_anim(!r.on_ground && r.grace <= 0.0);
    if a.layers.first().is_none_or(|l| l.anim != wings) {
        // (0x00408e30 with weight 0.25)
        a.blend = 0.25;
        a.play(0, wings, true, crate::skin::Mask::All);
    }
    match (jp.boost, pm.boost) {
        (true, None) => {
            let joint = a.skeleton.index.get("ref_turbo").and_then(|&i| a.joints.get(i).copied());
            let model = data.templates.get("pickup_turbo_boost").and_then(|t| t.model.clone());
            if let (Some(j), Some(m)) = (joint, model) {
                if let Some(b) = kit.spawn(&mut commands, &m, Transform::IDENTITY, true) {
                    commands.entity(j).add_child(b);
                    pm.boost = Some(b);
                }
            }
        }
        (false, Some(b)) => {
            commands.entity(b).despawn();
            pm.boost = None;
        }
        _ => {}
    }
}

/// The playtest state log's "jetpack": the pack, its boost, the flight, the pack model on Roman and its wing anim.
fn log(q: Query<&JetPack>, models: Query<&crate::skin::Animator, With<PackModel>>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let (Some(mut state), Ok(jp)) = (state, q.single()) else { return };
    let wings = models.iter().next().and_then(|a| a.layers.first()).map(|l| l.anim.clone());
    state.0.insert("jetpack".into(), serde_json::json!({ "pack": jp.pack, "boost": jp.boost, "flight": format!("{:?}", jp.flight), "model": models.iter().count(), "wings": wings }));
}

#[cfg(test)]
mod tests {
    use super::*;

    fn lev_3_m1() -> Tuning {
        // p_trotter on lev_3_m1 (work/remaster/lev_3_m1/properties.json)
        Tuning {
            fwd_speed: 23.0,
            side_speed: 10.0,
            force: 1000.0,
            boost_force: 1750.0,
            time: 0.2,
            gravity_scale: 2.5,
            power: 2.5,
            glide_zvel: 25.0,
            glide_xyvel: 40.0,
            glide_fwd_speed: 50.0,
            glide_bkd_speed: 55.0,
            glide_side_speed: 50.0,
            glide_power: (4.7, 4.7),
            glide_gravity_scale: 0.5,
            speed_min: 4.0,
            fall_speed: -200.0,
            land_speed: 40.0,
            landing_damage: crate::player::LandingDamage { min_hurt: 100.0, max_hurt: 100.0, min_speed: 350.0, max_speed: 350.0 },
            death_time: 6.0,
            death_distance: 600.0,
        }
    }

    #[test]
    fn launch_tap_and_glide() {
        let mut jp = JetPack::new(lev_3_m1(), true);
        // a tap on the ground: the turbo state, the whole kick (200) on the next frame
        assert!(jp.jump_input(true, true, true, false, false, 0.0));
        assert_eq!(jp.flight, Flight::Turbo);
        assert!(!jp.landed(), "still on the ground before the kick");
        assert_eq!(jp.take_kick(), Some(200.0));
        assert_eq!(jp.take_kick(), None);
        // rising, Jump up: nothing yet
        assert!(!jp.jump_input(false, false, false, false, false, 50.0));
        assert_eq!(jp.flight, Flight::Turbo);
        // past the top: the wings by themselves (oracle: wings_out at the frame vz turns negative)
        jp.jump_input(false, false, false, false, false, -0.6);
        assert_eq!(jp.flight, Flight::Glide);
        // Jump held folds them, let go opens them again
        jp.jump_input(true, true, false, false, false, -20.0);
        assert_eq!(jp.flight, Flight::Turbo);
        jp.jump_input(false, true, false, false, false, -20.0);
        assert_eq!(jp.flight, Flight::Turbo);
        jp.jump_input(false, false, false, false, false, -21.0);
        assert_eq!(jp.flight, Flight::Glide);
        assert_eq!(jp.events, vec![JetEvent::Launch, JetEvent::WingsOut, JetEvent::WingsIn, JetEvent::WingsOut]);
    }

    #[test]
    fn no_pack_no_launch_and_boost_used_up() {
        let mut jp = JetPack::new(lev_3_m1(), false);
        assert!(!jp.jump_input(true, true, true, false, false, 0.0));
        assert!(give_pack(&mut jp) && !give_pack(&mut jp));
        assert!(give_boost(&mut jp) && !give_boost(&mut jp));
        assert!(jp.jump_input(true, true, true, false, false, 0.0));
        assert_eq!(jp.take_kick(), Some(350.0));
        assert!(!jp.boost);
    }

    #[test]
    fn glide_settles_as_measured() {
        // the original's glide on lev_3_m1, no stick: 40 units/s forward, -31.8 down
        let mut jp = JetPack::new(lev_3_m1(), true);
        jp.flight = Flight::Glide;
        let (fwd, right) = (Vec3::NEG_Z, Vec3::X);
        let mut v = Vec3::new(0.0, -120.0, 0.0);
        let dt = 1.0 / 60.0;
        for _ in 0..600 {
            v = jp.air_velocity(v, fwd, right, dt).unwrap();
            v.y -= 64.348 * jp.gravity_scale().unwrap() * dt;
        }
        assert!((v.dot(fwd) - 40.0).abs() < 0.5, "{v}");
        assert!((v.y + 31.8).abs() < 0.3, "{v}");
        // W held: 40 + 50
        jp.stick = Vec2::new(0.0, 1.0);
        for _ in 0..600 {
            v = jp.air_velocity(v, fwd, right, dt).unwrap();
            v.y -= 64.348 * jp.gravity_scale().unwrap() * dt;
        }
        assert!((v.dot(fwd) - 90.0).abs() < 1.0, "{v}");
        // S held: 40 - 55 = 15 back (oracle: 14.8)
        jp.stick = Vec2::new(0.0, -1.0);
        for _ in 0..900 {
            v = jp.air_velocity(v, fwd, right, dt).unwrap();
            v.y -= 64.348 * jp.gravity_scale().unwrap() * dt;
        }
        assert!((v.dot(fwd) + 15.0).abs() < 1.0, "{v}");
    }

    #[test]
    fn wings_by_stick() {
        let mut jp = JetPack::new(lev_3_m1(), true);
        assert_eq!(jp.wing_anim(true), "turbo_idle");
        jp.flight = Flight::Glide;
        assert_eq!(jp.wing_anim(false), "turbo_idle");
        assert_eq!(jp.wing_anim(true), "turbo_up_cycle");
        jp.stick = Vec2::new(0.0, -1.0);
        assert_eq!(jp.wing_anim(true), "turbo_backward_cycle");
        jp.stick = Vec2::new(1.0, 0.0);
        assert_eq!(jp.wing_anim(true), "turbo_right_cycle");
        // a turn picks the side only in the air from a launch (flag 0x80): gliding after a fall the stick decides
        jp.stick = Vec2::new(0.0, 1.0);
        jp.turn = 0.1;
        assert_eq!(jp.wing_anim(true), "turbo_forward_cycle");
        jp.launched = true;
        assert_eq!(jp.wing_anim(true), "turbo_right_cycle");
        jp.turn = -0.1;
        assert_eq!(jp.wing_anim(true), "turbo_left_cycle");
    }

    #[test]
    fn landing_ends_the_launch() {
        let mut jp = JetPack::new(lev_3_m1(), true);
        assert!(jp.jump_input(true, true, true, false, false, 0.0));
        assert!(jp.launched);
        jp.take_kick();
        assert!(jp.landed());
        assert!(!jp.launched);
    }
}
