//! Developer tools for play-testing the remaster (remaster only: the original has no counterpart).
//!
//! - [`DevSettings`]: the toggles kept in options.json under "dev" (god mode, invincible, infinite ammo, big heads /
//!   hands / boots, the upside-down screen, every level and movie unlocked, the FPS and state overlays). The cheat
//!   toggles drive the existing cheat flags ([`crate::cheats::Cheats`]): switched on, a toggle sets its flag at every
//!   level load and whenever it changes; switched off, it clears the flag (a code typed on the Cheats page still works
//!   as before).
//! - [`DevCommand`]: everything the menu, the hotkeys and the command line can do (toggles, actions, a level by name).
//! - Hotkeys: F1 the developer menu (pad: View / Create held + Y / Triangle), F5 quick save, F9 quick load, F8 a bookmark (session.rs).
//! - The command line: `--level lev_3_m1` starts in that level (the New Game setup, no title menu), `--dev
//!   god,infammo,...` switches toggles on for the run ([`DevToggle::from_cli`]).
//! - [`menu`]: the menu (egui; mouse / trackpad, keyboard and pad), [`overlay`]: FPS and state overlays,
//!   [`session`]: the session recording (logs, state, input, frame times, bookmarks).
//!
//! API: [`DevToolsPlugin`], [`DevSettings`], [`DevCommand`], [`overlay_closed`] (a run condition for the game's
//! menus: they take no input while the developer menu or a bookmark note is open).

pub mod menu;
pub mod overlay;
pub mod session;

use crate::AppState;
use crate::cheats::{self, Cheats};
use crate::levelreset::{RestartLevel, RestartTo};
use crate::options::GameOptions;
use bevy::prelude::*;
use serde::{Deserialize, Serialize};

/// The developer toggles, saved in options.json "dev".
#[derive(Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Debug, Default)]
#[serde(default)]
pub struct DevSettings {
    pub god_mode: bool,
    pub invincible: bool,
    pub infinite_ammo: bool,
    pub big_heads: bool,
    pub big_hands: bool,
    pub big_boots: bool,
    pub topsy_screen: bool,
    /// every level in Extras > Replay Level and every movie in Extras > Cutscenes (the MISSIONS / AMATINEE cheats)
    pub unlock_all: bool,
    pub show_fps: bool,
    pub show_state: bool,
}

/// One toggle of [`DevSettings`].
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DevToggle {
    GodMode,
    Invincible,
    InfiniteAmmo,
    BigHeads,
    BigHands,
    BigBoots,
    TopsyScreen,
    UnlockAll,
    ShowFps,
    ShowState,
}

impl DevToggle {
    pub const ALL: [DevToggle; 10] = [
        DevToggle::GodMode,
        DevToggle::Invincible,
        DevToggle::InfiniteAmmo,
        DevToggle::BigHeads,
        DevToggle::BigHands,
        DevToggle::BigBoots,
        DevToggle::TopsyScreen,
        DevToggle::UnlockAll,
        DevToggle::ShowFps,
        DevToggle::ShowState,
    ];

    pub fn label(self) -> &'static str {
        match self {
            DevToggle::GodMode => "God mode",
            DevToggle::Invincible => "Invincible",
            DevToggle::InfiniteAmmo => "Infinite ammo",
            DevToggle::BigHeads => "Big heads",
            DevToggle::BigHands => "Big hands",
            DevToggle::BigBoots => "Big boots",
            DevToggle::TopsyScreen => "Topsy turvy screen",
            DevToggle::UnlockAll => "Unlock all levels and movies",
            DevToggle::ShowFps => "Show FPS / frame ms",
            DevToggle::ShowState => "Show state overlay",
        }
    }

    /// The name on the command line (`--dev god,infammo`) and in the state log.
    pub fn cli(self) -> &'static str {
        match self {
            DevToggle::GodMode => "god",
            DevToggle::Invincible => "invincible",
            DevToggle::InfiniteAmmo => "infammo",
            DevToggle::BigHeads => "bigheads",
            DevToggle::BigHands => "bighands",
            DevToggle::BigBoots => "bigboots",
            DevToggle::TopsyScreen => "topsy",
            DevToggle::UnlockAll => "unlock",
            DevToggle::ShowFps => "fps",
            DevToggle::ShowState => "state",
        }
    }

    pub fn from_cli(name: &str) -> Option<DevToggle> {
        let n = name.trim().to_ascii_lowercase();
        DevToggle::ALL.into_iter().find(|t| t.cli() == n || t.cli().trim_end_matches('s') == n)
    }

    /// The cheat flags the toggle drives (cheats.rs), none for the overlays.
    pub fn cheat_flags(self) -> u32 {
        match self {
            DevToggle::GodMode => cheats::GOD_MODE,
            DevToggle::Invincible => cheats::INVINCIBLE,
            DevToggle::InfiniteAmmo => cheats::INFINITE_AMMO,
            DevToggle::BigHeads => cheats::BIG_HEADS,
            DevToggle::BigHands => cheats::BIG_HANDS,
            DevToggle::BigBoots => cheats::BIG_BOOTS,
            DevToggle::TopsyScreen => cheats::FLIP_SCREEN,
            DevToggle::UnlockAll => cheats::ALL_LEVELS | cheats::ALL_MOVIES,
            DevToggle::ShowFps | DevToggle::ShowState => 0,
        }
    }

    pub fn get(self, s: &DevSettings) -> bool {
        match self {
            DevToggle::GodMode => s.god_mode,
            DevToggle::Invincible => s.invincible,
            DevToggle::InfiniteAmmo => s.infinite_ammo,
            DevToggle::BigHeads => s.big_heads,
            DevToggle::BigHands => s.big_hands,
            DevToggle::BigBoots => s.big_boots,
            DevToggle::TopsyScreen => s.topsy_screen,
            DevToggle::UnlockAll => s.unlock_all,
            DevToggle::ShowFps => s.show_fps,
            DevToggle::ShowState => s.show_state,
        }
    }

    pub fn set(self, s: &mut DevSettings, on: bool) {
        let f = match self {
            DevToggle::GodMode => &mut s.god_mode,
            DevToggle::Invincible => &mut s.invincible,
            DevToggle::InfiniteAmmo => &mut s.infinite_ammo,
            DevToggle::BigHeads => &mut s.big_heads,
            DevToggle::BigHands => &mut s.big_hands,
            DevToggle::BigBoots => &mut s.big_boots,
            DevToggle::TopsyScreen => &mut s.topsy_screen,
            DevToggle::UnlockAll => &mut s.unlock_all,
            DevToggle::ShowFps => &mut s.show_fps,
            DevToggle::ShowState => &mut s.show_state,
        };
        *f = on;
    }
}

impl DevSettings {
    /// The cheat flags these settings switch on.
    pub fn cheat_flags(&self) -> u32 {
        DevToggle::ALL.iter().filter(|t| t.get(self)).fold(0, |m, t| m | t.cheat_flags())
    }

    /// The toggles on, by their command-line names (the state log, the events log).
    pub fn names_on(&self) -> Vec<&'static str> {
        DevToggle::ALL.iter().filter(|t| t.get(self)).map(|t| t.cli()).collect()
    }
}

/// The one-shot actions.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DevAction {
    FullHealth,
    FullAmmo,
    /// every special to its maximum, the empty gun slots filled with guns not carried (Roman carries three guns:
    /// the Weapons page swaps any gun into his hands)
    GiveAll,
    QuickSave,
    QuickLoad,
    RestartLevel,
    Bookmark,
}

impl DevAction {
    pub const ALL: [DevAction; 7] = [DevAction::FullHealth, DevAction::FullAmmo, DevAction::GiveAll, DevAction::QuickSave, DevAction::QuickLoad, DevAction::RestartLevel, DevAction::Bookmark];

    pub fn label(self) -> &'static str {
        match self {
            DevAction::FullHealth => "Full health",
            DevAction::FullAmmo => "Full ammo",
            DevAction::GiveAll => "Give every special and fill the gun slots",
            DevAction::QuickSave => "Quick save",
            DevAction::QuickLoad => "Quick load",
            DevAction::RestartLevel => "Restart level",
            DevAction::Bookmark => "Bookmark (screenshot, save, note)",
        }
    }

    /// The hotkey shown beside it.
    pub fn hotkey(self) -> Option<&'static str> {
        match self {
            DevAction::QuickSave => Some("F5"),
            DevAction::QuickLoad => Some("F9"),
            DevAction::Bookmark => Some("F8"),
            _ => None,
        }
    }

    /// Needs a level in play (Roman).
    pub fn needs_level(self) -> bool {
        !matches!(self, DevAction::QuickLoad)
    }
}

/// What the developer tools are asked to do (menu, hotkeys, command line).
#[derive(Message, Clone, Debug, PartialEq)]
pub enum DevCommand {
    Toggle(DevToggle),
    Action(DevAction),
    /// start this level (by its lev_ id) with the New Game setup
    Level(String),
    /// put this gun in Roman's hands (a free slot, else in place of the gun in hand), full
    Gun(crate::weapons::WeaponId),
}

/// The command line's developer setup (main.rs): toggles switched on for this run.
#[derive(Resource, Default, Clone)]
pub struct DevLaunch {
    pub toggles: Vec<DevToggle>,
}

impl DevLaunch {
    /// `--dev god,infammo,...`; unknown names are reported and skipped.
    pub fn parse(list: &str) -> Self {
        let mut toggles = Vec::new();
        for n in list.split(',').filter(|n| !n.trim().is_empty()) {
            match DevToggle::from_cli(n) {
                Some(t) => toggles.push(t),
                None => eprintln!("--dev: unknown toggle {n:?} (known: {})", DevToggle::ALL.map(|t| t.cli()).join(", ")),
            }
        }
        DevLaunch { toggles }
    }
}

/// A screen the developer tools show over the game (the menu, the bookmark note): the game is paused under it when it
/// opened during play, and resumes when it closes.
#[derive(Resource, Default, Debug)]
pub struct DevOverlay {
    /// the developer menu is open
    pub menu: bool,
    /// a bookmark's note box is open (session.rs)
    pub note: bool,
    /// close: back to play (it opened in play)
    pub resume: bool,
    /// the frame it opened: its input starts next frame (the press that opened it is not also a choice)
    pub opened_frame: u32,
}

impl DevOverlay {
    pub fn open(&self) -> bool {
        self.menu || self.note
    }
}

/// Run condition: no developer screen is open (the game's menus take input).
pub fn overlay_closed(o: Option<Res<DevOverlay>>) -> bool {
    o.is_none_or(|o| !o.open())
}

pub struct DevToolsPlugin;

impl Plugin for DevToolsPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<DevOverlay>()
            .init_resource::<DevLaunch>()
            .add_message::<DevCommand>()
            .add_systems(PreStartup, apply_launch)
            .add_systems(Update, (hotkeys, run_commands, apply_toggles, log_state).chain().after(crate::gamemenu::MenuSystems))
            .add_systems(Update, hide_game_menu.before(crate::gamemenu::MenuSystems))
            .add_plugins((menu::DevMenuPlugin, overlay::OverlayPlugin, session::SessionPlugin));
    }
}

/// `--dev`: the toggles on for this run (in the options as if chosen in the menu).
fn apply_launch(launch: Res<DevLaunch>, mut opts: ResMut<GameOptions>) {
    for t in &launch.toggles {
        t.set(&mut opts.dev, true);
    }
}

/// Pauses the game under a developer screen when it opens during play (as Esc opens the pause menu: gamemenu.rs).
/// Returns whether play should resume when it closes.
pub fn pause_for_overlay(state: &State<AppState>, next: &mut NextState<AppState>, vtime: &mut Time<Virtual>, menu: &mut crate::gamemenu::GameMenu) -> bool {
    if *state.get() != AppState::Playing {
        return false;
    }
    next.set(AppState::Paused);
    vtime.pause();
    // paused like the pause menu, but without showing it under the developer screen
    menu.suppressed = true;
    true
}

/// The game's own menu is not drawn while a developer screen is up (the human saw the pause menu and the developer
/// menu together); it comes back when the screen closes over a paused game or the title.
fn hide_game_menu(overlay: Res<DevOverlay>, mut menu: ResMut<crate::gamemenu::GameMenu>) {
    let hide = overlay.open();
    if menu.suppressed != hide {
        menu.suppressed = hide;
    }
}

/// Leaves a developer screen: back to play when it opened in play.
pub fn resume_after_overlay(overlay: &mut DevOverlay, next: &mut NextState<AppState>, vtime: &mut Time<Virtual>) {
    if std::mem::take(&mut overlay.resume) && !overlay.open() {
        next.set(AppState::Playing);
        vtime.unpause();
    }
}

/// The developer keys: F1 the menu, F5 quick save, F9 quick load, F8 a bookmark. Reserved: the Controls page cannot
/// bind them (bindings.rs `capture`).
pub const RESERVED_KEYS: [KeyCode; 4] = [KeyCode::F1, KeyCode::F5, KeyCode::F8, KeyCode::F9];

/// F5 quick save, F9 quick load, F8 a bookmark (F1, the menu: menu.rs).
fn hotkeys(keys: Res<ButtonInput<KeyCode>>, overlay: Res<DevOverlay>, state: Res<State<AppState>>, mut out: MessageWriter<DevCommand>) {
    if overlay.note || *state.get() == AppState::Loading {
        return;
    }
    for (k, a) in [(KeyCode::F5, DevAction::QuickSave), (KeyCode::F9, DevAction::QuickLoad), (KeyCode::F8, DevAction::Bookmark)] {
        if keys.just_pressed(k) {
            out.write(DevCommand::Action(a));
        }
    }
}

/// Carries out the commands.
#[allow(clippy::too_many_arguments)]
fn run_commands(
    mut cmds: MessageReader<DevCommand>,
    mut opts: ResMut<GameOptions>,
    mut roman: Query<(&mut crate::player::Roman, &mut crate::fx::Health)>,
    data: Option<Res<crate::gamedata::GameData>>,
    (mut restart, mut replay, levels): (ResMut<RestartLevel>, ResMut<crate::campaign::ReplayMode>, Res<crate::campaign::Levels>),
    (mut save, mut mission): (ResMut<crate::savegame::SaveRequest>, Option<ResMut<crate::mission::Mission>>),
    (mut overlay, mut next, mut vtime, mut bookmark): (ResMut<DevOverlay>, ResMut<NextState<AppState>>, ResMut<Time<Virtual>>, ResMut<session::BookmarkRequest>),
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    for c in cmds.read() {
        match c {
            DevCommand::Toggle(t) => {
                let on = !t.get(&opts.dev);
                t.set(&mut opts.dev, on);
                opts.save();
                info!("EVENT dev: {} {}", t.cli(), if on { "on" } else { "off" });
            }
            DevCommand::Level(level) => {
                if levels.dir(level).is_none() {
                    warn!("dev: level {level} was not exported");
                    continue;
                }
                // the Extras replay's start (gamemenu.rs Act::Replay: the New Game money, the level's own loadout),
                // but the campaign goes on to the next level after a win
                crate::campaign::new_game(&mut opts);
                replay.0 = false;
                restart.to = Some(RestartTo::Level(level.clone()));
                overlay.menu = false;
                overlay.resume = false;
                info!("EVENT dev: level select {level}");
            }
            DevCommand::Gun(id) => {
                let (Ok((mut r, _)), Some(data)) = (roman.single_mut(), data.as_deref()) else { continue };
                let gun = crate::weapons::Gun::full(*id, crate::weapons::spec::GunSpec::load(data, *id));
                if !r.arsenal.has(*id) && !r.arsenal.add(gun.clone()) {
                    let hand = r.arsenal.in_hand_id();
                    r.arsenal.replace(hand, gun);
                }
                r.arsenal.equip(*id);
                info!("EVENT dev: gun {}", id.name());
            }
            DevCommand::Action(a) => {
                let in_level = roman.single().is_ok() && mission.is_some();
                if a.needs_level() && !in_level {
                    sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
                    continue;
                }
                match a {
                    DevAction::FullHealth => {
                        if let Ok((r, mut h)) = roman.single_mut() {
                            cheats::full_health(&r, &mut h);
                        }
                    }
                    DevAction::FullAmmo => {
                        if let (Ok((mut r, _)), Some(data)) = (roman.single_mut(), data.as_deref()) {
                            cheats::full_ammo(&mut r.arsenal, data);
                        }
                    }
                    DevAction::GiveAll => {
                        if let (Ok((mut r, _)), Some(data)) = (roman.single_mut(), data.as_deref()) {
                            give_all(&mut r.arsenal, data);
                        }
                    }
                    DevAction::QuickSave => {
                        // always allowed (the checkpoint rule is for the pause menu's saves)
                        save.save = true;
                        save.path = None;
                        save.source = crate::savegame::SaveSource::Script;
                    }
                    DevAction::QuickLoad => {
                        let p = crate::savegame::save_path();
                        if crate::savegame::read(&p).is_some() {
                            restart.to = Some(RestartTo::Load(p));
                            overlay.menu = false;
                            overlay.resume = false;
                        } else {
                            if let Some(m) = mission.as_deref_mut() {
                                m.feed("No quick save yet (F5)");
                            }
                            sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
                            continue;
                        }
                    }
                    DevAction::RestartLevel => {
                        restart.to = Some(RestartTo::Play);
                        overlay.menu = false;
                        overlay.resume = false;
                    }
                    DevAction::Bookmark => {
                        // the menu closes first, so the screenshot shows the game
                        if overlay.menu {
                            overlay.menu = false;
                            resume_after_overlay(&mut overlay, &mut next, &mut vtime);
                        }
                        bookmark.0 = true;
                    }
                }
                info!("EVENT dev: {a:?}");
            }
        }
        sounds.write(crate::audio::PlaySound::menu("ui_select"));
    }
}

/// Every special to its maximum and the empty gun slots filled with the guns not carried (in weapon table order).
fn give_all(arsenal: &mut crate::weapons::Arsenal, data: &crate::gamedata::GameData) {
    use crate::weapons::{Gun, SpecialId, WeaponId, spec};
    for s in SpecialId::ALL {
        arsenal.set_count(s, spec::special_max(data, s));
    }
    for id in WeaponId::ALL {
        if !arsenal.has(id) && !arsenal.add(Gun::full(id, spec::GunSpec::load(data, id))) {
            break;
        }
    }
    cheats::full_ammo(arsenal, data);
}

/// The toggles' cheat flags: set at every level load and whenever a toggle changes; a toggle switched off clears its
/// flags.
fn apply_toggles(opts: Res<GameOptions>, mut cheats: ResMut<Cheats>, generation: Res<crate::levelreset::LevelGeneration>, mut last: Local<Option<(DevSettings, u32)>>) {
    let now = (opts.dev, generation.0);
    if *last == Some(now) {
        return;
    }
    let before = last.map(|l| l.0.cheat_flags()).unwrap_or(0);
    let flags = opts.dev.cheat_flags();
    cheats.active = (cheats.active & !(before & !flags)) | flags;
    *last = Some(now);
}

/// The state log's "dev": the menu (open, page, the selected item's label), the toggles on, the overlay.
fn log_state(overlay: Res<DevOverlay>, menu: Res<menu::DevMenu>, opts: Res<GameOptions>, cheats: Res<Cheats>, state: Option<ResMut<crate::playtest::PlayState>>) {
    let Some(mut state) = state else { return };
    state.0.insert(
        "dev".into(),
        serde_json::json!({ "open": overlay.menu, "note": overlay.note, "page": format!("{:?}", menu.page), "selected": menu.selected_label(), "on": opts.dev.names_on(), "cheats": cheats.active }),
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The command line's names, singular or plural; the flags of a set of toggles.
    #[test]
    fn toggles() {
        assert_eq!(DevToggle::from_cli("god"), Some(DevToggle::GodMode));
        assert_eq!(DevToggle::from_cli("InfAmmo"), Some(DevToggle::InfiniteAmmo));
        assert_eq!(DevToggle::from_cli("bighead"), Some(DevToggle::BigHeads));
        assert_eq!(DevToggle::from_cli("nope"), None);
        let l = DevLaunch::parse("god,infammo,,nope");
        assert_eq!(l.toggles, vec![DevToggle::GodMode, DevToggle::InfiniteAmmo]);
        let mut s = DevSettings::default();
        for t in DevToggle::ALL {
            assert!(!t.get(&s));
            t.set(&mut s, true);
            assert!(t.get(&s));
        }
        assert_eq!(s.cheat_flags(), 0x1 | 0x20 | 0x10 | 0x2 | 0x4 | 0x8 | 0x4000 | 0x800 | 0x1000);
    }

    /// options.json keeps the toggles under "dev"; an older file without it keeps the defaults.
    #[test]
    fn options_section() {
        let mut o = GameOptions::default();
        o.dev.god_mode = true;
        o.dev.show_fps = true;
        let j = serde_json::to_value(&o).unwrap();
        assert_eq!(j["dev"]["god_mode"], true);
        assert_eq!(GameOptions::from_json(&j.to_string()).dev, o.dev);
        assert_eq!(GameOptions::from_json(r#"{"music_volume": 0.5}"#).dev, DevSettings::default());
    }
}
