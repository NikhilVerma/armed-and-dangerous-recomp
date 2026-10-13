//! The game's own menus from the UI bank layout (docs/subsystems/hud.md, menus.md): the main menu
//! (pui_mainmenu: New Game, Resume Game, Options, Extras, Exit Game over the title art) and the pause menu
//! (pui_pausemenu: Continue, Save Game, Quick Load, Restart the level, Options, Abandon Mission), in the game font
//! at their layout rows, the selected item white and pulsing (size = (|sin(phase)| * 0.15 + 1) * scale, phase +=
//! dt * 5), disabled items #404040. Items whose screens are not ported yet are shown disabled.
//! Keys: Up/Down (W/S), Enter or click to choose, Esc for the pause menu.

use crate::AppState;
use crate::hud::{Anchor, HUD_LAYER, HudElement, HudText};
use bevy::camera::visibility::RenderLayers;
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum Item {
    NewGame,
    Continue,
    Exit,
    Abandon,
    Disabled,
}

/// One menu row (a HudText child of the menu root).
#[derive(Component)]
pub struct MenuRow {
    pub page: &'static str,
    pub item: Item,
    pub base_scale: f32,
    pub colour: Color,
    pub index: usize,
}

#[derive(Resource, Default)]
pub struct GameMenu {
    pub selected: usize,
    phase: f32,
}

/// The title art behind the main menu.
#[derive(Component)]
pub struct TitleArt;

pub struct GameMenuPlugin;

impl Plugin for GameMenuPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<GameMenu>().add_systems(Update, (navigate, animate));
    }
}

/// Spawns the rows of both pages (the layout's strings, positions, scales, colours) and the title art.
pub fn spawn(commands: &mut Commands, assets: &AssetServer, meshes: &mut Assets<Mesh>, materials: &mut Assets<ColorMaterial>) {
    let main: [(&str, f32, Item); 5] = [
        ("New Game", 295.275, Item::NewGame),
        ("Resume Game", 247.275, Item::Disabled),
        ("Options", 199.275, Item::Disabled),
        ("Extras", 151.275, Item::Disabled),
        ("Exit Game", 103.275, Item::Exit),
    ];
    for (i, (s, y, item)) in main.iter().enumerate() {
        row(commands, "main", s, Vec2::new(320.0, *y), 1.3, Color::srgb_u8(0xd0, 0x50, 0x30), *item, i);
    }
    let pause: [(&str, f32, Item); 6] = [
        ("Continue", 180.44, Item::Continue),
        ("Save Game", 156.44, Item::Disabled),
        ("Quick Load", 132.44, Item::Disabled),
        ("Restart the level", 108.44, Item::Disabled),
        ("Options", 84.44, Item::Disabled),
        ("Abandon Mission", 60.44, Item::Abandon),
    ];
    for (i, (s, y, item)) in pause.iter().enumerate() {
        row(commands, "pause", s, Vec2::new(320.0, *y), 1.0, Color::srgb_u8(0xa0, 0xff, 0xa0), *item, i);
    }
    // title art (and_title, 1024 x 512) over the upper part of the 640x480 screen
    let tex: Handle<Image> = assets.load("ui/textures/and_title.png");
    let mat = materials.add(ColorMaterial { texture: Some(tex), ..default() });
    commands.spawn((
        HudElement { name: "title_art".into(), pos: Vec2::new(320.0, 390.0), anchor: Anchor::Centre },
        Transform::default(),
        Visibility::Hidden,
        RenderLayers::layer(HUD_LAYER),
        TitleArt,
        children![(Mesh2d(meshes.add(Rectangle::new(400.0, 200.0))), MeshMaterial2d(mat), Transform::from_xyz(0.0, 0.0, 5.0), RenderLayers::layer(HUD_LAYER))],
    ));
}

#[allow(clippy::too_many_arguments)]
fn row(commands: &mut Commands, page: &'static str, s: &str, pos: Vec2, scale: f32, colour: Color, item: Item, index: usize) {
    let colour = if item == Item::Disabled { Color::srgb_u8(0x40, 0x40, 0x40) } else { colour };
    commands.spawn((
        HudElement { name: format!("menu_{page}_{index}"), pos, anchor: Anchor::Centre },
        HudText { text: s.to_owned(), built: None, scale, colour, fit: 0.0 },
        MenuRow { page, item, base_scale: scale, colour, index },
        Transform::default(),
        Visibility::Hidden,
        RenderLayers::layer(HUD_LAYER),
    ));
}

fn page_of(state: &AppState) -> Option<&'static str> {
    match state {
        AppState::Title => Some("main"),
        AppState::Paused => Some("pause"),
        AppState::Playing => None,
    }
}

#[allow(clippy::too_many_arguments)]
fn navigate(
    keys: Res<ButtonInput<KeyCode>>,
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<GameMenu>,
    rows: Query<&MenuRow>,
    mut exit: MessageWriter<AppExit>,
    mut vtime: ResMut<Time<Virtual>>,
    mission: Option<Res<crate::mission::Mission>>,
) {
    // Esc toggles the pause menu (not over a paused mission message)
    if keys.just_pressed(KeyCode::Escape) && mission.as_ref().is_none_or(|m| m.showing.is_none()) {
        match state.get() {
            AppState::Playing => {
                next.set(AppState::Paused);
                vtime.pause();
                menu.selected = 0;
                return;
            }
            AppState::Paused => {
                next.set(AppState::Playing);
                vtime.unpause();
                return;
            }
            AppState::Title => {}
        }
    }
    let Some(page) = page_of(state.get()) else { return };
    let items: Vec<&MenuRow> = {
        let mut v: Vec<&MenuRow> = rows.iter().filter(|r| r.page == page).collect();
        v.sort_by_key(|r| r.index);
        v
    };
    if items.is_empty() {
        return;
    }
    let n = items.len();
    let step = |from: usize, d: i32| -> usize {
        let mut i = from;
        for _ in 0..n {
            i = ((i as i32 + d).rem_euclid(n as i32)) as usize;
            if items[i].item != Item::Disabled {
                break;
            }
        }
        i
    };
    if items[menu.selected.min(n - 1)].item == Item::Disabled {
        menu.selected = step(menu.selected.min(n - 1), 1);
    }
    if keys.just_pressed(KeyCode::ArrowDown) || keys.just_pressed(KeyCode::KeyS) {
        menu.selected = step(menu.selected, 1);
    }
    if keys.just_pressed(KeyCode::ArrowUp) || keys.just_pressed(KeyCode::KeyW) {
        menu.selected = step(menu.selected, -1);
    }
    if keys.just_pressed(KeyCode::Enter) || keys.just_pressed(KeyCode::Space) {
        match items[menu.selected.min(n - 1)].item {
            Item::NewGame | Item::Continue => {
                next.set(AppState::Playing);
                vtime.unpause();
            }
            Item::Exit | Item::Abandon => {
                exit.write(AppExit::Success);
            }
            Item::Disabled => {}
        }
    }
}

/// Shows the current page's rows; the selected one white and pulsing.
fn animate(
    time: Res<Time<Real>>,
    state: Res<State<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut rows: Query<(&MenuRow, &mut HudText, &mut Visibility)>,
    mut art: Query<&mut Visibility, (With<TitleArt>, Without<MenuRow>)>,
) {
    menu.phase += time.delta_secs() * 5.0;
    let page = page_of(state.get());
    for mut v in &mut art {
        *v = if page == Some("main") { Visibility::Inherited } else { Visibility::Hidden };
    }
    for (r, mut t, mut v) in &mut rows {
        let shown = page == Some(r.page);
        *v = if shown { Visibility::Inherited } else { Visibility::Hidden };
        if !shown {
            continue;
        }
        let sel = r.index == menu.selected;
        let scale = if sel { (menu.phase.sin().abs() * 0.15 + 1.0) * r.base_scale } else { r.base_scale };
        let colour = if sel { Color::WHITE } else { r.colour };
        // rebuild only when the look changes by a visible amount
        if (scale - t.scale).abs() > 0.01 || colour != t.colour {
            t.scale = scale;
            t.colour = colour;
            t.built = None;
        }
    }
}
