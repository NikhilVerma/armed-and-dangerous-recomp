//! The developer menu: F1 anywhere (the title, play, the pause menu), the pad's View / Create button, or the
//! "Developer menu" button shown under the title and pause menus (a click or a trackpad tap). Drawn with egui over the
//! game; the game pauses under it when it opened in play.
//!
//! Input: the mouse / trackpad clicks an item; the keyboard and the pad move the selection (Up / Down, W / S, D-pad,
//! stick), choose (Enter / Space / A, or Left / Right on a toggle) and go back (Esc / right click / B; F1 closes).
//! Pages: Main (level select, the toggles, the actions, weapons), Levels (every campaign and bonus level by name and
//! id), Weapons (any gun into Roman's hands).

use super::{DevAction, DevCommand, DevOverlay, DevToggle};
use crate::AppState;
use crate::options::GameOptions;
use bevy::prelude::*;
use bevy_egui::{EguiContexts, EguiPrimaryContextPass, egui};

#[derive(Clone, Copy, PartialEq, Eq, Debug, Default)]
pub enum DevPage {
    #[default]
    Main,
    Levels,
    Weapons,
}

/// What an item does.
#[derive(Clone, Debug, PartialEq)]
pub enum ItemCmd {
    Dev(DevCommand),
    Page(DevPage),
    Close,
}

#[derive(Clone, Debug)]
pub struct Item {
    pub label: String,
    pub cmd: ItemCmd,
    pub enabled: bool,
    /// a toggle's state
    pub on: Option<bool>,
}

/// The menu's state.
#[derive(Resource, Default)]
pub struct DevMenu {
    pub page: DevPage,
    pub selected: usize,
    /// the items on the page now (rebuilt each frame)
    items: Vec<Item>,
    /// an item clicked in the egui pass, done next frame
    clicked: Option<usize>,
    /// the button under the game's menus was clicked: open next frame
    open_request: bool,
    /// the selection moved by keys / pad: scroll it into view
    scroll: bool,
}

impl DevMenu {
    pub fn selected_label(&self) -> Option<&str> {
        self.items.get(self.selected).map(|i| i.label.as_str())
    }
}

pub struct DevMenuPlugin;

impl Plugin for DevMenuPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<DevMenu>()
            .add_systems(Update, input.after(crate::gamemenu::MenuSystems).before(super::hotkeys))
            .add_systems(EguiPrimaryContextPass, draw.after(super::overlay::zoom));
    }
}

/// What the pages list, from the game's state.
struct Context<'a> {
    opts: &'a GameOptions,
    levels: &'a crate::campaign::Levels,
    texts: Option<&'a crate::gamemenu::Texts>,
    in_level: bool,
    quicksave: bool,
}

fn items(page: DevPage, c: &Context) -> Vec<Item> {
    let item = |label: String, cmd: ItemCmd, enabled: bool| Item { label, cmd, enabled, on: None };
    match page {
        DevPage::Main => {
            let mut v = vec![item("Level select".into(), ItemCmd::Page(DevPage::Levels), true)];
            for t in DevToggle::ALL {
                v.push(Item { label: t.label().into(), cmd: ItemCmd::Dev(DevCommand::Toggle(t)), enabled: true, on: Some(t.get(&c.opts.dev)) });
            }
            for a in DevAction::ALL {
                let label = match a.hotkey() {
                    Some(k) => format!("{} ({k})", a.label()),
                    None => a.label().into(),
                };
                let enabled = if a == DevAction::QuickLoad { c.quicksave } else { c.in_level || !a.needs_level() };
                v.push(item(label, ItemCmd::Dev(DevCommand::Action(a)), enabled));
            }
            v.push(item("Weapons".into(), ItemCmd::Page(DevPage::Weapons), c.in_level));
            v.push(item("Close".into(), ItemCmd::Close, true));
            v
        }
        DevPage::Levels => {
            let mut v = vec![item("Back".into(), ItemCmd::Page(DevPage::Main), true)];
            for l in crate::extras::LEVELS.iter().chain(crate::extras::BONUS_LEVELS.iter()).filter(|l| c.levels.dir(l).is_some()) {
                let name = c.texts.map(|t| t.get(l)).filter(|n| n != l).unwrap_or_default();
                let label = if name.is_empty() { (*l).to_owned() } else { format!("{name}  ({l})") };
                v.push(item(label, ItemCmd::Dev(DevCommand::Level((*l).to_owned())), true));
            }
            v
        }
        DevPage::Weapons => {
            let mut v = vec![item("Back".into(), ItemCmd::Page(DevPage::Main), true)];
            for id in crate::weapons::WeaponId::ALL {
                v.push(item(format!("Take the {}", id.name()), ItemCmd::Dev(DevCommand::Gun(id)), c.in_level));
            }
            v
        }
    }
}

/// The View (Xbox) / Create (PlayStation) button of the pad in use.
fn pad_view(pads: &Query<&Gamepad>, active: &crate::input::ActivePad) -> bool {
    pads.iter().any(|p| p.just_pressed(GamepadButton::Select)) && (active.0.is_some() || crate::input::pad_active())
}

#[allow(clippy::too_many_arguments)]
fn input(
    keys: Res<ButtonInput<KeyCode>>,
    ctl: Res<crate::input::Controls>,
    (pads, active_pad): (Query<&Gamepad>, Res<crate::input::ActivePad>),
    mut menu: ResMut<DevMenu>,
    mut overlay: ResMut<DevOverlay>,
    (state, mut next, mut vtime, mut game_menu): (Res<State<AppState>>, ResMut<NextState<AppState>>, ResMut<Time<Virtual>>, ResMut<crate::gamemenu::GameMenu>),
    (opts, levels, texts): (Res<GameOptions>, Res<crate::campaign::Levels>, Option<Res<crate::gamemenu::Texts>>),
    (roman, mission): (Query<(), With<crate::player::Roman>>, Option<Res<crate::mission::Mission>>),
    frame: Res<bevy::diagnostic::FrameCount>,
    mut out: MessageWriter<DevCommand>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
) {
    let c = Context { opts: &opts, levels: &levels, texts: texts.as_deref(), in_level: !roman.is_empty() && mission.is_some(), quicksave: overlay.menu && crate::savegame::save_path().is_file() };
    let toggle_key = keys.just_pressed(KeyCode::F1) || pad_view(&pads, &active_pad) || std::mem::take(&mut menu.open_request);
    if !overlay.menu {
        if toggle_key && !overlay.note && *state.get() != AppState::Loading {
            overlay.menu = true;
            overlay.resume = super::pause_for_overlay(&state, &mut next, &mut vtime, &mut game_menu);
            overlay.opened_frame = frame.0;
            menu.page = DevPage::Main;
            menu.selected = 0;
            menu.scroll = true;
            sounds.write(crate::audio::PlaySound::menu("ui_select"));
            info!("EVENT dev: menu opened");
        }
        menu.items = if overlay.menu { items(menu.page, &c) } else { Vec::new() };
        return;
    }
    menu.items = items(menu.page, &c);
    let close = |menu: &mut DevMenu, overlay: &mut DevOverlay, next: &mut NextState<AppState>, vtime: &mut Time<Virtual>| {
        overlay.menu = false;
        menu.items.clear();
        super::resume_after_overlay(overlay, next, vtime);
    };
    if frame.0 <= overlay.opened_frame {
        return;
    }
    if toggle_key {
        close(&mut menu, &mut overlay, &mut next, &mut vtime);
        sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        return;
    }
    let n = menu.items.len();
    if n == 0 {
        return;
    }
    menu.selected = menu.selected.min(n - 1);
    // up / down over the enabled items, wrapping
    if ctl.step.y != 0 {
        let dir = if ctl.step.y < 0 { 1 } else { n - 1 };
        let mut i = menu.selected;
        for _ in 0..n {
            i = (i + dir) % n;
            if menu.items[i].enabled {
                break;
            }
        }
        menu.selected = i;
        menu.scroll = true;
        sounds.write(crate::audio::PlaySound::menu("ui_move"));
    }
    let toggle_step = ctl.step.x != 0 && menu.items[menu.selected].on.is_some();
    let chosen = if let Some(i) = menu.clicked.take() {
        menu.selected = i.min(n - 1);
        Some(menu.selected)
    } else if ctl.pressed(crate::input::Act::Select) || toggle_step {
        Some(menu.selected)
    } else {
        None
    };
    if ctl.pressed(crate::input::Act::Back) {
        if menu.page == DevPage::Main {
            close(&mut menu, &mut overlay, &mut next, &mut vtime);
        } else {
            menu.page = DevPage::Main;
            menu.selected = 0;
            menu.scroll = true;
        }
        sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        return;
    }
    let Some(i) = chosen else { return };
    let item = menu.items[i].clone();
    if !item.enabled {
        sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        return;
    }
    match item.cmd {
        ItemCmd::Dev(cmd) => {
            out.write(cmd);
        }
        ItemCmd::Page(p) => {
            menu.page = p;
            menu.selected = 0;
            menu.scroll = true;
            sounds.write(crate::audio::PlaySound::menu("ui_select"));
        }
        ItemCmd::Close => {
            close(&mut menu, &mut overlay, &mut next, &mut vtime);
            sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        }
    }
}

/// The prompt line for the device in use (keyboard and mouse, or the pad).
fn hints() -> &'static str {
    if crate::input::pad_active() {
        "D-pad / stick: choose    A / Cross: select    B / Circle: back    View / Create: close"
    } else {
        "Click or Up / Down + Enter    Left / Right: toggle    Esc: back    F1: close"
    }
}

fn draw(
    mut contexts: EguiContexts,
    mut menu: ResMut<DevMenu>,
    overlay: Res<DevOverlay>,
    state: Res<State<AppState>>,
    game_menu: Res<crate::gamemenu::GameMenu>,
) -> Result {
    let ctx = contexts.ctx_mut()?;
    if !overlay.open() && matches!(state.get(), AppState::Title | AppState::Paused) && matches!(game_menu.on_screen(), Some(crate::gamemenu::Page::Main | crate::gamemenu::Page::Pause)) {
        // the way in for the mouse / trackpad, under the game's own menus
        let key = if crate::input::pad_active() { "View" } else { "F1" };
        egui::Area::new(egui::Id::new("dev_menu_button")).anchor(egui::Align2::LEFT_BOTTOM, [12.0, -12.0]).show(ctx, |ui| {
            if ui.button(format!("Developer menu ({key})")).clicked() {
                menu.open_request = true;
            }
        });
    }
    if !overlay.menu {
        return Ok(());
    }
    let title = match menu.page {
        DevPage::Main => "Developer",
        DevPage::Levels => "Developer: level select",
        DevPage::Weapons => "Developer: weapons",
    };
    let max_h = ctx.content_rect().height() * 0.8;
    let mut clicked = None;
    let scroll = std::mem::take(&mut menu.scroll);
    egui::Window::new(title).anchor(egui::Align2::CENTER_CENTER, [0.0, 0.0]).collapsible(false).resizable(false).show(ctx, |ui| {
        ui.set_min_width(440.0);
        ui.label(egui::RichText::new(hints()).small());
        ui.separator();
        egui::ScrollArea::vertical().max_height(max_h).show(ui, |ui| {
            for (i, it) in menu.items.iter().enumerate() {
                let text = match it.on {
                    Some(on) => format!("[{}]  {}", if on { "on " } else { "off" }, it.label),
                    None => it.label.clone(),
                };
                let sel = i == menu.selected;
                let r = ui.add_enabled(it.enabled, egui::Button::selectable(sel, egui::RichText::new(text).monospace()));
                if sel && scroll {
                    r.scroll_to_me(Some(egui::Align::Center));
                }
                if r.clicked() {
                    clicked = Some(i);
                }
            }
        });
    });
    if clicked.is_some() {
        menu.clicked = clicked;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The main page lists level select, every toggle with its state, every action (the level ones disabled at the
    /// title), weapons and close.
    #[test]
    fn main_page() {
        let mut opts = GameOptions::default();
        opts.dev.god_mode = true;
        let levels = crate::campaign::Levels::new(std::path::Path::new("no/such/lev_1_m1"), std::path::Path::new("no/such/lev_1_m1"));
        let c = Context { opts: &opts, levels: &levels, texts: None, in_level: false, quicksave: false };
        let v = items(DevPage::Main, &c);
        assert_eq!(v.len(), 1 + DevToggle::ALL.len() + DevAction::ALL.len() + 2);
        assert_eq!(v[1].on, Some(true));
        assert_eq!(v[2].on, Some(false));
        let quick = v.iter().find(|i| i.cmd == ItemCmd::Dev(DevCommand::Action(DevAction::QuickSave))).unwrap();
        assert!(!quick.enabled && quick.label.ends_with("(F5)"));
        // no exported levels here: the level page has only Back
        assert_eq!(items(DevPage::Levels, &c).len(), 1);
    }
}
