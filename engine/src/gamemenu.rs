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
//! Restart the level and Return to title screen start the game again (a fresh process: the level state is not
//! reset in place).

use crate::AppState;
use crate::hud::{Anchor, HUD_LAYER, HudElement, HudText, TextAlign};
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
            Page::YesNo | Page::Pause | Page::Failed | Page::Pub => Back::Game,
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
    InvertMouse,
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
    CheatResult,
    Credits,
}

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
}

impl Default for GameMenu {
    fn default() -> Self {
        GameMenu { page: Page::Main, selected: 0, stack: Vec::new(), ask: None, phase: 0.0, edit: None, failed_shown: false, band: false }
    }
}

impl GameMenu {
    /// The HUD stays on screen under this page (the pause menu, its yes/no box, mission failed).
    pub fn hud_under(&self) -> bool {
        matches!(self.page, Page::Pause | Page::Failed) || (self.page == Page::YesNo && self.stack.last().is_some_and(|s| s.0.back() == Back::Game))
    }
}

/// The display strings (ui/texts.json).
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
        app.init_resource::<GameMenu>().init_resource::<Texts>().add_systems(Update, (failed, navigate, animate, values, pause_band, credits).chain());
    }
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
}

impl Spawner<'_, '_, '_> {
    #[allow(clippy::too_many_arguments)]
    fn row(&mut self, page: Page, id: &str, pos: Vec2, scale: f32, colour: Color, act: Act, align: f32) {
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
            HudElement { name: format!("menu_{page:?}_{id}"), pos, anchor: Anchor::Centre },
            HudText { text, built: None, scale, colour, fit: 0.0 },
            MenuRow { page, act, order, base_scale: scale, colour, disabled },
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
            HudElement { name: format!("menu_{page:?}_text"), pos, anchor: Anchor::Centre },
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

    /// The segmented bar of a slider (captured: x 250..447, 16 units high, ten segments), its value text at x 460.
    fn slider(&mut self, page: Page, label: &str, y: f32, opt: Opt, enabled: bool) {
        let colour = if enabled { GREEN } else { DISABLED };
        self.row(page, label, Vec2::new(240.0, y), 1.0, colour, if enabled { Act::Slide(opt) } else { Act::None }, 1.0);
        let (w, h) = (197.0, 16.0);
        let border = self.rect(Vec2::new(w + 2.0, h + 2.0), Color::srgb(0.05, 0.12, 0.02), 14.0);
        let back = self.rect(Vec2::new(w, h), Color::srgb(0.13, 0.25, 0.02), 15.0);
        let fill_colour = if enabled { Color::srgb(0.27, 0.75, 0.22) } else { Color::srgb(0.25, 0.3, 0.25) };
        let fill_mat = self.materials.add(ColorMaterial { color: fill_colour, ..default() });
        let fill = self
            .commands
            .spawn((
                Mesh2d(self.meshes.add(Rectangle::new(1.0, h - 2.0))),
                MeshMaterial2d(fill_mat),
                Transform::from_xyz(-w / 2.0, 0.0, 16.0),
                RenderLayers::layer(HUD_LAYER),
                BarFill(opt),
            ))
            .id();
        let mut kids = vec![border, back, fill];
        for i in 1..10 {
            let t = self.rect(Vec2::new(1.0, h), Color::srgb(0.08, 0.16, 0.02), 17.0);
            self.commands.entity(t).insert(Transform::from_xyz(-w / 2.0 + w * i as f32 / 10.0, -h / 4.0, 17.0));
            kids.push(t);
        }
        let bar = self
            .commands
            .spawn((
                HudElement { name: format!("menu_{page:?}_bar"), pos: Vec2::new(250.0 + w / 2.0, y), anchor: Anchor::Centre },
                Part { page: Some(page), kind: PartKind::Bar(opt) },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(bar).add_children(&kids);
        self.text(page, "", Vec2::new(460.0, y), 1.0, colour, -1.0, PartKind::Value(opt));
    }

    /// A slider that cannot be changed: its fixed (best) value shown greyed.
    fn fixed(&mut self, page: Page, label: &str, y: f32, value: &str, frac: f32) {
        self.row(page, label, Vec2::new(240.0, y), 1.0, DISABLED, Act::None, 1.0);
        let (w, h) = (197.0, 16.0);
        let back = self.rect(Vec2::new(w, h), Color::srgb(0.12, 0.14, 0.12), 15.0);
        let fill = self.rect(Vec2::new(w * frac, h - 2.0), Color::srgb(0.25, 0.3, 0.25), 16.0);
        self.commands.entity(fill).insert(Transform::from_xyz(-w * (1.0 - frac) / 2.0, 0.0, 16.0));
        let bar = self
            .commands
            .spawn((
                HudElement { name: format!("menu_{page:?}_fixed"), pos: Vec2::new(250.0 + w / 2.0, y), anchor: Anchor::Centre },
                Part { page: Some(page), kind: PartKind::Fixed },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(bar).add_children(&[back, fill]);
        self.text(page, value, Vec2::new(460.0, y), 1.0, DISABLED, -1.0, PartKind::Fixed);
    }

    /// A checkbox: a 14-unit box (dark red when set) before a label centred at `label_x`.
    fn check(&mut self, page: Page, label: &str, box_x: f32, label_x: f32, y: f32, flag: Option<Flag>, fixed_on: bool) {
        let enabled = flag.is_some();
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
                HudElement { name: format!("menu_{page:?}_check"), pos: Vec2::new(box_x, y), anchor: Anchor::Centre },
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
                HudElement { name: format!("menu_art_{back:?}"), pos: Vec2::new(320.0, 240.0), anchor: Anchor::Centre },
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
        let mut s = Spawner { commands, meshes, materials, texts: &texts, orders: HashMap::new() };
        s.art(Back::Title, assets.load("ui/textures/and_title.png"));
        s.art(Back::Shell, assets.load("ui/textures/b_background_01.png"));

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
        s.slider(Page::Gfx, "ui_gfxmenu_brightness", 366.691, Opt::Brightness, true);
        s.slider(Page::Gfx, "ui_gfxmenu_resolution", 342.691, Opt::Resolution, true);
        let best = |t: &Texts, id: &str| t.get(id);
        let (tq, lod) = (best(&texts, "ui_gfxmenu_tq2"), best(&texts, "ui_gfxmenu_lod4"));
        s.fixed(Page::Gfx, "ui_gfxmenu_texturequality", 318.691, &tq, 1.0);
        s.fixed(Page::Gfx, "ui_gfxmenu_texturelod", 294.691, &lod, 1.0);
        s.fixed(Page::Gfx, "ui_gfxmenu_maxtextures", 270.691, "-", 1.0);
        s.fixed(Page::Gfx, "ui_gfxmenu_modellod", 246.691, &lod, 1.0);
        s.slider(Page::Gfx, "ui_gfxmenu_drawdistance", 222.691, Opt::DrawDistance, true);
        s.check(Page::Gfx, "ui_gfxmenu_bumpmap", 51.0, 171.215, 174.691, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_pixelshaders", 51.0, 171.215, 150.691, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_blur", 51.0, 171.215, 126.691, None, false);
        s.check(Page::Gfx, "ui_gfxmenu_skinquality", 346.0, 465.773, 172.0, None, true);
        s.check(Page::Gfx, "ui_gfxmenu_shadows", 346.0, 465.773, 148.0, Some(Flag::Shadows), true);
        s.check(Page::Gfx, "ui_gfxmenu_decals", 346.0, 465.773, 124.0, None, false);
        s.row(Page::Gfx, "ui_gfxmenu_apply", Vec2::new(320.0, 60.44), 1.2, GREEN, Act::Apply, 0.0);
        s.row(Page::Gfx, "ui_shellmenu_cancel", Vec2::new(320.0, 36.44), 1.1, GREEN, Act::Cancel, 0.0);

        // audio (pui_sfxopt; orig_ui_sfx-1120.png)
        s.row(Page::Sfx, "ui_sfxmenu_title", Vec2::new(320.0, 420.996), 1.4, Color::WHITE, Act::None, 0.0);
        s.slider(Page::Sfx, "ui_sfxmenu_soundvolume", 342.691, Opt::Effects, true);
        s.slider(Page::Sfx, "ui_sfxmenu_musicvolume", 294.691, Opt::Music, true);
        s.slider(Page::Sfx, "ui_sfxmenu_voicevolume", 246.691, Opt::Voice, true);
        // no cutscene movies in the remaster yet: the movie subtitle option has nothing to switch
        s.check(Page::Sfx, "ui_sfxmenu_subtitles_movie", 200.0, 320.0, 132.996, None, true);
        s.check(Page::Sfx, "ui_sfxmenu_subtitles_ingame", 200.0, 320.0, 108.996, Some(Flag::SubsGame), true);
        s.row(Page::Sfx, "ui_shellmenu_back", Vec2::new(320.0, 60.44), 1.4, YELLOW, Act::Back, 0.0);

        // controls (pui_showcontrols; orig_ui_controls-1100.png): the remaster's bindings, two columns of
        // "action: key" (labels right-aligned at x 172 / 466, keys from x 187 / 482), rows 24 apart
        s.row(Page::Controls, "ui_mainmenu_controls", Vec2::new(320.0, 420.996), 1.4, GREEN, Act::None, 0.0);
        let left = [
            ("Move Forward", "W"),
            ("Move Backward", "S"),
            ("Move Left", "A"),
            ("Move Right", "D"),
            ("Turn Left", "Mouse Left"),
            ("Turn Right", "Mouse Right"),
            ("Look Up", "Mouse Up"),
            ("Look Down", "Mouse Down"),
            ("Fire Weapon", "Mouse 1"),
            ("Fire Special", "Mouse 2"),
        ];
        let right = [("Jump", "Space"), ("Reload", "R"), ("Activate", "X or F"), ("Continue", "Enter"), ("Pause", "Esc")];
        for (i, (a, k)) in left.iter().enumerate() {
            let y = 390.691 - 24.0 * i as f32;
            s.text(Page::Controls, a, Vec2::new(172.0, y), 0.8, GREEN, 1.0, PartKind::Fixed);
            s.text(Page::Controls, k, Vec2::new(187.0, y), 0.8, GREEN, -1.0, PartKind::Fixed);
        }
        for (i, (a, k)) in right.iter().enumerate() {
            let y = 388.0 - 24.0 * i as f32;
            s.text(Page::Controls, a, Vec2::new(466.0, y), 0.8, GREEN, 1.0, PartKind::Fixed);
            s.text(Page::Controls, k, Vec2::new(482.0, y), 0.8, GREEN, -1.0, PartKind::Fixed);
        }
        s.slider(Page::Controls, "ui_controls_mousescale", 150.691, Opt::MouseSens, true);
        // no auto-aim in the remaster yet
        s.check(Page::Controls, "ui_controls_autoaim", 200.0, 320.0, 108.44, None, false);
        s.check(Page::Controls, "ui_controls_flipmousey", 200.0, 320.0, 84.44, Some(Flag::InvertMouse), false);
        s.row(Page::Controls, "ui_controls_reset", Vec2::new(320.0, 60.44), 1.0, GREEN, Act::Ask(Ask::ResetControls), 0.0);
        s.row(Page::Controls, "ui_shellmenu_back", Vec2::new(320.0, 36.44), 1.4, GREEN, Act::Back, 0.0);

        // cheats (pui_cheats): title, instruction, the typed code (ui_cheatbox), the result (ui_cheatdesc rows),
        // Back
        s.row(Page::Cheats, "ui_cheatmenu_title", Vec2::new(320.0, 411.564), 1.4, Color::WHITE, Act::None, 0.0);
        s.row(Page::Cheats, "ui_cheatmenu_instruct", Vec2::new(320.0, 387.564), 1.0, Color::WHITE, Act::None, 0.0);
        s.text(Page::Cheats, "", Vec2::new(320.0, 339.564), 1.0, GREEN, 0.0, PartKind::CheatTyped);
        s.text(Page::Cheats, "", Vec2::new(320.0, 252.44), 1.0, GREEN, 0.0, PartKind::CheatResult);
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

/// Starts the game again in a fresh process continuing the saved game.
fn relaunch_load() {
    let Ok(exe) = std::env::current_exe() else { return };
    let mut args: Vec<String> = Vec::new();
    let mut it = std::env::args().skip(1);
    while let Some(a) = it.next() {
        match a.as_str() {
            "--play" | "--snap" | "--load" => {
                it.next();
            }
            "--skip-title" => {}
            _ => args.push(a),
        }
    }
    args.push("--load".into());
    args.push(crate::savegame::save_path().to_string_lossy().into_owned());
    if let Err(e) = std::process::Command::new(exe).args(&args).spawn() {
        warn!("menu: could not start the game again: {e}");
    }
}

/// Starts the game again in a fresh process: on the title screen, or straight into the level.
fn relaunch(into_level: bool) {
    let Ok(exe) = std::env::current_exe() else { return };
    let mut args: Vec<String> = Vec::new();
    let mut it = std::env::args().skip(1);
    while let Some(a) = it.next() {
        match a.as_str() {
            "--play" | "--snap" | "--load" => {
                it.next();
            }
            "--skip-title" => {}
            _ => args.push(a),
        }
    }
    if into_level {
        args.push("--skip-title".into());
    }
    if let Err(e) = std::process::Command::new(exe).args(&args).spawn() {
        warn!("menu: could not start the game again: {e}");
    }
}

fn rows_of(rows: &Query<(&MenuRow, &mut HudText, &mut Visibility), Without<Part>>, page: Page) -> Vec<(usize, Act)> {
    let mut v: Vec<(usize, Act)> = rows.iter().filter(|(r, _, _)| r.page == page).filter_map(|(r, _, _)| r.order.map(|o| (o, r.act))).collect();
    v.sort_by_key(|x| x.0);
    v
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

#[allow(clippy::too_many_arguments)]
fn navigate(
    keys: Res<ButtonInput<KeyCode>>,
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
    placed: Query<(&MenuRow, &HudElement)>,
    mut last_cursor: Local<Option<Vec2>>,
) {
    cheats.entering = menu.page == Page::Cheats && *state.get() != AppState::Playing;
    // the mouse in the menus (the original: Mouse 1 selects, Mouse 2 cancels): the row under the cursor is selected
    // when the cursor moves, a click activates it. Cursor (window pixels, y down) -> the menus' 640x480 space (y up),
    // scaled by height / 480 about the centre (hud.rs place)
    let mut hovered: Option<usize> = None;
    let mut cursor_moved = false;
    if *state.get() != AppState::Playing {
        if let Ok(w) = windows.single() {
            if let Some(c) = w.cursor_position() {
                let (ww, wh) = (w.width(), w.height());
                let s = wh / 480.0;
                let p = Vec2::new(320.0 + (c.x - ww / 2.0) / s, 240.0 - (c.y - wh / 2.0) / s);
                cursor_moved = last_cursor.is_some_and(|l| l.distance(c) > 0.5);
                *last_cursor = Some(c);
                hovered = placed
                    .iter()
                    .filter(|(r, _)| r.page == menu.page && r.order.is_some())
                    .filter(|(r, e)| (p.y - e.pos.y).abs() < 13.0 * r.base_scale && (p.x - e.pos.x).abs() < 150.0)
                    .min_by(|a, b| (p.y - a.1.pos.y).abs().total_cmp(&(p.y - b.1.pos.y).abs()))
                    .and_then(|(r, _)| r.order);
            }
        }
    }
    // the credits page: any of Esc / Enter leaves
    if menu.page == Page::Credits && *state.get() != AppState::Playing {
        if keys.just_pressed(KeyCode::Escape) || keys.just_pressed(KeyCode::Enter) {
            if let Some((p, s)) = menu.stack.pop() {
                menu.page = p;
                menu.selected = s;
            }
            sounds.write(crate::audio::PlaySound::ui("ui_cancel"));
        }
        return;
    }
    // the cheat page takes letters and Enter (cheats.rs); Esc leaves it
    if cheats.entering {
        if keys.just_pressed(KeyCode::Escape) {
            cheats.typed.clear();
            cheats.result.clear();
            if let Some((p, s)) = menu.stack.pop() {
                menu.page = p;
                menu.selected = s;
            }
            sounds.write(crate::audio::PlaySound::ui("ui_cancel"));
        }
        return;
    }
    let play = |s: &mut MessageWriter<crate::audio::PlaySound>, n: &str| {
        s.write(crate::audio::PlaySound::ui(n));
    };
    // Esc in play opens the pause menu (not over a paused mission message)
    if *state.get() == AppState::Playing {
        if keys.just_pressed(KeyCode::Escape) && mission.as_ref().is_none_or(|m| m.showing.is_none()) {
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
    let items = rows_of(&rows, page);
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
    if keys.just_pressed(KeyCode::ArrowDown) || keys.just_pressed(KeyCode::KeyS) {
        menu.selected = (menu.selected + 1) % n;
        play(&mut sounds, "ui_move");
    }
    if keys.just_pressed(KeyCode::ArrowUp) || keys.just_pressed(KeyCode::KeyW) {
        menu.selected = (menu.selected + n - 1) % n;
        play(&mut sounds, "ui_move");
    }
    let cur = items[menu.selected].1;
    let step = if keys.just_pressed(KeyCode::ArrowRight) || keys.just_pressed(KeyCode::KeyD) {
        1.0
    } else if keys.just_pressed(KeyCode::ArrowLeft) || keys.just_pressed(KeyCode::KeyA) {
        -1.0
    } else {
        0.0
    };
    if step != 0.0 {
        if let Act::Slide(o) = cur {
            let win = windows.single().map(|w| (w.resolution.width() as u32, w.resolution.height() as u32)).unwrap_or((1920, 1080));
            let target = if o == Opt::Brightness || o == Opt::Resolution || o == Opt::DrawDistance { menu.edit.get_or_insert_with(|| opts.clone()) } else { &mut *opts };
            slide(target, o, step, win);
            play(&mut sounds, "ui_move");
        }
    }
    let cur = items[menu.selected].1;
    if keys.just_pressed(KeyCode::Enter) || keys.just_pressed(KeyCode::Space) || (mouse.just_pressed(MouseButton::Left) && hovered.is_some()) {
        act = Some(cur);
    }
    if keys.just_pressed(KeyCode::Escape) || mouse.just_pressed(MouseButton::Right) {
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
        Act::NewGame | Act::Continue => {
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
                Some(Ask::Restart) => {
                    relaunch(true);
                    exit.write(AppExit::Success);
                }
                Some(Ask::QuitMission) => {
                    relaunch(false);
                    exit.write(AppExit::Success);
                }
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
        Act::Restart => {
            relaunch(true);
            exit.write(AppExit::Success);
        }
        Act::ToTitle => {
            relaunch(false);
            exit.write(AppExit::Success);
        }
        Act::Save => {
            save_req.save = true;
            next.set(AppState::Playing);
            vtime.unpause();
        }
        Act::LoadSave => {
            if crate::savegame::has_save() {
                relaunch_load();
                exit.write(AppExit::Success);
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
            let target = if f == Flag::Shadows { menu.edit.get_or_insert_with(|| opts.clone()) } else { &mut *opts };
            match f {
                Flag::Shadows => target.shadows = !target.shadows,
                Flag::SubsGame => target.subtitles_game = !target.subtitles_game,
                Flag::InvertMouse => target.invert_mouse = !target.invert_mouse,
            }
        }
    }
    play(&mut sounds, if matches!(act, Act::Back | Act::No | Act::Cancel) { "ui_cancel" } else { "ui_select" });
}

/// The window sizes offered by "Resolution" (the launch size is added when not listed).
const SIZES: [(u32, u32); 6] = [(1280, 720), (1600, 900), (1920, 1080), (2560, 1440), (3440, 1440), (3840, 2160)];

fn slide(o: &mut GameOptions, opt: Opt, step: f32, win: (u32, u32)) {
    let bump = |v: f32, d: f32, _lo: f32, _hi: f32| ((v + d * step) * 100.0).round() / 100.0;
    match opt {
        Opt::Brightness => o.brightness = bump(o.brightness, 0.1, 0.5, 2.5).clamp(0.5, 2.5),
        Opt::DrawDistance => o.draw_distance = bump(o.draw_distance, 0.05, 0.25, 1.0).clamp(0.25, 1.0),
        Opt::Effects => o.sound_volume = bump(o.sound_volume, 0.05, 0.0, 1.0).clamp(0.0, 1.0),
        Opt::Music => o.music_volume = bump(o.music_volume, 0.05, 0.0, 1.0).clamp(0.0, 1.0),
        Opt::Voice => o.voice_volume = bump(o.voice_volume, 0.05, 0.0, 1.0).clamp(0.0, 1.0),
        Opt::MouseSens => o.mouse_sensitivity = bump(o.mouse_sensitivity, 0.05, 0.05, 1.0).clamp(0.05, 1.0),
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

/// A value's bar fraction and text.
fn value(o: &GameOptions, opt: Opt, win: (u32, u32)) -> (f32, String) {
    match opt {
        Opt::Brightness => ((o.brightness - 0.5) / 2.0, format!("{:.1}", o.brightness)),
        Opt::DrawDistance => (o.draw_distance, format!("{:.0}%", o.draw_distance * 100.0)),
        Opt::Effects => (o.sound_volume, format!("{:.0}", o.sound_volume * 100.0)),
        Opt::Music => (o.music_volume, format!("{:.0}", o.music_volume * 100.0)),
        Opt::Voice => (o.voice_volume, format!("{:.0}", o.voice_volume * 100.0)),
        Opt::MouseSens => (o.mouse_sensitivity, format!("{:.0}", o.mouse_sensitivity * 100.0)),
        Opt::Resolution => {
            let r = if o.resolution == (0, 0) { win } else { o.resolution };
            let i = SIZES.iter().position(|s| *s == r).map(|i| (i + 1) as f32 / SIZES.len() as f32).unwrap_or(0.5);
            (i, format!("{}x{}", r.0, r.1))
        }
    }
}

/// Shows the current page's rows and parts; the selected row white and pulsing; the yes/no question and the
/// failure text.
#[allow(clippy::too_many_arguments)]
fn animate(
    time: Res<Time<Real>>,
    state: Res<State<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut rows: Query<(&MenuRow, &mut HudText, &mut Visibility), Without<Part>>,
    mut parts: Query<(&Part, &mut Visibility), Without<MenuRow>>,
    texts: Res<Texts>,
    mission: Option<Res<crate::mission::Mission>>,
) {
    menu.phase += time.delta_secs() * 5.0;
    let page = if *state.get() == AppState::Playing { None } else { Some(menu.page) };
    let back = page.map(|p| p.back()).or(if *state.get() == AppState::Title { Some(Back::Title) } else { None });
    // the yes/no box shows over the page that asked
    let under = if page == Some(Page::YesNo) { menu.stack.last().map(|s| s.0) } else { None };
    let art = if page == Some(Page::YesNo) { under.map(|p| p.back()) } else { back };
    for (p, mut v) in &mut parts {
        let show = match p.kind {
            PartKind::Art(b) => art == Some(b),
            _ => p.page.is_some() && p.page == page,
        };
        *v = if show { Visibility::Inherited } else { Visibility::Hidden };
    }
    let order = rows_of(&rows, menu.page);
    let sel_order = order.get(menu.selected).map(|x| x.0);
    for (r, mut t, mut v) in &mut rows {
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
        let scale = if sel { (menu.phase.sin().abs() * 0.15 + 1.0) * r.base_scale } else { r.base_scale };
        let colour = if sel { Color::WHITE } else { r.colour };
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
    mut texts: Query<(&Part, &mut HudText)>,
    cheats: Res<crate::cheats::Cheats>,
    mut fills: Query<(&BarFill, &mut Transform)>,
    checks: Query<(&CheckFill, &MeshMaterial2d<ColorMaterial>)>,
    mut materials: ResMut<Assets<ColorMaterial>>,
) {
    let win = windows.single().map(|w| (w.resolution.width() as u32, w.resolution.height() as u32)).unwrap_or((1920, 1080));
    let gfx = menu.edit.as_ref().unwrap_or(&opts);
    let pick = |o: Opt| -> &GameOptions { if matches!(o, Opt::Brightness | Opt::Resolution | Opt::DrawDistance) { gfx } else { &opts } };
    for (p, mut t) in &mut texts {
        let line = match p.kind {
            PartKind::CheatTyped => Some(format!("{}_", cheats.typed)),
            PartKind::CheatResult => Some(cheats.result.clone()),
            _ => None,
        };
        if let Some(l) = line {
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
    let w = 197.0;
    for (f, mut tr) in &mut fills {
        let frac = value(pick(f.0), f.0, win).0.clamp(0.0, 1.0);
        tr.scale.x = (w * frac).max(0.01);
        tr.translation.x = -w / 2.0 + w * frac / 2.0;
    }
    for (c, m) in &checks {
        let on = match c.0 {
            Flag::Shadows => gfx.shadows,
            Flag::SubsGame => opts.subtitles_game,
            Flag::InvertMouse => opts.invert_mouse,
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
