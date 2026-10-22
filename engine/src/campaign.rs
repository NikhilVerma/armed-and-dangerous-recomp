//! The campaign: which level is loaded, and what follows a won level (docs/subsystems/win-sequence.md, campaign.md).
//!
//! The original plays its levels one after another in the same process. When the Stats screen is left, the end
//! sequence 0x0044a230 (state 11 onwards, ui_01.cpp:1991) pushes "PlayMovie" with the level's `success_movie` when
//! it has one; after `lev_5_m5` (0x005b253c) it pushes "Credits" and then loads lev_shell (the title); otherwise it
//! banks the money (0x00401ef0: Game+0x570 -> +0x57c) and loads `next_level_name` (Game+0x268c, 0x004907c0 and
//! "Loading"; NULL, so lev_shell, when Game+0x568 bit 2 is set). Game_StartLevel 0x00403690 then tears the level down
//! and builds the next one; MissionObject_Update shows its start movie (except on lev_1_m1).
//!
//! What a level carries into the next (evidence in docs/subsystems/campaign.md):
//! - the money: banked at the end (0x00401ef0) and taken back by every level start (0x00401f10 from 0x00403690); here
//!   `GameOptions::money`, which only the Stats screen changes;
//! - the Extras tokens and what they unlocked (the profile: `GameOptions::tokens`, `tokens_taken`);
//! - the difficulty (Game+0x550, `gamedata::Difficulty`);
//! - not the weapons: the player object is built again with the level, and its reset (0x00475650) gives it the
//!   mission's loadout (`mission::Loadout`) with full ammo.
//!
//! The remaster rebuilds the level in-process (levelreset.rs) from another export folder: [`Levels`] holds the
//! folders and the one loaded; the default asset source reads from it ([`LevelAssetReader`]), and [`enter`] points
//! everything that reads the level's files at the new folder before the level spawners run again.
//!
//! API: [`CampaignPlugin`], [`Levels`] (the folders; `current_name`), [`LevelEnd`] (statscreen.rs asks for the end
//! sequence), [`enter`] (levelreset.rs, before a rebuild).

use crate::gamemenu::{GameMenu, Page};
use crate::levelreset::{LevelAppExt, RestartLevel, RestartTo};
use crate::movie::{MoviePlayer, PlayMovies};
use bevy::asset::io::{AssetReader, AssetReaderError, AssetSourceBuilder, PathStream, VecReader};
use bevy::prelude::*;
use std::path::{Path, PathBuf};
use std::sync::{Arc, RwLock};

/// The level after which the end sequence shows the credits instead of loading the next level (0x005b253c).
pub const LAST_LEVEL: &str = "lev_5_m5";
/// The front end's level (0x005bc144): the remaster's title menu.
pub const SHELL_LEVEL: &str = "lev_shell";
/// The level a new game starts.
pub const FIRST_LEVEL: &str = "lev_1_m1";
/// The asset-server paths whose file differs between the level folders. Every other file the asset server loads
/// (textures, ui art, sounds) is byte-identical in all 24 exports under the same name (checked 2026-10-16: 6001
/// textures, 22585 sounds, 10700 models compared), so a handle kept from the last level stays right; these are
/// reloaded on a change.
const PER_LEVEL_ASSETS: [&str; 1] = ["ui/icons.png"];

/// The exported level folders (`<root>/<level>`, tools/export_level.py) and the one loaded.
#[derive(Resource, Clone)]
pub struct Levels {
    root: PathBuf,
    /// the level the title menu stands on (the launch's folder: lev_1_m1 unless --scene says otherwise)
    home: PathBuf,
    current: Arc<RwLock<PathBuf>>,
}

impl Levels {
    /// The folders around `home`, the level behind the title menu (its parent holds the other levels), with `start`
    /// loaded (`home`, or a saved game's level: `--load`).
    pub fn new(home: &Path, start: &Path) -> Self {
        let root = home.parent().map(Path::to_path_buf).unwrap_or_default();
        Levels { root, home: home.to_path_buf(), current: Arc::new(RwLock::new(start.to_path_buf())) }
    }

    /// The folder of a level by name, when it was exported.
    pub fn dir(&self, level: &str) -> Option<PathBuf> {
        let d = self.root.join(level);
        (!level.is_empty() && d.join("scene.json").is_file()).then_some(d)
    }

    pub fn current_dir(&self) -> PathBuf {
        self.current.read().map(|p| p.clone()).unwrap_or_default()
    }

    /// The loaded level's name (its folder's).
    pub fn current_name(&self) -> String {
        folder_name(&self.current_dir())
    }

    pub fn home(&self) -> &Path {
        &self.home
    }

    /// The default asset source over the loaded level's folder (registered before the AssetPlugin).
    pub fn asset_source(&self) -> AssetSourceBuilder {
        let current = self.current.clone();
        AssetSourceBuilder::new(move || Box::new(LevelAssetReader(current.clone())))
    }

    fn set_current(&self, dir: &Path) {
        if let Ok(mut c) = self.current.write() {
            *c = dir.to_path_buf();
        }
    }
}

fn folder_name(dir: &Path) -> String {
    dir.file_name().and_then(|n| n.to_str()).unwrap_or_default().to_owned()
}

/// Reads the asset server's files from the loaded level's folder (a level change points it at the next folder).
struct LevelAssetReader(Arc<RwLock<PathBuf>>);

impl LevelAssetReader {
    fn full(&self, path: &Path) -> PathBuf {
        self.0.read().map(|r| r.join(path)).unwrap_or_else(|_| path.to_path_buf())
    }
}

impl AssetReader for LevelAssetReader {
    async fn read<'a>(&'a self, path: &'a Path) -> Result<VecReader, AssetReaderError> {
        let full = self.full(path);
        match std::fs::read(&full) {
            Ok(bytes) => Ok(VecReader::new(bytes)),
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => Err(AssetReaderError::NotFound(full)),
            Err(e) => Err(AssetReaderError::Io(Arc::new(e))),
        }
    }

    async fn read_meta<'a>(&'a self, path: &'a Path) -> Result<VecReader, AssetReaderError> {
        // no .meta files: every asset loads with its loader's defaults or the settings given in code
        Err(AssetReaderError::NotFound(self.full(path)))
    }

    async fn read_directory<'a>(&'a self, path: &'a Path) -> Result<Box<PathStream>, AssetReaderError> {
        let full = self.full(path);
        let entries = std::fs::read_dir(&full).map_err(|_| AssetReaderError::NotFound(full))?;
        let paths: Vec<PathBuf> = entries.filter_map(|e| e.ok()).map(|e| path.join(e.file_name())).collect();
        Ok(Box::new(bevy::tasks::futures_lite::stream::iter(paths)))
    }

    async fn is_directory<'a>(&'a self, path: &'a Path) -> Result<bool, AssetReaderError> {
        Ok(self.full(path).is_dir())
    }
}

/// Points everything that reads the level's files at `dir` (before the level spawners run, levelreset.rs): the
/// launch folder the spawners read, the asset source, the remaster texture set, the animation library's caches, the
/// level's movies. Returns whether the level changed.
pub fn enter(world: &mut World, dir: &Path) -> bool {
    let levels = world.resource::<Levels>().clone();
    if levels.current_dir() == dir {
        return false;
    }
    let from = levels.current_name();
    levels.set_current(dir);
    world.resource_mut::<crate::Launch>().scene = dir.to_path_buf();
    crate::graphics::set_level_textures(dir);
    world.resource_mut::<crate::skin::AnimLibrary>().set_dir(dir);
    world.resource_mut::<crate::cutscenes::Cutscenes>().set_level(dir);
    let assets = world.resource::<AssetServer>().clone();
    for path in PER_LEVEL_ASSETS {
        if assets.get_handle_untyped(path).is_some() {
            assets.reload(path);
        }
    }
    info!("EVENT campaign: level {from} -> {}", folder_name(dir));
    true
}

/// The end sequence after the Stats screen (0x0044a230 states 11-14).
#[derive(Resource, Default, Debug, PartialEq)]
pub enum LevelEnd {
    #[default]
    None,
    /// the Stats screen was left: start the sequence
    Requested,
    /// the success movie: whether it started, frames waited for it to start
    Movie { started: bool, waited: u32 },
    /// the credits after the last level
    Credits,
}

pub struct CampaignPlugin;

impl Plugin for CampaignPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<LevelEnd>().init_resource::<ReplayMode>().add_systems(Update, (end_sequence, log_state).after(crate::gamemenu::MenuSystems));
    }
}

/// Where the campaign goes after this level: the next level when it was exported, else (and in replay mode) the title
/// (lev_shell).
fn next_target(levels: &Levels, mission: Option<&crate::mission::Mission>, replay: bool) -> RestartTo {
    if replay {
        return RestartTo::Title;
    }
    let next = mission.and_then(|m| m.next_level.clone()).unwrap_or_default();
    if next.eq_ignore_ascii_case(SHELL_LEVEL) || next.is_empty() {
        return RestartTo::Title;
    }
    match levels.dir(&next) {
        Some(_) => RestartTo::Level(next),
        None => {
            // as a failed load in the original (0x00492fd0 frame 5: lev_shell with ui_corruptcontent)
            warn!("campaign: the next level {next} was not exported: back to the title");
            RestartTo::Title
        }
    }
}

/// Game+0x568 bit 2, the replay mode: a level played from Extras' Replay Mission (UiScreen_Activate case 41) goes back
/// to the title after its Stats instead of on to the next level (0x0044a230 state 11: 0x004907c0(NULL)). New Game and
/// Load Game clear it (cases 1 / 2).
#[derive(Resource, Default, Debug)]
pub struct ReplayMode(pub bool);

/// A new game's campaign state (0x00401e80 with flag2 at the front end: Game+0x570.. = {5000, 0, 0}, banked at +0x57c):
/// the money and the pub's once-a-campaign lines.
pub fn new_game(opts: &mut crate::options::GameOptions) {
    opts.money = crate::savegame::NEW_GAME_MONEY;
    opts.pub_intro_heard = false;
    opts.pub_once_heard.clear();
}

#[allow(clippy::too_many_arguments)]
fn end_sequence(
    mut end: ResMut<LevelEnd>,
    (mut opts, pub_lines, replay): (ResMut<crate::options::GameOptions>, Res<crate::pubscreen::PubLines>, Res<ReplayMode>),
    levels: Res<Levels>,
    cut: Res<crate::cutscenes::Cutscenes>,
    mission: Option<Res<crate::mission::Mission>>,
    player: Res<MoviePlayer>,
    mut menu: ResMut<GameMenu>,
    mut restart: ResMut<RestartLevel>,
    mut play: MessageWriter<PlayMovies>,
) {
    let level = levels.current_name();
    let last = level.eq_ignore_ascii_case(LAST_LEVEL);
    let after_movie = |menu: &mut GameMenu, restart: &mut RestartLevel| -> LevelEnd {
        if last {
            menu.open_over(Page::Credits, Page::Main);
            info!("EVENT campaign: the end: credits");
            LevelEnd::Credits
        } else {
            let to = next_target(&levels, mission.as_deref(), replay.0);
            info!("EVENT campaign: {level} done -> {to:?}");
            restart.to = Some(to);
            LevelEnd::None
        }
    };
    match *end {
        LevelEnd::None => {}
        LevelEnd::Requested => {
            // the bank (0x00401ef0): the money is already the Stats screen's; the pub's once lines heard here are kept
            if pub_lines.bank(&mut opts) {
                opts.save();
            }
            *end = match cut.end_movie() {
                Some(m) => {
                    info!("EVENT campaign: end movie {m}");
                    play.write(PlayMovies::list(m));
                    LevelEnd::Movie { started: false, waited: 0 }
                }
                None => after_movie(&mut menu, &mut restart),
            };
        }
        LevelEnd::Movie { started, waited } => {
            // the sequence goes on once the movie has played (or ended by a skip), or when it never started (a movie
            // that was not exported) within a second
            let busy = player.busy();
            if busy || (!started && waited < 60) {
                *end = LevelEnd::Movie { started: started || busy, waited: waited + 1 };
            } else {
                *end = after_movie(&mut menu, &mut restart);
            }
        }
        LevelEnd::Credits => {
            // the credits page left: lev_shell (0x0044a230 state 14 -> 0x004907c0(0) and "Loading")
            if menu.page != Page::Credits {
                restart.to = Some(RestartTo::Title);
                *end = LevelEnd::None;
            }
        }
    }
}

/// The state log's "campaign" (playtest expect steps): the level loaded, the money, the tokens, the difficulty, the end
/// sequence; and "loadout": Roman's weapons and specials (absent without Roman).
fn log_state(
    levels: Res<Levels>,
    opts: Res<crate::options::GameOptions>,
    difficulty: Res<crate::gamedata::Difficulty>,
    (end, replay): (Res<LevelEnd>, Res<ReplayMode>),
    roman: Query<&crate::player::Roman>,
    state: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mut state) = state else { return };
    let loadout = roman.iter().next().map(|r| {
        use crate::weapons::{SpecialId, WeaponId};
        let a = &r.arsenal;
        serde_json::json!({ "rifle": a.has(WeaponId::Rifle), "mg": a.gun(WeaponId::Machinegun).map(|w| w.clip + w.ammo), "in_hand": a.in_hand_id().map_or("", WeaponId::name), "guns": a.guns().map(|g| g.id.name()).collect::<Vec<_>>(), "stickies": a.count(SpecialId::StickyBomb), "topsy": a.count(SpecialId::TopsyTurvy) })
    });
    state.0.insert("loadout".into(), loadout.unwrap_or(serde_json::Value::Null));
    let end = match *end {
        LevelEnd::None => "none",
        LevelEnd::Requested => "requested",
        LevelEnd::Movie { .. } => "movie",
        LevelEnd::Credits => "credits",
    };
    state.0.insert("campaign".into(), serde_json::json!({ "level": levels.current_name(), "money": opts.money, "tokens": opts.tokens, "difficulty": difficulty.0, "end": end, "replay": replay.0, "levels_reached": opts.levels_reached.len(), "movies_seen": opts.movies_seen.len() }));
}
