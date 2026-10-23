//! The save / load slot screens (feedback 26): the original's SaveGame and LoadGame screens (archive/src/ui/ui_03.cpp,
//! archive/src/renderer/render_r6.cpp) over the six save slots of savegame.rs. gamemenu.rs spawns the pages and drives the
//! selection; this module is what a slot shows and how the selection moves, and the system that draws the slots.
//!
//! The original (page pui_savegame, docs/subsystems/menus.md):
//! - the screen (show 0x004a3b60): the title "ui_savemenu_title" for SaveGame, else "ui_loadmenu_title"; the default
//!   slot selected: saving, the first empty one; loading, the newest save (0x004a3020's 0x0063a13c);
//! - six slot widgets (ui_saveslot, item ids 7200..7205, code 72) in a 3 x 2 grid (0x004a3260: from ref_info
//!   (320, 276.44), x + (n % 3 - 1) x 180, the top row 0.3667 x 480 = 176 higher), then "Continue without saving"
//!   (ui_tcrc42_nosave, 7308, code 73) and "Cancel" (ui_shellmenu_cancel, 509, code 5); their nav links (left, right,
//!   up, down) in [`nav`];
//! - a slot (fill 0x004a3020, draw 0x004a33d0): a panel from (-96, 0) to (96, -184) around its point (Ui_DrawFrame
//!   0x0046b0d0, colour 0x800a2532, selected 0xe00a2532), the thumbnail, its name ("Game N", ui_savegame_slot%d) 8
//!   below the top, then at 104 / 120 / 136 / 152 below, scale 0.75: the level, the difficulty (ui_diff_%d), the file
//!   date ("%d/%02d/%02d %d:%02d:%02d": month, day, year % 100, hour, minute, second, local time) and the tokens
//!   ("ui_savegame_tokens" of Game+0x200 and 21, 0x005cadd0); an empty slot "ui_savegame_empty", an unreadable one
//!   "ui_savegame_damaged", both at scale 1. Every text gets the selected flag, which UiText_Draw 0x00464750 does not
//!   read (edi unused): the text stays white. The selected slot blinks a prompt (24 of 32 frames) 48 below its top, fitted to 192
//!   (0x004a3300): table 0x005fb920 [save / load][state] (read from the executable): an empty slot "ui_savegame_create"
//!   (saving) or none, a damaged one "ui_savegame_overwrite" / "ui_savegame_delete", a saved one
//!   "ui_savegame_overwrite" / "ui_savegame_load"; its %s is the Select binding (0x00415740 with action 0x1b); it
//!   draws over everything (0x00464430 puts the slot on the HUD's on-top list; here the texts' z);
//! - accept (0x004a36f0): saving, an empty slot is saved (with free disk space: 0x004a36c0 asks 0x00433fa0(0x20);
//!   the remaster does not check), an occupied one asks "ui_yesno_overwrite"; loading, an empty slot gives the cancel
//!   sound, a damaged one asks "ui_yesno_delete", a saved one is loaded. After a save the Saving screen returns to the
//!   pause menu (0x004a3a80).
//!
//! The remaster: the thumbnail is a PNG beside the save (thumbnail.rs); a Delete binding (Delete / Backspace, the pad's
//! West button) deletes any saved slot after "ui_yesno_delete", shown as a hint with the device's own button
//! (the original deletes only damaged slots); the load screen's "Continue" row (the bank's ui_savegame_continue)
//! loads the newest save, the quick save included, in the place of "Continue without saving".

use crate::hud::HudText;
use crate::savegame::Slot;
use bevy::prelude::*;
use bevy::sprite_render::{ColorMaterial, MeshMaterial2d};
use std::time::SystemTime;

/// Draws the save / load screens' slots (gamemenu.rs spawns them and moves the selection).
pub struct SaveSlotsPlugin;

impl Plugin for SaveSlotsPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Update, (draw_slots, draw_thumbs).after(crate::gamemenu::MenuSystems));
    }
}

/// A slot's panel quad.
#[derive(Component)]
pub struct SlotPanel {
    pub mode: Mode,
    pub slot: u8,
}

/// A slot's text: line 0 the blinking prompt, 1 the state / level, 2..=4 difficulty, date, tokens.
#[derive(Component)]
pub struct SlotLine {
    pub mode: Mode,
    pub slot: u8,
    pub line: u8,
}

/// A slot's thumbnail quad (thumbnail.rs makes the pictures).
#[derive(Component)]
pub struct SlotThumb {
    pub mode: Mode,
    pub slot: u8,
}

/// The thumbnails as loaded: per slot, the picture file's time and its image.
#[derive(Default)]
struct Thumbs([Option<(SystemTime, Handle<Image>, f32)>; crate::savegame::SLOTS as usize]);

/// The screen's delete hint (remaster): shown while a saved slot is selected.
#[derive(Component)]
pub struct DeleteHint(pub Mode);

/// The panel's colour (0x004a33d0's statics: 0x800a2532, selected 0xe00a2532).
pub fn panel_colour(selected: bool) -> Color {
    Color::srgba_u8(0x0a, 0x25, 0x32, if selected { 0xe0 } else { 0x80 })
}

/// Fills the open screen's slots from GameMenu::slots: the lines, the selected slot's blinking prompt with the
/// Select binding of the device in use, the panels' colours, the delete hint.
#[allow(clippy::too_many_arguments)]
fn draw_slots(
    menu: Res<crate::gamemenu::GameMenu>,
    bank: Res<crate::gamemenu::Texts>,
    opts: Res<crate::options::GameOptions>,
    time: Res<Time<Real>>,
    mut lines: Query<(&SlotLine, &mut HudText), Without<DeleteHint>>,
    mut hints: Query<(&DeleteHint, &mut HudText), Without<SlotLine>>,
    panels: Query<(&SlotPanel, &MeshMaterial2d<ColorMaterial>)>,
    mut materials: ResMut<Assets<ColorMaterial>>,
) {
    let Some(mode) = menu.slot_mode() else { return };
    let text = |id: &str| bank.get(id);
    let slot = |i: u8| menu.slots.get(i as usize).cloned().unwrap_or(Slot::Empty);
    let set = |t: &mut HudText, s: String| {
        if t.text != s {
            t.text = s;
        }
    };
    for (l, mut t) in &mut lines {
        if l.mode != mode {
            continue;
        }
        let v = view(&slot(l.slot), mode, &text, opts.tokens);
        let s = match l.line {
            0 => match v.prompt {
                Some(id) if menu.slot_sel == Some(l.slot) && prompt_on(time.elapsed_secs()) => {
                    t.fit = 192.0;
                    text(id).replace("%s", &crate::input::key(crate::input::Act::Select))
                }
                _ => String::new(),
            },
            1 => {
                // the empty and damaged texts at scale 1, a saved slot's level at 0.75 like the lines under it
                // (0x004a33d0)
                let scale = if matches!(slot(l.slot), Slot::Saved { .. }) { 0.75 } else { 1.0 };
                if t.scale != scale {
                    t.scale = scale;
                    t.built = None;
                }
                v.status
            }
            n => v.info.get(n as usize - 2).cloned().unwrap_or_default(),
        };
        set(&mut *t, s);
    }
    let deletable = menu.slot_sel.is_some_and(|i| slot(i) != Slot::Empty);
    for (h, mut t) in &mut hints {
        if h.0 == mode {
            set(&mut *t, if deletable { text("ui_savegame_delete").replace("%s", &crate::input::key(crate::input::Act::Delete)) } else { String::new() });
        }
    }
    for (p, m) in &panels {
        if p.mode != mode {
            continue;
        }
        let c = panel_colour(menu.slot_sel == Some(p.slot));
        if materials.get(&m.0).is_some_and(|mat| mat.color != c) {
            if let Some(mut mat) = materials.get_mut(&m.0) {
                mat.color = c;
            }
        }
    }
}

/// The slots' thumbnails (0x004a33d0 draws the slot's texture +0x5a8, loaded from the save by 0x0044f1f0): each saved
/// slot's picture when its file is there, read again when the file changes.
#[allow(clippy::too_many_arguments)]
fn draw_thumbs(
    menu: Res<crate::gamemenu::GameMenu>,
    time: Res<Time<Real>>,
    mut thumbs: Local<Thumbs>,
    mut next_check: Local<f32>,
    mut quads: Query<(&SlotThumb, &MeshMaterial2d<ColorMaterial>, &mut Visibility)>,
    mut materials: ResMut<Assets<ColorMaterial>>,
    mut images: ResMut<Assets<Image>>,
    play: Option<ResMut<crate::playtest::PlayState>>,
) {
    let Some(mode) = menu.slot_mode() else {
        *next_check = 0.0;
        return;
    };
    *next_check -= time.delta_secs();
    if *next_check <= 0.0 {
        *next_check = 0.25;
        for (i, cached) in thumbs.0.iter_mut().enumerate() {
            let path = crate::thumbnail::thumb_path(&crate::savegame::slot_path(i as u8 + 1));
            let saved = !matches!(menu.slots.get(i), Some(Slot::Empty) | None);
            let t = std::fs::metadata(&path).and_then(|m| m.modified()).ok().filter(|_| saved);
            if t == cached.as_ref().map(|c| c.0) {
                continue;
            }
            *cached = t.and_then(|t| {
                let bytes = std::fs::read(&path).ok()?;
                let img = Image::from_buffer(
                    &bytes,
                    bevy::image::ImageType::Extension("png"),
                    bevy::image::CompressedImageFormats::NONE,
                    true,
                    bevy::image::ImageSampler::Default,
                    bevy::asset::RenderAssetUsages::RENDER_WORLD,
                )
                .ok()?;
                // its mean luminance, for the state log (a picture of the scene, not a blank)
                let px = img.data.as_deref().unwrap_or(&[]);
                let n = (px.len() / 4).max(1) as f32;
                let luma = px.chunks_exact(4).map(|p| 0.2126 * p[0] as f32 + 0.7152 * p[1] as f32 + 0.0722 * p[2] as f32).sum::<f32>() / n / 255.0;
                Some((t, images.add(img), luma))
            });
        }
    }
    let mut shown = [false; crate::savegame::SLOTS as usize];
    for (q, m, mut v) in &mut quads {
        if q.mode != mode {
            continue;
        }
        let tex = thumbs.0.get(q.slot as usize).and_then(|c| c.as_ref()).map(|c| c.1.clone());
        let want = if tex.is_some() { Visibility::Inherited } else { Visibility::Hidden };
        if *v != want {
            *v = want;
        }
        shown[q.slot as usize] = tex.is_some();
        if materials.get(&m.0).is_some_and(|mat| mat.texture != tex) {
            if let Some(mut mat) = materials.get_mut(&m.0) {
                mat.texture = tex;
            }
        }
    }
    if let Some(mut p) = play {
        p.0.insert("save_thumbnails".into(), shown.to_vec().into());
        // by slot number ("save_thumbnail_luma.2")
        let luma: serde_json::Map<String, serde_json::Value> =
            thumbs.0.iter().enumerate().map(|(i, c)| ((i + 1).to_string(), c.as_ref().map_or(0.0, |c| (c.2 * 100.0).round() / 100.0).into())).collect();
        p.0.insert("save_thumbnail_luma".into(), luma.into());
    }
}

/// Which screen: SaveGame or LoadGame.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Mode {
    Save,
    Load,
}

/// The slots' grid point in the 640 x 480 layout (y up): slot `i` (0..6) of 0x004a3260.
pub fn slot_point(i: usize) -> (f32, f32) {
    let (q, r) = (i / 3, i % 3);
    let x = 320.0 + (r as f32 - 1.0) * 180.0;
    let y = 276.44 + if q % 2 == 1 { 0.0 } else { 0.3667 * 480.0 };
    (x, y)
}

/// The panel's corners around the slot point (0x004a33d0 / 0x004a37e0: -96, 0 .. 96, -184).
pub const PANEL: ((f32, f32), (f32, f32)) = ((-96.0, -184.0), (96.0, 0.0));
/// The thumbnail's centre below the slot point (0x004a33d0: from q - 70 to -70 - q).
pub const THUMB_Y: f32 = -70.0;
/// The y offsets below the slot point: name, prompt, then the four info lines (0x004a33d0 / 0x004a3300).
pub const NAME_Y: f32 = -8.0;
pub const PROMPT_Y: f32 = -48.0;
pub const LINE_Y: [f32; 4] = [-104.0, -120.0, -136.0, -152.0];
/// The rows after the slots: "Continue without saving" (load screen: "Continue") and "Cancel".
pub const NOSAVE: usize = 6;
pub const CANCEL: usize = 7;
/// The total of tokens (0x005cadd0 = 21 in the executable's data).
pub const TOKENS_TOTAL: u32 = 21;

/// Where the selection goes from row `from` (0..5 the slots, NOSAVE, CANCEL) in direction (x, y), y up: the items'
/// nav links (layout.json pui_savegame: 7200 right 7201 down 7203; 7203 up 7200 down 7308; 7204 up 7201; 7308 up
/// 7204 down 509), none past the edges.
pub fn nav(from: usize, dx: i32, dy: i32) -> Option<usize> {
    match (from, dx, dy) {
        (s @ 0..=5, 1, 0) if s % 3 < 2 => Some(s + 1),
        (s @ 0..=5, -1, 0) if s % 3 > 0 => Some(s - 1),
        (s @ 3..=5, 0, 1) => Some(s - 3),
        (s @ 0..=2, 0, -1) => Some(s + 3),
        (3..=5, 0, -1) => Some(NOSAVE),
        (NOSAVE, 0, 1) => Some(4),
        (NOSAVE, 0, -1) => Some(CANCEL),
        (CANCEL, 0, 1) => Some(NOSAVE),
        _ => None,
    }
}

/// The text ids a slot shows: its state line, the blinking prompt (table 0x005fb920), and the info lines' content.
#[derive(Clone, Debug, PartialEq)]
pub struct SlotView {
    /// the line 104 below the top: the level's name, or the empty / damaged text id
    pub status: String,
    /// difficulty, date, tokens (saved slots only)
    pub info: [String; 3],
    pub prompt: Option<&'static str>,
}

/// What slot `slot` shows on the `mode` screen; `text` looks up the bank, `tokens` is the profile's count (Game+0x200).
pub fn view(slot: &Slot, mode: Mode, text: &dyn Fn(&str) -> String, tokens: u32) -> SlotView {
    match slot {
        Slot::Empty => SlotView { status: text("ui_savegame_empty"), info: Default::default(), prompt: (mode == Mode::Save).then_some("ui_savegame_create") },
        Slot::Damaged => SlotView {
            status: text("ui_savegame_damaged"),
            info: Default::default(),
            prompt: Some(if mode == Mode::Save { "ui_savegame_overwrite" } else { "ui_savegame_delete" }),
        },
        Slot::Saved { level, difficulty, written } => SlotView {
            status: text(level),
            info: [text(&format!("ui_diff_{}", difficulty + 1)), date_text(*written), text("ui_savegame_tokens").replacen("%d", &tokens.to_string(), 1).replacen("%d", &TOKENS_TOTAL.to_string(), 1)],
            prompt: Some(if mode == Mode::Save { "ui_savegame_overwrite" } else { "ui_savegame_load" }),
        },
    }
}

/// The blinking prompt is on 24 of every 32 frames (0x004a33d0's counter, at 60 frames a second).
pub fn prompt_on(seconds: f32) -> bool {
    ((seconds * 60.0) as u32 & 0x1f) < 0x18
}

/// The slot the screen opens on (0x004a3020 / 0x004a3b60): saving, the first empty slot; loading, the newest save;
/// else the first.
pub fn default_slot(slots: &[Slot], mode: Mode) -> usize {
    match mode {
        Mode::Save => slots.iter().position(|s| *s == Slot::Empty).unwrap_or(0),
        Mode::Load => slots
            .iter()
            .enumerate()
            .filter_map(|(i, s)| match s {
                Slot::Saved { written, .. } => Some((i, *written)),
                _ => None,
            })
            .max_by_key(|(i, w)| (*w, std::cmp::Reverse(*i)))
            .map_or(0, |(i, _)| i),
    }
}

/// A file time as the slot shows it: "%d/%02d/%02d %d:%02d:%02d" (month, day, year % 100, hour, minute, second) in
/// local time (0x004a3020: FileTimeToLocalFileTime, FileTimeToSystemTime).
pub fn date_text(t: SystemTime) -> String {
    let secs = t.duration_since(SystemTime::UNIX_EPOCH).map(|d| d.as_secs() as i64).unwrap_or(0);
    let (y, mo, d, h, mi, s) = civil(secs + local_offset(secs));
    format!("{mo}/{d:02}/{:02} {h}:{mi:02}:{s:02}", y.rem_euclid(100))
}

/// Seconds since 1970 (UTC) as (year, month, day, hour, minute, second) of the proleptic Gregorian calendar.
pub fn civil(secs: i64) -> (i64, u32, u32, u32, u32, u32) {
    let days = secs.div_euclid(86400);
    let rem = secs.rem_euclid(86400);
    // days to civil (H. Hinnant's algorithm)
    let z = days + 719468;
    let era = z.div_euclid(146097);
    let doe = z.rem_euclid(146097);
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = (doy - (153 * mp + 2) / 5 + 1) as u32;
    let m = if mp < 10 { mp + 3 } else { mp - 9 } as u32;
    let y = yoe + era * 400 + if m <= 2 { 1 } else { 0 };
    (y, m, d, (rem / 3600) as u32, (rem / 60 % 60) as u32, (rem % 60) as u32)
}

/// The local time zone's offset from UTC at `secs` (as FileTimeToLocalFileTime gives it); 0 off Windows.
#[cfg(windows)]
fn local_offset(secs: i64) -> i64 {
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn FileTimeToLocalFileTime(file: *const [u32; 2], local: *mut [u32; 2]) -> i32;
    }
    // FILETIME: 100 ns ticks since 1601
    let ticks = ((secs + 11_644_473_600) as u64) * 10_000_000;
    let ft = [ticks as u32, (ticks >> 32) as u32];
    let mut local = [0u32; 2];
    // SAFETY: both pointers are to live 8-byte FILETIME-shaped arrays
    if unsafe { FileTimeToLocalFileTime(&ft, &mut local) } == 0 {
        return 0;
    }
    let l = (local[0] as u64 | (local[1] as u64) << 32) as i64;
    (l - ticks as i64) / 10_000_000
}

#[cfg(not(windows))]
fn local_offset(_secs: i64) -> i64 {
    0
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    /// The grid of 0x004a3260: columns 140 / 320 / 500, the top row 176 above the bottom one (at ref_info's 276.44).
    #[test]
    fn grid() {
        let (x, y) = slot_point(0);
        assert!(x == 140.0 && (y - 452.456).abs() < 1e-3, "{x} {y}");
        assert_eq!(slot_point(4), (320.0, 276.44));
        assert_eq!(slot_point(5).0, 500.0);
    }

    /// The nav links of layout.json's pui_savegame items.
    #[test]
    fn links() {
        assert_eq!(nav(0, 1, 0), Some(1));
        assert_eq!(nav(2, 1, 0), None, "no wrap at the right edge");
        assert_eq!(nav(0, 0, -1), Some(3));
        assert_eq!(nav(0, 0, 1), None);
        assert_eq!(nav(5, 0, -1), Some(NOSAVE), "7205 down 7308");
        assert_eq!(nav(NOSAVE, 0, 1), Some(4), "7308 up 7204");
        assert_eq!(nav(NOSAVE, 0, -1), Some(CANCEL));
        assert_eq!(nav(CANCEL, 0, 1), Some(NOSAVE));
        assert_eq!(nav(CANCEL, 1, 0), None);
    }

    /// Table 0x005fb920 by screen and slot state; the info lines of a saved slot.
    #[test]
    fn views() {
        let t = |id: &str| format!("[{id}]");
        let at = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
        let saved = Slot::Saved { level: "lev_1_m1".into(), difficulty: 1, written: at };
        assert_eq!(view(&Slot::Empty, Mode::Save, &t, 0).prompt, Some("ui_savegame_create"));
        assert_eq!(view(&Slot::Empty, Mode::Load, &t, 0).prompt, None);
        assert_eq!(view(&Slot::Damaged, Mode::Save, &t, 0).prompt, Some("ui_savegame_overwrite"));
        assert_eq!(view(&Slot::Damaged, Mode::Load, &t, 0).prompt, Some("ui_savegame_delete"));
        assert_eq!(view(&saved, Mode::Save, &t, 0).prompt, Some("ui_savegame_overwrite"));
        let v = view(&saved, Mode::Load, &|id: &str| if id == "ui_savegame_tokens" { "Tokens: %d/%d".into() } else { t(id) }, 3);
        assert_eq!(v.prompt, Some("ui_savegame_load"));
        assert_eq!(v.status, "[lev_1_m1]");
        assert_eq!(v.info[0], "[ui_diff_2]");
        assert_eq!(v.info[2], "Tokens: 3/21");
        assert_eq!(view(&Slot::Empty, Mode::Save, &t, 0).status, "[ui_savegame_empty]");
    }

    /// Saving opens on the first empty slot, loading on the newest save.
    #[test]
    fn default_selection() {
        let at = |s: u64| Slot::Saved { level: String::new(), difficulty: 1, written: SystemTime::UNIX_EPOCH + Duration::from_secs(s) };
        let slots = vec![at(10), Slot::Empty, at(30), Slot::Damaged, at(20), Slot::Empty];
        assert_eq!(default_slot(&slots, Mode::Save), 1);
        assert_eq!(default_slot(&slots, Mode::Load), 2);
        assert_eq!(default_slot(&vec![Slot::Empty; 6], Mode::Load), 0);
    }

    /// The calendar and the original's date format (local time aside).
    #[test]
    fn dates() {
        assert_eq!(civil(0), (1970, 1, 1, 0, 0, 0));
        assert_eq!(civil(951_782_400), (2000, 2, 29, 0, 0, 0));
        assert_eq!(civil(1_700_000_000), (2023, 11, 14, 22, 13, 20));
        assert!(prompt_on(0.0) && !prompt_on(24.0 / 60.0 + 0.001) && prompt_on(32.0 / 60.0 + 0.001));
    }
}
