//! The game's menus from the UI bank (docs/subsystems/menus.md, hud.md 6): pages, rows and their positions from
//! layout.json, labels from the text bank (ui/texts.json), checked against captures of the original
//! (work/shots/orig_ui_*.png, tools/shot.py):
//! - main menu (pui_mainmenu) over the title art; rows at the captured positions (the layout's are 77 units lower);
//! - options (pui_options), graphics (pui_gfxopt), audio (pui_sfxopt), controls (pui_showcontrols): sliders as
//!   label, segmented bar, value; checkboxes as a box before the label, as captured;
//! - extras (pui_extrasmenu), the yes/no box (pui_yesno), the pause menu (pui_pausemenu: the HUD and the objectives
//!   band stay on screen), mission failed (pui_missionfailed).
//!
//! The selected row is white and pulses (size = (|sin(phase)| * 0.15 + 1) * scale, phase += dt * 5); disabled rows
//! are #404040 and skipped. Sounds: ui_move, ui_select, ui_cancel. Keys: Up/Down (W/S), Left/Right (A/D) for
//! sliders and lists, Enter or Space to choose, Esc to go back.
//!
//! Save Game / Quick Load / Restart from last save / Resume Game: savegame.rs. Cheats: cheats.rs. Credits: credits.txt
//! scrolling (this file). Shown disabled: Play Cutscene and Replay Mission (disabled in the original too). Graphics options that have no meaning in the new renderer show their best value, disabled.
//! Restart the level, Return to title screen and the rows that load the saved game start the level again in this
//! process (levelreset.rs), as the original does.

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::hud::{Anchor, HUD_LAYER, HudElement, HudText, TextAlign, TextSize};
use crate::levelreset::RestartTo;
use crate::options::GameOptions;
use bevy::camera::visibility::RenderLayers;
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};
use std::collections::HashMap;

#[derive(Clone, Copy, PartialEq, Eq, Hash, Debug)]
pub enum Page {
    Main,
    Options,
    Extras,
    Gfx,
    Sfx,
    Controls,
    YesNo,
    Pause,
    Failed,
    /// the pub screen (pubscreen.rs draws and drives it)
    Pub,
    /// cheat entry (cheats.rs)
    Cheats,
    /// the credits (credits.txt) scrolling up
    Credits,
    /// the end-of-mission statistics (statscreen.rs draws and drives it)
    Stats,
}

/// The art behind a page.
#[derive(Clone, Copy, PartialEq, Debug)]
enum Back {
    /// and_title over a black backdrop
    Title,
    /// b_background_01 over a black backdrop
    Shell,
    /// the game (and its HUD)
    Game,
}

impl Page {
    fn back(self) -> Back {
        match self {
            Page::Main | Page::Options | Page::Extras => Back::Title,
            Page::Gfx | Page::Sfx | Page::Controls | Page::Cheats | Page::Credits => Back::Shell,
            Page::YesNo | Page::Pause | Page::Failed | Page::Pub | Page::Stats => Back::Game,
        }
    }
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Ask {
    ExitGame,
    Restart,
    QuitMission,
    ResetExtras,
    ResetControls,
}

impl Ask {
    fn text_id(self) -> &'static str {
        match self {
            Ask::ExitGame => "ui_yesno_exitgame",
            Ask::Restart => "ui_yesno_restart",
            Ask::QuitMission => "ui_yesno_quitgame",
            Ask::ResetExtras => "ui_yesno_reset",
            Ask::ResetControls => "ui_yesno_resetcontrols",
        }
    }
}

/// A slider / list value.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Opt {
    Brightness,
    Resolution,
    DrawDistance,
    /// remaster: the presentation preset (graphics.rs) and the anti-aliasing, steppers over named values
    Preset,
    AntiAliasing,
    Effects,
    Music,
    Voice,
    MouseSens,
}

/// A checkbox.
#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Flag {
    Shadows,
    SubsGame,
    /// the movie subtitle option (Game+0x176: movie.rs shows the lines that are not always shown)
    SubsMovie,
    InvertMouse,
    Fullscreen,
    /// remaster: a boolean presentation setting by its name in graphics.rs (options.json `graphics`)
    Graphics(&'static str),
}

/// The options the graphics page edits on its copy (Apply keeps them, Cancel drops them).
fn on_gfx_copy(o: Opt) -> bool {
    matches!(o, Opt::Brightness | Opt::Resolution | Opt::DrawDistance | Opt::Preset | Opt::AntiAliasing)
}

fn flag_on_gfx_copy(f: Flag) -> bool {
    matches!(f, Flag::Shadows | Flag::Fullscreen | Flag::Graphics(_))
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Act {
    /// a heading or a disabled row
    None,
    NewGame,
    Continue,
    Go(Page),
    Back,
    Ask(Ask),
    Yes,
    No,
    Restart,
    ToTitle,
    Apply,
    Cancel,
    /// save the game (pause menu) / continue the saved game (Quick Load, Resume Game, Restart from last save)
    Save,
    LoadSave,
    Slide(Opt),
    Toggle(Flag),
}

/// One menu row: a text (HudText) at its layout position.
#[derive(Component)]
pub struct MenuRow {
    pub page: Page,
    pub act: Act,
    /// selectable rows in up/down order; headings and fixed rows have none
    pub order: Option<usize>,
    pub base_scale: f32,
    pub colour: Color,
    pub disabled: bool,
    /// an x range (640 units) the hit box covers besides the text: a stepper's gauge and value, a checkbox's box
    pub extra: Option<(f32, f32)>,
}

/// Every entity the menus own (the HUD's visibility rules leave them alone).
#[derive(Component)]
pub struct MenuOwned;

/// A part drawn for a row: a slider's value text and bar, a checkbox, a page's art.
#[derive(Component)]
struct Part {
    page: Option<Page>,
    kind: PartKind,
}

#[derive(Clone, Copy, PartialEq)]
enum PartKind {
    Value(Opt),
    Fixed,
    Bar(Opt),
    Check(Flag),
    Art(Back),
    CheatTyped,
    /// a cheat of the table 0x00589090 in the cheat page's grid (0x00410be0)
    CheatName(usize),
    Credits,
    /// an action's binding on the device in use (input::key)
    Binding(crate::input::Act),
}

/// A row's highlight size fade (UiText +0x4e0: 0..1).
#[derive(Component, Default)]
pub struct Glow(f32);

/// The screen transition's black quad (its material's alpha: `animate`).
#[derive(Component)]
struct FadeQuad(Handle<ColorMaterial>);

/// The fill of a bar (scaled in x by the value).
#[derive(Component)]
struct BarFill(Opt);

/// The inside of a checkbox (red when set).
#[derive(Component)]
struct CheckFill(Flag);

#[derive(Resource)]
pub struct GameMenu {
    pub page: Page,
    pub selected: usize,
    stack: Vec<(Page, usize)>,
    ask: Option<Ask>,
    phase: f32,
    /// the graphics page edits a copy; Apply keeps it, Cancel drops it
    edit: Option<GameOptions>,
    failed_shown: bool,
    /// the objectives band was put up by the pause menu
    band: bool,
    /// the screen transition's time (0 none, 0..1 fading out the shown page, 1..2 fading in the new one: `animate`)
    pub fade: f32,
    /// the page on screen (it lags `page` through a fade), its selection, and the page a shown yes/no box asked over
    shown: Option<Page>,
    shown_sel: usize,
    shown_under: Option<Page>,
    /// a saved game exists (the rows that load it are disabled without one)
    pub has_save: bool,
}

impl Default for GameMenu {
    fn default() -> Self {
        GameMenu { page: Page::Main, selected: 0, stack: Vec::new(), ask: None, phase: 0.0, edit: None, failed_shown: false, band: false, fade: 0.0, shown: None, shown_sel: 0, shown_under: None, has_save: false }
    }
}

impl GameMenu {
    /// The HUD stays on screen under this page (the pause menu, its yes/no box, mission failed).
    pub fn hud_under(&self) -> bool {
        matches!(self.page, Page::Pause | Page::Failed) || (self.page == Page::YesNo && self.stack.last().is_some_and(|s| s.0.back() == Back::Game))
    }
}

/// The display strings (ui/texts.json). A missing id shows itself (as 0x004187e0 copies the key when Text_Lookup
/// 0x00495f00 finds nothing; Text_Lookup itself would show "Text lookup failure: %s").
#[derive(Resource, Default)]
pub struct Texts(pub HashMap<String, String>);

impl Texts {
    pub fn get(&self, id: &str) -> String {
        self.0.get(id).cloned().unwrap_or_else(|| id.to_owned())
    }
}

pub struct GameMenuPlugin;

impl Plugin for GameMenuPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<GameMenu>().init_resource::<Texts>().add_systems(Update, (failed, navigate.run_if(crate::movie::no_movie_input), animate, values, pause_band, credits, log_menu).chain());
    }
}

/// The state log's "menu": the page and the selected row's text id (playtest expect steps: "menu.selected").
#[allow(clippy::type_complexity)]
fn log_menu(
    menu: Res<GameMenu>,
    rows: Query<(&MenuRow, &crate::hud::HudElement)>,
    state: Option<ResMut<crate::playtest::PlayState>>,
    (opts, windows, parts): (Res<GameOptions>, Query<&Window>, Query<(&Part, &Visibility)>),
) {
    let Some(mut state) = state else { return };
    let prefix = format!("menu_{:?}_", menu.page);
    let sel_order = rows_of(rows.iter().map(|x| x.0), menu.page, menu.has_save).get(menu.selected).map(|x| x.0);
    let row = rows.iter().find(|(r, _)| r.page == menu.page && r.order.is_some() && r.order == sel_order);
    let sel = row.map(|(_, h)| h.name.trim_start_matches(&prefix).to_owned());
    // the selected stepper's value text (the graphics page's edit copy while it is open)
    let win = windows.single().map(|w| (w.resolution.width() as u32, w.resolution.height() as u32)).unwrap_or((1920, 1080));
    let value = row.and_then(|(r, _)| match r.act {
        Act::Slide(o) => Some(value(if on_gfx_copy(o) { menu.edit.as_ref().unwrap_or(&opts) } else { &opts }, o, win).1),
        _ => None,
    });
    let cheats_shown = parts.iter().filter(|(p, v)| matches!(p.kind, PartKind::CheatName(_)) && **v != Visibility::Hidden).count();
    state.0.insert(
        "menu".into(),
        serde_json::json!({ "page": format!("{:?}", menu.page), "selected": sel, "fading": menu.fade > 0.0, "shown": menu.shown.map(|p| format!("{p:?}")), "value": value, "cheats_shown": cheats_shown }),
    );
}

const ORANGE: Color = Color::srgb(0xd0 as f32 / 255.0, 0x50 as f32 / 255.0, 0x30 as f32 / 255.0);
const GREEN: Color = Color::srgb(0xa0 as f32 / 255.0, 1.0, 0xa0 as f32 / 255.0);
const YELLOW: Color = Color::srgb(0xd0 as f32 / 255.0, 0xd0 as f32 / 255.0, 0x40 as f32 / 255.0);
const DISABLED: Color = Color::srgb(0x40 as f32 / 255.0, 0x40 as f32 / 255.0, 0x40 as f32 / 255.0);

struct Spawner<'a, 'w, 's> {
    commands: &'a mut Commands<'w, 's>,
    meshes: &'a mut Assets<Mesh>,
    materials: &'a mut Assets<ColorMaterial>,
    texts: &'a Texts,
    orders: HashMap<Page, usize>,
    /// the next row's fit width (0: none) and extra hit range (`slider`, `check`)
    next: (f32, Option<(f32, f32)>),
}

impl Spawner<'_, '_, '_> {
    #[allow(clippy::too_many_arguments)]
    fn row(&mut self, page: Page, id: &str, pos: Vec2, scale: f32, colour: Color, act: Act, align: f32) {
        let (fit, extra) = std::mem::take(&mut self.next);
        let disabled = act == Act::None && colour == DISABLED;
        let order = if act != Act::None {
            let o = self.orders.entry(page).or_insert(0);
            *o += 1;
            Some(*o - 1)
        } else {
            None
        };
        let text = self.texts.get(id);
        let mut e = self.commands.spawn((
            HudElement { name: format!("menu_{page:?}_{id}"), pos, anchor: Anchor::Centre, full: true },
            HudText { text, built: None, scale, colour, fit },
            MenuRow { page, act, order, base_scale: scale, colour, disabled, extra },
            Glow::default(),
            MenuOwned,
            Transform::default(),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
        ));
        if align != 0.0 {
            e.insert(TextAlign(align));
        }
    }

    /// A text that is not a row (a slider's value, a key name).
    fn text(&mut self, page: Page, s: &str, pos: Vec2, scale: f32, colour: Color, align: f32, kind: PartKind) {
        self.commands.spawn((
            HudElement { name: format!("menu_{page:?}_text"), pos, anchor: Anchor::Centre, full: true },
            HudText { text: s.to_owned(), built: None, scale, colour, fit: 0.0 },
            TextAlign(align),
            Part { page: Some(page), kind },
            MenuOwned,
            Transform::default(),
            Visibility::Hidden,
            RenderLayers::layer(HUD_LAYER),
        ));
    }

    fn rect(&mut self, size: Vec2, colour: Color, z: f32) -> Entity {
        let mat = self.materials.add(ColorMaterial { color: colour, ..default() });
        self.commands.spawn((Mesh2d(self.meshes.add(Rectangle::new(size.x, size.y))), MeshMaterial2d(mat), Transform::from_xyz(0.0, 0.0, z), RenderLayers::layer(HUD_LAYER))).id()
    }

    /// A stepper's gauge (0x0046d130, relative to the item's point x): the frame from +77 to +275, 18 high
    /// (colour +0x578, 0xff204000: Stepper_Init 0x0046cdb0), the fill from +80, up to 192 wide, 12 high (+0x57c,
    /// 0xff40c040), and with +0x5ac (every menu stepper) a tick above and below the fill at each of the positions.
    /// Disabled (the remaster's fixed options) in greys.
    fn gauge(&mut self, page: Page, item: Vec2, positions: usize, frac: f32, opt: Option<Opt>, enabled: bool) {
        let (frame_c, fill_c) = if enabled { (Color::srgb_u8(0x20, 0x40, 0x00), Color::srgb_u8(0x40, 0xc0, 0x40)) } else { (Color::srgb(0.12, 0.14, 0.12), Color::srgb(0.25, 0.3, 0.25)) };
        // the gauge's centre: (77 + 275) / 2 = 176 right of the item
        let frame = self.rect(Vec2::new(198.0, 18.0), frame_c, 14.0);
        let fill_mat = self.materials.add(ColorMaterial { color: fill_c, ..default() });
        let fill = self.commands.spawn((Mesh2d(self.meshes.add(Rectangle::new(1.0, 12.0))), MeshMaterial2d(fill_mat), Transform::from_xyz(80.0 - 176.0, 0.0, 16.0), RenderLayers::layer(HUD_LAYER))).id();
        match opt {
            Some(o) => {
                self.commands.entity(fill).insert(BarFill(o));
            }
            None => {
                self.commands.entity(fill).insert(Transform { translation: Vec3::new(80.0 - 176.0 + 96.0 * frac, 0.0, 16.0), scale: Vec3::new((192.0 * frac).max(0.01), 1.0, 1.0), ..default() });
            }
        }
        let mut kids = vec![frame, fill];
        // ticks: x = i * (1 / steps * 192) + 80 (0x005bcde0, 0x005bcddc), from y 6 to 9 and -6 to -9
        let steps = positions.max(2) - 1;
        for i in 0..=steps {
            let x = 80.0 + 192.0 * i as f32 / steps as f32 - 176.0;
            for y in [7.5, -7.5] {
                let t = self.rect(Vec2::new(1.0, 3.0), fill_c, 17.0);
                self.commands.entity(t).insert(Transform::from_xyz(x, y, 17.0));
                kids.push(t);
            }
        }
        let bar = self
            .commands
            .spawn((
                HudElement { name: format!("menu_{page:?}_bar"), pos: Vec2::new(item.x + 176.0, item.y), anchor: Anchor::Centre, full: true },
                Part { page: Some(page), kind: opt.map(PartKind::Bar).unwrap_or(PartKind::Fixed) },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(bar).add_children(&kids);
    }

    /// A stepper (ui_progressbar, Stepper_Setup 0x0046cce0): its label centred on the item's point and fitted to 144
    /// units (Stepper_Layout 0x0046d050), the gauge (0x0046d130), the value text from +288 (left-aligned).
    fn slider(&mut self, page: Page, label: &str, item: Vec2, scale: f32, opt: Opt, enabled: bool) {
        let colour = if enabled { GREEN } else { DISABLED };
        // the hit box covers the gauge and the value too (Stepper_Layout adds the value's bounds; a click on the
        // track sets the value: Stepper_HandleInput 0x0046d290)
        self.next = (144.0, Some((item.x + 77.0, item.x + 340.0)));
        self.row(page, label, item, scale, colour, if enabled { Act::Slide(opt) } else { Act::None }, 0.0);
        self.gauge(page, item, positions(opt), 0.0, Some(opt), enabled);
        self.text(page, "", Vec2::new(item.x + 288.0, item.y), scale, colour, -1.0, PartKind::Value(opt));
    }

    /// A stepper that cannot be changed (no meaning in the new renderer): its best value shown greyed.
    fn fixed(&mut self, page: Page, label: &str, item: Vec2, value: &str, positions: usize, frac: f32) {
        self.next = (144.0, None);
        self.row(page, label, item, 1.0, DISABLED, Act::None, 0.0);
        self.gauge(page, item, positions, frac, None, false);
        self.text(page, value, Vec2::new(item.x + 288.0, item.y), 1.0, DISABLED, -1.0, PartKind::Fixed);
    }

    /// A checkbox (CheckBox_Layout 0x0046cc80: its label centred on the item's point, fitted to 192 units; the box
    /// from -128 to -112, -8..8 around it: CheckBox_Render 0x0046cc40), dark red when set.
    fn check(&mut self, page: Page, label: &str, box_x: f32, label_x: f32, y: f32, flag: Option<Flag>, fixed_on: bool) {
        let enabled = flag.is_some();
        self.next = (192.0, Some((box_x - 8.0, box_x + 8.0)));
        self.row(page, label, Vec2::new(label_x, y), 1.0, if enabled { GREEN } else { DISABLED }, flag.map(Act::Toggle).unwrap_or(Act::None), 0.0);
        let outline = self.rect(Vec2::new(16.0, 16.0), Color::srgb(0.55, 0.55, 0.55), 14.0);
        let inside = self.rect(Vec2::new(13.0, 13.0), Color::srgb(0.05, 0.08, 0.1), 15.0);
        let on = if enabled { Color::srgb(0.55, 0.05, 0.05) } else if fixed_on { Color::srgb(0.3, 0.12, 0.12) } else { Color::srgb(0.05, 0.08, 0.1) };
        let fill = self.rect(Vec2::new(13.0, 13.0), on, 16.0);
        if let Some(f) = flag {
            self.commands.entity(fill).insert(CheckFill(f));
        }
        let b = self
            .commands
            .spawn((
                HudElement { name: format!("menu_{page:?}_check"), pos: Vec2::new(box_x, y), anchor: Anchor::Centre, full: true },
                Part { page: Some(page), kind: flag.map(PartKind::Check).unwrap_or(PartKind::Fixed) },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(b).add_children(&[outline, inside, fill]);
    }

    /// A page's art: the texture over the 640x480 screen and a black backdrop to the window's edges.
    fn art(&mut self, back: Back, tex: Handle<Image>) {
        let backdrop = self.rect(Vec2::new(4000.0, 480.0), Color::BLACK, 1.0);
        let mat = self.materials.add(ColorMaterial { texture: Some(tex), ..default() });
        let art = self.commands.spawn((Mesh2d(self.meshes.add(Rectangle::new(640.0, 480.0))), MeshMaterial2d(mat), Transform::from_xyz(0.0, 0.0, 2.0), RenderLayers::layer(HUD_LAYER))).id();
        let root = self
            .commands
            .spawn((
                HudElement { name: format!("menu_art_{back:?}"), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
                Part { page: None, kind: PartKind::Art(back) },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(root).add_children(&[backdrop, art]);
    }
}

/// Spawns every page (hidden) and the page art; loads the text bank.
pub fn spawn(commands: &mut Commands, assets: &AssetServer, meshes: &mut Assets<Mesh>, materials: &mut Assets<ColorMaterial>, scene: &std::path::Path) {
    let texts = Texts(std::fs::read_to_string(scene.join("ui").join("texts.json")).ok().and_then(|s| serde_json::from_str(&s).ok()).unwrap_or_default());
    {
        let mut s = Spawner { commands, meshes, materials, texts: &texts, orders: HashMap::new(), next: (0.0, None) };
        // the steppers' and checkboxes' item points (layout.json: ref_text_05 x 171.215, ref_text_04 x 465.773)
        let it = |y: f32| Vec2::new(171.215, y);
        s.art(Back::Title, assets.load("ui/textures/and_title.png"));
        s.art(Back::Shell, assets.load("ui/textures/b_background_01.png"));
        // the screen transition's black quad over everything (0x00491670 -> the full-screen fade 0x0047cff0, colour 0)
        let mat = s.materials.add(ColorMaterial { color: Color::srgba(0.0, 0.0, 0.0, 0.0), alpha_mode: bevy::sprite_render::AlphaMode2d::Blend, ..default() });
        let quad = s.commands.spawn((Mesh2d(s.meshes.add(Rectangle::new(4000.0, 480.0))), MeshMaterial2d(mat.clone()), Transform::default(), RenderLayers::layer(HUD_LAYER))).id();
        s.commands
            .spawn((
                HudElement { name: "menu_fade".into(), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre, full: true },
                FadeQuad(mat),
                MenuOwned,
                Transform::from_xyz(0.0, 0.0, 90.0),
                Visibility::Inherited,
                RenderLayers::layer(HUD_LAYER),
            ))
            .add_child(quad);

        // main menu: rows 48 apart from y 372.3 (captured, orig_ui_menu-800.png), scale 1.3, #d05030
        let main = [
            ("ui_mainmenu_startgame", Act::NewGame),
            ("ui_mainmenu_loadgame", Act::LoadSave),
            ("ui_mainmenu_options", Act::Go(Page::Options)),
            ("ui_mainmenu_extras", Act::Go(Page::Extras)),
            ("ui_mainmenu_exitgame", Act::Ask(Ask::ExitGame)),
        ];
        for (i, (id, act)) in main.into_iter().enumerate() {
            let c = if act == Act::None { DISABLED } else { ORANGE };
            s.row(Page::Main, id, Vec2::new(320.0, 372.3 - 48.0 * i as f32), 1.3, c, act, 0.0);
        }
        // the version text of a screen with flag 0x800 (the main menu, 0x8a0): 0x004927f0 draws "v%d.%02d" of 1, 0 in
        // 0xffc0c0c0 at scale 0.8, its right edge at x 0.9 and y 0.1 of the screen (orig_ui_menu-800.png: "v1.00")
        s.text(Page::Main, "v1.00", Vec2::new(576.0, 38.0), 0.8, Color::srgb_u8(0xc0, 0xc0, 0xc0), 1.0, PartKind::Fixed);

        // options (pui_options, layout positions; matches orig_ui_opts-950.png)
        s.row(Page::Options, "ui_options_title", Vec2::new(320.0, 427.761), 1.4, Color::WHITE, Act::None, 0.0);
        let opts = [
            ("ui_optionsmenu_gfx", 374.659, Act::Go(Page::Gfx)),
            ("ui_optionsmenu_sfx", 326.659, Act::Go(Page::Sfx)),
            ("ui_mainmenu_controls", 278.659, Act::Go(Page::Controls)),
            ("ui_optionsmenu_reset", 230.659, Act::Ask(Ask::ResetExtras)),
            ("ui_optionsmenu_cheats", 182.659, Act::Go(Page::Cheats)),
        ];
        for (id, y, act) in opts {
            s.row(Page::Options, id, Vec2::new(320.0, y), 1.3, if act == Act::None { DISABLED } else { ORANGE }, act, 0.0);
        }
        s.row(Page::Options, "ui_shellmenu_back", Vec2::new(320.0, 60.44), 1.4, YELLOW, Act::Back, 0.0);

        // extras (pui_extrasmenu; the layout has no point for its rows: positions from orig_ui_extras-950.png)
        s.row(Page::Extras, "ui_extrasmenu_title", Vec2::new(320.0, 428.0), 1.4, Color::WHITE, Act::None, 0.0);
        for (id, y) in [("ui_extrasmenu_cutscene", 330.0), ("ui_extrasmenu_replay", 282.0)] {
            s.row(Page::Extras, id, Vec2::new(320.0, y), 1.3, DISABLED, Act::None, 0.0);
        }
        // Credits: locked on a fresh profile in the original (cost 99 in the token table: unlocked by other means);
        // the remaster opens it
        s.row(Page::Extras, "ui_extrasmenu_credits", Vec2::new(320.0, 235.0), 1.3, ORANGE, Act::Go(Page::Credits), 0.0);
        s.row(Page::Extras, "ui_shellmenu_back", Vec2::new(320.0, 84.44), 1.4, YELLOW, Act::Back, 0.0);

        // graphics (pui_gfxopt; orig_ui_gfx-1140.png): labels right-aligned at x 240, bar 250..447, value at 460
        s.row(Page::Gfx, "ui_gfxmenu_title", Vec2::new(320.0, 420.996), 1.4, GREEN, Act::None, 0.0);
        s.slider(Page::Gfx, "ui_gfxmenu_brightness", it(366.691), 1.0, Opt::Brightness, true);
        s.slider(Page::Gfx, "ui_gfxmenu_resolution", it(342.691), 1.0, Opt::Resolution, true);
        let best = |t: &Texts, id: &str| t.get(id);
        let tq = best(&texts, "ui_gfxmenu_tq2");
        s.fixed(Page::Gfx, "ui_gfxmenu_texturequality", it(318.691), &tq, 3, 1.0);
        // remaster: the presentation preset (graphics.rs) in place of Texture LOD, and the anti-aliasing in place of
        // Model LOD (both always the finest in the new renderer), as Fullscreen took Max Textures' place
        s.slider(Page::Gfx, "Graphics", it(294.691), 1.0, Opt::Preset, true);
        s.check(Page::Gfx, "Fullscreen", 250.0, 190.0, 270.691, Some(Flag::Fullscreen), true);
        s.slider(Page::Gfx, "Anti-aliasing", it(246.691), 1.0, Opt::AntiAliasing, true);
        s.slider(Page::Gfx, "ui_gfxmenu_drawdistance", it(222.691), 1.0, Opt::DrawDistance, true);
        s.check(Page::Gfx, "ui_gfxmenu_bumpmap", 51.0, 171.215, 174.691, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_pixelshaders", 51.0, 171.215, 150.691, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_blur", 51.0, 171.215, 126.691, None, false);
        s.check(Page::Gfx, "ui_gfxmenu_skinquality", 346.0, 465.773, 172.0, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_shadows", 346.0, 465.773, 148.0, Some(Flag::Shadows), true);
        s.check(Page::Gfx, "ui_gfxmenu_decals", 346.0, 465.773, 124.0, None, false);
        // remaster: the preset's main switches, a row of additions under the original's checkboxes
        s.check(Page::Gfx, "Remaster Textures", 51.0, 171.215, 100.691, Some(Flag::Graphics("remaster_textures")), false);
        s.check(Page::Gfx, "Ambient Occlusion", 346.0, 465.773, 100.0, Some(Flag::Graphics("ambient_occlusion")), false);
        s.check(Page::Gfx, "Soft Particles", 51.0, 171.215, 80.691, Some(Flag::Graphics("soft_particles")), false);
        s.check(Page::Gfx, "Soft Shadows", 346.0, 465.773, 80.0, Some(Flag::Graphics("soft_shadows")), false);
        s.row(Page::Gfx, "ui_gfxmenu_apply", Vec2::new(320.0, 60.44), 1.2, GREEN, Act::Apply, 0.0);
        s.row(Page::Gfx, "ui_shellmenu_cancel", Vec2::new(320.0, 36.44), 1.1, GREEN, Act::Cancel, 0.0);

        // audio (pui_sfxopt; orig_ui_sfx-1120.png)
        s.row(Page::Sfx, "ui_sfxmenu_title", Vec2::new(320.0, 420.996), 1.4, Color::WHITE, Act::None, 0.0);
        s.slider(Page::Sfx, "ui_sfxmenu_soundvolume", it(342.691), 1.0, Opt::Effects, true);
        s.slider(Page::Sfx, "ui_sfxmenu_musicvolume", it(294.691), 1.0, Opt::Music, true);
        s.slider(Page::Sfx, "ui_sfxmenu_voicevolume", it(246.691), 1.0, Opt::Voice, true);
        s.check(Page::Sfx, "ui_sfxmenu_subtitles_movie", 200.0, 320.0, 132.996, Some(Flag::SubsMovie), true);
        s.check(Page::Sfx, "ui_sfxmenu_subtitles_ingame", 200.0, 320.0, 108.996, Some(Flag::SubsGame), true);
        s.row(Page::Sfx, "ui_shellmenu_back", Vec2::new(320.0, 60.44), 1.4, YELLOW, Act::Back, 0.0);

        // controls (pui_showcontrols; orig_ui_controls-1100.png): the remaster's bindings, two columns of
        // "action: key" (labels right-aligned at x 172 / 466, keys from x 187 / 482), rows 24 apart
        s.row(Page::Controls, "ui_mainmenu_controls", Vec2::new(320.0, 420.996), 1.4, GREEN, Act::None, 0.0);
        // the bindings of the device in use (keyboard and mouse, or the pad last pressed: input.rs), as icons
        use crate::input::Act as I;
        let left = [
            ("Move", I::Move),
            ("Look", I::Look),
            ("Fire Weapon", I::Fire),
            ("Aim / Zoom", I::Aim),
            ("Fire Special", I::Special),
            ("Next Special", I::NextSpecial),
            ("Next Weapon", I::SwitchWeapon),
        ];
        let right = [("Jump", I::Jump), ("Reload", I::Reload), ("Activate", I::Use), ("Send Team", I::TeamSend), ("Recall Team", I::TeamRecall), ("Continue", I::Continue), ("Pause", I::Pause)];
        for (i, (a, k)) in left.iter().enumerate() {
            let y = 390.691 - 30.0 * i as f32;
            s.text(Page::Controls, a, Vec2::new(172.0, y), 0.8, GREEN, 1.0, PartKind::Fixed);
            s.text(Page::Controls, "", Vec2::new(187.0, y), 0.8, GREEN, -1.0, PartKind::Binding(*k));
        }
        for (i, (a, k)) in right.iter().enumerate() {
            let y = 390.691 - 30.0 * i as f32;
            s.text(Page::Controls, a, Vec2::new(466.0, y), 0.8, GREEN, 1.0, PartKind::Fixed);
            s.text(Page::Controls, "", Vec2::new(482.0, y), 0.8, GREEN, -1.0, PartKind::Binding(*k));
        }
        // scale 0.8 (layout.json: the controls page's ui_progressbar)
        s.slider(Page::Controls, "ui_controls_mousescale", it(150.691), 0.8, Opt::MouseSens, true);
        // no auto-aim in the remaster yet
        s.check(Page::Controls, "ui_controls_autoaim", 200.0, 320.0, 108.44, None, false);
        s.check(Page::Controls, "ui_controls_flipmousey", 200.0, 320.0, 84.44, Some(Flag::InvertMouse), false);
        s.row(Page::Controls, "ui_controls_reset", Vec2::new(320.0, 60.44), 1.0, GREEN, Act::Ask(Ask::ResetControls), 0.0);
        s.row(Page::Controls, "ui_shellmenu_back", Vec2::new(320.0, 36.44), 1.4, GREEN, Act::Back, 0.0);

        // cheats (pui_cheats): title, instruction, the typed code (ui_cheatbox), the result (ui_cheatdesc rows),
        // Back
        s.row(Page::Cheats, "ui_cheatmenu_title", Vec2::new(320.0, 411.564), 1.4, Color::WHITE, Act::None, 0.0);
        s.row(Page::Cheats, "ui_cheatmenu_instruct", Vec2::new(320.0, 387.564), 1.0, Color::WHITE, Act::None, 0.0);
        // the code box (ui_cheatbox, row 5 of ref_cheat_menu): what is typed, then the result (Cheats_Check
        // 0x004107c0 sets the box's own text)
        s.text(Page::Cheats, "", Vec2::new(320.0, 339.564), 1.0, GREEN, 0.0, PartKind::CheatTyped);
        // the cheats known (ui_cheatdesc, class init 0x00410ad0, code 64, rows 0..11 = the table 0x00589090's entries: 0x00410be0): placed
        // on ref_info (320, 276.44) by 0x00410c20 in three columns, x + (n % 3 - 1) x 0.25 of the screen, y - (n / 3
        // x 40 + 24) units; shown (0x00410b20) when known (Game+0x48c) or active (Game+0x4c8) or List Cheats (0x80) is
        // on, greyed (+0x474 0x40) unless active
        for n in 0..crate::cheats::CHEATS.len() {
            let pos = Vec2::new(320.0 + ((n % 3) as f32 - 1.0) * 160.0, 276.44 - (n / 3) as f32 * 40.0 - 24.0);
            let name = s.texts.get(crate::cheats::CHEATS[n].0);
            s.text(Page::Cheats, &name, pos, 1.0, DISABLED, 0.0, PartKind::CheatName(n));
        }
        s.row(Page::Cheats, "ui_shellmenu_back", Vec2::new(320.0, 60.44), 1.4, GREEN, Act::Back, 0.0);

        // credits: the whole text as one block, moved up by the credits system (a lone row so Esc works)
        let credits = std::fs::read_to_string(scene.join("ui").join("credits.txt")).unwrap_or_default().replace("\r", "").replace('\n', "<br>");
        s.text(Page::Credits, &credits, Vec2::new(320.0, -10.0), 0.9, Color::WHITE, 0.0, PartKind::Credits);

        // yes/no (pui_yesno, hud.md 6): message 1.4 white at y 276.4, Yes 1.1 #a0ffa0, No 1.1 #ff6060
        s.row(Page::YesNo, "ui_yesno_defaulttext", Vec2::new(320.0, 276.4), 1.4, Color::WHITE, Act::None, 0.0);
        s.row(Page::YesNo, "ui_yesno_yes", Vec2::new(320.0, 156.44), 1.1, GREEN, Act::Yes, 0.0);
        s.row(Page::YesNo, "ui_yesno_no", Vec2::new(320.0, 132.44), 1.1, Color::srgb(1.0, 0x60 as f32 / 255.0, 0x60 as f32 / 255.0), Act::No, 0.0);

        // pause (pui_pausemenu; orig_ui_play-720.png): Save Game and Quick Load are greyed there too
        let pause = [
            ("ui_pausemenu_resumegame", 180.44, Act::Continue),
            ("ui_pausemenu_savegame", 156.44, Act::Save),
            ("ui_pausemenu_quickload", 132.44, Act::LoadSave),
            ("ui_mfail_restart", 108.44, Act::Ask(Ask::Restart)),
            ("ui_mainmenu_options", 84.44, Act::Go(Page::Options)),
            ("ui_pausemenu_quitgame", 60.44, Act::Ask(Ask::QuitMission)),
        ];
        for (id, y, act) in pause {
            s.row(Page::Pause, id, Vec2::new(320.0, y), 1.0, if act == Act::None { DISABLED } else { GREEN }, act, 0.0);
        }

        // mission failed (pui_missionfailed): the title is the mission's failure text (the bank's
        // ui_mfail_title is the placeholder "ui_mfail_title.")
        s.row(Page::Failed, "ui_mfail_title", Vec2::new(320.0, 324.996), 1.0, GREEN, Act::None, 0.0);
        s.row(Page::Failed, "ui_mfail_lastsave", Vec2::new(320.0, 228.44), 1.0, GREEN, Act::LoadSave, 0.0);
        s.row(Page::Failed, "ui_mfail_restart", Vec2::new(320.0, 204.44), 1.0, GREEN, Act::Restart, 0.0);
        s.row(Page::Failed, "ui_mfail_menu", Vec2::new(320.0, 180.44), 1.0, GREEN, Act::ToTitle, 0.0);
    }
    commands.insert_resource(texts);
}

/// The page's selectable rows in up / down order: (order, action). A row that loads the saved game is disabled
/// (+0x474 0x40, skipped by the selection: 0x00491b20) while there is none: Load Game (code 2, 0x004a2c10 slot -1),
/// Quick Load and Restart from last save (code 34, 0x004a2e90).
fn rows_of<'a>(rows: impl Iterator<Item = &'a MenuRow>, page: Page, has_save: bool) -> Vec<(usize, Act)> {
    let mut v: Vec<(usize, Act)> = rows.filter(|r| r.page == page && (has_save || r.act != Act::LoadSave)).filter_map(|r| r.order.map(|o| (o, r.act))).collect();
    v.sort_by_key(|x| x.0);
    v
}

/// A row's hit box in 640 x 480 units (corners): its text's bounds (TextSize, placed by its TextAlign) grown by 32 in
/// x and 4 in y (UiText_SetBounds 0x004655b0: 32 and 4 pixels at the original's 640 x 480), joined with its `extra`
/// x range. None before the text is built.
fn hit_box(r: &MenuRow, e: &HudElement, size: Option<&TextSize>, align: Option<&TextAlign>) -> Option<(Vec2, Vec2)> {
    let size = size?.0;
    let cx = e.pos.x - align.map(|a| a.0).unwrap_or(0.0) * size.x / 2.0;
    let mut lo = Vec2::new(cx - size.x / 2.0 - 32.0, e.pos.y - size.y / 2.0 - 4.0);
    let mut hi = Vec2::new(cx + size.x / 2.0 + 32.0, e.pos.y + size.y / 2.0 + 4.0);
    if let Some((x0, x1)) = r.extra {
        lo.x = lo.x.min(x0);
        hi.x = hi.x.max(x1);
    }
    Some((lo, hi))
}

/// Mission failed: the failed page once the mission is lost (level state 3 after Roman's 5 s death timer).
fn failed(mission: Option<Res<crate::mission::Mission>>, state: Res<State<AppState>>, mut next: ResMut<NextState<AppState>>, mut menu: ResMut<GameMenu>, mut vtime: ResMut<Time<Virtual>>) {
    let Some(m) = mission else { return };
    if m.outcome == crate::mission::Outcome::Failed && !menu.failed_shown && *state.get() == AppState::Playing {
        menu.failed_shown = true;
        menu.page = Page::Failed;
        menu.stack.clear();
        menu.selected = 0;
        next.set(AppState::Paused);
        vtime.pause();
    }
}

/// A menu screen's input (UiScreen_Update 0x004929e0 through the menu input 0x00491140): the mouse (the cursor,
/// 0x00491380 / 0x00463ac0, mapped to the 640 x 480 layout) selects the item under it with the move sound when it
/// moves, a click activates it; up / down move the selection (the items' nav links, 0x00464a10, here the rows'
/// order), left / right step a stepper, select activates (UiScreen_Activate 0x004921a0 by the item's action),
/// back cancels (0x00490b60 with the cancel sound).
#[allow(clippy::too_many_arguments)]
fn navigate(
    ctl: Res<crate::input::Controls>,
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<GameMenu>,
    rows: Query<(&MenuRow, &mut HudText, &mut Visibility), Without<Part>>,
    mut exit: MessageWriter<AppExit>,
    mut vtime: ResMut<Time<Virtual>>,
    mission: Option<Res<crate::mission::Mission>>,
    mut opts: ResMut<GameOptions>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    windows: Query<&Window>,
    mut save_req: ResMut<crate::savegame::SaveRequest>,
    mut cheats: ResMut<crate::cheats::Cheats>,
    mouse: Res<ButtonInput<MouseButton>>,
    mut restart: ResMut<crate::levelreset::RestartLevel>,
    (placed, names, mut last_cursor, time_real, mut save_t): (
        Query<(&MenuRow, &HudElement, Option<&TextSize>, Option<&TextAlign>)>,
        Query<(&Part, &HudElement, Option<&TextSize>, &Visibility)>,
        Local<Option<Vec2>>,
        Res<Time<Real>>,
        Local<f32>,
    ),
) {
    cheats.entering = menu.page == Page::Cheats && *state.get() != AppState::Playing;
    // a screen transition is under way (0x004915b0: no input while a request is pending)
    if menu.fade > 0.0 {
        return;
    }
    // the mouse in the menus (the original: Mouse 1 selects, Mouse 2 cancels): the row under the cursor is selected
    // when the cursor moves, a click activates it. Cursor (window pixels, y down) -> the menus' 640x480 space (y up),
    // scaled by height / 480 about the centre (hud.rs place)
    let mut hovered: Option<usize> = None;
    let mut cursor_moved = false;
    let mut cursor: Option<Vec2> = None;
    if *state.get() != AppState::Playing {
        if let Ok(w) = windows.single() {
            if let Some(c) = w.cursor_position() {
                let (ww, wh) = (w.width(), w.height());
                let s = wh / 480.0;
                let p = Vec2::new(320.0 + (c.x - ww / 2.0) / s, 240.0 - (c.y - wh / 2.0) / s);
                cursor = Some(p);
                cursor_moved = last_cursor.is_some_and(|l| l.distance(c) > 0.5);
                *last_cursor = Some(c);
                // the item under the cursor (0x00463930 tests each item's box +0x490..+0x49c): the text's bounds
                // grown by 32 units in x and 4 in y (UiText_SetBounds 0x004655b0), with a stepper's gauge and value
                // or a checkbox's box (`extra`); of several, the nearest row (hook/enhance's nearest-hit rule)
                hovered = placed
                    .iter()
                    .filter(|(r, ..)| r.page == menu.page && r.order.is_some())
                    .filter(|(r, e, size, align)| hit_box(r, e, *size, *align).is_some_and(|(lo, hi)| p.cmpge(lo).all() && p.cmple(hi).all()))
                    .min_by(|a, b| (p.y - a.1.pos.y).abs().total_cmp(&(p.y - b.1.pos.y).abs()))
                    .and_then(|(r, ..)| r.order);
            }
        }
    }
    let win = windows.single().map(|w| (w.resolution.width() as u32, w.resolution.height() as u32)).unwrap_or((1920, 1080));
    // the cheat page: a click on a cheat's name types its code and checks it (Cheats_Check 0x004107c0, state 1)
    if menu.page == Page::Cheats && mouse.just_pressed(MouseButton::Left) {
        if let Some(p) = cursor {
            for (part, e, size, vis) in &names {
                let PartKind::CheatName(i) = part.kind else { continue };
                let half = size.map(|s| s.0 / 2.0).unwrap_or(Vec2::new(60.0, 12.0)) + Vec2::new(32.0, 4.0);
                if *vis != Visibility::Hidden && (p - e.pos).abs().cmple(half).all() {
                    cheats.typed = crate::cheats::CHEATS[i].1.to_owned();
                    cheats.submit = true;
                    sounds.write(crate::audio::PlaySound::menu("ui_select"));
                }
            }
        }
    }
    // a stepper's track under the mouse sets its value (Stepper_HandleInput 0x0046d290: x 80..272, y -6..6 around
    // the item; held, so the remaster's trackpad can drag it)
    if mouse.pressed(MouseButton::Left) {
        if let Some(p) = cursor {
            for (r, e, ..) in &placed {
                let Act::Slide(o) = r.act else { continue };
                if r.page != menu.page || (p.y - e.pos.y).abs() > 9.0 || p.x < e.pos.x + 77.0 || p.x > e.pos.x + 275.0 {
                    continue;
                }
                let frac = (p.x - (e.pos.x + 80.0)) / 192.0;
                let target = if on_gfx_copy(o) { menu.edit.get_or_insert_with(|| opts.clone()) } else { &mut *opts };
                let before = value(target, o, win).1;
                set_position(target, o, frac, win);
                if value(target, o, win).1 != before {
                    sounds.write(crate::audio::PlaySound::menu("ui_move"));
                }
            }
        }
    }
    // the credits page: any of Esc / Enter / a click (a trackpad tap) leaves
    if menu.page == Page::Credits && *state.get() != AppState::Playing {
        if ctl.pressed(crate::input::Act::Back) || ctl.pressed(crate::input::Act::Select) || mouse.just_pressed(MouseButton::Left) {
            if let Some((p, s)) = menu.stack.pop() {
                menu.page = p;
                menu.selected = s;
            }
            sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        }
        return;
    }
    // the cheat page takes letters and Enter (cheats.rs); Esc or a click on Back leaves it
    if cheats.entering {
        if ctl.pressed(crate::input::Act::Back) || (mouse.just_pressed(MouseButton::Left) && hovered.is_some()) {
            // the box forgets the typed code (0x00410710, as its init 0x004106d0)
            cheats.typed.clear();
            cheats.result.clear();
            if let Some((p, s)) = menu.stack.pop() {
                menu.page = p;
                menu.selected = s;
            }
            sounds.write(crate::audio::PlaySound::menu("ui_cancel"));
        }
        return;
    }
    let play = |s: &mut MessageWriter<crate::audio::PlaySound>, n: &str| {
        s.write(crate::audio::PlaySound::menu(n));
    };
    // Esc in play opens the pause menu (not over a paused mission message)
    if *state.get() == AppState::Playing {
        if ctl.pressed(crate::input::Act::Pause) && mission.as_ref().is_none_or(|m| m.showing.is_none()) {
            next.set(AppState::Paused);
            vtime.pause();
            menu.page = Page::Pause;
            menu.stack.clear();
            menu.selected = 0;
            play(&mut sounds, "ui_select");
        }
        return;
    }
    let page = menu.page;
    // whether a saved game exists (checked twice a second: the save rows' enabling)
    *save_t -= time_real.delta_secs();
    if *save_t <= 0.0 {
        *save_t = 0.5;
        menu.has_save = crate::savegame::has_save();
    }
    let items = rows_of(rows.iter().map(|x| x.0), page, menu.has_save);
    // the row under the mouse as an index into this list
    let hovered = hovered.and_then(|h| items.iter().position(|x| x.0 == h));
    if items.is_empty() {
        return;
    }
    let n = items.len();
    menu.selected = menu.selected.min(n - 1);
    let mut act = None;
    if let Some(h) = hovered.filter(|h| *h < n) {
        if (cursor_moved || mouse.just_pressed(MouseButton::Left)) && menu.selected != h {
            menu.selected = h;
            play(&mut sounds, "ui_move");
        }
    }
    if ctl.step.y < 0 {
        menu.selected = (menu.selected + 1) % n;
        play(&mut sounds, "ui_move");
    }
    if ctl.step.y > 0 {
        menu.selected = (menu.selected + n - 1) % n;
        play(&mut sounds, "ui_move");
    }
    let cur = items[menu.selected].1;
    let step = ctl.step.x as f32;
    if step != 0.0 {
        if let Act::Slide(o) = cur {
            let target = if on_gfx_copy(o) { menu.edit.get_or_insert_with(|| opts.clone()) } else { &mut *opts };
            slide(target, o, step, win);
            play(&mut sounds, "ui_move");
        }
    }
    let cur = items[menu.selected].1;
    if ctl.pressed(crate::input::Act::Select) || (mouse.just_pressed(MouseButton::Left) && hovered.is_some()) {
        act = Some(cur);
    }
    if ctl.pressed(crate::input::Act::Back) {
        act = match page {
            Page::Main | Page::Failed => None,
            Page::Pause => Some(Act::Continue),
            Page::YesNo => Some(Act::No),
            Page::Gfx => Some(Act::Cancel),
            _ => Some(Act::Back),
        };
    }
    let Some(act) = act else { return };
    let go = |menu: &mut GameMenu, p: Page| {
        menu.stack.push((menu.page, menu.selected));
        menu.page = p;
        // the options page opens on Back, as in the original
        menu.selected = if p == Page::Options { usize::MAX } else { 0 };
    };
    let back = |menu: &mut GameMenu| {
        if let Some((p, s)) = menu.stack.pop() {
            menu.page = p;
            menu.selected = s;
        }
    };
    match act {
        Act::None => return,
        Act::NewGame => {
            // a new game's money (0x00401e80 at the front end; savegame.rs)
            opts.money = crate::savegame::NEW_GAME_MONEY;
            next.set(AppState::Playing);
            vtime.unpause();
        }
        Act::Continue => {
            next.set(AppState::Playing);
            vtime.unpause();
        }
        Act::Go(p) => {
            if p == Page::Gfx {
                menu.edit = Some(opts.clone());
            }
            go(&mut menu, p);
        }
        Act::Back => {
            opts.save();
            back(&mut menu);
        }
        Act::Ask(a) => {
            menu.ask = Some(a);
            go(&mut menu, Page::YesNo);
            // the safe answer first
            menu.selected = 1;
        }
        Act::No => back(&mut menu),
        Act::Yes => {
            let a = menu.ask.take();
            back(&mut menu);
            match a {
                Some(Ask::ExitGame) => {
                    exit.write(AppExit::Success);
                }
                Some(Ask::Restart) => restart.to = Some(RestartTo::Play),
                Some(Ask::QuitMission) => restart.to = Some(RestartTo::Title),
                // nothing is unlocked in the remaster yet: nothing to reset
                Some(Ask::ResetExtras) | None => {}
                Some(Ask::ResetControls) => {
                    let d = GameOptions::default();
                    opts.mouse_sensitivity = d.mouse_sensitivity;
                    opts.invert_mouse = d.invert_mouse;
                    opts.save();
                }
            }
        }
        Act::Restart => restart.to = Some(RestartTo::Play),
        Act::ToTitle => restart.to = Some(RestartTo::Title),
        Act::Save => {
            save_req.save = true;
            next.set(AppState::Playing);
            vtime.unpause();
        }
        Act::LoadSave => {
            if crate::savegame::has_save() {
                restart.to = Some(RestartTo::Load(crate::savegame::save_path()));
            } else {
                play(&mut sounds, "ui_cancel");
                return;
            }
        }
        Act::Apply => {
            if let Some(e) = menu.edit.take() {
                *opts = e;
                opts.save();
            }
            back(&mut menu);
        }
        Act::Cancel => {
            menu.edit = None;
            back(&mut menu);
        }
        Act::Slide(_) => return,
        Act::Toggle(f) => {
            let target = if flag_on_gfx_copy(f) { menu.edit.get_or_insert_with(|| opts.clone()) } else { &mut *opts };
            match f {
                Flag::Shadows => target.shadows = !target.shadows,
                Flag::SubsGame => target.subtitles_game = !target.subtitles_game,
                Flag::SubsMovie => target.subtitles_movie = !target.subtitles_movie,
                Flag::InvertMouse => target.invert_mouse = !target.invert_mouse,
                Flag::Fullscreen => target.fullscreen = !target.fullscreen,
                Flag::Graphics(key) => crate::graphics::menu::toggle(target, key),
            }
        }
    }
    play(&mut sounds, if matches!(act, Act::Back | Act::No | Act::Cancel) { "ui_cancel" } else { "ui_select" });
}

/// The window sizes offered by "Resolution" (the launch size is added when not listed).
const SIZES: [(u32, u32); 6] = [(1280, 720), (1600, 900), (1920, 1080), (2560, 1440), (3440, 1440), (3840, 2160)];

/// The steppers' ranges (GfxMenu_SetupWidget 0x004710d0, UiScreen_InitItem 0x00491b70): (min, max, positions).
/// Brightness 1.0..2.0 in 11, draw distance 0.25..1.0 in 16, mouse scale 0..1 in 21, the three volumes 0..1 in 21.
fn range(opt: Opt) -> (f32, f32, usize) {
    match opt {
        Opt::Brightness => (1.0, 2.0, 11),
        Opt::DrawDistance => (0.25, 1.0, 16),
        Opt::MouseSens | Opt::Effects | Opt::Music | Opt::Voice => (0.0, 1.0, 21),
        Opt::Resolution => (0.0, 1.0, SIZES.len()),
        Opt::Preset => (0.0, 1.0, crate::graphics::menu::PRESETS.len()),
        Opt::AntiAliasing => (0.0, 1.0, crate::graphics::AntiAliasing::ALL.len()),
    }
}

/// How many positions a stepper has (its ticks, 0x0046d130).
fn positions(opt: Opt) -> usize {
    range(opt).2
}

/// Stepper_SetValue 0x0046cec0 / Stepper_SetIndex 0x0046cfd0: the value's index (rounded to the nearest position),
/// and the value of an index.
fn index_of(opt: Opt, v: f32) -> usize {
    let (lo, hi, n) = range(opt);
    let step = (hi - lo) / (n - 1) as f32;
    (((v - lo) + step * 0.5) / step).max(0.0).floor().min((n - 1) as f32) as usize
}

fn value_at(opt: Opt, i: usize) -> f32 {
    let (lo, hi, n) = range(opt);
    let v = lo + (hi - lo) * i.min(n - 1) as f32 / (n - 1) as f32;
    (v * 100.0).round() / 100.0
}

/// Left / right steps the index by one (Stepper_HandleInput 0x0046d290: bits 4 / 8), not past the ends.
fn slide(o: &mut GameOptions, opt: Opt, step: f32, win: (u32, u32)) {
    let bump = |v: f32| value_at(opt, (index_of(opt, v) as i32 + step as i32).max(0) as usize);
    match opt {
        Opt::Brightness => o.brightness = bump(o.brightness),
        Opt::DrawDistance => o.draw_distance = bump(o.draw_distance),
        Opt::Effects => o.sound_volume = bump(o.sound_volume),
        Opt::Music => o.music_volume = bump(o.music_volume),
        Opt::Voice => o.voice_volume = bump(o.voice_volume),
        Opt::MouseSens => o.mouse_sensitivity = bump(o.mouse_sensitivity),
        Opt::Preset => {
            let i = crate::graphics::menu::preset_index(o) as i32 + step as i32;
            crate::graphics::menu::set_preset(o, i.clamp(0, positions(opt) as i32 - 1) as usize);
        }
        Opt::AntiAliasing => {
            let i = crate::graphics::menu::aa_index(o) as i32 + step as i32;
            crate::graphics::menu::set_aa(o, i.clamp(0, positions(opt) as i32 - 1) as usize);
        }
        Opt::Resolution => {
            let cur = if o.resolution == (0, 0) { win } else { o.resolution };
            let mut list: Vec<(u32, u32)> = SIZES.to_vec();
            if !list.contains(&cur) {
                list.push(cur);
                list.sort();
            }
            let i = list.iter().position(|s| *s == cur).unwrap_or(0) as i32;
            let j = (i + step as i32).clamp(0, list.len() as i32 - 1) as usize;
            o.resolution = list[j];
        }
    }
}

/// A click on a stepper's track (Stepper_HandleInput 0x0046d290: x 80..272, y -6..6 around the item) sets its
/// position: index = ftol((0.5 / steps + position) * steps) (Stepper_SetPosition 0x0046ce70).
fn set_position(o: &mut GameOptions, opt: Opt, frac: f32, win: (u32, u32)) {
    let n = positions(opt);
    let i = ((0.5 / (n - 1) as f32 + frac.clamp(0.0, 1.0)) * (n - 1) as f32).floor().min((n - 1) as f32) as usize;
    let v = value_at(opt, i);
    match opt {
        Opt::Brightness => o.brightness = v,
        Opt::DrawDistance => o.draw_distance = v,
        Opt::Effects => o.sound_volume = v,
        Opt::Music => o.music_volume = v,
        Opt::Voice => o.voice_volume = v,
        Opt::MouseSens => o.mouse_sensitivity = v,
        Opt::Preset => crate::graphics::menu::set_preset(o, i),
        Opt::AntiAliasing => crate::graphics::menu::set_aa(o, i),
        Opt::Resolution => {
            let cur = if o.resolution == (0, 0) { win } else { o.resolution };
            let _ = cur;
            o.resolution = SIZES[i.min(SIZES.len() - 1)];
        }
    }
}

/// A value's bar fraction (the stepper's position, index / steps) and text.
fn value(o: &GameOptions, opt: Opt, win: (u32, u32)) -> (f32, String) {
    let frac = |v: f32| index_of(opt, v) as f32 / (positions(opt) - 1) as f32;
    match opt {
        // 0x00470eb0 / 0x00470f20 / 0x00471050 / 0x0048fef0 set the value texts: "1.5", "100%", "50"
        Opt::Brightness => (frac(o.brightness), format!("{:.1}", o.brightness)),
        Opt::DrawDistance => (frac(o.draw_distance), format!("{:.0}%", o.draw_distance * 100.0)),
        Opt::Effects => (frac(o.sound_volume), format!("{:.0}", o.sound_volume * 100.0)),
        Opt::Music => (frac(o.music_volume), format!("{:.0}", o.music_volume * 100.0)),
        Opt::Voice => (frac(o.voice_volume), format!("{:.0}", o.voice_volume * 100.0)),
        Opt::MouseSens => (frac(o.mouse_sensitivity), format!("{:.0}", o.mouse_sensitivity * 100.0)),
        Opt::Preset => (crate::graphics::menu::preset_index(o) as f32 / (positions(opt) - 1) as f32, crate::graphics::menu::preset_text(o)),
        Opt::AntiAliasing => (crate::graphics::menu::aa_index(o) as f32 / (positions(opt) - 1) as f32, crate::graphics::menu::aa_text(o)),
        Opt::Resolution => {
            let r = if o.resolution == (0, 0) { win } else { o.resolution };
            let i = SIZES.iter().position(|s| *s == r).map(|i| i as f32 / (SIZES.len() - 1) as f32).unwrap_or(0.5);
            (i, format!("{}x{}", r.0, r.1))
        }
    }
}

/// Whether a change between two screens is instant (no fade): the original skips the fade when the current top
/// screen and the new one (or none) both have flag 4 (0x00490e90 at time 0 with mgr+0xf0 bit 2: the pause menu
/// 0x100c, mission failed 0x24; with no screen the bit is set, 0x00490c20; a push 0x00490ba0 sets it from the new top).
/// The requests (0x00490cb0, run by 0x00490d80 at the fade's middle) are the navigate's page changes. The remaster
/// also makes every change into or out of play instant (the game would run under a fading menu).
fn instant(from: Option<Page>, to: Option<Page>) -> bool {
    let flag4 = |p: Option<Page>| p.is_none_or(|p| matches!(p, Page::Pause | Page::Failed));
    from.is_none() || to.is_none() || (flag4(from) && flag4(to))
}

/// Shows the shown page's rows and parts; the selected row white and pulsing; the yes/no question and the failure
/// text. Screen changes fade through black (0x00490e90 / 0x00491670): the time runs 0..1 over the request's 0.5 s
/// (UI_PushScreen 0x00490b00 and the pops: fade-in 0.5) with the old screen still up, the change happens at 1, then
/// 1..2 over 0.25 s on the new one; a full-screen black quad at alpha v^3, v = t below 1, else 2 - t.
#[allow(clippy::too_many_arguments, clippy::type_complexity)]
fn animate(
    time: Res<Time<Real>>,
    state: Res<State<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut rows: Query<(&MenuRow, &mut HudText, &mut Visibility, &mut Glow), Without<Part>>,
    mut parts: Query<(&Part, &mut Visibility), Without<MenuRow>>,
    texts: Res<Texts>,
    mission: Option<Res<crate::mission::Mission>>,
    (fades, mut materials): (Query<&FadeQuad>, ResMut<Assets<ColorMaterial>>),
) {
    let dt = time.delta_secs();
    // the selection pulse (0x00463fa0: phase += dt x 5, wrapped to (-pi, pi]; size |sin(phase) x 0.15| + 1)
    menu.phase += dt * 5.0;
    while menu.phase > std::f32::consts::PI {
        menu.phase -= std::f32::consts::TAU;
    }
    let want = if *state.get() == AppState::Playing { None } else { Some(menu.page) };
    if menu.fade == 0.0 && want != menu.shown {
        if instant(menu.shown, want) {
            menu.shown = want;
            menu.shown_under = if want == Some(Page::YesNo) { menu.stack.last().map(|s| s.0) } else { None };
        } else {
            menu.fade = 1e-6;
        }
    }
    if menu.fade > 0.0 {
        if menu.fade < 1.0 {
            menu.fade += dt / 0.5;
            if menu.fade >= 1.0 {
                menu.fade = 1.0;
                menu.shown = want;
                menu.shown_under = if want == Some(Page::YesNo) { menu.stack.last().map(|s| s.0) } else { None };
            }
        } else {
            menu.fade += dt / 0.25;
            if menu.fade >= 2.0 {
                menu.fade = 0.0;
            }
        }
    }
    if menu.fade == 0.0 && menu.shown == want {
        menu.shown_sel = menu.selected;
    }
    let v = if menu.fade < 1.0 { menu.fade } else { 2.0 - menu.fade };
    for f in &fades {
        if let Some(mut m) = materials.get_mut(&f.0) {
            m.color.set_alpha(v * v * v);
        }
    }
    let page = menu.shown;
    let back = page.map(|p| p.back()).or(if *state.get() == AppState::Title { Some(Back::Title) } else { None });
    // the yes/no box shows over the page that asked
    let art = if page == Some(Page::YesNo) { menu.shown_under.map(|p| p.back()) } else { back };
    for (p, mut v) in &mut parts {
        let show = match p.kind {
            PartKind::Art(b) => art == Some(b),
            _ => p.page.is_some() && p.page == page,
        };
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
    // a page entered "on its last row" (Options opens on Back: usize::MAX) is clamped here too, as navigate does
    let n = rows_of(rows.iter().map(|x| x.0), menu.page, menu.has_save).len();
    if n > 0 && menu.selected >= n {
        menu.selected = n - 1;
    }
    let order = rows_of(rows.iter().map(|x| x.0), page.unwrap_or(menu.page), menu.has_save);
    let sel_order = order.get(if page == Some(menu.page) { menu.selected } else { menu.shown_sel }).map(|x| x.0);
    for (r, mut t, mut v, mut glow) in &mut rows {
        let shown = Some(r.page) == page;
        *v = if shown { Visibility::Inherited } else { Visibility::Hidden };
        if !shown {
            continue;
        }
        // the page's own texts that change
        if r.page == Page::YesNo && r.act == Act::None {
            // over the game the question goes on the message band instead (pause_band)
            let q = if menu.hud_under() { String::new() } else { menu.ask.map(|a| texts.get(a.text_id())).unwrap_or_default() };
            if t.text != q {
                t.text = q;
            }
        }
        if r.page == Page::Failed && r.act == Act::None && !r.disabled {
            let f = mission.as_ref().and_then(|m| m.failure_text.clone()).map(|s| crate::mission::long_form(&s)).unwrap_or_else(|| texts.get("ui_mfail_title"));
            if t.text != f {
                t.text = f;
            }
        }
        let sel = r.order.is_some() && r.order == sel_order;
        // UiText_Draw 0x00464750: size ((pulse - 1) x fade + 1) x scale, the fade +0x4e0 moving 0.2 per drawn frame
        // (here 12 / s) towards 1 while highlighted (+0x474 & 0x80), else towards 0; the colour white at once
        glow.0 = if sel { (glow.0 + 12.0 * dt).min(1.0) } else { (glow.0 - 12.0 * dt).max(0.0) };
        let pulse = (menu.phase.sin() * 0.15).abs() + 1.0;
        let scale = ((pulse - 1.0) * glow.0 + 1.0) * r.base_scale;
        let colour = if sel {
            Color::WHITE
        } else if r.act == Act::LoadSave && !menu.has_save {
            DISABLED
        } else {
            r.colour
        };
        if (scale - t.scale).abs() > 0.01 || colour != t.colour {
            t.scale = scale;
            t.colour = colour;
            t.built = None;
        }
    }
}

/// Slider bars and values, checkbox fills, from the options (the graphics page's edit copy while it is open).
#[allow(clippy::type_complexity)]
fn values(
    menu: Res<GameMenu>,
    opts: Res<GameOptions>,
    windows: Query<&Window>,
    mut texts: Query<(&Part, &mut HudText, &mut Visibility)>,
    cheats: Res<crate::cheats::Cheats>,
    mut fills: Query<(&BarFill, &mut Transform)>,
    checks: Query<(&CheckFill, &MeshMaterial2d<ColorMaterial>)>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    bank: Res<Texts>,
) {
    let win = windows.single().map(|w| (w.resolution.width() as u32, w.resolution.height() as u32)).unwrap_or((1920, 1080));
    let gfx = menu.edit.as_ref().unwrap_or(&opts);
    let pick = |o: Opt| -> &GameOptions { if on_gfx_copy(o) { gfx } else { &opts } };
    for (p, mut t, mut vis) in &mut texts {
        // the cheat grid (0x00410b20): shown when known, active or with List Cheats on; greyed unless active
        if let PartKind::CheatName(n) = p.kind {
            let flag = crate::cheats::CHEATS[n].2;
            let active = cheats.on(flag);
            if !(opts.cheats_known.contains(&flag) || active || cheats.on(0x80)) {
                *vis = Visibility::Hidden;
            }
            let c = if active { GREEN } else { DISABLED };
            if t.colour != c {
                t.colour = c;
                t.built = None;
            }
            continue;
        }
        let line = match p.kind {
            // the box shows the result until the next letter (Cheats_Check 0x004107c0 sets the box's text)
            PartKind::CheatTyped => Some(if cheats.typed.is_empty() && !cheats.result.is_empty() { cheats.result.clone() } else { format!("{}_", cheats.typed) }),
            // a key binding entry's label (0x00447e70, refreshed each frame as 0x00448260 does when the bindings
            // change): both bindings as "%s or %s" (pms_input_aorb), one alone, or "Unassigned"
            PartKind::Binding(a) => Some(match crate::input::keys(a).as_slice() {
                [] => bank.get("pms_input_unassigned"),
                [k] => k.clone(),
                [k1, k2, ..] => bank.get("pms_input_aorb").replacen("%s", k1, 1).replacen("%s", k2, 1),
            }),
            _ => None,
        };
        if let Some(l) = line {
            // the key label fitted to 128 units (0x00447da0: FitText_SetWidth 0x0046d5a0)
            if matches!(p.kind, PartKind::Binding(_)) {
                t.fit = 128.0;
            }
            if t.text != l {
                t.text = l;
            }
            continue;
        }
        if let PartKind::Value(o) = p.kind {
            let s = value(pick(o), o, win).1;
            if t.text != s {
                t.text = s;
            }
        }
    }
    // the fill: from +80 to 80 + position x 192 (0x0046d130; the gauge entity sits at +176)
    let w = 192.0;
    for (f, mut tr) in &mut fills {
        let frac = value(pick(f.0), f.0, win).0.clamp(0.0, 1.0);
        tr.scale.x = (w * frac).max(0.01);
        tr.translation.x = 80.0 - 176.0 + w * frac / 2.0;
    }
    for (c, m) in &checks {
        let on = match c.0 {
            Flag::Shadows => gfx.shadows,
            Flag::SubsGame => opts.subtitles_game,
            Flag::SubsMovie => opts.subtitles_movie,
            Flag::InvertMouse => opts.invert_mouse,
            Flag::Fullscreen => gfx.fullscreen,
            Flag::Graphics(key) => crate::graphics::menu::flag(gfx, key),
        };
        if let Some(mut mat) = materials.get_mut(&m.0) {
            mat.color = if on { Color::srgb(0.55, 0.05, 0.05) } else { Color::srgb(0.05, 0.08, 0.1) };
        }
    }
}

/// The pause menu keeps the HUD and shows the objectives (the short form of the current objectives text) on the
/// band at ref_mission_hint, as captured (orig_ui_play-720.png).
fn pause_band(state: Res<State<AppState>>, mut menu: ResMut<GameMenu>, mission: Option<Res<crate::mission::Mission>>, mut hud: ResMut<crate::hud::HudState>, texts: Res<Texts>) {
    let want = *state.get() == AppState::Paused && menu.hud_under() && menu.page != Page::Failed;
    if want {
        // the yes/no question over the game sits on the band (readable over the world), else the objectives
        let text = if menu.page == Page::YesNo {
            menu.ask.map(|a| format!("<rgb=ffffff>{}", texts.get(a.text_id()))).unwrap_or_default()
        } else {
            mission.as_ref().map(|m| crate::mission::short_form(&m.objectives_raw)).unwrap_or_default()
        };
        if !text.is_empty() {
            hud.briefing = Some(text);
            menu.band = true;
        }
    } else if menu.band {
        hud.briefing = None;
        menu.band = false;
    }
}

/// The credits scroll up (the original's scroll length is (lines + K) / line height, 0x00415940; its speed was not
/// traced: guess, 30 units of the 640x480 screen per second); they start below the screen each time the page opens.
fn credits(time: Res<Time<Real>>, menu: Res<GameMenu>, mut q: Query<(&Part, &mut HudElement)>, mut was_open: Local<bool>) {
    let open = menu.page == Page::Credits;
    for (p, mut e) in &mut q {
        if p.kind != PartKind::Credits {
            continue;
        }
        if open && !*was_open {
            e.pos.y = -40.0 - 586.0 * 22.5 / 2.0;
        }
        if open {
            e.pos.y += 30.0 * time.delta_secs();
        }
    }
    *was_open = open;
}
