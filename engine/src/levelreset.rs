//! Starting the level again inside the running game: Abandon Mission / Return to title, Restart, Load Game and
//! Restart from last save (docs/player-feedback.md 23, 28), and the campaign's next level (campaign.rs enters the
//! other level's folder before the rebuild). The original tears the level down and builds it again in
//! the same process (the level teardown, kill everything 0x0045d670 / delete 0x0045da20, then Game_StartLevel
//! 0x00403690) behind its loading screen; before this module the remaster started a fresh process instead, which
//! closed the window.
//!
//! A restart, at the end of the frame it is asked for ([`RestartLevel`]):
//! 1. despawns every entity the level made: the ones its spawners made at start-up and everything spawned since,
//!    except the engine's own (windows, monitors, observers, gamepads) and what existed before the level
//!    (the input contexts, Bevy's own entities);
//! 2. puts every level resource back to its first value (plugins register them with
//!    [`LevelAppExt::init_level_resource`]), empties the level's message queues
//!    ([`LevelAppExt::add_level_message`]: a trigger, a spawn or an fx the old level sent is not delivered to the new
//!    one) and gives physics a new Jolt world;
//! 3. bumps [`LevelGeneration`], which resets the systems' per-level locals ([`PerLevel`]);
//! 4. runs the level spawners again ([`LevelAppExt::add_level_spawner`]: main.rs setup, the fx pools); the resources
//!    a spawner makes ([`LevelAppExt::spawned_level_resource`]: the mission, the scene library, ...) are taken out
//!    first, so none of the old level's survives a spawner that no longer makes it, and dropped only after the
//!    spawners ran; then the new entities' transforms are propagated (as PostStartup does at launch);
//! 5. loads ([`AppState::Loading`]: the world runs behind the loading screen, as it does behind the title menu after
//!    a launch) until the level has settled: a few frames in, its frames quick again (the rebuild's long frame, the
//!    new level's first uploads); then it enters the asked state: the title menu, play from the start, or play from
//!    a saved game.
//!
//! Feedback 28 ("3 fps, characters missing, Roman looking down" after a restart): the rebuilt level's entities had no
//! GlobalTransform yet in the next frame (a launch propagates them in PostStartup; a restart did not), so every
//! grunt found Roman at distance 0 at the origin, the whole level took him as its target and the alarm sent its
//! troops (restart_parity: 30 grunts against a fresh game's 27; AIDBG "spotted ... at (0, 0, 0) d 0" by all 27).
//! Step 4 now propagates the transforms; step 5 also keeps play from taking the rebuild's seconds as its first time
//! step (Time<Virtual> clamps it to 0.25 s), as New Game from the title does.
//!
//! What stays: the options, the graphics settings, the mixer, the controls, the cheats entered, the playtest
//! script, the movie settings, the "boot intro played" flag (the intro plays once a session, as the original's shell
//! flag 2), caches keyed by name (animation library, textures).
//!
//! API: [`LevelResetPlugin`], [`RestartLevel`] (set `to` to ask), [`LevelAppExt`], [`PerLevel`],
//! [`LevelGeneration`], [`LevelSettle`] (the loading wait's limits).

use crate::AppState;
use bevy::ecs::message::{Message, Messages};
use bevy::ecs::system::{SystemId, SystemParam};
use bevy::prelude::*;
use std::any::Any;
use std::collections::HashSet;
use std::path::PathBuf;
use std::time::{Duration, Instant};

/// Where a restart goes.
#[derive(Clone, Debug, PartialEq)]
pub enum RestartTo {
    /// the title menu over the campaign's home level (Abandon Mission, Return to title, the end of the campaign)
    Title,
    /// play from the level's start (Restart)
    Play,
    /// play from this saved game, in its level (Load Game, Quick Load, Restart from last save)
    Load(PathBuf),
    /// play this level from its start (the campaign's next level, campaign.rs)
    Level(String),
}

/// Ask for a restart: `to` is taken at the end of the frame.
#[derive(Resource, Default)]
pub struct RestartLevel {
    pub to: Option<RestartTo>,
}

/// How many times the level was started again (0 for the first level of the process).
#[derive(Resource, Default, Clone, Copy, PartialEq, Eq, Debug)]
pub struct LevelGeneration(pub u32);

/// A system's local state that starts over with each level (a `Local` would keep the last level's).
#[derive(SystemParam)]
pub struct PerLevel<'w, 's, T: Default + Send + Sync + 'static> {
    generation: Res<'w, LevelGeneration>,
    state: Local<'s, (u32, T)>,
}

impl<T: Default + Send + Sync + 'static> PerLevel<'_, '_, T> {
    pub fn get(&mut self) -> &mut T {
        if self.state.0 != self.generation.0 {
            *self.state = (self.generation.0, T::default());
        }
        &mut self.state.1
    }
}

/// When a rebuilt level has settled (step 5 of the module doc): at least `min_frames` frames, the last `calm_frames`
/// of them each shorter than `calm_frame`; or `max_wait` after the rebuild, whatever the frames.
#[derive(Resource, Clone, Copy, Debug)]
pub struct LevelSettle {
    pub min_frames: u32,
    pub calm_frames: u32,
    pub calm_frame: Duration,
    pub max_wait: Duration,
}

impl Default for LevelSettle {
    fn default() -> Self {
        LevelSettle { min_frames: 10, calm_frames: 5, calm_frame: Duration::from_millis(100), max_wait: Duration::from_secs(10) }
    }
}

impl LevelSettle {
    /// Whether the level has settled after `frames` frames, `calm` of the latest in a row quick, `waited` since the
    /// rebuild.
    pub fn settled(&self, frames: u32, calm: u32, waited: Duration) -> bool {
        (frames >= self.min_frames && calm >= self.calm_frames) || waited >= self.max_wait
    }
}

/// A rebuilt level waiting to settle (AppState::Loading), and the state it enters then.
#[derive(Resource)]
struct Settling {
    to: AppState,
    frames: u32,
    calm: u32,
    since: Instant,
    last: Instant,
}

type TakeResource = fn(&mut World) -> Option<Box<dyn Any + Send + Sync>>;

/// The level spawners (run in this order at start-up and on each restart), how to put the level resources back and
/// empty the level's message queues, and the resources the spawners make.
#[derive(Resource, Default)]
struct LevelParts {
    spawners: Vec<SystemId>,
    resets: Vec<fn(&mut World)>,
    spawned: Vec<TakeResource>,
}

/// The entities that are not the level's (taken on the first frame).
#[derive(Resource, Default)]
struct Persistent(HashSet<Entity>);

/// The entities the spawners made at start-up.
#[derive(Resource, Default)]
struct StartupLevel(HashSet<Entity>);

pub trait LevelAppExt {
    /// A resource that belongs to the level: initialised now and again (FromWorld) whenever the level restarts.
    fn init_level_resource<R: Resource + FromWorld>(&mut self) -> &mut Self;
    /// The same with its first value from `make`.
    fn insert_level_resource<R: Resource>(&mut self, make: fn() -> R) -> &mut Self;
    /// A system that builds part of the level: run at start-up and again on each restart, in the order added.
    fn add_level_spawner<M>(&mut self, system: impl IntoSystem<(), (), M> + 'static) -> &mut Self;
    /// A resource a level spawner inserts: removed before the spawners run again on a restart (kept alive until they
    /// ran, see the module doc), so a level without it has none.
    fn spawned_level_resource<R: Resource>(&mut self) -> &mut Self;
    /// A message the level's objects send each other: registered (as `add_message`), and its queue emptied on a
    /// restart.
    fn add_level_message<M: Message>(&mut self) -> &mut Self;
}

impl LevelAppExt for App {
    fn init_level_resource<R: Resource + FromWorld>(&mut self) -> &mut Self {
        self.init_resource::<R>();
        self.world_mut().get_resource_or_init::<LevelParts>().resets.push(|w| {
            let r = R::from_world(w);
            w.insert_resource(r);
        });
        self
    }

    fn insert_level_resource<R: Resource>(&mut self, make: fn() -> R) -> &mut Self {
        self.insert_resource(make());
        // a fn pointer per type: the constructor is kept in a resource of its own
        self.insert_resource(Maker(make));
        self.world_mut().get_resource_or_init::<LevelParts>().resets.push(|w| {
            let make = w.resource::<Maker<R>>().0;
            w.insert_resource(make());
        });
        self
    }

    fn add_level_spawner<M>(&mut self, system: impl IntoSystem<(), (), M> + 'static) -> &mut Self {
        let id = self.world_mut().register_system(system);
        self.world_mut().get_resource_or_init::<LevelParts>().spawners.push(id);
        self
    }

    fn spawned_level_resource<R: Resource>(&mut self) -> &mut Self {
        self.world_mut().get_resource_or_init::<LevelParts>().spawned.push(|w| w.remove_resource::<R>().map(|r| Box::new(r) as Box<dyn Any + Send + Sync>));
        self
    }

    fn add_level_message<M: Message>(&mut self) -> &mut Self {
        self.add_message::<M>();
        self.world_mut().get_resource_or_init::<LevelParts>().resets.push(|w| {
            if let Some(mut m) = w.get_resource_mut::<Messages<M>>() {
                m.clear();
            }
        });
        self
    }
}

#[derive(Resource)]
struct Maker<R: Resource>(fn() -> R);

pub struct LevelResetPlugin;

impl Plugin for LevelResetPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<RestartLevel>()
            .init_resource::<LevelGeneration>()
            .init_resource::<LevelSettle>()
            .init_resource::<Persistent>()
            .init_resource::<StartupLevel>()
            .init_resource::<LevelParts>()
            .add_systems(Startup, spawn_level)
            .add_systems(First, take_persistent)
            .add_systems(Update, settle.run_if(resource_exists::<Settling>))
            .add_systems(Last, restart.run_if(|r: Res<RestartLevel>| r.to.is_some()));
    }
}

fn run_spawners(world: &mut World) {
    let ids = world.resource::<LevelParts>().spawners.clone();
    for id in ids {
        if let Err(e) = world.run_system(id) {
            error!("level: a spawner failed: {e}");
        }
    }
}

fn spawn_level(world: &mut World) {
    let before: HashSet<Entity> = world.query::<Entity>().iter(world).collect();
    run_spawners(world);
    let made: HashSet<Entity> = world.query::<Entity>().iter(world).filter(|e| !before.contains(e)).collect();
    world.resource_mut::<StartupLevel>().0 = made;
}

/// On the first frame (every start-up command applied): what exists and is not the level's stays on a restart.
fn take_persistent(world: &mut World, mut done: Local<bool>) {
    if *done {
        return;
    }
    *done = true;
    let level = std::mem::take(&mut world.resource_mut::<StartupLevel>().0);
    let keep: HashSet<Entity> = world.query::<Entity>().iter(world).filter(|e| !level.contains(e)).collect();
    world.resource_mut::<Persistent>().0 = keep;
}

/// On an entity spawned while running that is not the level's and outlives it (the Gameplay input context, rebuilt when
/// the Controls page changes a binding: input.rs).
#[derive(Component, Clone, Copy)]
pub struct KeepOnRestart;

/// Entities the engine spawns while running that are not the level's.
fn engine_owned(world: &World, e: Entity) -> bool {
    let Ok(r) = world.get_entity(e) else { return true };
    // resources are entities in Bevy 0.20: one inserted after start-up is in neither list (a despawn of it warns
    // "Resource entities are not supposed to be despawned" and breaks the queued commands that use it)
    r.contains::<bevy::ecs::resource::IsResource>()
        || r.contains::<KeepOnRestart>()
        || r.contains::<Window>()
        || r.contains::<bevy::window::Monitor>()
        || r.contains::<bevy::ecs::observer::Observer>()
        || r.contains::<bevy::input::gamepad::Gamepad>()
        || r.contains::<bevy::render::view::screenshot::Screenshot>()
}

fn restart(world: &mut World) {
    let Some(to) = world.resource_mut::<RestartLevel>().to.take() else { return };
    let t = Instant::now();
    // 0. the level to build: the title stands on the campaign's home level, a saved game is in its own level
    let save = match &to {
        RestartTo::Load(p) => {
            let s = crate::savegame::read(p);
            if s.is_none() {
                warn!("level: the saved game {} could not be read: starting the level", p.display());
            }
            s
        }
        _ => None,
    };
    let levels = world.resource::<crate::campaign::Levels>().clone();
    let dir = match &to {
        RestartTo::Title => Some(levels.home().to_path_buf()),
        RestartTo::Play => None,
        RestartTo::Load(_) => save.as_ref().and_then(|s| levels.dir(&s.level)),
        RestartTo::Level(name) => levels.dir(name),
    };
    if let Some(dir) = dir {
        crate::campaign::enter(world, &dir);
    }
    // 1. the level's entities
    let keep = std::mem::take(&mut world.resource_mut::<Persistent>().0);
    let all: Vec<Entity> = world.query::<Entity>().iter(world).collect();
    let doomed: Vec<Entity> = all.into_iter().filter(|e| !keep.contains(e) && !engine_owned(world, *e)).collect();
    let count = doomed.len();
    for e in doomed {
        if let Ok(em) = world.get_entity_mut(e) {
            em.despawn();
        }
    }
    world.resource_mut::<Persistent>().0 = keep;
    // 2. the level's resources, message queues and physics
    let resets = world.resource::<LevelParts>().resets.clone();
    for r in resets {
        r(world);
    }
    world.insert_non_send(crate::physics::Physics::new(crate::level::METERS_PER_UNIT));
    world.remove_resource::<crate::savegame::PendingLoad>();
    // 3. the systems' per-level locals
    world.resource_mut::<LevelGeneration>().0 += 1;
    // 4. the level again; what the spawners made last time is held (not in the world) until they ran
    let takes = world.resource::<LevelParts>().spawned.clone();
    let held: Vec<Box<dyn Any + Send + Sync>> = takes.into_iter().filter_map(|take| take(world)).collect();
    run_spawners(world);
    drop(held);
    // the new level's GlobalTransforms now, as a launch gets them in PostStartup (bevy_transform: "so the first update
    // is correct"). Without this the frame after a restart saw every new entity at the origin: each grunt "heard"
    // Roman at distance 0 (ai.rs AI_FindTarget's hear_distance), the whole level took him as its target, the alarm
    // rose and its generators sent troops (feedback 28: "3 fps, characters missing"; AIDBG: 27 grunts spotted
    // Roman at (0, 0, 0) on the first frame after Restart)
    propagate_transforms(world);
    // the debug overlay's egui context lived on the old 3D camera (bevy_egui attaches it to the first camera only)
    let cam = world.query_filtered::<Entity, With<Camera3d>>().iter(world).next();
    if let Some(cam) = cam {
        world.entity_mut(cam).insert((bevy_egui::EguiContext::default(), bevy_egui::PrimaryEguiContext));
    }
    // 5. where to, once the level has settled (`settle`)
    let from_save = match save {
        Some(save) => {
            world.insert_resource(crate::savegame::PendingLoad(save, 0));
            world.insert_resource(crate::campath::CameraPath::skipping());
            true
        }
        None => false,
    };
    world.resource_mut::<crate::cutscenes::Cutscenes>().from_save = from_save;
    let target = if to == RestartTo::Title { AppState::Title } else { AppState::Playing };
    let now = Instant::now();
    world.insert_resource(Settling { to: target, frames: 0, calm: 0, since: now, last: now });
    world.resource_mut::<NextState<AppState>>().set(AppState::Loading);
    // the clock runs while loading, as behind the title menu: the rebuild's long frame is spent there, not in play
    world.resource_mut::<Time<Virtual>>().unpause();
    let generation = world.resource::<LevelGeneration>().0;
    info!("EVENT level: restarted ({to:?}, generation {generation}): {count} entities despawned, rebuilt in {:.0} ms", t.elapsed().as_secs_f32() * 1000.0);
    if let Some(mut st) = world.get_resource_mut::<crate::playtest::PlayState>() {
        st.0.insert("level_generation".into(), generation.into());
        st.0.insert("loading".into(), true.into());
    }
}

/// Bevy's transform propagation (PostStartup / PostUpdate's TransformSystems::Propagate), run once now; a new system
/// state treats every transform as changed.
fn propagate_transforms(world: &mut World) {
    use bevy::ecs::system::RunSystemOnce;
    use bevy::transform::systems::{mark_dirty_trees, propagate_parent_transforms, sync_simple_transforms};
    let ok = world.run_system_once(mark_dirty_trees).is_ok() && world.run_system_once(propagate_parent_transforms).is_ok() && world.run_system_once(sync_simple_transforms).is_ok();
    if !ok {
        error!("level: the new level's transforms could not be propagated");
    }
}

/// The loading wait (step 5 of the module doc): frames are counted from the first one in AppState::Loading; once the
/// level has settled the asked state is entered, play as New Game enters it (the clock running).
fn settle(
    mut commands: Commands,
    limits: Res<LevelSettle>,
    mut s: ResMut<Settling>,
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut vtime: ResMut<Time<Virtual>>,
    play: Option<ResMut<crate::playtest::PlayState>>,
    strategy: Option<Res<bevy::time::TimeUpdateStrategy>>,
) {
    let now = Instant::now();
    if *state.get() != AppState::Loading {
        s.last = now;
        return;
    }
    let frame = now - s.last;
    s.last = now;
    s.frames += 1;
    // a scripted run steps game time by a fixed amount a frame (main.rs): a slow frame is no long game step there, and
    // waiting for quick wall-clock frames made the loading take a machine-load-dependent number of frames (game time
    // runs while loading), so every frame counts as calm and the wait is min_frames frames
    let stepped = matches!(strategy.as_deref(), Some(bevy::time::TimeUpdateStrategy::ManualDuration(_)));
    s.calm = if stepped || frame < limits.calm_frame { s.calm + 1 } else { 0 };
    let waited = if stepped { Duration::ZERO } else { now - s.since };
    if !limits.settled(s.frames, s.calm, waited) {
        return;
    }
    next.set(s.to);
    vtime.unpause();
    info!("EVENT level: settled after {} frames, {:.0} ms: {:?}", s.frames, (now - s.since).as_secs_f32() * 1000.0, s.to);
    if let Some(mut p) = play {
        p.0.insert("loading".into(), false.into());
    }
    commands.remove_resource::<Settling>();
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The loading wait: not before min_frames, then once calm_frames quick frames follow each other; a level that
    /// never calms down still starts after max_wait.
    #[test]
    fn settle_rule() {
        let l = LevelSettle::default();
        let s = Duration::from_millis(500);
        assert!(!l.settled(3, 3, s), "too few frames");
        assert!(!l.settled(12, 4, s), "not calm long enough");
        assert!(l.settled(12, 5, s));
        assert!(!l.settled(40, 0, Duration::from_secs(9)), "slow frames keep it loading");
        assert!(l.settled(40, 0, Duration::from_secs(10)), "but not past max_wait");
    }
}
