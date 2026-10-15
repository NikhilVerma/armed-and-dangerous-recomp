//! Starting the level again inside the running game: Abandon Mission / Return to title, Restart, Load Game and
//! Restart from last save (docs/player-feedback.md 23). The original tears the level down and builds it again in
//! the same process (the level teardown, kill everything 0x0045d670 / delete 0x0045da20, then Game_StartLevel
//! 0x00403690); before this module the remaster started a fresh process instead, which closed the window.
//!
//! A restart, at the end of the frame it is asked for ([`RestartLevel`]):
//! 1. despawns every entity the level made: the ones its spawners made at start-up and everything spawned since,
//!    except the engine's own (windows, monitors, observers, gamepads) and what existed before the level
//!    (the input contexts, Bevy's own entities);
//! 2. puts every level resource back to its first value (plugins register them with
//!    [`LevelAppExt::init_level_resource`]) and gives physics a new Jolt world;
//! 3. bumps [`LevelGeneration`], which resets the systems' per-level locals ([`PerLevel`]);
//! 4. runs the level spawners again ([`LevelAppExt::add_level_spawner`]: main.rs setup, the fx pools);
//! 5. enters the asked state: the title menu, play from the start, or play from a saved game.
//!
//! What stays: the options, the graphics settings, the mixer, the controls, the cheats entered, the playtest
//! script, the movie settings, the "boot intro played" flag (the intro plays once a session, as the original's shell
//! flag 2), caches keyed by name (animation library, textures).
//!
//! API: [`LevelResetPlugin`], [`RestartLevel`] (set `to` to ask), [`LevelAppExt`], [`PerLevel`],
//! [`LevelGeneration`].

use crate::AppState;
use bevy::ecs::system::{SystemId, SystemParam};
use bevy::prelude::*;
use std::collections::HashSet;
use std::path::PathBuf;

/// Where a restart goes.
#[derive(Clone, Debug, PartialEq)]
pub enum RestartTo {
    /// the title menu over a fresh level (Abandon Mission, Return to title, the end of the statistics)
    Title,
    /// play from the level's start (Restart)
    Play,
    /// play from this saved game (Load Game, Quick Load, Restart from last save)
    Load(PathBuf),
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

/// The level spawners (run in this order at start-up and on each restart) and how to put the level resources back.
#[derive(Resource, Default)]
struct LevelParts {
    spawners: Vec<SystemId>,
    resets: Vec<fn(&mut World)>,
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
}

#[derive(Resource)]
struct Maker<R: Resource>(fn() -> R);

pub struct LevelResetPlugin;

impl Plugin for LevelResetPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<RestartLevel>()
            .init_resource::<LevelGeneration>()
            .init_resource::<Persistent>()
            .init_resource::<StartupLevel>()
            .init_resource::<LevelParts>()
            .add_systems(Startup, spawn_level)
            .add_systems(First, take_persistent)
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

/// Entities the engine spawns while running that are not the level's.
fn engine_owned(world: &World, e: Entity) -> bool {
    let Ok(r) = world.get_entity(e) else { return true };
    r.contains::<Window>()
        || r.contains::<bevy::window::Monitor>()
        || r.contains::<bevy::ecs::observer::Observer>()
        || r.contains::<bevy::input::gamepad::Gamepad>()
        || r.contains::<bevy::render::view::screenshot::Screenshot>()
}

fn restart(world: &mut World) {
    let Some(to) = world.resource_mut::<RestartLevel>().to.take() else { return };
    let t = std::time::Instant::now();
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
    // 2. the level's resources and physics
    let resets = world.resource::<LevelParts>().resets.clone();
    for r in resets {
        r(world);
    }
    world.insert_non_send(crate::physics::Physics::new(crate::level::METERS_PER_UNIT));
    world.remove_resource::<crate::savegame::PendingLoad>();
    // 3. the systems' per-level locals
    world.resource_mut::<LevelGeneration>().0 += 1;
    // 4. the level again
    run_spawners(world);
    // the debug overlay's egui context lived on the old 3D camera (bevy_egui attaches it to the first camera only)
    let cam = world.query_filtered::<Entity, With<Camera3d>>().iter(world).next();
    if let Some(cam) = cam {
        world.entity_mut(cam).insert((bevy_egui::EguiContext::default(), bevy_egui::PrimaryEguiContext));
    }
    // 5. where to
    let from_save = match &to {
        RestartTo::Load(p) => match crate::savegame::read(p) {
            Some(save) => {
                world.insert_resource(crate::savegame::PendingLoad(save, 0));
                world.insert_resource(crate::campath::CameraPath::skipping());
                true
            }
            None => {
                warn!("level: the saved game {} could not be read: starting the level", p.display());
                false
            }
        },
        _ => false,
    };
    world.resource_mut::<crate::cutscenes::Cutscenes>().from_save = from_save;
    world.resource_mut::<NextState<AppState>>().set(if to == RestartTo::Title { AppState::Title } else { AppState::Playing });
    world.resource_mut::<Time<Virtual>>().unpause();
    let generation = world.resource::<LevelGeneration>().0;
    info!("EVENT level: restarted ({to:?}, generation {generation}): {count} entities despawned, rebuilt in {:.0} ms", t.elapsed().as_secs_f32() * 1000.0);
    if let Some(mut st) = world.get_resource_mut::<crate::playtest::PlayState>() {
        st.0.insert("level_generation".into(), generation.into());
    }
}
