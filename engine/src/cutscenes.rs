//! Armed and Dangerous's use of the movie player (movie.rs): which movies play when.
//!
//! - **Boot.** The first front-end load pushes "PlayMovie" with the intro list `LucasAnim,pm_logo,pre_1_1_opening`
//!   (0x005bc108) under the main menu, unless the startup options turn it off (Game_StartLevel 0x00403690,
//!   src/game/start_level.cpp:120; `g_startupOptions.playIntroMovies`). The remaster plays it before the title menu
//!   once per session (a level restart, levelreset.rs, keeps the boot flag), as the original's shell flag 2 keeps it
//!   to the first load. The story opening, pre_1_1_opening, is the last of the list.
//! - **Level start.** Once the mission timer passes, the mission's `start_movie` is pushed as "PlayMovie", except on
//!   `lev_1_m1` (MissionObject_Update 0x0044db80, `_stricmp(level, "lev_1_m1")` 0x005b1738): its start movie,
//!   pre_1_1_opening, already played at boot, so a new game goes straight to the fly-in.
//! - **Level end.** The end sequence (0x0044a230) pushes "Stats", then "PlayMovie" with `success_movie` when one
//!   is set (lev_1_m1 has none), then "Loading" for the next level (`end_movie` below).
//! - **Pub.** The pub screen opens "pub_01" (0x004930c0, 0x0058fd4c) and sets it looping (movie virtual +0x1c),
//!   drawn across the whole 640-wide screen behind the pub art (the screen has no flag 0x8000).
//! - **Credits.** The credits screen (0x00415940) opens the movie "credits" (0x005b9740) behind the scrolling text.
//!
//! While a full-window movie plays the game is paused, as for every menu screen (Game_SetPaused 0x004031e0 from
//! 0x00490a40), and the menus' input goes to the movie (input.rs, gamemenu.rs).

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::gamemenu::{GameMenu, Page};
use crate::movie::{MoviePlayer, MovieSettings, MovieView, PlayMovies, StopMovies};
use bevy::prelude::*;
use std::path::Path;

/// The boot intro list (0x005bc108).
pub const INTRO_MOVIES: &str = "LucasAnim,pm_logo,pre_1_1_opening";
/// The level whose start movie is skipped (0x005b1738).
const NO_START_MOVIE_LEVEL: &str = "lev_1_m1";
/// The pub screen's movie (0x0058fd4c) and the credits screen's (0x005b9740).
const PUB_MOVIE: &str = "pub_01";
const CREDITS_MOVIE: &str = "credits";
/// Where the background movies sit among the HUD layer's parts: the pub's above its dark backdrop (z -1) and under
/// the pub art (z 9 and up, hud.rs spawn_pub); the credits' above the shell art (z 2), under the text (z 20).
const PUB_MOVIE_Z: f32 = 0.0;
const CREDITS_MOVIE_Z: f32 = 3.0;

/// What this run plays (main.rs fills it from the arguments and the level's data).
#[derive(Resource, Default, Clone)]
pub struct Cutscenes {
    /// play the intro list before the title menu
    pub intro: bool,
    /// the level's name and its mission's start / success movies (mission.json)
    pub level: String,
    pub start_movie: String,
    #[allow(dead_code)]
    pub success_movie: String,
    /// the run continues a saved game (past the level's start: no start movie)
    pub from_save: bool,
}

impl Cutscenes {
    /// The level's movies from the exported mission (work/remaster/<level>/mission.json).
    pub fn from_scene(scene: &Path, intro: bool) -> Self {
        let v: serde_json::Value = std::fs::read_to_string(scene.join("mission.json")).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default();
        let r = &v["mission"]["resolved"];
        let s = |k: &str| r[k].as_str().unwrap_or_default().to_owned();
        Cutscenes { intro, level: v["level"].as_str().unwrap_or_default().to_owned(), start_movie: s("start_movie"), success_movie: s("success_movie"), from_save: false }
    }

    /// The movie the level starts with: its start_movie, except on lev_1_m1 (0x0044db80).
    pub fn level_start_movie(&self) -> Option<&str> {
        (!self.start_movie.is_empty() && !self.level.eq_ignore_ascii_case(NO_START_MOVIE_LEVEL)).then_some(self.start_movie.as_str())
    }

    /// The movie after the level's statistics (0x0044a230 state 11), if any. lev_1_m1 has none and the remaster has
    /// no next level yet (statscreen.rs returns to the title): call this from the level change once one exists.
    #[allow(dead_code)]
    pub fn end_movie(&self) -> Option<&str> {
        (!self.success_movie.is_empty()).then_some(self.success_movie.as_str())
    }
}

pub struct CutscenesPlugin;

impl Plugin for CutscenesPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<Cutscenes>()
            .init_level_resource::<GamePausedByMovie>()
            .add_systems(Update, (settings, boot, level_start, screens, playtest_movies).before(crate::movie::MovieSystems))
            .add_systems(Update, (pause_game, crate::movie::log_state).after(crate::movie::MovieSystems));
    }
}

/// The options that act on movies: the movie subtitle option (Game+0x176), the master volume.
fn settings(opts: Res<crate::options::GameOptions>, mixer: Res<crate::audio::Mixer>, mut s: ResMut<MovieSettings>) {
    let volume = mixer.master;
    if s.subtitles != opts.subtitles_movie || s.volume != volume {
        s.subtitles = opts.subtitles_movie;
        // guess: the movie stream's level is the master's alone (the stream 0x0050e1a0 creates is in no volume group
        // the options set)
        s.volume = volume;
    }
}

/// The intro list at boot, before the title menu.
fn boot(mut done: Local<bool>, cut: Res<Cutscenes>, state: Res<State<AppState>>, mut play: MessageWriter<PlayMovies>) {
    if *done {
        return;
    }
    *done = true;
    if cut.intro && *state.get() == AppState::Title {
        play.write(PlayMovies::list(INTRO_MOVIES));
    }
}

/// The level's start movie once play begins (none on lev_1_m1).
fn level_start(mut done: crate::levelreset::PerLevel<bool>, cut: Res<Cutscenes>, state: Res<State<AppState>>, mut play: MessageWriter<PlayMovies>) {
    let done = done.get();
    if *done || *state.get() != AppState::Playing {
        return;
    }
    *done = true;
    // a loaded game is past its start (the original shows the start movie on the mission's first update only)
    if cut.from_save {
        return;
    }
    if let Some(m) = cut.level_start_movie() {
        play.write(PlayMovies::list(m));
    }
}

/// The pub's and the credits' background movies follow their pages.
fn screens(menu: Res<GameMenu>, player: Res<MoviePlayer>, mut last: Local<Option<Page>>, mut play: MessageWriter<PlayMovies>, mut stop: MessageWriter<StopMovies>) {
    let page = Some(menu.page);
    if *last == page {
        return;
    }
    let was = *last;
    *last = page;
    let background = |name: &str, looped: bool, z: f32| PlayMovies { names: vec![name.into()], skippable: false, looped, view: MovieView::Behind { width: 640.0, z }, start_at: 0.0 };
    match menu.page {
        Page::Pub => {
            play.write(background(PUB_MOVIE, true, PUB_MOVIE_Z));
        }
        Page::Credits => {
            play.write(background(CREDITS_MOVIE, false, CREDITS_MOVIE_Z));
        }
        _ if matches!(was, Some(Page::Pub | Page::Credits)) && player.busy() && !player.takes_input() => {
            stop.write(StopMovies);
        }
        _ => {}
    }
}

/// Whether the movie paused the game (so it resumes only what it paused).
#[derive(Resource, Default)]
struct GamePausedByMovie(bool);

/// A full-window movie pauses the game (Game_SetPaused 0x004031e0 for any screen).
fn pause_game(player: Res<MoviePlayer>, mut vtime: ResMut<Time<Virtual>>, mut by: ResMut<GamePausedByMovie>, play: Option<ResMut<crate::playtest::PlayState>>) {
    if let Some(mut p) = play {
        p.0.insert("game_paused".into(), vtime.is_paused().into());
    }
    let on = player.takes_input() && player.busy();
    if on && !vtime.is_paused() {
        vtime.pause();
        by.0 = true;
    } else if !on && by.0 {
        if vtime.is_paused() {
            vtime.unpause();
        }
        by.0 = false;
    }
}

/// Playtest `movie` steps: "name,name" (a skippable list), or {"play": "list", "at": seconds into the first,
/// "skippable": bool, "loop": bool, "behind": z, "subtitles": the movie subtitle option}, or {"stop": true}.
fn playtest_movies(pt: Option<ResMut<crate::playtest::Playtest>>, mut play: MessageWriter<PlayMovies>, mut stop: MessageWriter<StopMovies>, mut opts: ResMut<crate::options::GameOptions>) {
    let Some(mut pt) = pt else { return };
    for m in std::mem::take(&mut pt.movies) {
        // {"subtitles": bool}: the movie subtitle option, as the audio page's checkbox sets it
        if let Some(on) = m["subtitles"].as_bool() {
            opts.subtitles_movie = on;
            if m.get("play").is_none() {
                continue;
            }
        }
        if let Some(s) = m.as_str() {
            play.write(PlayMovies::list(s));
            continue;
        }
        if m["stop"].as_bool() == Some(true) {
            stop.write(StopMovies);
            continue;
        }
        let mut p = PlayMovies::list(m["play"].as_str().unwrap_or_default());
        p.start_at = m["at"].as_f64().unwrap_or(0.0) as f32;
        p.skippable = m["skippable"].as_bool().unwrap_or(true);
        p.looped = m["loop"].as_bool().unwrap_or(false);
        if let Some(z) = m["behind"].as_f64() {
            p.view = MovieView::Behind { width: 640.0, z: z as f32 };
        }
        play.write(p);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lev_1_m1_skips_its_start_movie() {
        let c = Cutscenes { intro: true, level: "lev_1_m1".into(), start_movie: "pre_1_1_opening".into(), success_movie: String::new(), from_save: false };
        assert_eq!(c.level_start_movie(), None);
        assert_eq!(c.end_movie(), None);
        let c = Cutscenes { level: "lev_2_m1".into(), start_movie: "pre_2_1_Sacred_Lamb".into(), ..c };
        assert_eq!(c.level_start_movie(), Some("pre_2_1_Sacred_Lamb"));
    }
}
