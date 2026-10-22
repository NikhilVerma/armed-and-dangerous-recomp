//! Twiglets (Twiglet, vtable 0x005af020; src/ai/ai_08.cpp, ai_09.cpp; docs/subsystems/twiglets.md): little wooden
//! creatures that come out of the dense trees (lev_4_m1's, the only ones a trigger activates in the shipped data) or are
//! summoned by monks (lev_4_m1, lev_4_m2), pull a bomb from their back and run at the player to blow it up at his feet.
//!
//! Three parts, as in the original:
//! - [`Generator`]: a tree with behaviour `twiglet_generator` (TwigletGenerator_Slot6 0x0049d3c0): `release_max`
//!   twiglets to give, `trigger_activate` / `trigger_deactivate` (TwigletGenerator_Slot14 0x0049d530 puts it on / takes
//!   it off the swarm's list 0x0063a140), up to three `initial_ic_N` mover anims the twiglets come out by (nodes of the
//!   tree's reference anim of the same name).
//! - [`Swarm`]: the controller (TwigletController_Slot47 0x0049d080, one per level, made by the first generator,
//!   0x0049ce00): every 0.1..0.25 s, while fewer than 100 twiglets live, the active generator with the most twiglets
//!   left gives one (0x0049d590); its swarm loop plays while any has some left.
//! - [`Twiglet`]: the creature's update (Twiglet_Update 0x0049dcf0, a switch on +0x464: [`State`]).
//!
//! The monk's summon (ai.rs state 0xf2 / 0xf3, ai_06.cpp:2906-2972) makes five twiglets round the monk through
//! [`SpawnTwiglet`].
//!
//! A twiglet made by code (the swarm, the monk: ObjectSystem_CreateObject without a property block) starts in state 0
//! and its maker wakes it (0x0049f380): the swarm's at once, the monk's after as many seconds as twiglets he made
//! before it. The idle state 0x01 (trigger_los, activation_range, receive_trigger_activate: Twiglet_Slot6 with a
//! property block) is for twiglets placed in a level, and no level places one: not ported.
//!
//! Not ported (no caller reaches them in the shipped game): the spear thrower (flag bit 0 of +0x460: states 0x14 /
//! 0x15 / 0x16 / 0x18, long_range_dist, the "fire" note's twiglet_spear; nothing sets the bit, so Twiglet_RunToTarget
//! always picks 0x0a), the leap onto the player (0x3c / 0x3d / 0x3e and the "hit" note's claw: nothing enters 0x3c or
//! 0x0b), the dying fade 0x28 (nothing enters it), bof_anti_gravity (Twiglet_Vt8C under flag bit 4, which nothing
//! sets). Deferred: the knockout glove's grab / throw / punch of a twiglet (vt+0x120 0x004a0420.., states 0x64..0x66;
//! feature knockout-bomb).

use crate::levelreset::LevelAppExt;
use bevy::prelude::*;
use oxijolt::prelude::{Activation, AllowedDofs, BodySettings, Shape};

/// 0x0063a148: the swarm stops giving twiglets while this many live (0x0049cdc0).
pub const MAX_ALIVE: usize = 100;
/// Twiglet_Slot6: +0x578, seconds a twiglet lives before it may wander too far from where it was made.
const LEASH_DELAY: f32 = 10.0;
/// Twiglet_Update state 0x0a: within this (2D) of the player a carried bomb is set off, 10 (K 0x005bcb60 = 100 on
/// the squared distance).
const BOMB_REACH: f32 = 10.0;
/// state 0x0a: within this (2D, 400 squared: K 0x005bcf7c) the repath aims at a point 5 units off the player.
const CLOSE_REPATH: f32 = 20.0;
/// state 0x0a's and the summon's sidestep, 5 units (K 0x005bcbdc).
const SIDESTEP: f32 = 5.0;
/// A black hole's carry-off (damage flag 0x800): the run away from it (K 0x005bcf00), the pull's speed factor on
/// move_speed (K 0x005bce20) and its rate (the drive's mass x 5, K 0x005bcbdc), swallowed within 2 (K 0x005bcb80).
const FLEE: f32 = 250.0;
const PULL_SPEED: f32 = 1.5;
const PULL_RATE: f32 = 5.0;
const SWALLOW: f32 = 2.0;
/// The body: a capsule round the 4 x 2.5 x 5.2 model (twiglet_coll_0's bounds).
const RADIUS: f32 = 1.5;
const HEIGHT: f32 = 5.2;
/// 0x00454d80's force gain: mass x 50 (0x00471750's speed argument), i.e. 50 / s on the velocity error
const DRIVE_GAIN: f32 = 50.0;
/// the twiglet's own turn to its way (0x00454e60 with 16: about a sixteenth of the gap a frame, guess)
const TURN_RATE: f32 = 8.0;

/// A tree that gives twiglets (TwigletGenerator 0x005aed80).
#[derive(Component, Clone, Debug)]
pub struct Generator {
    /// trigger_activate (+0x460) / trigger_deactivate (+0x464)
    pub activate: i64,
    pub deactivate: i64,
    /// release_max (+0x470, default 10): twiglets left to give
    pub left: u32,
    /// the initial IC movers (+0x474 + n * 0x20, "none" skipped) and their origins (game space: the tree's matrix times
    /// the node of the mover's name in its reference anim)
    pub movers: Vec<(String, Mat4)>,
    /// +0x46c: the last mover used (pre-incremented: the first twiglet takes the second mover)
    pub next: usize,
    /// on the swarm's list: the order it joined (the list is walked in that order)
    pub active: Option<u64>,
    /// the tree's own matrix (game space): twiglets without movers are placed round it
    pub world: Mat4,
}

impl Generator {
    /// TwigletGenerator_Slot6 0x0049d3c0: the properties, the movers found among the tree's nodes (`node`: model space,
    /// game axes).
    pub fn from_props(p: &crate::gamedata::Props, world: Mat4, node: impl Fn(&str) -> Option<Mat4>) -> Self {
        let movers = (0..3)
            .filter_map(|i| p.s(&format!("initial_ic_{i}")).map(str::to_owned))
            .filter(|n| !n.eq_ignore_ascii_case("none"))
            // (0x00453c50 leaves the matrix as the tree's own when the node is missing: guess, every level's has it)
            .map(|n| {
                let m = node(&n).unwrap_or(Mat4::IDENTITY);
                (n.to_lowercase(), world * m)
            })
            .collect();
        Generator {
            activate: p.i("trigger_activate", 0),
            deactivate: p.i("trigger_deactivate", 0),
            left: p.i("release_max", 10).max(0) as u32,
            movers,
            next: 0,
            active: None,
            world,
        }
    }

    /// 0x0049d590's mover choice: +0x46c pre-incremented, wrapped.
    fn take_mover(&mut self) -> Option<(String, Mat4)> {
        if self.movers.is_empty() {
            return None;
        }
        self.next = (self.next + 1) % self.movers.len();
        Some(self.movers[self.next].clone())
    }
}

/// The swarm controller's state (TwigletController 0x005a7590 and its globals 0x0063a140..0x0063a14c).
#[derive(Resource, Default)]
pub struct Swarm {
    /// 0x0063a14c: seconds to the next twiglet (rolled at the level start, counting from the first frame of play)
    pub timer: f32,
    /// +0x460: the generator that gave the last one
    pub last: Option<Entity>,
    /// +0x464: the swarm loop playing
    pub sound: Option<crate::audio::SoundId>,
    /// Game+0x2710 / +0x270c
    pub made: u32,
    pub killed: u32,
    /// bombs gone off (each an FxExplosion death, which the oracle hook lists among the deaths with id 0)
    pub bombs_gone: u32,
    /// the order generators join the list
    joined: u64,
    /// twiglet_max_distance (Game+0xd60, the level's global_settings: p_globalsettings 1000, lev_4_m1 500), read once
    max_distance: Option<f32>,
    /// topsy_turvy_fall_pct (Game+0xca8, 50): a stunned twiglet hangs on at or above a roll of it
    fall_pct: f32,
    /// twiglets the topsy turvy blew into the sky that died coming down (state 0x52's contact)
    pub landed: u32,
    /// twiglets a black hole carried off (damage flag 0x800), and the ones it swallowed
    pub carried: u32,
    pub swallowed: u32,
}

/// The twiglets' own random numbers (the game's CRT rand form, crate::fxvm::CrtRand): kept apart from the shared
/// FxStats stream so that a level without twiglets draws exactly as before and the grunts' rolls do not shift.
#[derive(Resource, Default)]
pub struct TwigletRng(crate::fxvm::CrtRand);

impl TwigletRng {
    /// rand() / 32768: [0, 1)
    pub fn rand(&mut self) -> f32 {
        self.0.rand() as f32 / 32768.0
    }
    /// Rand_FloatRange 0x0046e2c0: [lo, hi)
    pub fn range(&mut self, lo: f32, hi: f32) -> f32 {
        self.0.r(lo, hi)
    }
}

/// What a twiglet is doing: Twiglet_Update's states (+0x464), [`State::code`] the original's number.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum State {
    /// 0x3f: standing (twiglet_stand) for its wake's time, then running (0x0049f380)
    Wait,
    /// 0x32: coming out of its tree along an IC mover (0x0049fbe0 / 0x0049fb60), then running (0x0049fab0)
    Emerging,
    /// 0x0a: running at the player (twiglet_run / twiglet_run_with_bomb)
    Run,
    /// 0x46: pulling a bomb out (twiglet_get_bomb_out; its note "get_bomb" makes it, 0x0049f930)
    GetBomb,
    /// 0x1e: a light hit (twiglet_hit_mgun*), 0x1f: a heavier one (twiglet_hit_med*)
    HitLight,
    HitHeavy,
    /// 0x50: hanging on while the world is upside down (twiglet_hang_into, then twiglet_hanging)
    Hang,
    /// 0x51: let go, falling into the sky (twiglet_blown_up_into / twiglet_blown_up; the custom-gravity list World+0xf1c
    /// with the getter Twiglet_Vt8C 0x004a0380's -1), for what is left of the topsy turvy
    BlownUp,
    /// 0x52: off the gravity list, falling back; dies at its first contact (Twiglet_VtA8 0x0049fe60)
    Falling,
    /// 0x5b: carried off by a black hole, first running 250 units away from it (twiglet_run) to its route's end
    Flee,
    /// 0x5a: then pulled into it, across at 1.5 x its speed and, after 1..2 s, up too (twiglet_blown_up); gone
    /// within 2 of it, or when the hole is
    Pull,
}

impl State {
    pub fn code(self) -> u32 {
        match self {
            State::Wait => 0x3f,
            State::Emerging => 0x32,
            State::Run => 0x0a,
            State::GetBomb => 0x46,
            State::HitLight => 0x1e,
            State::HitHeavy => 0x1f,
            State::Hang => 0x50,
            State::BlownUp => 0x51,
            State::Falling => 0x52,
            State::Flee => 0x5b,
            State::Pull => 0x5a,
        }
    }
}

/// One twiglet.
#[derive(Component, Debug)]
pub struct Twiglet {
    pub state: State,
    body: oxijolt::prelude::BodyId,
    /// +0x468: the repath timer
    repath: f32,
    /// +0x468 in states 0x3f / 0x5a: seconds left standing / before the black hole lifts it
    wait: f32,
    /// +0x46c: seconds to its next call (ctr_twiglet_call_1..3)
    call: f32,
    /// +0x56c / +0x578: where it was made and the leash delay
    home: Vec3,
    leash: f32,
    /// +0x480 move_speed (46; p_twiglet: 40), +0x490 hit_level_1 (10)
    speed: f32,
    hit_level: f32,
    /// +0x484 move_power (30; p_twiglet: 100): the gain of the vertical drive while blown up or falling (states 0x51 /
    /// 0x52: a force of mass x move_power on the velocity's error)
    power: f32,
    /// +0x568: the black hole that carried it off
    hole: Option<Entity>,
    /// +0x55c: the bomb it carries
    pub bomb: Option<Entity>,
    /// the way to the player (the remaster's nav replaces the map's route, 0x0046f6d0 / 0x00470aa0)
    route: Vec<Vec3>,
    yaw: f32,
    last_health: f32,
    /// +0x470: the topsy turvy's stun (rand 0..1 s from its hit, then the hang's time), +0x474: the topsy turvy's time
    /// left (Twiglet_TakeDamage 0x0049f530, damage flag 0x20)
    stun: f32,
    topsy_left: f32,
    /// the anim the state plays (restarted when the state changes)
    anim: &'static str,
    anim_once: bool,
}

/// A carried bomb (twiglet_bomb: behaviour fx_explosion), attached at the twiglet's ref_hand_00.
#[derive(Component)]
pub struct TwigletBomb;

/// A twiglet to make: at `at` (game space), coming out along `mover` when given (state 0x32); else woken by its maker
/// (0x0049f380): it stands `wait` seconds (state 0x3f), then runs at the player (at once when `wait` is 0).
#[derive(Message, Clone, Debug)]
pub struct SpawnTwiglet {
    pub at: Mat4,
    pub mover: Option<String>,
    pub wait: f32,
}

impl SpawnTwiglet {
    /// One that stands `wait` seconds where it is made, then runs (the monk's summon: 0, 1, 2.. by the order made).
    pub fn standing(at: Mat4, wait: f32) -> Self {
        SpawnTwiglet { at, mover: None, wait }
    }
}

pub struct TwigletPlugin;

impl Plugin for TwigletPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<Swarm>()
            .init_level_resource::<TwigletRng>()
            .add_level_message::<SpawnTwiglet>()
            .add_systems(Update, (activate, controller, spawn, carry_off, think, notes, log).chain().run_if(crate::playing))
            // a death as fx.rs reports it, before level.rs's husks remove the object (husks run after mission.rs deaths)
            .add_systems(Update, deaths.after(crate::fx::FxSet).before(crate::mission::deaths).run_if(crate::playing));
    }
}

/// TwigletGenerator_Slot14 0x0049d530: its activate trigger puts it on the swarm's list, its deactivate trigger takes
/// it off (0x0049ce70).
fn activate(
    mut triggers: MessageReader<crate::mission::SendTrigger>,
    mut q: Query<&mut Generator>,
    mut swarm: ResMut<Swarm>,
    data: Res<crate::gamedata::GameData>,
    launch: Res<crate::Launch>,
    mut rng: ResMut<TwigletRng>,
) {
    if swarm.max_distance.is_none() {
        // the controller is made with the level (0x0049ce00 from the first generator's load), its timer rolled then
        // (0x0049cdc0): the first twiglet comes 0.1..0.25 s after the first activation at the earliest
        swarm.timer = spawn_delay(&mut rng);
        let scene = crate::scene::read_scene(&launch.scene);
        let mut gs = data.props("p_globalsettings");
        for (k, v) in &scene.settings {
            gs.0.insert(k.clone(), v.clone());
        }
        swarm.max_distance = Some(gs.f("twiglet_max_distance", 1000.0));
        swarm.fall_pct = gs.f("topsy_turvy_fall_pct", 50.0);
    }
    // (trigger 0, every unset tree's, is never sent in the shipped data: dropped so that no stray 0 wakes them all)
    let got: Vec<i64> = triggers.read().map(|t| t.0).filter(|t| *t != 0).collect();
    for t in got {
        for mut g in &mut q {
            // the activate trigger is tested first: a tree with both the same is only ever put on the list
            if g.activate == t {
                if g.active.is_none() {
                    swarm.joined += 1;
                    g.active = Some(swarm.joined);
                    info!("EVENT twiglet: generator activated by {t} ({} to give)", g.left);
                }
            } else if g.deactivate == t && g.active.is_some() {
                g.active = None;
                info!("EVENT twiglet: generator deactivated by {t}");
            }
        }
    }
}

/// The spawn timer 0x0063a14c: rand() * 4.58e-6 + 0.1 (K 0x005bccb4, 0x005bcbfc): 0.1..0.25 s.
fn spawn_delay(rng: &mut TwigletRng) -> f32 {
    rng.range(0.1, 0.25)
}

/// TwigletController_Slot47 0x0049d080: the swarm's update.
#[allow(clippy::too_many_arguments)]
fn controller(
    time: Res<Time>,
    mut swarm: ResMut<Swarm>,
    mut gens: Query<(Entity, &mut Generator, &GlobalTransform)>,
    alive: Query<(), With<Twiglet>>,
    mut rng: ResMut<TwigletRng>,
    mut spawns: MessageWriter<SpawnTwiglet>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    mut stops: MessageWriter<crate::audio::StopVoice>,
) {
    swarm.timer -= time.delta_secs();
    if swarm.timer > 0.0 {
        return;
    }
    let mut stop = |swarm: &mut Swarm| {
        if let Some(id) = swarm.sound.take() {
            stops.write(crate::audio::StopVoice { id, immediate: false });
        }
    };
    if alive.iter().count() >= MAX_ALIVE {
        stop(&mut swarm);
        return;
    }
    // the active list in the order the generators joined it
    let mut list: Vec<(u64, Entity, u32, usize, Vec3)> =
        gens.iter().filter_map(|(e, g, gt)| g.active.map(|o| (o, e, g.left, g.movers.len(), gt.translation()))).collect();
    list.sort_by_key(|l| l.0);
    // the one with the most left (the last giver only as a fallback when it has exactly one mover)
    let (mut best, mut most, mut fallback, mut any) = (None, 0u32, None, false);
    for &(_, e, left, movers, at) in &list {
        if left > most {
            if Some(e) == swarm.last && movers == 1 {
                fallback = Some(e);
            } else {
                best = Some(e);
                most = left;
            }
        }
        if left > 0 {
            any = true;
            if swarm.sound.is_none() {
                // (rand() * 100 & ~0x7fff) < 0x190000: rand() < 16384
                let name = if rng.rand() < 0.5 { "ctr_twiglet_swarm_1_lp" } else { "ctr_twiglet_swarm_2_lp" };
                let id = crate::audio::SoundId::new();
                sounds.write(crate::audio::PlaySound { id: Some(id), ..crate::audio::PlaySound::at(name, at) });
                swarm.sound = Some(id);
            }
        }
    }
    if let Some(chosen) = best.or(fallback) {
        if let Ok((_, mut g, _)) = gens.get_mut(chosen) {
            spawns.write(release(&mut g, &mut rng));
        }
        // (the wake of the closest busy twiglet, 0x0049f380, needs flag bit 0, which no twiglet has: not ported)
        swarm.timer = spawn_delay(&mut rng);
        swarm.last = Some(chosen);
    }
    if !any {
        stop(&mut swarm);
    }
}

/// 0x0049d590: one twiglet from a generator: out of its next mover, or (no movers) 30..100 units round the tree at a
/// random turn of its facing, woken at once by the controller (0x0049f380 with 0: it runs); one less to give.
fn release(g: &mut Generator, rng: &mut TwigletRng) -> SpawnTwiglet {
    let ev = match g.take_mover() {
        Some((mover, origin)) => SpawnTwiglet { at: origin, mover: Some(mover), wait: 0.0 },
        None => {
            // the facing (0x00453150: the model's -y, guess) turned about z by rand * 1.9e-4 - pi (K 0x005bcdbc,
            // 0x005bcb70), normalised in 3D (a tilted tree's way out is the shorter across), its x and y used
            let a = rng.range(-std::f32::consts::PI, std::f32::consts::PI);
            let dir = (Quat::from_rotation_z(a) * g.world.transform_vector3(Vec3::NEG_Y)).normalize_or(Vec3::NEG_Y);
            // x and y each their own distance: rand * 2.14e-3 + 30 (K 0x005bcf2c, 0x005bccbc)
            let at = g.world.w_axis.truncate() + Vec3::new(dir.x * rng.range(30.0, 100.0), dir.y * rng.range(30.0, 100.0), 0.0);
            SpawnTwiglet { at: Mat4::from_translation(at), mover: None, wait: 0.0 }
        }
    };
    g.left = g.left.saturating_sub(1);
    ev
}

/// Twiglet_Slot6 0x0049d7a0 with its template's properties (p_twiglet): health, speed, move_power; then the IC start
/// (0x0049fbe0 + 0x0049fb60) when it comes out of a tree, else its wake (0x0049f380).
#[allow(clippy::too_many_arguments)]
fn spawn(
    mut commands: Commands,
    mut events: MessageReader<SpawnTwiglet>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut rng: ResMut<TwigletRng>,
    mut swarm: ResMut<Swarm>,
) {
    for ev in events.read() {
        // Game+0x2710: every twiglet made (the swarm's, the monks')
        swarm.made += 1;
        let p = data.template_props("twiglet");
        let Some(model) = data.templates.get("twiglet").and_then(|t| t.model.clone()) else { continue };
        let world = crate::mover::engine_transform(ev.at);
        let e = commands.spawn((world, Visibility::default(), Name::new("twiglet"))).id();
        let Some(bounds) = kit.attach(&mut commands, e, &model, true) else {
            commands.entity(e).despawn();
            continue;
        };
        let k = physics.meters_per_unit;
        let half = (HEIGHT * 0.5 - RADIUS).max(0.1);
        let Ok(shape) = Shape::new_capsule(half * k, RADIUS * k) else { continue };
        let lift = Vec3::Y * HEIGHT * 0.5;
        let s = BodySettings::new_dynamic()
            .mass(10.0)
            .friction(0.0)
            .allowed_dofs(AllowedDofs::TRANSLATION_X | AllowedDofs::TRANSLATION_Y | AllowedDofs::TRANSLATION_Z);
        let Some(body) = physics.add_dynamic(e, &shape, world.translation + lift, Quat::IDENTITY, Vec3::ZERO, s) else { continue };
        physics.materials.insert(body, p.s("material").unwrap_or("wood").to_owned());
        // the damage filter (Twiglet_TakeDamage 0x0049f530): a hit from a living source of its own side (vt+0xe4: the
        // grunts', the monks') does nothing; the player's, his team's and objects' (bombs, kegs) count
        let mut d = crate::vehicles::unit_health(&p, crate::fx::DClass::Building, None, 1.0);
        d.score = p.f("score_value", 0.0);
        let state = if ev.mover.is_some() { State::Emerging } else { State::Wait };
        let tw = Twiglet {
            state,
            body,
            repath: 0.0,
            wait: ev.wait,
            // +0x46c: rand * 1.83e-4 + 4 (K 0x005bcf88, 0x005bcbe0): 4..10 s
            call: rng.range(4.0, 10.0),
            home: world.translation,
            leash: LEASH_DELAY,
            speed: p.f("move_speed", 46.0),
            hit_level: p.f("hit_level_1", 10.0),
            power: p.f("move_power", 30.0),
            hole: None,
            bomb: None,
            route: Vec::new(),
            yaw: crate::ai::ic::yaw_of(world.rotation),
            last_health: d.health,
            stun: 0.0,
            topsy_left: 0.0,
            anim: if ev.mover.is_some() { "" } else { "twiglet_stand" },
            anim_once: false,
        };
        commands.entity(e).insert((
            crate::physics::Body { id: body, offset: lift, sync_rotation: false },
            crate::level::Placed { template: "twiglet".into(), collider: Some(body), husk: None, triggers: Default::default() },
            d,
            bounds,
            crate::hud::AimClass::Other,
            tw,
        ));
        if let Some(m) = &ev.mover {
            let mut mover = crate::mover::Mover::new(m, ev.at);
            mover.generic = false;
            commands.entity(e).insert(mover);
        }
        info!("EVENT twiglet: made at {:?}{}", (ev.at.w_axis.truncate() * 10.0).round() / 10.0, ev.mover.as_deref().map(|m| format!(" out of {m}")).unwrap_or_default());
    }
}

/// The original's world gravity, game units / s^2 (K 0x005bcd64, the throw solvers' -64.348).
const GRAVITY: f32 = 64.348;
/// The twiglet's ground friction (wood: wood_friction 20 against the statics' default_friction 1.0 in the global
/// settings, PhysMaterial_Load 0x00412140). Fitted to the oracle (twiglets_l4m1): with 2 the leading twiglet's distance
/// to the player agrees within 10 units every second of its 12 s run (the lone runners' speeds by slope fit 2.5 best;
/// Havok's geometric-mean combine of 20 and 1 would be 4.5: its contact solver is not modelled, this steady state stands
/// for it).
const GROUND_FRICTION: f32 = 2.0;

/// The ground speed of the AI's drive (0x00454d80, through the route step 0x00470aa0) on a wood body, along its way
/// up a slope of `sin` / `cos` (negative sin: downhill). The drive is a force of mass x 50 x (wanted - current
/// velocity) (0x00471750 with mass * 50: a 0.02 s time constant, so the velocity sits at its steady state), the
/// wanted velocity level at `speed`, its vertical part the body's own while it falls and 0 while it rises: uphill the
/// drive presses the body into the slope (50 x speed x sin per unit mass), and that, gravity along the slope and the
/// ground's friction hold it back:
///   s = speed cos - (g sin + mu g cos + [uphill] 50 mu speed sin) / 50.
/// Oracle twiglets_l4m1 (lone runners, 2D speed by the slope of their way): 0 deg 36-38, 3 deg 33.5, 6 deg 27, 9 deg 19,
/// 12 deg 11, 15 deg 8, downhill 3..9 deg 37.5-37.9; this gives 37.2, 33.1, 28.7, 24.2, 19.7, 15.1 and 37.4.
pub fn ground_speed(speed: f32, sin: f32, cos: f32) -> f32 {
    let press = if sin > 0.0 { DRIVE_GAIN * GROUND_FRICTION * speed * sin } else { 0.0 };
    (speed * cos - (GRAVITY * sin + GROUND_FRICTION * GRAVITY * cos + press) / DRIVE_GAIN).max(0.0)
}

/// The slope of the landscape along `dir` (engine axes, level) at `pos`: (sin, cos) of its rise.
fn slope_along(physics: &crate::physics::Physics, pos: Vec3, dir: Vec3) -> (f32, f32) {
    const RUN: f32 = 3.0;
    // (rays from low down: from high above they spend raycast_landscape's hits in the forest's canopies; the one under
    // the twiglet starts inside its own capsule and passes through it)
    let ground = |p: Vec3, lift: f32| physics.raycast_landscape(p + Vec3::Y * lift, Vec3::NEG_Y, lift + 20.0).map(|h| h.y);
    match (ground(pos, 0.5), ground(pos + dir * RUN, RUN)) {
        (Some(a), Some(b)) => {
            let v = Vec2::new(RUN, b - a).normalize_or(Vec2::X);
            (v.y, v.x)
        }
        _ => (0.0, 1.0),
    }
}

fn flat(v: Vec3) -> Vec3 {
    v.with_y(0.0)
}

/// Twiglet_RunToTarget 0x0049f2b0: the repath timer 1..3 s, a route to the player, the run anim, state 0x0a.
fn run_to(t: &mut Twiglet, pos: Vec3, target: Vec3, physics: &crate::physics::Physics, rng: &mut TwigletRng) {
    t.repath = rng.range(1.0, 3.0);
    t.route = crate::nav::path(physics, pos, target, &[]).unwrap_or_default();
    t.state = State::Run;
}

/// State 0x0a's goal close to the player: 5 units (K 0x005bcbdc) to a random side of him (its facing turned by rand
/// -pi..pi, 0x00453150 / 0x004e36a0).
fn sidestep(target: Vec3, rng: &mut TwigletRng) -> Vec3 {
    let a = rng.range(-std::f32::consts::PI, std::f32::consts::PI);
    target + Quat::from_rotation_y(a) * Vec3::Z * SIDESTEP
}

/// The route step (0x00470aa0): the waypoints within 4 (2D) are passed; the next one, or None at the route's end
/// (AIRoute_HasRoute 0x00470c30 false).
fn next_waypoint(t: &mut Twiglet, pos: Vec3) -> Option<Vec3> {
    while t.route.first().is_some_and(|w| flat(*w - pos).length() < 4.0) {
        t.route.remove(0);
    }
    t.route.first().copied()
}

/// The drive toward `way` (0x00454d80): level, at the ground speed up or down the slope; the velocity and the facing.
fn drive_toward(physics: &crate::physics::Physics, pos: Vec3, way: Vec3, speed: f32) -> (Vec3, Vec3) {
    let dir = flat(way - pos).normalize_or_zero();
    let (sin, cos) = slope_along(physics, pos, dir);
    (dir * ground_speed(speed, sin, cos) * cos, dir)
}

/// The topsy turvy's hit on a twiglet (Twiglet_TakeDamage 0x0049f530 with damage flag 0x20): stunned rand 0..1 s, then
/// it hangs on or is blown away (`blow`), with `duration` (Game+0xc7c, topsy_turvy_duration) of the effect to come.
pub fn topsy_hit(t: &mut Twiglet, duration: f32, rng: &mut TwigletRng) {
    t.stun = rng.rand();
    t.topsy_left = duration;
}

/// 0x004a01c0: the stun's end. Coming out of its tree (an IC move) it shrugs it off. With `may_hang`, more than 3 s of
/// the effect left and rand 100 >= topsy_turvy_fall_pct it hangs on for rand(0.25, left - 1) s (state 0x50); otherwise
/// it is blown away (state 0x51): stopped dead (Body_setLinearVelocity 0x00458510 with 0), into the sky.
fn blow(t: &mut Twiglet, may_hang: bool, fall_pct: f32, rng: &mut TwigletRng, physics: &mut crate::physics::Physics) {
    if t.state == State::Emerging {
        t.stun = 0.0;
        return;
    }
    if may_hang && t.topsy_left > 3.0 && rng.rand() * 100.0 >= fall_pct {
        t.stun = rng.range(0.25, t.topsy_left - 1.0);
        t.state = State::Hang;
        t.anim = "twiglet_hang_into";
        t.anim_once = true;
        return;
    }
    t.anim = if t.state == State::Hang { "twiglet_blown_up_into" } else { "twiglet_blown_up" };
    t.anim_once = true;
    t.state = State::BlownUp;
    if let Ok(mut b) = physics.world.body_mut(t.body) {
        let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
    }
}

/// The vertical speed to set in states 0x51 / 0x52 (Twiglet_Update), `vy` the body's now. The drive (0x00471750 with
/// mass x move_power) pulls the vertical speed toward itself held within 0..120 up (K 0x005bcf84) while rising, within
/// 0..120 down (K 0x005bcf80) while falling back, at `power` a second (p_twiglet's 100: within one step). Rising, the
/// world's gravity is reversed for it (the custom-gravity list World+0xf1c, Twiglet_Vt8C 0x004a0380's -1): the
/// physics step's gravity is added back twice.
fn topsy_vertical(state: State, vy: f32, power: f32, dt: f32) -> f32 {
    let (want, gravity) = match state {
        State::BlownUp => (vy.clamp(0.0, 120.0), 2.0 * GRAVITY * dt),
        _ => (vy.clamp(-120.0, 0.0), 0.0),
    };
    vy + (want - vy) * (power * dt).min(1.0) + gravity
}

/// State 0x5a's pull toward the hole along `dir` (3D): across at 1.5 x move_speed (K 0x005bce20), up too once
/// `lifted`; the drive (0x00471750 with mass x 5, K 0x005bcbdc) takes the velocity `v` toward it at 5 a second.
fn pull_velocity(v: Vec3, dir: Vec3, speed: f32, lifted: bool, dt: f32) -> Vec3 {
    let s = speed * PULL_SPEED;
    let want = Vec3::new(dir.x * s, if lifted { dir.y * s } else { 0.0 }, dir.z * s);
    v + (want - v) * (PULL_RATE * dt).min(1.0)
}

fn anim_done(a: &crate::skin::Animator, name: &str) -> bool {
    a.layers.first().is_some_and(|l| l.anim == name && l.duration > 0.0 && l.time >= l.duration - 1e-3)
}

/// The black hole's carry-off (Twiglet_TakeDamage 0x0049f530, damage flag 0x800: from the player, so neither the side
/// nor the bomb filter stops it): a living twiglet within its reach (the bounding sphere's surface, AreaDamage_Apply
/// 0x0047f020) takes the hole (+0x568). Its first runs 250 units (K 0x005bcf00) away from it (twiglet_run, state
/// 0x5b); a later one only takes over the hold. One coming out of its tree is taken off its mover.
#[allow(clippy::type_complexity)]
fn carry_off(
    mut commands: Commands,
    mut holes: MessageReader<crate::weapons::blackhole::CarryOff>,
    mut q: Query<(Entity, &mut Twiglet, &GlobalTransform, Option<&crate::fx::Bounds>, &crate::fx::Destructible)>,
    physics: NonSend<crate::physics::Physics>,
    mut swarm: ResMut<Swarm>,
) {
    for h in holes.read() {
        let mut nearest = f32::INFINITY;
        for (e, mut t, gt, bounds, d) in &mut q {
            if d.health <= 0.0 || d.dying.is_some() {
                continue;
            }
            let (c, r) = bounds.map_or((gt.translation(), 0.0), |b| (gt.transform_point(b.center), b.radius));
            let reach = (c - h.at).length() - r;
            nearest = nearest.min(reach);
            if reach > h.radius {
                continue;
            }
            if t.hole.replace(h.hole).is_some() {
                continue;
            }
            if t.state == State::Emerging {
                commands.entity(e).remove::<crate::mover::Mover>();
            }
            let pos = gt.translation();
            let away = flat(pos - h.at).normalize_or(Vec3::X);
            // (straight there when nav.rs finds no route, as the run)
            let goal = pos + away * FLEE;
            t.route = crate::nav::path(&physics, pos, goal, &[]).unwrap_or_else(|| vec![goal]);
            t.state = State::Flee;
            t.anim = "twiglet_run";
            swarm.carried += 1;
            info!("EVENT twiglet: {e:?} carried off by the black hole");
        }
        info!("EVENT twiglet: a black hole's carry-off, the nearest twiglet {nearest:.0} from it (reach {:.0})", h.radius);
    }
}

/// Twiglet_Update 0x0049dcf0: the leash, the states, the run.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn think(
    mut commands: Commands,
    time: Res<Time>,
    mut swarm: ResMut<Swarm>,
    mut q: Query<(Entity, &mut Twiglet, &mut Transform, &mut crate::fx::Destructible, Option<&crate::mover::Mover>, Option<&mut crate::skin::Animator>)>,
    mut bombs: Query<(&mut crate::fx::Destructible, &GlobalTransform), (With<TwigletBomb>, Without<Twiglet>)>,
    (player, holes): (Query<&GlobalTransform, With<crate::player::Roman>>, Query<&Transform, (With<crate::weapons::blackhole::BlackHole>, Without<Twiglet>)>),
    mut physics: NonSendMut<crate::physics::Physics>,
    mut rng: ResMut<TwigletRng>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    (topsy, glove, suction): (Res<crate::topsy::Topsy>, Option<Res<crate::weapons::glove::Glove>>, Option<Res<crate::weapons::blackhole::Suction>>),
) {
    let dt = time.delta_secs();
    if dt <= 0.0 {
        return;
    }
    // state 0x0a stands (twiglet_stand, no drive) while the player's specials hold the AI: the topsy turvy's timer
    // (Game+0xc80 > 0), the knockout glove on (Game+0xcbc), a black hole sucking (World+0xf00 not empty)
    let held = topsy.phase == crate::topsy::Phase::Hang || glove.is_some_and(|g| g.active()) || suction.is_some_and(|s| s.active);
    let roman = player.single().ok().map(|p| p.translation());
    let max_distance = swarm.max_distance.unwrap_or(1000.0);
    let k = physics.meters_per_unit;
    for (e, mut t, mut tr, mut d, mover, animator) in &mut q {
        if d.health <= 0.0 || d.dying.is_some() {
            // dying: its death is `deaths`'s
            continue;
        }
        let pos = tr.translation;
        // the leash: once its first 10 s are up, a twiglet farther than twiglet_max_distance (2D) from where it was
        // made dies (vt+0xd4; the re-roll 0x0046e2c0(3, 7) is dropped, so it is tested every frame after)
        t.leash -= dt;
        if t.leash <= 0.0 && flat(pos - t.home).length() >= max_distance {
            crate::vehicles::kill(&mut d);
            info!("EVENT twiglet: {e:?} too far from home");
            continue;
        }
        // a hit it lived through (Twiglet_TakeDamage 0x0049f530): at most hit_level_1 a light reaction (0x1e), more a
        // heavy one (0x1f); none during the IC move (0x0049fcb0); a carried bomb gone (shot) is forgotten (the
        // prologue's check of +0x55c's vt+0xd8 kills the twiglet with it: below). (The original weighs the record's
        // amount, +0x14, here the health lost: the same but for two hits in one frame.) At the reaction's end it runs
        // (0x1e: unless flag bit 1, which nothing sets)
        let lost = t.last_health - d.health;
        t.last_health = d.health;
        if lost > 0.0 && t.state != State::Emerging {
            let r = rng.rand() * 300.0;
            if lost <= t.hit_level {
                if t.state != State::HitLight && t.state != State::HitHeavy {
                    t.anim = if r < 100.0 { "twiglet_hit_mgun" } else if r < 200.0 { "twiglet_hit_mgun_2" } else { "twiglet_hit_mgun_3" };
                    t.anim_once = true;
                    t.state = State::HitLight;
                }
            } else if t.state != State::HitHeavy {
                t.anim = if r < 100.0 { "twiglet_hit_med" } else if r < 200.0 { "twiglet_hit_med_2" } else { "twiglet_hit_med_3" };
                t.anim_once = true;
                t.state = State::HitHeavy;
            }
        }
        if let Some(b) = t.bomb {
            if !bombs.contains(b) {
                // the prologue: a carried bomb no longer alive (vt+0xd8: it went off in its hand, shot or lit by a blast)
                // is dropped and the twiglet killed (vt+0xd4)
                t.bomb = None;
                commands.entity(e).remove::<crate::fx::BlastProof>();
                crate::vehicles::kill(&mut d);
                continue;
            }
        }
        // the topsy turvy's stun (+0x470): at its end it hangs on or is blown away (0x004a01c0 with 1); meanwhile the
        // effect's time left runs down
        if t.stun > 0.0 {
            t.stun -= dt;
            if t.stun <= 0.0 {
                blow(&mut t, true, swarm.fall_pct, &mut rng, &mut physics);
                info!("EVENT twiglet: {e:?} {}", if t.state == State::Hang { "hangs on" } else { "is blown away" });
            } else {
                t.topsy_left -= dt;
            }
        }
        let mut velocity = Vec3::ZERO;
        let mut face: Option<Vec3> = None;
        let mut vertical: Option<f32> = None;
        match t.state {
            State::Hang => {
                // hanging on (twiglet_hanging after the into): the hang's own count (twice with the stun's above)
                if animator.as_deref().is_some_and(|a| anim_done(a, "twiglet_hang_into")) {
                    t.anim = "twiglet_hanging";
                }
                t.stun -= dt;
                if t.stun <= 0.0 {
                    blow(&mut t, false, swarm.fall_pct, &mut rng, &mut physics);
                    info!("EVENT twiglet: {e:?} lets go");
                }
            }
            State::BlownUp | State::Falling => {
                if animator.as_deref().is_some_and(|a| anim_done(a, "twiglet_blown_up_into")) {
                    t.anim = "twiglet_blown_up";
                }
                if t.state == State::BlownUp {
                    t.topsy_left -= dt;
                    if t.topsy_left <= 0.0 {
                        // off the gravity list (0x00454750): it falls back, and dies at its first contact
                        t.state = State::Falling;
                        physics.set_contact_rule(t.body, crate::contacts::ContactRule::default());
                    }
                } else if !physics.touches(t.body).is_empty() {
                    crate::vehicles::kill(&mut d);
                    swarm.landed += 1;
                    info!("EVENT twiglet: {e:?} lands from the sky");
                    continue;
                }
                let vy = physics.world.body(t.body).map(|b| crate::physics::bv(b.linear_velocity()).y / k).unwrap_or(0.0);
                vertical = Some(topsy_vertical(t.state, vy, t.power, dt));
            }
            State::Wait => {
                // 0x3f: standing out its wake's time, then running at the player
                t.wait -= dt;
                if t.wait <= 0.0 {
                    if let Some(r) = roman {
                        run_to(&mut t, pos, r, &physics, &mut rng);
                    }
                }
            }
            State::Emerging => {
                // physics off: the body is put where the mover has it, the IC anim plays at the mover's time
                if let Ok(mut b) = physics.world.body_mut(t.body) {
                    let _ = b.set_position(crate::physics::jr((pos + Vec3::Y * HEIGHT * 0.5) * k), Activation::Activate);
                    let _ = b.set_linear_velocity(crate::physics::jv(Vec3::ZERO));
                }
                match mover {
                    Some(m) if !m.ended => {
                        if let Some(mut a) = animator {
                            if a.layers.first().is_none_or(|l| l.anim != m.anim) {
                                a.play_at(0, &m.anim, false, crate::skin::Mask::All, m.t);
                            }
                            if let Some(l) = a.layers.get_mut(0) {
                                l.time = m.t;
                                l.speed = 0.0;
                            }
                        }
                        continue;
                    }
                    _ => {
                        // 0x0049fab0(1): upright where the move left it, then run (+0x460 bit 5)
                        commands.entity(e).remove::<crate::mover::Mover>();
                        t.yaw = crate::ai::ic::yaw_of(tr.rotation);
                        if let Some(r) = roman {
                            run_to(&mut t, pos, r, &physics, &mut rng);
                        } else {
                            t.state = State::Wait;
                            t.wait = 0.0;
                        }
                        info!("EVENT twiglet: {e:?} out of its tree");
                    }
                }
            }
            State::GetBomb => {
                if animator.as_deref().is_some_and(|a| anim_done(a, "twiglet_get_bomb_out")) {
                    t.state = State::Run;
                }
            }
            State::HitLight | State::HitHeavy => {
                if animator.as_deref().is_some_and(|a| anim_done(a, t.anim)) {
                    if let Some(r) = roman {
                        run_to(&mut t, pos, r, &physics, &mut rng);
                    }
                }
            }
            State::Flee | State::Pull => {
                // the hole gone (its vt+0xd8 false): so is the twiglet (vt+0xd4)
                let Some(hole) = t.hole.and_then(|h| holes.get(h).ok()).map(|h| h.translation) else {
                    crate::vehicles::kill(&mut d);
                    info!("EVENT twiglet: {e:?} gone with the black hole");
                    continue;
                };
                if t.state == State::Flee {
                    // 0x5b: along its route away from the hole; at the route's end the pull, lifted after rand(1, 2) s
                    if let Some(way) = next_waypoint(&mut t, pos) {
                        let (v, dir) = drive_toward(&physics, pos, way, t.speed);
                        velocity = v;
                        face = Some(dir);
                    } else {
                        t.wait = rng.range(1.0, 2.0);
                        t.state = State::Pull;
                    }
                } else {
                    // 0x5a: pulled in; within 2 (K 0x005bcb80) of the hole it is gone
                    let to = hole - pos;
                    if to.length() <= SWALLOW {
                        crate::vehicles::kill(&mut d);
                        swarm.swallowed += 1;
                        info!("EVENT twiglet: {e:?} swallowed by the black hole");
                        continue;
                    }
                    if t.wait > 0.0 {
                        t.wait -= dt;
                        if t.wait <= 0.0 {
                            t.anim = "twiglet_blown_up";
                        }
                    }
                    let v = physics.world.body(t.body).map(|b| crate::physics::bv(b.linear_velocity()) / k).unwrap_or(Vec3::ZERO);
                    let pulled = pull_velocity(v, to.normalize_or_zero(), t.speed, t.wait <= 0.0, dt);
                    velocity = pulled.with_y(0.0);
                    vertical = Some(pulled.y);
                    face = Some(flat(to));
                }
            }
            State::Run if held => {}
            State::Run => {
                let Some(target) = roman else { continue };
                let d2 = flat(target - pos).length();
                // its calls every 4..10 s (Game+0x2c vt+0xc4 at the twiglet)
                t.call -= dt;
                if t.call < 0.0 {
                    t.call = rng.range(4.0, 10.0);
                    let r = rng.rand() * 300.0;
                    let name = if r < 100.0 { "ctr_twiglet_call_1" } else if r < 200.0 { "ctr_twiglet_call_2" } else { "ctr_twiglet_call_3" };
                    sounds.write(crate::audio::PlaySound { follow: Some(e), ..crate::audio::PlaySound::at(name, pos) });
                }
                // the repath every 1..3 s; close to the player it aims 5 units to a random side of him
                t.repath -= dt;
                if t.repath < 0.0 {
                    let goal = if d2 < CLOSE_REPATH { sidestep(target, &mut rng) } else { target };
                    t.route = crate::nav::path(&physics, pos, goal, &[]).unwrap_or_default();
                    t.repath = rng.range(1.0, 3.0);
                }
                if d2 < BOMB_REACH {
                    if let Some(b) = t.bomb.take() {
                        // the bomb set off at the player's feet (vt+0xd4 on it: FxExplosion's death); the twiglet,
                        // empty-handed, takes the blast
                        commands.entity(e).remove::<crate::fx::BlastProof>();
                        if let Ok((mut bd, gt)) = bombs.get_mut(b) {
                            crate::vehicles::kill(&mut bd);
                            commands.entity(b).remove::<ChildOf>().insert(gt.compute_transform());
                        }
                        info!("EVENT twiglet: {e:?} sets its bomb off");
                    } else if rng.rand() < 0.2 {
                        // (rand() * 100 & ~0x7fff) < 0xa0000: one in five
                        sounds.write(crate::audio::PlaySound { follow: Some(e), ..crate::audio::PlaySound::at("ctr_twiglet_attack_2", pos) });
                    }
                }
                if t.bomb.is_none() {
                    // no bomb: pull one out (state 0x46, upright and still)
                    t.state = State::GetBomb;
                    t.anim = "twiglet_get_bomb_out";
                    t.anim_once = true;
                } else {
                    // (the original's route end replans to 5 to a side of him and, finding none, kills the twiglet:
                    // 0x00470c30 false twice. Not ported: nav.rs's rays start inside the twiglet's own capsule and find
                    // no route at all, so the run goes straight at him when the route is out; see docs/subsystems/twiglets.md)
                    let way = next_waypoint(&mut t, pos).unwrap_or(target);
                    let (v, dir) = drive_toward(&physics, pos, way, t.speed);
                    velocity = v;
                    face = Some(dir);
                }
            }
        }
        // the state's anim
        let want: (&'static str, bool) = match t.state {
            State::Wait => ("twiglet_stand", true),
            State::Run if held => ("twiglet_stand", true),
            State::Run => (if t.bomb.is_some() { "twiglet_run_with_bomb" } else { "twiglet_run" }, true),
            State::GetBomb | State::HitLight | State::HitHeavy => (t.anim, false),
            State::Hang | State::BlownUp | State::Falling => (t.anim, matches!(t.anim, "twiglet_hanging" | "twiglet_blown_up")),
            State::Flee | State::Pull => (t.anim, true),
            State::Emerging => ("", false),
        };
        if let Some(mut a) = animator {
            let playing = a.layers.first().map(|l| l.anim.clone()).unwrap_or_default();
            if !want.0.is_empty() && (playing != want.0 || (t.anim_once && !want.1)) {
                a.play(0, want.0, want.1, crate::skin::Mask::All);
            }
        }
        t.anim_once = false;
        if let Some(f) = face.filter(|f| *f != Vec3::ZERO) {
            let want = f.x.atan2(f.z);
            let gap = (want - t.yaw + std::f32::consts::PI).rem_euclid(std::f32::consts::TAU) - std::f32::consts::PI;
            t.yaw += gap * (dt * TURN_RATE).min(1.0);
        }
        tr.rotation = Quat::from_rotation_y(t.yaw) * crate::level::game_to_engine();
        if let Ok(mut b) = physics.world.body_mut(t.body) {
            // the drive sets the level velocity (`ground_speed`); the vertical is the body's own (gravity, the ground)
            let v = crate::physics::bv(b.linear_velocity());
            let vy = vertical.map_or(v.y, |y| y * k);
            let _ = b.set_linear_velocity(crate::physics::jv(Vec3::new(velocity.x * k, vy, velocity.z * k)));
            b.activate();
        }
    }
}

/// The run anims' and the bomb's notes (Twiglet_HandleEvent 0x0049f930): "get_bomb" makes a twiglet_bomb in its hand
/// (its ref_bomb at the twiglet's ref_hand_00, Object_Attach 0x004539f0).
fn notes(
    mut commands: Commands,
    mut notes: MessageReader<crate::skin::AnimNote>,
    mut q: Query<(&mut Twiglet, &crate::skin::Animator)>,
    data: Res<crate::gamedata::GameData>,
    mut kit: crate::army::ModelKit,
) {
    for n in notes.read() {
        if !n.text.eq_ignore_ascii_case("get_bomb") {
            continue;
        }
        let Ok((mut t, a)) = q.get_mut(n.entity) else { continue };
        if t.bomb.is_some() {
            continue;
        }
        let Some(hand) = a.skeleton.index.get("ref_hand_00").and_then(|&i| a.joints.get(i).copied()) else { continue };
        let Some(model) = data.templates.get("twiglet_bomb").and_then(|t| t.model.clone()) else { continue };
        let reference = data.templates.get("twiglet_bomb").and_then(|t| t.default_anim.clone()).unwrap_or_else(|| "twiglet_bomb_ref".into());
        let dir = kit.dir();
        let at = -crate::mission::pub_bone(&dir, &reference, "ref_bomb").unwrap_or(Vec3::ZERO);
        let Some(bomb) = kit.spawn(&mut commands, &model, Transform::from_translation(at), true) else { continue };
        let p = data.template_props("twiglet_bomb");
        if let Some(d) = crate::level::destructible(&p) {
            commands.entity(bomb).insert(d);
        }
        commands.entity(bomb).insert((TwigletBomb, Name::new("twiglet_bomb")));
        commands.entity(hand).add_child(bomb);
        // a twiglet with a bomb lets blasts pass (Twiglet_TakeDamage 0x0049f530: flag 0x40 with +0x55c set)
        commands.entity(n.entity).insert(crate::fx::BlastProof);
        t.bomb = Some(bomb);
    }
}

/// Deaths (fx.rs `Died`): a twiglet's (Twiglet_Kill 0x0049f430): a carried bomb is let go and set off where it is, its
/// body leaves the world (the death fx fx_exp_twiglet and the score are fx.rs's, the removal level.rs's); a bomb gone
/// off is removed.
fn deaths(
    mut commands: Commands,
    mut died: MessageReader<crate::fx::Died>,
    q: Query<&Twiglet>,
    mut bombs: Query<(Option<&mut crate::fx::Destructible>, &GlobalTransform), With<TwigletBomb>>,
    mut physics: NonSendMut<crate::physics::Physics>,
    mut swarm: ResMut<Swarm>,
) {
    for crate::fx::Died(e) in died.read() {
        if let Ok(t) = q.get(*e) {
            if let Some((Some(mut bd), gt)) = t.bomb.and_then(|b| bombs.get_mut(b).ok()) {
                crate::vehicles::kill(&mut bd);
                // (out of the dead twiglet's hierarchy, which level.rs despawns)
                commands.entity(t.bomb.expect("a bomb")).remove::<ChildOf>().insert(gt.compute_transform());
            }
            physics.remove(t.body);
            swarm.killed += 1;
        } else if bombs.contains(*e) {
            swarm.bombs_gone += 1;
            commands.entity(*e).despawn();
        }
    }
}

/// The playtest state's `twiglets`: live, made, killed, by state, with bombs.
fn log(q: Query<&Twiglet>, swarm: Res<Swarm>, gens: Query<&Generator>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    let count = |s: State| q.iter().filter(|t| t.state == s).count();
    state.0.insert(
        "twiglets".into(),
        serde_json::json!({
            "live": q.iter().count(),
            "made": swarm.made,
            "killed": swarm.killed,
            "bombs": q.iter().filter(|t| t.bomb.is_some()).count(),
            "waiting": count(State::Wait),
            "emerging": count(State::Emerging),
            "running": count(State::Run),
            "hanging": count(State::Hang),
            "blown_up": count(State::BlownUp) + count(State::Falling),
            "landed": swarm.landed,
            "fleeing": count(State::Flee),
            "pulled": count(State::Pull),
            "carried": swarm.carried,
            "swallowed": swarm.swallowed,
            "generators": gens.iter().filter(|g| g.active.is_some()).count(),
            "left": gens.iter().filter(|g| g.active.is_some()).map(|g| g.left).sum::<u32>(),
        }),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    fn props(v: serde_json::Value) -> crate::gamedata::Props {
        crate::gamedata::Props(v.as_object().cloned().unwrap_or_default())
    }

    /// TwigletGenerator_Slot6 0x0049d3c0: release_max, the triggers, the movers that are not "none"; 0x0049d590 takes
    /// the second mover first and wraps.
    #[test]
    fn a_generator_cycles_its_movers_from_the_second() {
        // lev_4_m1 64
        let p = props(serde_json::json!({ "trigger_activate": 200, "release_max": 10, "initial_ic_0": "mover_twiglet_tree_a_01",
            "initial_ic_1": "mover_twiglet_tree_a_02", "initial_ic_2": "none" }));
        let mut g = Generator::from_props(&p, Mat4::from_translation(Vec3::new(100.0, 0.0, 0.0)), |_| Some(Mat4::from_translation(Vec3::Z)));
        assert_eq!((g.activate, g.deactivate, g.left, g.movers.len()), (200, 0, 10, 2));
        assert_eq!(g.movers[0].1.w_axis.truncate(), Vec3::new(100.0, 0.0, 1.0));
        let names: Vec<String> = (0..3).map(|_| g.take_mover().unwrap().0).collect();
        assert_eq!(names, ["mover_twiglet_tree_a_02", "mover_twiglet_tree_a_01", "mover_twiglet_tree_a_02"]);
    }

    /// 0x0049d590 without movers: 30..100 units out in x and y, at the tree's height; one less to give.
    #[test]
    fn a_tree_without_movers_drops_twiglets_round_it() {
        let p = props(serde_json::json!({ "release_max": 2 }));
        let mut g = Generator::from_props(&p, Mat4::from_translation(Vec3::new(0.0, 0.0, 50.0)), |_| None);
        let mut rng = TwigletRng::default();
        for _ in 0..2 {
            let ev = release(&mut g, &mut rng);
            let at = ev.at.w_axis.truncate();
            assert!(ev.mover.is_none());
            assert_eq!(at.z, 50.0);
            let r = at.truncate().length();
            assert!((0.0..=142.0).contains(&r), "{r}");
        }
        assert_eq!(g.left, 0);
        let _ = release(&mut g, &mut rng);
        assert_eq!(g.left, 0);
    }
}
