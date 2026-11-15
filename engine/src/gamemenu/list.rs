//! Remaster menu parts (`modern_menus`, display/mod.rs), drawn with the original menu's own text, colours, pulse and
//! sounds: a scrolling list (rows with a [`ListPos`], a window of [`WINDOW`] rows, a scroll bar, a bar behind the
//! selected row), a tab bar, a line of help for the selected row, a box behind the yes/no question, and the Level
//! Select page built on them. The original has none of these: its list boxes (ui_listbox_*) are not ported, and its
//! pages are fixed layouts.
//!
//! The list keeps every row spawned. [`view`] moves the rows of the open page to their places and scrolls so the
//! selected row stays in the window; `animate` hides the rows outside it. The mouse wheel scrolls and takes the
//! selection along.

use super::*;
use bevy::input::mouse::{MouseScrollUnit, MouseWheel};

/// The first row's y, the rows' spacing (the original pages' 24) and how many rows a list shows.
pub const TOP: f32 = 372.0;
pub const ROW: f32 = 24.0;
pub const WINDOW: usize = 11;

/// A row's (or a row part's) place in a list.
#[derive(Clone, Copy, PartialEq, Debug)]
pub struct ListPos {
    /// the Settings tab it belongs to (None: the page has no tabs)
    pub tab: Option<usize>,
    pub slot: usize,
}

impl ListPos {
    pub fn in_window(self, scroll: usize) -> bool {
        (scroll..scroll + WINDOW).contains(&self.slot)
    }

    pub fn y(self, scroll: usize) -> f32 {
        TOP - ROW * (self.slot as f32 - scroll as f32)
    }
}

/// The thumb of a page's scroll bar.
#[derive(Component)]
pub(super) struct Thumb(Page);

/// The selection's move on the tabbed page. `items`: the tabs first, then the open tab's rows, then Back. Left and
/// right walk the tab bar; down from a tab enters the list; up from the first row returns to the open tab.
pub fn tabbed_neighbour(items: &[(usize, Act)], from: usize, dir: IVec2, tab: usize) -> Option<usize> {
    let tabs = items.iter().filter(|x| matches!(x.1, Act::Tab(_))).count();
    if matches!(items[from].1, Act::Tab(_)) {
        return match (dir.x, dir.y) {
            (x, 0) if x < 0 => from.checked_sub(1),
            (x, 0) if x > 0 => (from + 1 < tabs).then_some(from + 1),
            (_, y) if y < 0 => (tabs < items.len()).then_some(tabs),
            _ => None,
        };
    }
    if dir.y > 0 {
        return if from == tabs { Some(tab.min(tabs.saturating_sub(1))) } else { from.checked_sub(1) };
    }
    if dir.y < 0 {
        return (from + 1 < items.len()).then_some(from + 1);
    }
    None
}

impl Spawner<'_, '_, '_> {
    fn part(&mut self, page: Page, name: &str, pos: Vec2, kind: PartKind, kids: &[Entity]) -> Entity {
        let e = self
            .commands
            .spawn((
                HudElement { name: format!("menu_{page:?}_{name}"), pos, anchor: Anchor::Centre, full: true },
                Part { page: Some(page), kind, list: None },
                MenuOwned,
                Transform::default(),
                Visibility::Hidden,
                RenderLayers::layer(HUD_LAYER),
            ))
            .id();
        self.commands.entity(e).add_children(kids);
        e
    }

    /// A list's furniture: the bar behind the selected row and the scroll bar at the right.
    pub(super) fn list_frame(&mut self, page: Page) {
        let edge = self.rect(Vec2::new(596.0, ROW - 2.0), Color::srgb_u8(0x40, 0xc0, 0x40), 4.9);
        let bar = self.rect(Vec2::new(594.0, ROW - 4.0), Color::srgb_u8(0x14, 0x2c, 0x08), 5.0);
        self.part(page, "selectbar", Vec2::new(320.0, TOP), PartKind::SelectBar, &[edge, bar]);
        let height = ROW * WINDOW as f32;
        let rail = self.rect(Vec2::new(4.0, height), Color::srgb_u8(0x20, 0x40, 0x00), 6.0);
        let thumb = self.rect(Vec2::new(4.0, 1.0), Color::srgb_u8(0x40, 0xc0, 0x40), 7.0);
        self.commands.entity(thumb).insert(Thumb(page));
        self.part(page, "scrollbar", Vec2::new(626.0, TOP - ROW * (WINDOW as f32 - 1.0) / 2.0), PartKind::ScrollBar, &[rail, thumb]);
    }

    /// The line of help for the selected row, under the list.
    pub(super) fn hint_line(&mut self, page: Page) {
        self.list = None;
        self.text(page, "", Vec2::new(320.0, 92.0), 0.75, Color::srgb_u8(0xc0, 0xd8, 0xc0), 0.0, PartKind::Hint);
    }

    /// A thin rule across the page.
    pub(super) fn rule(&mut self, page: Page, pos: Vec2, width: f32, colour: Color, kind: PartKind) {
        let line = self.rect(Vec2::new(width, 2.0), colour, 6.0);
        self.part(page, "rule", pos, kind, &[line]);
    }

    /// The box behind a question.
    pub(super) fn dialog(&mut self, page: Page, centre: Vec2, size: Vec2) {
        let edge = self.rect(size + Vec2::splat(4.0), Color::srgb_u8(0x40, 0xc0, 0x40), 3.0);
        let fill = self.rect(size, Color::srgb_u8(0x06, 0x10, 0x04), 4.0);
        self.part(page, "dialog", centre, PartKind::Dialog, &[edge, fill]);
    }

    /// Level Select: every exported level, the campaign's in order and then the bonus levels, each by its title; the
    /// help line names the level's folder.
    pub(super) fn levels_page(&mut self, levels: &crate::campaign::Levels) {
        let p = Page::Levels;
        self.row(p, "Level Select", Vec2::new(320.0, 432.0), 1.4, Color::WHITE, Act::None, 0.0);
        self.rule(p, Vec2::new(320.0, 408.0), 600.0, Color::srgb_u8(0x20, 0x40, 0x00), PartKind::Fixed);
        self.list_frame(p);
        let all = crate::extras::LEVELS.iter().chain(crate::extras::BONUS_LEVELS.iter()).copied().filter(|l| levels.dir(l).is_some());
        for (slot, level) in all.enumerate() {
            let pos = ListPos { tab: None, slot };
            self.list = Some(pos);
            self.hint = level;
            self.next.0 = 520.0;
            self.row(p, level, Vec2::new(320.0, pos.y(0)), 0.9, ORANGE, Act::Level(level), 0.0);
        }
        self.hint_line(p);
        self.row(p, "ui_shellmenu_back", Vec2::new(320.0, 48.0), 1.4, YELLOW, Act::Back, 0.0);
    }
}

/// The Settings page's tab keys: Q / E, Page Up / Page Down and the pad's shoulder buttons step the open tab; the
/// selection goes to the tab's first row.
pub(super) fn tab_keys(mut menu: ResMut<GameMenu>, keys: Res<ButtonInput<KeyCode>>, pads: Query<&Gamepad>, state: Res<State<AppState>>, mut sounds: MessageWriter<crate::audio::PlaySound>) {
    if menu.page != Page::Settings || menu.fade > 0.0 || menu.capture.is_some() || matches!(*state.get(), AppState::Playing | AppState::Loading) {
        return;
    }
    let pad = |b: GamepadButton| pads.iter().any(|p| p.just_pressed(b));
    let next = keys.any_just_pressed([KeyCode::KeyE, KeyCode::PageDown]) || pad(GamepadButton::RightTrigger);
    let prev = keys.any_just_pressed([KeyCode::KeyQ, KeyCode::PageUp]) || pad(GamepadButton::LeftTrigger);
    let tabs = settings::TABS.len();
    let to = (menu.tab + next as usize).min(tabs - 1).saturating_sub(prev as usize);
    if to != menu.tab {
        menu.tab = to;
        menu.scroll = 0;
        menu.selected = tabs;
        sounds.write(MenuSound::Move.sound());
    }
}

/// Places the open page's list: the scroll follows the selection (or the mouse wheel, which takes the selection
/// along), the rows and their parts sit at their window places, the select bar behind the selected row, the scroll
/// bar's thumb over the part of the list in view.
#[allow(clippy::type_complexity)]
pub(super) fn view(
    mut menu: ResMut<GameMenu>,
    settings: Res<crate::graphics::GraphicsSettings>,
    mut wheel: MessageReader<MouseWheel>,
    mut turned: Local<f32>,
    mut rows: Query<(&MenuRow, &mut HudElement), Without<Part>>,
    mut parts: Query<(&Part, &mut HudElement, &mut Visibility), Without<MenuRow>>,
    mut thumbs: Query<(&Thumb, &mut Transform)>,
) {
    if menu.gate.modern != settings.display.modern_menus || menu.gate.tab != menu.tab {
        menu.gate.modern = settings.display.modern_menus;
        menu.gate.tab = menu.tab;
    }
    for w in wheel.read() {
        *turned += match w.unit {
            MouseScrollUnit::Line => w.y,
            MouseScrollUnit::Pixel => w.y / 40.0,
        };
    }
    let page = menu.page;
    let gate = menu.gate;
    let count = rows.iter().filter(|(r, _)| r.page == page && r.live(gate)).filter_map(|(r, _)| r.list).map(|l| l.slot + 1).max().unwrap_or(0);
    if count == 0 {
        *turned = 0.0;
        // a page without a list: no bar of another page's list stays up
        for (p, _, mut vis) in &mut parts {
            if matches!(p.kind, PartKind::SelectBar | PartKind::ScrollBar) && *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
        }
        return;
    }
    let items = rows_of(rows.iter().map(|x| x.0), page, gate);
    let slot_of = |order: usize| rows.iter().find(|(r, _)| r.page == page && r.live(gate) && r.order == Some(order)).and_then(|(r, _)| r.list).map(|l| l.slot);
    let selected = items.get(menu.selected).and_then(|x| slot_of(x.0));
    let last = count.saturating_sub(WINDOW);
    let mut scroll = menu.scroll.min(last);
    let steps = turned.trunc();
    if steps != 0.0 && menu.fade == 0.0 {
        *turned -= steps;
        scroll = (scroll as i32 - steps as i32).clamp(0, last as i32) as usize;
        // the selection stays in view
        if let Some(s) = selected {
            let to = s.clamp(scroll, scroll + WINDOW - 1);
            if let Some(i) = items.iter().position(|x| slot_of(x.0) == Some(to)).filter(|_| to != s) {
                menu.selected = i;
            }
        }
    } else if let Some(s) = selected {
        scroll = scroll.clamp((s + 1).saturating_sub(WINDOW), s);
    }
    if menu.scroll != scroll {
        menu.scroll = scroll;
    }
    let selected = items.get(menu.selected).and_then(|x| slot_of(x.0));
    for (r, mut e) in &mut rows {
        if let (true, Some(l)) = (r.page == page, r.list) {
            if e.pos.y != l.y(scroll) {
                e.pos.y = l.y(scroll);
            }
        }
    }
    let on_screen = menu.on_screen() == Some(page);
    for (p, mut e, mut vis) in &mut parts {
        if p.page != Some(page) {
            if matches!(p.kind, PartKind::SelectBar | PartKind::ScrollBar) && *vis != Visibility::Hidden {
                *vis = Visibility::Hidden;
            }
            continue;
        }
        let show = match p.kind {
            PartKind::SelectBar => {
                if let Some(s) = selected {
                    e.pos.y = ListPos { tab: None, slot: s }.y(scroll);
                }
                Some(selected.is_some())
            }
            PartKind::ScrollBar => Some(count > WINDOW),
            _ => None,
        };
        if let Some(show) = show {
            let v = if show && on_screen { Visibility::Inherited } else { Visibility::Hidden };
            if *vis != v {
                *vis = v;
            }
        } else if let Some(l) = p.list {
            if e.pos.y != l.y(scroll) {
                e.pos.y = l.y(scroll);
            }
        }
    }
    let height = ROW * WINDOW as f32;
    for (t, mut tr) in &mut thumbs {
        if t.0 == page {
            let h = height * (WINDOW as f32 / count as f32).min(1.0);
            tr.scale.y = h;
            tr.translation.y = height / 2.0 - height * scroll as f32 / count as f32 - h / 2.0;
        }
    }
}

/// The help line shows the selected row's hint.
pub(super) fn hints(menu: Res<GameMenu>, rows: Query<&MenuRow>, mut parts: Query<(&Part, &mut HudText)>) {
    if !parts.iter().any(|(p, _)| p.kind == PartKind::Hint && p.page == Some(menu.page)) {
        return;
    }
    let order = rows_of(rows.iter(), menu.page, menu.gate).get(menu.selected).map(|x| x.0);
    let hint = rows.iter().find(|r| r.page == menu.page && r.live(menu.gate) && r.order.is_some() && r.order == order).map_or("", |r| r.hint);
    for (p, mut t) in &mut parts {
        if p.kind == PartKind::Hint && p.page == Some(menu.page) && t.text != hint {
            t.text = hint.to_owned();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The tab bar: left / right along it, down into the list, up from the first row back to the open tab.
    #[test]
    fn tab_navigation() {
        let items = [(0, Act::Tab(0)), (1, Act::Tab(1)), (2, Act::Tab(2)), (3, Act::Apply), (4, Act::Cancel), (5, Act::Back)];
        let (l, r, u, d) = (IVec2::new(-1, 0), IVec2::new(1, 0), IVec2::new(0, 1), IVec2::new(0, -1));
        assert_eq!((tabbed_neighbour(&items, 0, l, 0), tabbed_neighbour(&items, 0, r, 0), tabbed_neighbour(&items, 2, r, 2)), (None, Some(1), None));
        assert_eq!((tabbed_neighbour(&items, 1, d, 1), tabbed_neighbour(&items, 1, u, 1)), (Some(3), None));
        assert_eq!((tabbed_neighbour(&items, 3, u, 1), tabbed_neighbour(&items, 4, u, 1), tabbed_neighbour(&items, 4, d, 1), tabbed_neighbour(&items, 5, d, 1)), (Some(1), Some(3), Some(5), None));
        assert_eq!(tabbed_neighbour(&items, 3, l, 1), None);
    }

    #[test]
    fn window() {
        let p = ListPos { tab: None, slot: 12 };
        assert!(!p.in_window(0) && p.in_window(2) && p.in_window(12) && !p.in_window(13));
        assert_eq!((p.y(12), p.y(2)), (TOP, TOP - ROW * 10.0));
    }
}
