//! The pub screen (page pui_pub; opened by 0x00467fe0, docs/subsystems/mission.md 6): entering the pub pauses the
//! game; Roman's health is topped up from the pub's pool (min(max, health + pool), the pool keeps the rest); the
//! awards show one by one ("You gain the Ticker Bombs! Press %s to Continue", then the pub's special weapon when
//! Roman lacks it); when the pub sells weapons Roman does not hold, "New weapons are available!", then "Select the
//! weapon you want and press %s" with the highlight on the pub's inventory; Cancel (Esc) leaves ("Press %s to exit
//! pub") and the pub's exit trigger is sent. The slots, bars, labels and art are the bank's (hud.rs spawn_pub).
//!
//! Guesses, labelled: the order of the award lines (ticker bombs first, as the code checks +0x466 before +0x468);
//! the dark backdrop behind the pub art (what the original shows through its transparent middle is not known).
//! Not ported: weapons other than the rifle and the machine gun (lev_1_m1's pub sells only the machine gun to a
//! player holding the rifle), the Topsy Turvy bomb's throw, the amb_pub_busy loop.

use crate::AppState;
use crate::fx::Health;
use crate::gamemenu::{GameMenu, Page, Texts};
use crate::hud::PubView;
use crate::player::Roman;
use bevy::prelude::*;

/// Roman's weapon slots (3) and special slots (5) in the pub: slot indices of hud.rs PubSlot.
const WEAPON_SLOTS: usize = 5;
const SPECIAL_SLOTS: usize = 8;
const TICKER_SLOT: usize = 13;

#[derive(Resource, Default)]
pub struct PubScreen {
    open: bool,
    /// the award / notice lines still to show (text ids)
    steps: Vec<&'static str>,
    step: usize,
    /// the weapons on offer (sold, not held)
    offer: Vec<&'static str>,
    cursor: usize,
}

pub struct PubScreenPlugin;

impl Plugin for PubScreenPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<PubScreen>().add_systems(Update, (open, input, view).chain());
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

/// Opens the screen when the player enters the pub: health from the pool, the awards.
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
) {
    let Some(m) = mission.as_deref_mut() else { return };
    if !m.pub_request || *state.get() != AppState::Playing {
        return;
    }
    m.pub_request = false;
    let Ok((mut r, mut h)) = roman.single_mut() else { return };
    let info = m.pub_info.clone();
    let mut steps = Vec::new();
    if let Some(p) = info.as_ref() {
        // health: min(max, health + pool); the pool keeps what was not used
        let want = (r.max_health - h.0).max(0.0);
        let used = want.min(p.pool);
        h.0 += used;
        if let Some(pi) = m.pub_info.as_mut() {
            pi.pool -= used;
        }
        if p.has_tickerbomb && m.tickers == 0 {
            m.tickers = 4;
            steps.push("pub_award_tickerbomb");
        }
        if p.special == Some("topsy") && r.topsy == 0 {
            r.topsy = data.props("p_globalsettings").i("topsyturvy_max_ammo", 1) as u32;
            steps.push("pub_award_topsy");
        }
    }
    // the machine gun Roman holds is filled (should_refill_ammo)
    if let Some(w) = r.mg.as_mut() {
        let full = crate::player::machinegun(&data);
        w.ammo = full.ammo;
        w.clip = full.clip;
    }
    let offer: Vec<&'static str> = info.map(|p| p.sells.into_iter().filter(|w| !held(&r, w)).collect()).unwrap_or_default();
    if !offer.is_empty() {
        steps.push("pub_notify_new");
    }
    *screen = PubScreen { open: true, steps, step: 0, offer, cursor: 0 };
    m.log.push(format!("pub screen: {:?}, offer {:?}", screen.steps, screen.offer));
    menu.page = Page::Pub;
    next.set(AppState::Paused);
    vtime.pause();
    sounds.write(crate::audio::PlaySound::ui("ui_pub_enter"));
}

#[allow(clippy::too_many_arguments)]
fn input(
    keys: Res<ButtonInput<KeyCode>>,
    mut next: ResMut<NextState<AppState>>,
    mut menu: ResMut<GameMenu>,
    mut screen: ResMut<PubScreen>,
    mut mission: Option<ResMut<crate::mission::Mission>>,
    mut roman: Query<&mut Roman>,
    mut vtime: ResMut<Time<Virtual>>,
    mut sounds: MessageWriter<crate::audio::PlaySound>,
    data: Res<crate::gamedata::GameData>,
    mut physics: NonSendMut<crate::physics::Physics>,
) {
    if !screen.open || menu.page != Page::Pub {
        return;
    }
    let select = keys.just_pressed(KeyCode::Enter) || keys.just_pressed(KeyCode::Space);
    if screen.step < screen.steps.len() {
        if select {
            screen.step += 1;
            sounds.write(crate::audio::PlaySound::ui("ui_select"));
        }
        return;
    }
    // choosing: the pub's inventory, left / right
    let n = screen.offer.len();
    if n > 0 {
        if keys.just_pressed(KeyCode::ArrowRight) || keys.just_pressed(KeyCode::KeyD) {
            screen.cursor = (screen.cursor + 1) % n;
            sounds.write(crate::audio::PlaySound::ui("ui_pub_move_1"));
        }
        if keys.just_pressed(KeyCode::ArrowLeft) || keys.just_pressed(KeyCode::KeyA) {
            screen.cursor = (screen.cursor + n - 1) % n;
            sounds.write(crate::audio::PlaySound::ui("ui_pub_move_2"));
        }
        if select {
            let w = screen.offer[screen.cursor.min(n - 1)];
            if let Ok(mut r) = roman.single_mut() {
                if w == "machinegun" && r.mg.is_none() {
                    r.mg = Some(crate::player::machinegun(&data));
                    let i = screen.cursor.min(n - 1);
                    screen.offer.remove(i);
                    screen.cursor = 0;
                    sounds.write(crate::audio::PlaySound::ui("ui_pub_pickup"));
                    if let Some(m) = mission.as_deref_mut() {
                        m.log.push("pub: took the machinegun".into());
                    }
                }
            }
        }
    }
    if keys.just_pressed(KeyCode::Escape) {
        screen.open = false;
        menu.page = Page::Main;
        next.set(AppState::Playing);
        vtime.unpause();
        sounds.write(crate::audio::PlaySound::ui("ui_pub_exit"));
        if let Some(m) = mission.as_deref_mut() {
            m.pub_exit_pending = true;
            // the player comes out at the pub's exit
            if let (Some(at), Ok(r)) = (m.pub_exit_pos, roman.single()) {
                physics.place_walker(&r.walker, at);
            }
        }
    }
}

/// Fills the view hud.rs draws.
fn view(screen: Res<PubScreen>, mut v: ResMut<PubView>, texts: Res<Texts>, roman: Query<(&Roman, &Health)>, mission: Option<Res<crate::mission::Mission>>) {
    if !screen.open {
        if v.open {
            v.open = false;
        }
        return;
    }
    let Ok((r, h)) = roman.single() else { return };
    let key = |s: String, k: &str| s.replace("%s", k);
    let choosing = screen.step >= screen.steps.len();
    let message = if !choosing {
        key(texts.get(screen.steps[screen.step]), crate::mission::KEY_CONTINUE)
    } else if !screen.offer.is_empty() {
        key(texts.get("pub_notify_select"), crate::mission::KEY_CONTINUE)
    } else {
        String::new()
    };
    let highlight = (choosing && !screen.offer.is_empty()).then_some(screen.cursor.min(screen.offer.len().saturating_sub(1)));
    let info = highlight.map(|i| texts.get(&info_id(screen.offer[i]))).unwrap_or_default();
    let exit_text = if choosing { key(texts.get("pub_notify_exit"), "Esc") } else { String::new() };
    // icons: the offer in the inventory row, Roman's weapons and specials, the ticker bombs
    let mut icons: Vec<(usize, &'static str)> = screen.offer.iter().enumerate().map(|(i, w)| (i, icon(w))).collect();
    let mut weapons = vec!["rifle"];
    if r.mg.is_some() {
        weapons.push("machinegun");
    }
    for (i, w) in weapons.iter().enumerate() {
        icons.push((WEAPON_SLOTS + i, icon(w)));
    }
    let mut specials = Vec::new();
    if r.stickies > 0 {
        specials.push("stickybomb");
    }
    if r.topsy > 0 {
        specials.push("topsy");
    }
    for (i, s) in specials.iter().enumerate() {
        icons.push((SPECIAL_SLOTS + i, icon(s)));
    }
    if mission.as_ref().is_some_and(|m| m.tickers > 0) {
        icons.push((TICKER_SLOT, icon("tickerbomb")));
    }
    let pool = mission.as_ref().and_then(|m| m.pub_info.as_ref()).map(|p| p.pool / p.bar_rate).unwrap_or(0.0);
    let changed = !v.open || v.icons != icons;
    v.open = true;
    v.award = if !choosing { screen.steps[screen.step].strip_prefix("pub_award_").unwrap_or_default().to_owned() } else { String::new() };
    v.message = message;
    v.info = info;
    v.exit_text = exit_text;
    v.highlight = highlight;
    v.on_exit = false;
    v.health = h.0 / r.max_health.max(1.0);
    v.pool = pool;
    if changed {
        v.icons = icons;
        v.version += 1;
    }
}
