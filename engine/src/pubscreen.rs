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
//! taken weapon goes into Roman's hands (the original's capture topsyO shows the machine gun after the pub); a carried
//! weapon's icon rides the cursor. Guesses: the dark backdrop behind the pub art (hud.rs).
//!
//! The swap (0x004695d0's +0x74, 0x004a0f70 / 0x004a10c0; feature weapon-swap): Roman carries three guns. Select on a
//! pub weapon with all three slots full picks it up (carried) and puts the cursor on his third slot, and "Select the
//! weapon to replace" takes the select prompt's place; while carrying, the pointer moves the cursor only onto his weapon
//! slots (the keys go anywhere); Select on one of his slots then swaps the two (ui_pub_putdown): the new gun takes the
//! slot and is equipped, the old one goes back into the pub's slot. Back on his slots while carrying puts the carried
//! weapon back.
//!
//! On entry (0x00467fe0) every gun and special Roman holds is topped up where the pub refills, and his living
//! companions are healed in full. The line the pub opens with (0x00468dd0, [`PubLines::choose`]): the intro, the
//! once-a-campaign lines for the sniper rifle and the Vindaloo on offer, or a random general or level line.

use crate::levelreset::LevelAppExt;
use crate::AppState;
use crate::fx::Health;
use crate::gamemenu::{GameMenu, Page, Texts};
use crate::hud::{HudElement, PubSlot, PubSlotKind, PubView};
use crate::player::Roman;
use crate::weapons::{Gun, GunSpec, SpecialId, WeaponId};
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
/// The pub's slots for its weapons (the five +0x58 markers, ref_slot_01..05).
const SHOP_SLOTS: usize = 5;
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
    /// a taken weapon's icon gliding from its pub slot (from, seconds), and the slot it went to
    taken: Option<(Vec2, f32)>,
    taken_slot: usize,
    /// the pub slot whose weapon is carried while Roman's slots are full (+0x74)
    carried: Option<usize>,
    /// the bars: shown and target values, full scales, rate (fraction of the full scale per second)
    pool: (f32, f32, f32),
    health: (f32, f32, f32),
    rate: f32,
    /// presentation steps taken (awards and the notice), for the state log
    steps: usize,
    /// left: the screen fades out (gamemenu.rs), the game paused under it; the exit happens when it is gone
    closing: bool,
    /// the pub's line playing (+0x90), cut off when the screen is left (0x004690c0)
    line: Option<&'static str>,
}

pub struct PubScreenPlugin;

impl Plugin for PubScreenPlugin {
    fn build(&self, app: &mut App) {
        app.init_level_resource::<PubScreen>().init_level_resource::<PubLines>().add_systems(Update, (open, input, leave_pub, view).chain());
    }
}

/// The pub's once-only lines heard in this level (Game+0x574's intro flag and the new-weapon lines' +0x575 / +0x576,
/// live): the campaign banks them at the level's end (campaign.rs, with the money: 0x00401ef0), so a restarted level
/// hears them again as the original does.
#[derive(Resource, Default)]
pub struct PubLines {
    pub intro_said: bool,
    /// the new-weapon lines heard ([`ONCE_LINES`])
    pub once_said: Vec<String>,
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
        "guyfawkes" => "k_player_guyfawkes",
        "blackhole" => "k_player_blackhole",
        "bof" => "k_player_knockoutbomb",
        _ => "k_player_stickybomb",
    }
}

/// A gun's name on the pub's slots and texts (its template without "player_": rlvindaloo, sharkgun, ...).
fn gun_item(id: WeaponId) -> &'static str {
    id.template().trim_start_matches("player_")
}

/// A special's name on the pub's awards and texts (pub_award_<item>, ui_pub_award_<item>).
fn special_item(s: SpecialId) -> &'static str {
    match s {
        SpecialId::TopsyTurvy => "topsy",
        SpecialId::BunchOfFives => "bof",
        s => s.name(),
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
    WeaponId::from_name(w).is_some_and(|id| r.arsenal.has(id))
}

/// The items in Roman's slots: his weapons, his specials, the ticker bombs (an item still being awarded is not in
/// its slot yet).
fn roman_items(r: &Roman, tickers: u32, awarding: &[Award]) -> Vec<(SlotRef, &'static str)> {
    let pending = |i: &str| awarding.iter().any(|a| a.item == i);
    let mut out = Vec::new();
    // his three gun slots as they are (0x004a1600)
    for (i, id) in r.arsenal.slot_ids().into_iter().enumerate() {
        if let Some(id) = id {
            out.push(((PubSlotKind::Weapon, i), gun_item(id)));
        }
    }
    let mut n = 0;
    for (s, _) in r.arsenal.specials() {
        let item = special_item(s);
        if !pending(item) {
            out.push(((PubSlotKind::Special, n), item));
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
    (mut lines, mut rng, opts, levels): (ResMut<PubLines>, ResMut<crate::fx::FxStats>, Res<crate::options::GameOptions>, Res<crate::campaign::Levels>),
    mut companions: Query<(&crate::ai::Companion, &mut Health), Without<Roman>>,
) {
    let Some(m) = mission.as_deref_mut() else { return };
    if !m.pub_request || *state.get() != AppState::Playing {
        return;
    }
    m.pub_request = false;
    let Ok((mut r, mut h)) = roman.single_mut() else { return };
    let info = m.entered_pub_info().cloned();
    let mut awards = Vec::new();
    let (mut pool, mut rate) = ((0.0, 0.0, 1.0), 0.5);
    let health_before = h.0;
    if let Some(p) = info.as_ref() {
        // health: min(max, health + pool); the pool keeps what was not used
        let want = (r.max_health - h.0).max(0.0);
        let used = want.min(p.pool);
        h.0 += used;
        if let Some(pi) = m.entered_pub_info() {
            pi.pool -= used;
        }
        pool = (p.pool, p.pool - used, p.pool_full.max(1.0));
        rate = p.bar_rate;
        if p.has_tickerbomb && m.tickers == 0 {
            m.tickers = 4;
            awards.push(Award { item: "tickerbomb", stage: Stage::Grow, t: 0.0 });
        }
    }
    // every gun and every special Roman holds is topped up (0x004a13a0 with 100000, so to its maximum: his three gun
    // slots 0x004a1600 and the special list 0x004a1630) unless the pub's should_refill_ammo is clear (0x0046a5e0).
    // Without a pub the original also hands out all five specials and heals from a pool of 100: the remaster opens
    // the screen only from a pub's door (mission.rs), so that path is not taken.
    if info.as_ref().is_none_or(|p| p.refill) {
        for id in WeaponId::ALL {
            if let Some(w) = r.arsenal.gun_mut(id) {
                w.add_ammo(100_000);
            }
        }
        let held: Vec<SpecialId> = r.arsenal.specials().map(|(s, _)| s).collect();
        for s in held {
            let max = crate::weapons::spec::special_max(&data, s);
            r.arsenal.add_special(s, 100_000, max);
        }
    }
    // the pub's special (+0x468) Roman lacks: awarded, set to its maximum (0x004a14e0 with 10000; one he holds was
    // topped up above)
    if let Some(s) = info.as_ref().and_then(|p| p.special).and_then(SpecialId::from_name) {
        if r.arsenal.count(s) == 0 {
            r.arsenal.set_count(s, crate::weapons::spec::special_max(&data, s));
            awards.push(Award { item: special_item(s), stage: Stage::Grow, t: 0.0 });
        }
    }
    // Roman's companions are healed in full (0x00467fe0: the player's Jonesy 0x00475d30 and Q 0x00475d40, +0x454 =
    // max_health), the ones still alive
    for (c, mut ch) in &mut companions {
        if c.dead.is_none() && ch.0 > 0.0 {
            ch.0 = c.max_health;
        }
    }
    // the weapons sold that Roman does not hold go into the pub slots in order, as many as there are slots (0x00469350:
    // the five +0x58 markers; further offers are dropped)
    let shop: Vec<Option<&'static str>> = info.map(|p| p.sells.into_iter().filter(|w| !held(&r, w)).take(SHOP_SLOTS).map(Some).collect()).unwrap_or_default();
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
        taken_slot: 0,
        carried: None,
        pool,
        health: (health_before, h.0, r.max_health.max(1.0)),
        rate,
        steps: 0,
        closing: false,
        line: None,
    };
    m.log.push(format!("pub screen: awards {:?}, offer {:?}", screen.awards.iter().map(|a| a.item).collect::<Vec<_>>(), screen.shop));
    menu.page = Page::Pub;
    next.set(AppState::Paused);
    vtime.pause();
    sounds.write(crate::audio::PlaySound::ui("ui_pub_enter"));
    // the pub's sounds (0x00467fe0): the busy loop amb_pub_busy and a line (Pub_PlayAmbientLine 0x00468dd0)
    sounds.write(crate::audio::PlaySound { looped: Some(true), ..crate::audio::PlaySound::ui("amb_pub_busy") });
    let offered = |w: WeaponId| screen.shop.iter().flatten().any(|i| WeaponId::from_name(i) == Some(w));
    let line = lines.choose(&opts, &levels.current_name(), offered, || rng.rand());
    screen.line = Some(line);
    sounds.write(crate::audio::PlaySound::ui(line));
}

/// The fifteen general lines (0x00468dd0's table in its frame).
const GENERAL_LINES: [&str; 15] = [
    "PE150014_Pub_Ambient", "PE150025_Pub_Ambient", "PE150056_Pub_Ambient", "PE150057_Pub_Ambient", "PE150059_Pub_Ambient",
    "PE150060_Pub_Ambient", "PE250014_Pub_Ambient", "PE250015_Pub_Ambient", "PE250016_Pub_Ambient", "PE250017_Pub_Ambient",
    "PE250018_Pub_Ambient", "PE250051_Pub_Ambient", "JON50081_Ambient", "JON50045_Ambient", "ROM50169_Pub",
];
/// The once-a-campaign intro (Game+0x574, 0x005a093c).
const INTRO_LINE: &str = "PE150013_Pub_Once_Intro";
/// The once-a-campaign lines for a new weapon on offer (0x00469350 flags owner +0x141 for the sniper rifle, kind 1, and
/// +0x142 for the Vindaloo, kind 3, unless Game+0x575 / +0x576 says heard; 0x00468dd0 plays the first flagged and sets
/// that byte). The third, PE150018_Pub_Once_Topsy (+0x143, Game+0x577), is flagged by no code in the executable.
const ONCE_LINES: [(WeaponId, &str); 2] = [(WeaponId::Snipergun, "PE150015_Pub_Once_Sniper"), (WeaponId::Vindaloo, "PE150020_Pub_Once_Vindaloo")];

/// The extra lines that join the random pick (0x00468dd0's ids 0..4 on owner +0x144..+0x148), in id order: the
/// Vindaloo on offer (+0x144, 0x00469350 kind 3), then by the level's name (0x00467fe0, its first five characters
/// without case): lev_1 / lev_2 (+0x146), else lev_3 .. lev_5 (+0x147), and lev_2 .. lev_5 (+0x148). Id 1
/// (PE150021_Pub_Ambient_Sticky, +0x145) is enabled by no code.
fn extra_lines(level: &str, vindaloo_offered: bool) -> Vec<&'static str> {
    let world = level.get(..5).map(str::to_ascii_lowercase).unwrap_or_default();
    let is = |w: &[&str]| w.contains(&world.as_str());
    let mut out = Vec::new();
    if vindaloo_offered {
        out.push("PE150016_Pub_Ambient_Vindaloo");
    }
    if is(&["lev_1", "lev_2"]) {
        out.push("PE150022_Pub_Ambient_l1_l2");
    } else if is(&["lev_3", "lev_4", "lev_5"]) {
        out.push("PE150023_Pub_Ambient_l3_l4_l5");
    }
    if is(&["lev_2", "lev_3", "lev_4", "lev_5"]) {
        out.push("PE150024_Pub_Ambient_l2_3_4_5");
    }
    out
}

impl PubLines {
    /// Banks the once lines heard in this level into the campaign's (0x00401ef0 copies Game+0x570.. to +0x57c, the
    /// flags with the money); true when that changed something.
    pub fn bank(&self, opts: &mut crate::options::GameOptions) -> bool {
        let mut changed = false;
        if self.intro_said && !opts.pub_intro_heard {
            opts.pub_intro_heard = true;
            changed = true;
        }
        let heard = self.once_heard(opts);
        if heard.len() != opts.pub_once_heard.len() {
            opts.pub_once_heard = heard;
            changed = true;
        }
        changed
    }

    /// The new-weapon lines heard in the campaign so far: the bank's and this level's.
    pub fn once_heard(&self, opts: &crate::options::GameOptions) -> Vec<String> {
        let mut heard = opts.pub_once_heard.clone();
        heard.extend(self.once_said.iter().filter(|l| !opts.pub_once_heard.contains(l)).cloned());
        heard
    }

    /// The line the pub opens with (Pub_PlayAmbientLine 0x00468dd0): the intro once a campaign, else the first new-weapon
    /// line on offer not yet heard, else r = rand x (15 + extras): below 15 one of the general lines (a second rand),
    /// else that extra. A line counts as heard from this level (live) or from the campaign's bank (options).
    fn choose(&mut self, opts: &crate::options::GameOptions, level: &str, offered: impl Fn(WeaponId) -> bool, mut rand: impl FnMut() -> f32) -> &'static str {
        if !self.intro_said && !opts.pub_intro_heard {
            self.intro_said = true;
            return INTRO_LINE;
        }
        let heard = |l: &str| self.once_said.iter().chain(&opts.pub_once_heard).any(|h| h == l);
        if let Some((_, line)) = ONCE_LINES.into_iter().find(|(w, l)| offered(*w) && !heard(l)) {
            self.once_said.push(line.to_owned());
            return line;
        }
        let extras = extra_lines(level, offered(WeaponId::Vindaloo));
        let n = GENERAL_LINES.len() + extras.len();
        let r = ((rand() * n as f32) as usize).min(n - 1);
        match r.checked_sub(GENERAL_LINES.len()) {
            Some(i) => extras[i],
            None => GENERAL_LINES[((rand() * 15.0) as usize).min(14)],
        }
    }
}

/// A gun bought at the pub: its record as the mission set it (player_set_ammo_*: every weapon's record is filled at
/// the level's start, 0x00475650 with the default -2, whether carried or not).
fn bought(data: &crate::gamedata::GameData, mission: Option<&crate::mission::Mission>, id: WeaponId) -> Gun {
    let mut g = Gun::full(id, GunSpec::load(data, id));
    g.set_ammo(mission.and_then(|m| m.loadout.ammo.get(id.name()).copied()).unwrap_or(-2));
    g
}

impl PubScreen {
    /// The text id of the line under the art: the award's while it waits, the notice, then the selection prompt (+0xb0 /
    /// +0xb8, +0xc0, +0xc4); while a weapon is carried "Select the weapon to replace" in its place (+0xc8: 0x004695d0
    /// swaps the two at the pick-up, the swap and the drop).
    fn message_id(&self) -> Option<String> {
        match self.awards.first() {
            Some(a) if a.stage == Stage::Wait => Some(format!("pub_award_{}", a.item)),
            Some(_) => None,
            None if self.notify_new => Some("pub_notify_new".into()),
            None if self.carried.is_some() => Some("pub_notify_replace".into()),
            None if self.select_shown => Some("pub_notify_select".into()),
            None => None,
        }
    }
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
    (windows, slots, mut last, settings): (Query<&Window>, Query<(&PubSlot, &HudElement)>, Local<Option<Vec2>>, Res<crate::graphics::GraphicsSettings>),
) {
    if !screen.open || menu.page != Page::Pub {
        return;
    }
    // the screen runs while the game is paused: real time (the original's frame time 0x00604a90; remaster: a frame
    // longer than 0.1 s counts as 0.1 s, so a hitch does not skip the bars' run)
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
    // menus' mapping, display/ui.rs): the slot under it, or the exit prompt (remaster)
    let mut pointed: Option<SlotRef> = None;
    let mut pointer_exit = false;
    let mut moved = false;
    if let Some((w, c)) = windows.single().ok().and_then(|w| w.cursor_position().map(|c| (w, c))) {
        let p = crate::display::ui::UiLayout::new(&settings.display, Vec2::new(w.width(), w.height())).to_layout(c, true);
        moved = last.is_some_and(|l| l.distance(c) > 0.5);
        *last = Some(c);
        // the box: left <= x < right, bottom <= y < top (0x00467f50); the original takes the first slot of its object
        // list whose box holds the point (0x00463a00), an order not recovered: where boxes overlap (the right-hand
        // column's are 48 apart, 67 high) the nearest centre
        let inside = |d: Vec2| d.cmpge(-SLOT_HIT).all() && d.cmplt(SLOT_HIT).all();
        pointed = map
            .iter()
            .filter(|(r, (pos, _))| r.0 != PubSlotKind::Bombs && inside(p - *pos))
            .min_by(|a, b| p.distance_squared(a.1.0).total_cmp(&p.distance_squared(b.1.0)))
            .map(|(r, _)| *r);
        pointer_exit = pointed.is_none() && (p - EXIT_POINT).abs().cmple(Vec2::new(60.0, 20.0)).all();
    }
    let click = mouse.just_pressed(MouseButton::Left);
    let select = ctl.pressed(crate::input::Act::Select) || click;
    let mut back = ctl.pressed(crate::input::Act::Back);
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
        }
        // no move from the keys (none pressed, or no neighbour that way): the pointer when it moved (0x00491380 reports
        // a move) or clicked
        if target.is_none() && (moved || click) {
            // the pointer: the slot under it unless it is the selected one; while a weapon is carried only Roman's
            // weapon slots (0x004695d0: kind 0); the exit prompt (remaster)
            let allowed = |p: &SlotRef| s.carried.is_none() || p.0 == PubSlotKind::Weapon;
            if let Some(p) = pointed.filter(|p| (Some(*p) != s.cursor || s.on_exit) && allowed(p)) {
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
        // must be on the selected slot (remaster: a click elsewhere, or on a slot the carry refused, takes nothing)
        let take = select && !s.on_exit && (!click || (pointed.is_some() && pointed == s.cursor));
        if std::env::var("PUBDBG").is_ok() && (select || back || link.is_some()) {
            info!("PUBDBG select {select} back {back} link {link:?} click {click} cursor {:?} carried {:?}", s.cursor, s.carried);
        }
        if take {
            if let Ok(mut r) = roman.single_mut() {
                match (s.cursor, s.carried) {
                    // a pub weapon Roman lacks: into his first free slot; all three full: carried (+0x74), the cursor on
                    // his third slot (+0x40)
                    (Some((PubSlotKind::Shop, i)), _) => {
                        if let Some(id) = s.shop.get(i).copied().flatten().and_then(WeaponId::from_name).filter(|id| !r.arsenal.has(*id)) {
                            sounds.write(crate::audio::PlaySound::ui("ui_pub_pickup"));
                            if r.arsenal.slot_ids().iter().any(Option::is_none) {
                                r.arsenal.add(bought(&data, mission.as_deref(), id));
                                // remaster: a taken weapon goes into his hands (the original's capture topsyO: after the
                                // pub the HUD shows the machine gun, 100/1000)
                                r.arsenal.equip(id);
                                s.shop[i] = None;
                                s.taken = map.get(&(PubSlotKind::Shop, i)).map(|(p, _)| (*p, 0.0));
                                s.taken_slot = r.arsenal.slot_ids().iter().position(|w| *w == Some(id)).unwrap_or(0);
                                if let Some(m) = mission.as_deref_mut() {
                                    m.log.push(format!("pub: took the {}", id.name()));
                                }
                            } else {
                                s.carried = Some(i);
                                s.glide = s.cursor.and_then(|c| map.get(&c)).map(|(p, _)| (*p, 0.0));
                                s.cursor = Some((PubSlotKind::Weapon, 2));
                            }
                        }
                    }
                    // the carried weapon onto one of his slots: the two swap (0x004a0f70, equipped: 0x004a10c0)
                    (Some((PubSlotKind::Weapon, k)), Some(i)) => {
                        if let (Some(new), Some(old)) = (s.shop.get(i).copied().flatten().and_then(WeaponId::from_name), r.arsenal.slot_ids().get(k).copied().flatten()) {
                            r.arsenal.replace(Some(old), bought(&data, mission.as_deref(), new));
                            r.arsenal.equip(new);
                            s.shop[i] = Some(gun_item(old));
                            s.carried = None;
                            sounds.write(crate::audio::PlaySound::ui("ui_pub_putdown"));
                            if let Some(m) = mission.as_deref_mut() {
                                m.log.push(format!("pub: swapped the {} for the {}", old.name(), new.name()));
                            }
                        }
                    }
                    _ => {}
                }
            }
        }
        // Back on his slots while carrying puts the carried weapon back (the original drops it there: bit 0x20 on a
        // kind-0 slot)
        if back && s.carried.is_some() && s.cursor.is_some_and(|c| c.0 == PubSlotKind::Weapon) {
            s.carried = None;
            back = false;
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
        // the hide (0x004690c0) releases both sound handles: the busy loop (+0x94) and the line, cut off if it still
        // plays (+0x90)
        stop.write(crate::audio::StopSound("amb_pub_busy".into()));
        if let Some(line) = s.line.take() {
            stop.write(crate::audio::StopSound(line.into()));
        }
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
                "pool": s.pool.0, "health": s.health.0, "info": v.info.clone(), "carried": s.carried.is_some(), "message": s.message_id(),
                "line": s.line }),
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
    let message = s.message_id().map_or(String::new(), |id| key(texts.get(&id), &select_key));
    // icons: the pub's inventory, Roman's weapons, specials and ticker bombs; a taken weapon glides in
    let tickers = mission.as_ref().map_or(0, |m| m.tickers);
    let mut items: Vec<(SlotRef, &'static str)> = s.shop.iter().enumerate().filter_map(|(i, w)| w.map(|w| ((PubSlotKind::Shop, i), w))).collect();
    items.extend(roman_items(r, tickers, &s.awards));
    let icons: Vec<(Vec2, &'static str)> = items
        .iter()
        .map(|(slot, item)| {
            let mut p = at(*slot);
            if let (Some((from, t)), PubSlotKind::Weapon) = (s.taken, slot.0) {
                if slot.1 == s.taken_slot {
                    p = lerp(from, p, t);
                }
            }
            // the carried weapon rides the cursor (remaster)
            if s.carried.is_some_and(|i| *slot == (PubSlotKind::Shop, i)) {
                p = s.glide.map_or(s.cursor.map_or(p, at), |(from, t)| lerp(from, s.cursor.map_or(p, at), t)) + Vec2::new(0.0, 12.0);
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

#[cfg(test)]
mod tests {
    use super::*;

    /// The level lines by world (0x00467fe0's name tests) and the Vindaloo's line when it is on offer.
    #[test]
    fn extras_by_level() {
        assert_eq!(extra_lines("lev_1_m1", false), vec!["PE150022_Pub_Ambient_l1_l2"]);
        assert_eq!(extra_lines("LEV_2_m3", false), vec!["PE150022_Pub_Ambient_l1_l2", "PE150024_Pub_Ambient_l2_3_4_5"]);
        assert_eq!(extra_lines("lev_4_m1", true), vec!["PE150016_Pub_Ambient_Vindaloo", "PE150023_Pub_Ambient_l3_l4_l5", "PE150024_Pub_Ambient_l2_3_4_5"]);
        assert!(extra_lines("lev_bonus_m1", false).is_empty());
    }

    /// The intro first, then each new-weapon line once a campaign (live or banked), then the random pick (0x00468dd0).
    #[test]
    fn line_order() {
        let mut opts = crate::options::GameOptions::default();
        let mut lines = PubLines::default();
        let sniper = |w: WeaponId| w == WeaponId::Snipergun;
        assert_eq!(lines.choose(&opts, "lev_1_m3", sniper, || 0.0), INTRO_LINE);
        assert_eq!(lines.choose(&opts, "lev_1_m3", sniper, || 0.0), "PE150015_Pub_Once_Sniper");
        assert_eq!(lines.choose(&opts, "lev_1_m3", sniper, || 0.0), GENERAL_LINES[0]);
        // r = 0.99 x 16 = 15: the level line
        assert_eq!(lines.choose(&opts, "lev_1_m3", sniper, || 0.99), "PE150022_Pub_Ambient_l1_l2");
        assert!(lines.bank(&mut opts) && opts.pub_intro_heard && opts.pub_once_heard == ["PE150015_Pub_Once_Sniper"]);
        assert!(!lines.bank(&mut opts));
        let mut next_level = PubLines::default();
        assert_eq!(next_level.choose(&opts, "lev_2_m1", |w| w == WeaponId::Vindaloo, || 0.0), "PE150020_Pub_Once_Vindaloo");
        assert_eq!(next_level.choose(&opts, "lev_2_m1", sniper, || 0.0), GENERAL_LINES[0]);
    }
}
