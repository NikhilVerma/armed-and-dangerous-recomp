//! The pub screen (page pui_pub; opened by 0x00467fe0, docs/subsystems/mission.md 6, run every frame by its input
//! handler 0x00493130 -> 0x004695d0): entering the pub pauses the game; Roman's health is topped up from the pub's
//! pool (min(max, health + pool), the pool keeps the rest) and the two bars run to the new values; the awards are
//! presented one by one; when the pub sells weapons Roman does not hold, "New weapons are available!", then the
//! cursor walks the slots and Select takes the weapon under it; Back (Esc) leaves ("Press %s to exit pub") and the
//! pub's exit trigger is sent. The slots, bars, labels and art are the bank's (hud.rs spawn_pub).
//!
//! The update 0x004695d0, in order:
//! - The bars (+0x14c pool, +0x150 health): the shown pool runs down to the pool left at health_bar_rate x 0.01 x the
//!   pool's full scale per second, the shown health up to the new health at that rate x max health; each bar shows its
//!   value over its full scale (the pool's: health_pool at load, pub +0x470).
//! - The awards (states 0..9 of +0xa8; the ticker bombs first, then the pub's special weapon when Roman lacks it):
//!   the award's big icon is placed in the middle (anchored on ref_pub_interior, 0x00463d20), grows to 3x over 0.15 s (1 + 2 t / 0.15), its
//!   line shows; Select hides the line, the icon shrinks back over 0.15 s and moves to its slot (the ticker bombs' slot,
//!   the first free special slot) over 0.1 s, where the item's icon then shows. Input is read only while the line
//!   is up (Select or Back).
//! - "New weapons are available!" (+0xbc): Select puts the cursor on the first pub slot and shows "Select the weapon
//!   you want and press %s".
//! - Choosing: the direction keys move the cursor along the slots' nav links (the bank's left / right / up / down
//!   record ids, 0x00463b90); otherwise the pointer moves it to the slot under it (0x00463a00; each slot's box is
//!   +-0.08 x +-0.07 of the screen, 0x00467f50, none for the ticker bombs' slot). A move plays ui_pub_move_1 (same
//!   kind of slot) or ui_pub_move_2 and glides the cursor over 0.1 s (0x00464c60 / 0x00464c90). The weapon info
//!   (pub_info_*) is the item in the selected slot's. Select on a pub slot holding a weapon Roman lacks takes it into
//!   his first free weapon slot (ui_pub_pickup; the icon glides there). Back leaves unless the cursor is on the ticker
//!   bombs' slot.
//!
//! Remaster additions, labelled: a pointer button on the exit prompt (a click leaves), a click away from the slots
//! takes nothing, and a click with nothing left to take leaves (player feedback 17: a trackpad player was stuck; the
//! keys' Select does not leave: the original leaves only on Back, oracle scenario pub); a
//! taken weapon goes into Roman's hands (the original's capture topsyO shows the machine gun after the pub). Guesses:
//! the dark backdrop behind the pub art (hud.rs). Not ported: weapons other than the rifle and the machine gun, and
//! the carry / swap when Roman's three weapon slots are full (0x004695d0's +0x74; lev_1_m1's Roman holds two at
//! most), the Topsy Turvy bomb's throw.

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::fx::Health;
use crate::gamemenu::{GameMenu, Page, Texts};
use crate::hud::{HudElement, PubSlot, PubSlotKind, PubView};
use crate::player::Roman;
use bevy::platform::collections::HashMap;
use bevy::prelude::*;

/// The award icon's grow and shrink time (0x005bcedc; the scale 1 + 2 x t x 6.67, 0x005bcf30) and its size while
/// its line is up.
const AWARD_RAMP: f32 = 0.15;
const AWARD_SCALE: f32 = 3.0;
/// A move between two slots (0x00464c90: t = (now - start) x 10, ended at 1).
const MOVE_SECS: f32 = 0.1;
/// The half size of a slot's pointer box (0x00467f50: 0.08 x 640, 0.07 x 480).
const SLOT_HIT: Vec2 = Vec2::new(51.2, 33.6);
/// Where the award icon sits (ref_pub_interior) and the exit prompt (ref_text_exit).
const CENTRE: Vec2 = Vec2::new(320.0, 240.0);
const EXIT_POINT: Vec2 = Vec2::new(493.072, 416.366);

/// A slot: its kind and its place among the slots of that kind.
type SlotRef = (PubSlotKind, usize);

#[derive(Clone, Copy, PartialEq, Debug)]
enum Stage {
    Grow,
    Wait,
    Shrink,
    Glide,
}

#[derive(Clone, Debug)]
struct Award {
    /// the award's name (ui_pub_award_<item>, its line pub_award_<item>)
    item: &'static str,
    stage: Stage,
    /// seconds into the stage
    t: f32,
}

#[derive(Resource, Default)]
pub struct PubScreen {
    open: bool,
    /// the awards still to present (the first is on screen); their items' icons wait until they land
    awards: Vec<Award>,
    /// "New weapons are available!" waits for Select (+0xbc)
    notify_new: bool,
    /// "Select the weapon you want and press %s" (+0xc4, shown once the notice is taken)
    select_shown: bool,
    /// the pub's inventory, per pub slot (+0x58..+0x68 markers' items)
    shop: Vec<Option<&'static str>>,
    /// the selected slot (+0x70), the cursor's glide from (position, seconds) and the remaster's exit button
    cursor: Option<SlotRef>,
    glide: Option<(Vec2, f32)>,
    on_exit: bool,
    /// a taken weapon's icon gliding from its pub slot (from, seconds)
    taken: Option<(Vec2, f32)>,
    /// the bars: shown and target values, full scales, rate (fraction of the full scale per second)
    pool: (f32, f32, f32),
    health: (f32, f32, f32),
    rate: f32,
    /// presentation steps taken (awards and the notice), for the state log
    steps: usize,
    /// left: the screen fades out (gamemenu.rs), the game paused under it; the exit happens when it is gone
    closing: bool,
}

pub struct PubScreenPlugin;

impl Plugin for PubScreenPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<PubScreen>().add_systems(Update, (open, input, leave_pub, view).chain());
    }
}

fn icon(item: &str) -> &'static str {
    match item {
        "rifle" => "k_player_rifle",
        "machinegun" => "k_player_mg",
        "snipergun" => "k_player_sniper",
        "rlgurner" => "k_player_rlgurner",
        "sharkgun" => "k_player_landshark",
        "rlvindaloo" => "k_player_rlvindaloo",
        "topsy" => "k_player_topsyturvybomb",
        "stickybomb" => "k_player_stickybomb",
        "tickerbomb" => "k_player_tickerbomb",
        _ => "k_player_stickybomb",
    }
}

fn info_id(item: &str) -> String {
    match item {
        "topsy" => "pub_info_topsyturvy".into(),
        "stickybomb" => "pub_info_sticky".into(),
        w => format!("pub_info_{w}"),
    }
}

fn held(r: &Roman, w: &str) -> bool {
    match w {
        "rifle" => true,
        "machinegun" => r.mg.is_some(),
        _ => false,
    }
}

/// The items in Roman's slots: his weapons, his specials, the ticker bombs (an item still being awarded is not in
/// its slot yet).
fn roman_items(r: &Roman, tickers: u32, awarding: &[Award]) -> Vec<(SlotRef, &'static str)> {
    let pending = |i: &str| awarding.iter().any(|a| a.item == i);
    let mut out = vec![((PubSlotKind::Weapon, 0), "rifle")];
    if r.mg.is_some() {
        out.push(((PubSlotKind::Weapon, 1), "machinegun"));
    }
    let mut n = 0;
    for (have, s) in [(r.stickies > 0, "stickybomb"), (r.topsy > 0, "topsy")] {
        if have && !pending(s) {
            out.push(((PubSlotKind::Special, n), s));
            n += 1;
        }
    }
    if tickers > 0 && !pending("tickerbomb") {
        out.push(((PubSlotKind::Bombs, 0), "tickerbomb"));
    }
    out
}

/// Where an award's icon lands: the ticker bombs' slot, or the first free special slot (0x004695d0 states 3 / 8).
fn award_slot(item: &str, r: &Roman, awarding: &[Award]) -> SlotRef {
    if item == "tickerbomb" {
        return (PubSlotKind::Bombs, 0);
    }
    let taken = roman_items(r, 0, awarding).iter().filter(|(s, _)| s.0 == PubSlotKind::Special).count();
    (PubSlotKind::Special, taken)
}

/// Opens the screen when the player enters the pub (0x00467fe0): health from the pool, the awards.
#[allow(clippy::too_many_arguments)]
fn open(
    state: Res<State<AppState>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut screen: ResMut<PubScreen>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut roman: Query<(&mut Roman, &mut Health)>,
    mut vtime: ResMut<Time<Virtual>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    data: Res<crate::gamedata::GameData>,
    (mut intro_said, mut rng): (crate::levelreset::PerLevel<bool>, ResMut<crate::fx::FxStats>),
) {
    let Some(m) = mission.as_deref_mut() else { return };
    if !m.pub_request || *state.get() != AppState::Playing {
        return;
    }
    m.pub_request = false;
    let Ok((mut r, mut h)) = roman.single_mut() else { return };
    let info = m.pub_info.clone();
    let mut awards = Vec::new();
    let (mut pool, mut rate) = ((0.0, 0.0, 1.0), 0.5);
    let health_before = h.0;
    if let Some(p) = info.as_ref() {
        // health: min(max, health + pool); the pool keeps what was not used
        let want = (r.max_health - h.0).max(0.0);
        let used = want.min(p.pool);
        h.0 += used;
        if let Some(pi) = m.pub_info.as_mut() {
            pi.pool -= used;
        }
        pool = (p.pool, p.pool - used, p.pool_full.max(1.0));
        rate = p.bar_rate;
        if p.has_tickerbomb && m.tickers == 0 {
            m.tickers = 4;
            awards.push(Award { item: "tickerbomb", stage: Stage::Grow, t: 0.0 });
        }
        if p.special == Some("topsy") && r.topsy == 0 {
            r.topsy = data.props("p_globalsettings").i("topsyturvy_max_ammo", 1) as u32;
            awards.push(Award { item: "topsy", stage: Stage::Grow, t: 0.0 });
        }
    }
    // the machine gun Roman holds is filled (should_refill_ammo)
    if let Some(w) = r.mg.as_mut() {
        let full = crate::player::machinegun(&data);
        w.ammo = full.ammo;
        w.clip = full.clip;
    }
    // the weapons sold that Roman does not hold go into the pub slots in order (0x00469350)
    let shop: Vec<Option<&'static str>> = info.map(|p| p.sells.into_iter().filter(|w| !held(&r, w)).map(Some).collect()).unwrap_or_default();
    let notify_new = !shop.is_empty();
    *screen = PubScreen {
        open: true,
        awards,
        notify_new,
        select_shown: false,
        shop,
        // the cursor starts on Roman's first weapon slot (+0x70 = marker 0, ref_weapon_01)
        cursor: Some((PubSlotKind::Weapon, 0)),
        glide: None,
        on_exit: false,
        taken: None,
        pool,
        health: (health_before, h.0, r.max_health.max(1.0)),
        rate,
        steps: 0,
        closing: false,
    };
    m.log.push(format!("pub screen: awards {:?}, offer {:?}", screen.awards.iter().map(|a| a.item).collect::<Vec<_>>(), screen.shop));
    menu.page = Page::Pub;
    next.set(AppState::Paused);
    vtime.pause();
    sounds.write(crate::audio::PlaySound::ui("ui_pub_enter"));
    // the pub's sounds (0x00467fe0): the busy loop amb_pub_busy and a line (Pub_PlayAmbientLine 0x00468dd0): the
    // intro once (Game+0x574), then one of the fifteen general lines (the extras +0x144.. and the once lines for new
    // weapons +0x141.. are not set up in lev_1_m1: guess)
    sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::ui("amb_pub_busy") });
    const LINES: [&str; 15] = [
        "PE150014_Pub_Ambient", "PE150025_Pub_Ambient", "PE150056_Pub_Ambient", "PE150057_Pub_Ambient", "PE150059_Pub_Ambient",
        "PE150060_Pub_Ambient", "PE250014_Pub_Ambient", "PE250015_Pub_Ambient", "PE250016_Pub_Ambient", "PE250017_Pub_Ambient",
        "PE250018_Pub_Ambient", "PE250051_Pub_Ambient", "JON50081_Ambient", "JON50045_Ambient", "ROM50169_Pub",
    ];
    let intro_said = intro_said.get();
    let line = if !*intro_said {
        *intro_said = true;
        "PE150013_Pub_Once_Intro"
    } else {
        LINES[((rng.rand() * 15.0) as usize).min(14)]
    };
    sounds.write(crate::audio::PlaySound::ui(line));
}

/// The slots of the page: position and nav links by slot.
fn slot_map(slots: &Query<(&PubSlot, &HudElement)>) -> HashMap<SlotRef, (Vec2, [i32; 5])> {
    slots.iter().map(|(s, e)| ((s.kind, s.order), (e.pos, s.nav))).collect()
}

/// The pub screen's update (0x004695d0): the bars, the awards, the notice, the cursor, taking a weapon, leaving.
#[allow(clippy::too_many_arguments)]
fn input(
    time: Res<Time<Real>>,
    ctl: Res<crate::input::Controls>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut screen: ResMut<PubScreen>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut roman: Query<&mut Roman>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    data: Res<crate::gamedata::GameData>,
    mouse: Res<ButtonInput<MouseButton>>,
    mut stop: MessageWriter<crate::audio::StopSound>,
    (windows, slots, mut last): (Query<&Window>, Query<(&PubSlot, &HudElement)>, Local<Option<Vec2>>),
) {
    if !screen.open || menu.page != Page::Pub {
        return;
    }
    // the screen runs while the game is paused: real time (the original's frame time 0x00604a90)
    let dt = time.delta_secs().min(0.1);
    let s = &mut *screen;
    // the bars run to their targets
    s.pool.0 = (s.pool.0 - s.rate * s.pool.2 * dt).max(s.pool.1);
    s.health.0 = (s.health.0 + s.rate * s.health.2 * dt).min(s.health.1);
    // the moves (0x00464c90)
    for g in [&mut s.glide, &mut s.taken] {
        if let Some((_, t)) = g {
            *t += dt;
            if *t >= MOVE_SECS {
                *g = None;
            }
        }
    }
    let map = slot_map(&slots);
    // the pointer (the pub screen shows the mouse cursor: 0x004930c0 sets the HUD's +0x50), mapped to the layout (the
    // menus' height / 480 about the centre): the slot under it, or the exit prompt (remaster)
    let mut pointed: Option<SlotRef> = None;
    let mut pointer_exit = false;
    let mut moved = false;
    if let Some((w, c)) = windows.single().ok().and_then(|w| w.cursor_position().map(|c| (w, c))) {
        let k = w.height() / 480.0;
        let p = Vec2::new(320.0 + (c.x - w.width() / 2.0) / k, 240.0 - (c.y - w.height() / 2.0) / k);
        moved = last.is_some_and(|l| l.distance(c) > 0.5);
        *last = Some(c);
        pointed = map.iter().find(|(r, (pos, _))| r.0 != PubSlotKind::Bombs && (p - *pos).abs().cmple(SLOT_HIT).all()).map(|(r, _)| *r);
        pointer_exit = pointed.is_none() && (p - EXIT_POINT).abs().cmple(Vec2::new(60.0, 20.0)).all();
    }
    let click = mouse.just_pressed(MouseButton::Left);
    let select = ctl.pressed(crate::input::Act::Select) || click;
    let back = ctl.pressed(crate::input::Act::Back);
    let mut leave = false;

    if let Some(a) = s.awards.first_mut() {
        // the award (states 1..4 / 6..9): grow, wait for Select or Back, shrink, move to the slot
        match a.stage {
            Stage::Grow | Stage::Shrink => {
                a.t += dt;
                if a.t > AWARD_RAMP {
                    a.stage = if a.stage == Stage::Grow { Stage::Wait } else { Stage::Glide };
                    a.t = 0.0;
                }
            }
            Stage::Wait => {
                if select {
                    a.stage = Stage::Shrink;
                    a.t = 0.0;
                    s.steps += 1;
                }
                leave = back;
            }
            Stage::Glide => {
                a.t += dt;
                if a.t >= MOVE_SECS {
                    s.awards.remove(0);
                }
            }
        }
    } else if s.notify_new {
        // the notice (+0xbc): Select moves the cursor to the first pub slot (0x00464b10: no glide)
        if select {
            s.notify_new = false;
            s.select_shown = true;
            s.cursor = Some((PubSlotKind::Shop, 0));
            s.glide = None;
            s.steps += 1;
        } else {
            leave = back;
        }
    } else {
        // choosing: a direction key follows the selected slot's nav link to the slot with that record id
        let link = if ctl.step.x < 0 {
            Some(1)
        } else if ctl.step.x > 0 {
            Some(2)
        } else if ctl.step.y > 0 {
            Some(3)
        } else if ctl.step.y < 0 {
            Some(4)
        } else {
            None
        };
        let from = s.cursor.and_then(|c| map.get(&c)).copied();
        let mut target = None;
        if let (Some(l), Some((_, nav))) = (link, from) {
            let id = nav[l];
            target = (id >= 0).then(|| map.iter().find(|(_, (_, n))| n[0] == id).map(|(r, _)| *r)).flatten();
            s.on_exit = false;
        } else if link.is_none() && (moved || click) {
            // the pointer: the slot under it unless it is the selected one; the exit prompt (remaster)
            if let Some(p) = pointed.filter(|p| Some(*p) != s.cursor || s.on_exit) {
                target = Some(p);
            } else if pointer_exit && !s.on_exit {
                s.on_exit = true;
                sounds.write(crate::audio::PlaySound::ui("ui_pub_move_2"));
            } else if pointed.is_none() && !pointer_exit && moved {
                s.on_exit = false;
            }
        }
        if let Some(t) = target {
            let same = s.cursor.is_some_and(|c| c.0 == t.0);
            sounds.write(crate::audio::PlaySound::ui(if same { "ui_pub_move_1" } else { "ui_pub_move_2" }));
            let at = if s.on_exit { Some(EXIT_POINT) } else { from.map(|f| f.0) };
            s.glide = at.map(|p| (p, 0.0));
            s.cursor = Some(t);
            s.on_exit = false;
        }
        let nothing_left = s.shop.iter().all(|i| i.is_none());
        // Select on a pub slot holding a weapon Roman lacks: into his first free weapon slot (ui_pub_pickup); a click
        // must be on a slot (remaster: a click elsewhere takes nothing)
        let take = select && !s.on_exit && (!click || pointed.is_some());
        if take {
            if let Some((PubSlotKind::Shop, i)) = s.cursor {
                if let (Some(Some(w)), Ok(mut r)) = (s.shop.get(i).copied(), roman.single_mut()) {
                    if w == "machinegun" && r.mg.is_none() {
                        r.mg = Some(crate::player::machinegun(&data));
                        // remaster: a taken weapon goes into his hands (the original's capture topsyO: after the pub
                        // the HUD shows the machine gun, 100/1000)
                        r.use_mg = true;
                        s.shop[i] = None;
                        s.taken = map.get(&(PubSlotKind::Shop, i)).map(|(p, _)| (*p, 0.0));
                        sounds.write(crate::audio::PlaySound::ui("ui_pub_pickup"));
                        if let Some(m) = mission.as_deref_mut() {
                            m.log.push("pub: took the machinegun".into());
                        }
                    }
                }
            }
        }
        // Back leaves except on the ticker bombs' slot; the exit prompt and a click with nothing left (remaster, for
        // the pointer: the keys' Select stays on the screen as in the original, which leaves only on Back)
        let on_bombs = s.cursor.is_some_and(|c| c.0 == PubSlotKind::Bombs);
        leave = (back && !on_bombs) || (select && s.on_exit) || (click && nothing_left);
    }
    if leave {
        // the page closes through the menus' fade (gamemenu.rs instant: 0.5 s out); the screen's hide (0x004930c0 ->
        // 0x004690c0) sends the exit trigger when it is gone (leave_pub)
        s.open = false;
        s.on_exit = false;
        s.closing = true;
        menu.page = Page::Main;
        next.set(AppState::Playing);
        sounds.write(crate::audio::PlaySound::ui("ui_pub_exit"));
        stop.write(crate::audio::StopSound("amb_pub_busy".into()));
    }
}

/// The pub screen hidden (0x004930c0 with show 0 -> 0x004690c0) once its fade-out has run: the exit trigger
/// (0x0046a720) and the game running again, Roman where he entered (the exit bone's position 0x0046a650 is taken and
/// dropped). The original's trace: Esc at play frame 440, trigger 900 and the unpause at 472, Roman not moved.
fn leave_pub(menu: Res<GameMenu>, mut screen: ResMut<PubScreen>, mut mission: Option<ResMut<crate::mission::Mission>>, mut vtime: ResMut<Time<Virtual>>) {
    if !screen.closing || menu.on_screen() == Some(Page::Pub) {
        return;
    }
    screen.closing = false;
    vtime.unpause();
    if let Some(m) = mission.as_deref_mut() {
        m.pub_exit_pending = true;
    }
}

/// Fills the view hud.rs draws.
fn view(
    screen: Res<PubScreen>,
    mut v: ResMut<PubView>,
    texts: Res<Texts>,
    roman: Query<&Roman>,
    mission: Option<Res<crate::mission::Mission>>,
    play: Option<ResMut<crate::playtest::PlayState>>,
    slots: Query<(&PubSlot, &HudElement)>,
) {
    let s = &*screen;
    // the state log's "pub" (playtest expect steps)
    if let Some(mut p) = play {
        let award = s.awards.first().map(|a| format!("{} {:?}", a.item, a.stage)).unwrap_or_default();
        p.0.insert(
            "pub".into(),
            serde_json::json!({ "open": s.open, "step": s.steps, "cursor": s.cursor.map(|c| format!("{:?} {}", c.0, c.1)), "on_exit": s.on_exit,
                "offer": s.shop.iter().flatten().count(), "exit_text": v.exit_text.clone(), "award": award, "notify": s.notify_new,
                "pool": s.pool.0, "health": s.health.0, "info": v.info.clone() }),
        );
    }
    if !s.open {
        if v.open {
            v.open = false;
        }
        return;
    }
    let Ok(r) = roman.single() else { return };
    let map = slot_map(&slots);
    let at = |slot: SlotRef| map.get(&slot).map(|(p, _)| *p).unwrap_or(CENTRE);
    let lerp = |from: Vec2, to: Vec2, t: f32| from + (to - from) * (t / MOVE_SECS).clamp(0.0, 1.0);
    let key = |s: String, k: &str| s.replace("%s", k);
    let select_key = crate::input::key(crate::input::Act::Select);
    let award = s.awards.first();
    let choosing = award.is_none() && !s.notify_new;
    // the line: the award's while it waits, the notice, then the selection prompt (+0xb0 / +0xb8, +0xc0, +0xc4)
    let message = match award {
        Some(a) if a.stage == Stage::Wait => key(texts.get(&format!("pub_award_{}", a.item)), &select_key),
        Some(_) => String::new(),
        None if s.notify_new => key(texts.get("pub_notify_new"), &select_key),
        None if s.select_shown => key(texts.get("pub_notify_select"), &select_key),
        None => String::new(),
    };
    // icons: the pub's inventory, Roman's weapons, specials and ticker bombs; a taken weapon glides in
    let tickers = mission.as_ref().map_or(0, |m| m.tickers);
    let mut items: Vec<(SlotRef, &'static str)> = s.shop.iter().enumerate().filter_map(|(i, w)| w.map(|w| ((PubSlotKind::Shop, i), w))).collect();
    items.extend(roman_items(r, tickers, &s.awards));
    let icons: Vec<(Vec2, &'static str)> = items
        .iter()
        .map(|(slot, item)| {
            let mut p = at(*slot);
            if let (Some((from, t)), PubSlotKind::Weapon, "machinegun") = (s.taken, slot.0, *item) {
                p = lerp(from, p, t);
            }
            (p, icon(item))
        })
        .collect();
    // the weapon info: the item in the selected slot (the 0x1c info texts of +0xd0, one shown)
    let info = if choosing && !s.on_exit { s.cursor.and_then(|c| items.iter().find(|(slot, _)| *slot == c)).map(|(_, i)| texts.get(&info_id(i))).unwrap_or_default() } else { String::new() };
    let cursor_to = if s.on_exit { EXIT_POINT } else { s.cursor.map_or(EXIT_POINT, at) };
    v.cursor = s.glide.map_or(cursor_to, |(from, t)| lerp(from, cursor_to, t));
    // the award's icon: 1 -> 3x and back over 0.15 s in the middle, then to its slot over 0.1 s
    if let Some(a) = award {
        let k = (a.t / AWARD_RAMP).clamp(0.0, 1.0);
        v.award_scale = match a.stage {
            Stage::Grow => 1.0 + (AWARD_SCALE - 1.0) * k,
            Stage::Wait => AWARD_SCALE,
            Stage::Shrink => 1.0 + (AWARD_SCALE - 1.0) * (1.0 - k),
            Stage::Glide => 1.0,
        };
        v.award_pos = if a.stage == Stage::Glide { lerp(CENTRE, at(award_slot(a.item, r, &s.awards)), a.t) } else { CENTRE };
    }
    let award_name = award.map(|a| a.item.to_owned()).unwrap_or_default();
    // the exit prompt names Back on the device in use (input.rs: Esc, or the pad's button); shown from the start
    let exit_text = key(texts.get("pub_notify_exit"), &crate::input::key(crate::input::Act::Back));
    let changed = !v.open || v.icons.len() != icons.len() || v.icons.iter().zip(&icons).any(|(a, b)| a.1 != b.1);
    v.open = true;
    v.award = award_name;
    v.message = message;
    v.info = info;
    v.exit_text = exit_text;
    v.health = s.health.0 / s.health.2;
    v.pool = s.pool.0 / s.pool.2;
    v.icons = icons;
    if changed {
        v.version += 1;
    }
}
